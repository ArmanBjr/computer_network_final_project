"""
WebSocket voice call relay.

Flow:
  1. Client connects to /ws/voice?username=X
  2. Client sends JSON: {"type":"call_request","target":"Y"}
  3. Server forwards to Y: {"type":"incoming_call","from":"X"}
  4. Y sends: {"type":"call_accept","target":"X"}
  5. Server tells both: {"type":"call_started"}
  6. Both send binary audio chunks (Int16 PCM @ 16kHz)
  7. Server relays binary from A→B and B→A
  8. Either sends: {"type":"call_end"}
  9. Server notifies the other: {"type":"call_ended"}
"""
from fastapi import APIRouter, WebSocket, WebSocketDisconnect, Query
import json
import logging
import asyncio

router = APIRouter()
logger = logging.getLogger(__name__)

# Connected voice clients: username → WebSocket
_voice_clients: dict[str, WebSocket] = {}

# Active calls: username → partner_username
_active_calls: dict[str, str] = {}


@router.websocket("/ws/voice")
async def ws_voice(ws: WebSocket, username: str = Query("")):
    if not username:
        await ws.close(code=4001, reason="username required")
        return

    await ws.accept()
    _voice_clients[username] = ws
    logger.info(f"VOICE_WS_CONNECT user={username} total={len(_voice_clients)}")

    try:
        while True:
            msg = await ws.receive()

            # Binary data = audio chunk → relay to partner
            if "bytes" in msg and msg["bytes"]:
                partner = _active_calls.get(username)
                if partner and partner in _voice_clients:
                    try:
                        await _voice_clients[partner].send_bytes(msg["bytes"])
                    except Exception:
                        pass
                continue

            # Text data = JSON signaling
            if "text" in msg and msg["text"]:
                try:
                    data = json.loads(msg["text"])
                except json.JSONDecodeError:
                    continue

                msg_type = data.get("type", "")

                if msg_type == "call_request":
                    target = data.get("target", "")
                    if target in _voice_clients:
                        # Forward call request to target
                        await _voice_clients[target].send_text(json.dumps({
                            "type": "incoming_call",
                            "from": username,
                        }))
                        logger.info(f"VOICE_CALL_REQ from={username} to={target}")
                    else:
                        await ws.send_text(json.dumps({
                            "type": "call_error",
                            "msg": f"{target} is not available",
                        }))

                elif msg_type == "call_accept":
                    caller = data.get("target", "")
                    if caller in _voice_clients:
                        # Set up the call
                        _active_calls[username] = caller
                        _active_calls[caller] = username
                        # Notify both
                        await _voice_clients[caller].send_text(json.dumps({
                            "type": "call_started",
                            "with": username,
                        }))
                        await ws.send_text(json.dumps({
                            "type": "call_started",
                            "with": caller,
                        }))
                        logger.info(f"VOICE_CALL_STARTED {caller} <-> {username}")

                elif msg_type == "call_reject":
                    caller = data.get("target", "")
                    if caller in _voice_clients:
                        await _voice_clients[caller].send_text(json.dumps({
                            "type": "call_rejected",
                            "by": username,
                        }))
                    logger.info(f"VOICE_CALL_REJECTED by={username}")

                elif msg_type == "call_end":
                    partner = _active_calls.pop(username, None)
                    if partner:
                        _active_calls.pop(partner, None)
                        if partner in _voice_clients:
                            await _voice_clients[partner].send_text(json.dumps({
                                "type": "call_ended",
                                "by": username,
                            }))
                    logger.info(f"VOICE_CALL_END by={username}")

    except (WebSocketDisconnect, Exception):
        pass
    finally:
        # Clean up
        partner = _active_calls.pop(username, None)
        if partner:
            _active_calls.pop(partner, None)
            if partner in _voice_clients:
                try:
                    await _voice_clients[partner].send_text(json.dumps({
                        "type": "call_ended",
                        "by": username,
                    }))
                except Exception:
                    pass
        _voice_clients.pop(username, None)
        logger.info(f"VOICE_WS_DISCONNECT user={username} total={len(_voice_clients)}")
