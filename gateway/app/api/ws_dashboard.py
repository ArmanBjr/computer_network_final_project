from fastapi import APIRouter, WebSocket
import time
import json

router = APIRouter()

@router.websocket("/ws")
async def ws_endpoint(ws: WebSocket):
    await ws.accept()
    try:
        while True:
            # Phase 0: فقط heartbeat دمو
            payload = {"ts": time.time(), "status": "gateway_ok"}
            await ws.send_text(json.dumps(payload))
            await asyncio_sleep(1)
    except Exception:
        pass

async def asyncio_sleep(sec: float):
    # بدون import asyncio مستقیم در top-level (ساده و واضح)
    import asyncio
    await asyncio.sleep(sec)
