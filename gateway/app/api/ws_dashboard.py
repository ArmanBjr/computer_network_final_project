from fastapi import APIRouter, WebSocket, WebSocketDisconnect
import asyncio
import json
import logging
import time
import traceback

from app.core_client import (
    get_online_users,
    get_transfer_list,
    get_voice_sessions,
    CoreConnectionError,
)
from app.services.user_session import session_manager

router = APIRouter()
logger = logging.getLogger("uvicorn.error")


def _fetch_core_sync() -> dict:
    """Blocking calls to Core (run in thread pool)."""
    data = {
        "online_core": [],
        "transfers": [],
        "voice_sessions": [],
    }
    try:
        data["online_core"] = get_online_users(timeout=2.0)
    except Exception as e:
        logger.debug(f"WS: get_online_users failed: {e}")
    try:
        data["transfers"] = get_transfer_list(timeout=2.0)
    except Exception as e:
        logger.debug(f"WS: get_transfer_list failed: {e}")
    try:
        data["voice_sessions"] = get_voice_sessions(timeout=2.0)
    except Exception as e:
        logger.debug(f"WS: get_voice_sessions failed: {e}")
    return data


@router.websocket("/ws")
async def ws_endpoint(ws: WebSocket):
    await ws.accept()
    logger.info("WS dashboard client connected")
    try:
        while True:
            try:
                # Fetch data from Core in thread (blocking TCP calls)
                core_data = await asyncio.get_event_loop().run_in_executor(
                    None, _fetch_core_sync
                )
            except Exception as e:
                logger.warning(f"WS: core fetch error: {e}")
                core_data = {"online_core": [], "transfers": [], "voice_sessions": []}

            # Gateway sessions (async)
            try:
                gw_users = set(await session_manager.get_online_usernames())
            except Exception:
                gw_users = set()
            core_users = set(core_data.get("online_core", []))
            all_users = sorted(list(core_users | gw_users))

            state = {
                "ts": time.time(),
                "online_users": all_users,
                "transfers": core_data.get("transfers", []),
                "voice_sessions": core_data.get("voice_sessions", []),
            }

            await ws.send_text(json.dumps(state, default=str))
            await asyncio.sleep(1.5)
    except WebSocketDisconnect:
        pass
    except Exception as e:
        logger.error(f"WS dashboard error: {e}\n{traceback.format_exc()}")
    finally:
        logger.info("WS dashboard client disconnected")
