from fastapi import APIRouter, WebSocket, WebSocketDisconnect
import asyncio
import json
import logging
import time

from app.core_client import (
    get_online_users,
    get_transfer_list,
    get_voice_sessions,
    CoreConnectionError,
)
from app.services.user_session import session_manager

router = APIRouter()
logger = logging.getLogger(__name__)


def _fetch_core_sync() -> dict:
    """Blocking calls to Core (run in thread pool)."""
    data = {
        "online_core": [],
        "transfers": [],
        "voice_sessions": [],
    }
    try:
        data["online_core"] = get_online_users(timeout=1.5)
    except Exception:
        pass
    try:
        data["transfers"] = get_transfer_list(timeout=1.5)
    except Exception:
        pass
    try:
        data["voice_sessions"] = get_voice_sessions(timeout=1.5)
    except Exception:
        pass
    return data


@router.websocket("/ws")
async def ws_endpoint(ws: WebSocket):
    await ws.accept()
    logger.info("WS dashboard client connected")
    try:
        while True:
            # Fetch data from Core in thread (blocking TCP calls)
            core_data = await asyncio.get_event_loop().run_in_executor(None, _fetch_core_sync)

            # Gateway sessions (async)
            gw_users = set(await session_manager.get_online_usernames())
            core_users = set(core_data.get("online_core", []))
            all_users = sorted(list(core_users | gw_users))

            state = {
                "ts": time.time(),
                "online_users": all_users,
                "transfers": core_data.get("transfers", []),
                "voice_sessions": core_data.get("voice_sessions", []),
            }

            await ws.send_text(json.dumps(state))
            await asyncio.sleep(1.5)
    except (WebSocketDisconnect, Exception):
        pass
    finally:
        logger.info("WS dashboard client disconnected")
