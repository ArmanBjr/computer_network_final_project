"""
File Transfer Proxy — routes web file uploads/downloads through the C++ Core
using the full binary FSX protocol (offer, accept, chunked upload with CRC32,
ACK/NAK retransmission, FILE_DONE with SHA-256, FILE_DOWNLOAD_REQ).
"""
import hashlib
import logging
import os
import socket
import struct
import time
import zlib
from typing import Optional, Tuple

from app.core_client import (
    CORE_TCP_HOST, CORE_TCP_PORT, HEADER_SIZE,
    _make_header, _parse_header, _recv_full,
    _make_auth_payload, _parse_login_resp,
    _consume_key_exchange_pubkey,
    CoreConnectionError, CoreProtocolError,
    MSG_TYPE_LOGIN_REQ, MSG_TYPE_LOGIN_RESP,
)

logger = logging.getLogger("uvicorn.error")

# Message type constants (must match core/include/fsx/protocol/message.h)
MSG_FILE_OFFER_REQ        = 30
MSG_FILE_OFFER_RESP       = 31
MSG_FILE_ACCEPT_REQ       = 32
MSG_FILE_ACCEPT_RESP      = 33
MSG_FILE_DONE             = 35
MSG_FILE_RESULT           = 36
MSG_FILE_UPLOAD_CHUNK     = 37
MSG_FILE_UPLOAD_ACK       = 38
MSG_FILE_UPLOAD_NAK       = 39
MSG_FILE_DOWNLOAD_REQ     = 40
MSG_FILE_DOWNLOAD_START   = 41
MSG_RESUME_QUERY          = 50
MSG_RESUME_REPLY          = 51

CHUNK_SIZE = 65536   # 64 KB chunks
MAX_RETRIES = 3
MAX_RESUME_ATTEMPTS = 2  # how many times to reconnect and resume


# ---------------------------------------------------------------------------
# Low-level helpers
# ---------------------------------------------------------------------------

def _connect_and_login(username: str, password: str, timeout: float = 10.0) -> socket.socket:
    """Open a TCP connection to Core, consume the key-exchange pubkey, and login.
    Returns the connected+authenticated socket.
    """
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(timeout)
    try:
        sock.connect((CORE_TCP_HOST, CORE_TCP_PORT))
        _consume_key_exchange_pubkey(sock)

        # LOGIN_REQ
        payload = _make_auth_payload(username, password)
        header = _make_header(MSG_TYPE_LOGIN_REQ, len(payload))
        sock.sendall(header + payload)

        # LOGIN_RESP
        hdr = _recv_full(sock, HEADER_SIZE)
        msg_type, plen = _parse_header(hdr)
        if msg_type != MSG_TYPE_LOGIN_RESP:
            raise CoreProtocolError(f"Expected LOGIN_RESP, got type={msg_type}")
        resp_data = _recv_full(sock, plen)
        result = _parse_login_resp(resp_data)
        if not result["ok"]:
            raise CoreProtocolError(f"Login failed: {result['msg']}")
        logger.info(f"PROXY_LOGIN_OK user={username}")
        return sock
    except Exception:
        sock.close()
        raise


def _send_msg(sock: socket.socket, msg_type: int, payload: bytes) -> None:
    hdr = _make_header(msg_type, len(payload))
    sock.sendall(hdr + payload)


def _recv_msg(sock: socket.socket) -> Tuple[int, bytes]:
    """Receive a framed message. Returns (msg_type, payload_bytes)."""
    hdr = _recv_full(sock, HEADER_SIZE)
    msg_type, plen = _parse_header(hdr)
    payload = _recv_full(sock, plen) if plen > 0 else b""
    return msg_type, payload


def _crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def _sha256(filepath: str) -> bytes:
    h = hashlib.sha256()
    with open(filepath, "rb") as f:
        while True:
            chunk = f.read(1 << 20)
            if not chunk:
                break
            h.update(chunk)
    return h.digest()


# ---------------------------------------------------------------------------
# FILE_OFFER_REQ builder
# ---------------------------------------------------------------------------

def _build_file_offer_req(receiver_username: str, filename: str,
                          file_size: int, chunk_size: int = CHUNK_SIZE) -> bytes:
    """Build a FILE_OFFER_REQ payload matching the C++ deserialize format."""
    buf = bytearray()
    # u64 client_transfer_id (0 — server assigns)
    buf += struct.pack(">Q", 0)
    # u16 receiver_username_len + bytes
    r = receiver_username.encode()
    buf += struct.pack(">H", len(r))
    buf += r
    # u16 filename_len + bytes
    f = filename.encode()
    buf += struct.pack(">H", len(f))
    buf += f
    # u64 file_size
    buf += struct.pack(">Q", file_size)
    # u32 chunk_size
    buf += struct.pack(">I", chunk_size)
    return bytes(buf)


def _parse_file_offer_resp(payload: bytes) -> dict:
    """Parse FILE_OFFER_RESP: u8 status + u64 transfer_id [+ u16 reason_len + reason]."""
    if len(payload) < 9:
        raise CoreProtocolError("FILE_OFFER_RESP too short")
    status = payload[0]
    ok = (status == 0)
    transfer_id = struct.unpack(">Q", payload[1:9])[0]
    reason = ""
    if not ok and len(payload) >= 11:
        rlen = struct.unpack(">H", payload[9:11])[0]
        if 11 + rlen <= len(payload):
            reason = payload[11:11+rlen].decode(errors="replace")
    return {"ok": ok, "transfer_id": transfer_id, "reason": reason}


# ---------------------------------------------------------------------------
# FILE_ACCEPT_REQ builder
# ---------------------------------------------------------------------------

def _build_file_accept_req(transfer_id: int, accept: bool = True) -> bytes:
    buf = struct.pack(">Q", transfer_id)
    buf += struct.pack("B", 1 if accept else 0)
    return buf


def _parse_file_accept_resp(payload: bytes) -> dict:
    """Parse FILE_ACCEPT_RESP: u8 status [+ u16 reason_len + reason]."""
    if len(payload) < 1:
        raise CoreProtocolError("FILE_ACCEPT_RESP too short")
    ok = (payload[0] == 0)
    reason = ""
    if not ok and len(payload) >= 3:
        rlen = struct.unpack(">H", payload[1:3])[0]
        if 3 + rlen <= len(payload):
            reason = payload[3:3+rlen].decode(errors="replace")
    return {"ok": ok, "reason": reason}


# ---------------------------------------------------------------------------
# FILE_UPLOAD_CHUNK builder
# ---------------------------------------------------------------------------

def _build_upload_chunk(transfer_id: int, chunk_index: int,
                        data: bytes) -> bytes:
    crc = _crc32(data)
    buf = bytearray()
    buf += struct.pack(">Q", transfer_id)     # u64 transfer_id
    buf += struct.pack(">I", chunk_index)     # u32 chunk_index
    buf += struct.pack(">I", len(data))       # u32 data_size
    buf += struct.pack(">I", crc)             # u32 crc32
    # no compression (original_size=0 means uncompressed — we skip the field)
    buf += data
    return bytes(buf)


def _parse_upload_ack(payload: bytes) -> dict:
    if len(payload) < 12:
        raise CoreProtocolError("FILE_UPLOAD_ACK too short")
    tid = struct.unpack(">Q", payload[0:8])[0]
    ci = struct.unpack(">I", payload[8:12])[0]
    return {"transfer_id": tid, "chunk_index": ci}


def _parse_upload_nak(payload: bytes) -> dict:
    if len(payload) < 20:
        raise CoreProtocolError("FILE_UPLOAD_NAK too short")
    tid = struct.unpack(">Q", payload[0:8])[0]
    ci = struct.unpack(">I", payload[8:12])[0]
    exp = struct.unpack(">I", payload[12:16])[0]
    got = struct.unpack(">I", payload[16:20])[0]
    return {"transfer_id": tid, "chunk_index": ci, "expected_crc32": exp, "got_crc32": got}


# ---------------------------------------------------------------------------
# FILE_DONE builder
# ---------------------------------------------------------------------------

def _build_file_done(transfer_id: int, total_chunks: int,
                     file_size: int, sha256: bytes) -> bytes:
    buf = bytearray()
    buf += struct.pack(">Q", transfer_id)
    buf += struct.pack(">I", total_chunks)
    buf += struct.pack(">Q", file_size)
    if len(sha256) == 32:
        buf += sha256
    return bytes(buf)


def _parse_file_result(payload: bytes) -> dict:
    """Parse FILE_RESULT: u64 tid + u8 status + u16 path_len + path [+ sha256 info]."""
    if len(payload) < 9:
        raise CoreProtocolError("FILE_RESULT too short")
    tid = struct.unpack(">Q", payload[0:8])[0]
    ok = (payload[8] == 0)
    path_or_reason = ""
    pos = 9
    if len(payload) >= pos + 2:
        plen = struct.unpack(">H", payload[pos:pos+2])[0]
        pos += 2
        if pos + plen <= len(payload):
            path_or_reason = payload[pos:pos+plen].decode(errors="replace")
    return {"transfer_id": tid, "ok": ok, "path_or_reason": path_or_reason}


# ---------------------------------------------------------------------------
# Resume helpers
# ---------------------------------------------------------------------------

def _build_resume_query(transfer_id: int) -> bytes:
    return struct.pack(">Q", transfer_id)


def _parse_resume_reply(payload: bytes) -> dict:
    """Parse RESUME_REPLY: u8 can_resume + u64 transfer_id + (if yes: resume info) (if no: reason)."""
    if len(payload) < 9:
        raise CoreProtocolError("RESUME_REPLY too short")
    pos = 0
    can_resume = (payload[pos] == 1)
    pos += 1
    transfer_id = struct.unpack(">Q", payload[pos:pos+8])[0]
    pos += 8

    if can_resume:
        # u16 filename_len + filename + u64 file_size + u32 chunk_size +
        # u32 last_acked_chunk_index + u64 bytes_received
        fn_len = struct.unpack(">H", payload[pos:pos+2])[0]
        pos += 2
        filename = payload[pos:pos+fn_len].decode(errors="replace")
        pos += fn_len
        file_size = struct.unpack(">Q", payload[pos:pos+8])[0]
        pos += 8
        chunk_size = struct.unpack(">I", payload[pos:pos+4])[0]
        pos += 4
        last_acked = struct.unpack(">I", payload[pos:pos+4])[0]
        pos += 4
        bytes_received = struct.unpack(">Q", payload[pos:pos+8])[0]
        pos += 8
        return {
            "can_resume": True, "transfer_id": transfer_id,
            "filename": filename, "file_size": file_size,
            "chunk_size": chunk_size, "last_acked_chunk_index": last_acked,
            "bytes_received": bytes_received,
        }
    else:
        reason = ""
        if len(payload) >= pos + 2:
            rlen = struct.unpack(">H", payload[pos:pos+2])[0]
            pos += 2
            if pos + rlen <= len(payload):
                reason = payload[pos:pos+rlen].decode(errors="replace")
        return {"can_resume": False, "transfer_id": transfer_id, "reason": reason}


def _query_resume(sender_username: str, sender_password: str,
                  transfer_id: int) -> Optional[dict]:
    """Connect to Core, send RESUME_QUERY, return resume info or None."""
    sock = None
    try:
        sock = _connect_and_login(sender_username, sender_password)
        _send_msg(sock, MSG_RESUME_QUERY, _build_resume_query(transfer_id))
        mtype, data = _recv_msg(sock)
        if mtype != MSG_RESUME_REPLY:
            logger.warning(f"RESUME: expected RESUME_REPLY, got type={mtype}")
            return None
        info = _parse_resume_reply(data)
        logger.info(f"RESUME_REPLY transfer_id={transfer_id} can_resume={info['can_resume']}")
        return info if info["can_resume"] else None
    except Exception as e:
        logger.warning(f"RESUME_QUERY_ERROR transfer_id={transfer_id} err={e}")
        return None
    finally:
        if sock:
            try: sock.close()
            except: pass


def _resume_upload(sender_username: str, sender_password: str,
                   transfer_id: int, filepath: str, file_size: int,
                   resume_chunk: int, chunk_size: int) -> dict:
    """Reconnect as sender and continue uploading from resume_chunk+1."""
    sender_sock = None
    try:
        sender_sock = _connect_and_login(sender_username, sender_password)
        start_chunk = resume_chunk + 1
        start_byte = start_chunk * chunk_size

        logger.info(f"RESUME_UPLOAD transfer_id={transfer_id} from_chunk={start_chunk} "
                     f"byte_offset={start_byte}")

        total_chunks = 0
        with open(filepath, "rb") as f:
            f.seek(start_byte)
            chunk_index = start_chunk
            while True:
                chunk_data = f.read(chunk_size)
                if not chunk_data:
                    break
                for attempt in range(MAX_RETRIES):
                    chunk_payload = _build_upload_chunk(transfer_id, chunk_index, chunk_data)
                    _send_msg(sender_sock, MSG_FILE_UPLOAD_CHUNK, chunk_payload)
                    mtype, resp = _recv_msg(sender_sock)
                    if mtype == MSG_FILE_UPLOAD_ACK:
                        ack = _parse_upload_ack(resp)
                        if ack["chunk_index"] == chunk_index:
                            break
                    elif mtype == MSG_FILE_UPLOAD_NAK:
                        logger.warning(f"RESUME_NAK chunk={chunk_index} attempt={attempt+1}")
                        if attempt == MAX_RETRIES - 1:
                            raise CoreProtocolError(f"Chunk {chunk_index} NAK'd {MAX_RETRIES} times")
                    else:
                        raise CoreProtocolError(f"Expected ACK/NAK, got type={mtype}")
                chunk_index += 1
            total_chunks = chunk_index

        # FILE_DONE
        file_sha = _sha256(filepath)
        done_payload = _build_file_done(transfer_id, total_chunks, file_size, file_sha)
        _send_msg(sender_sock, MSG_FILE_DONE, done_payload)

        mtype, data = _recv_msg(sender_sock)
        if mtype != MSG_FILE_RESULT:
            raise CoreProtocolError(f"Expected FILE_RESULT, got type={mtype}")
        result = _parse_file_result(data)

        logger.info(f"RESUME_UPLOAD_DONE transfer_id={transfer_id} ok={result['ok']}")
        return {
            "ok": result["ok"],
            "transfer_id": transfer_id,
            "core_path": result["path_or_reason"],
            "error": "" if result["ok"] else result["path_or_reason"],
        }
    except Exception as e:
        logger.error(f"RESUME_UPLOAD_ERROR transfer_id={transfer_id} err={e}")
        return {"ok": False, "transfer_id": transfer_id, "core_path": "", "error": str(e)}
    finally:
        if sender_sock:
            try: sender_sock.close()
            except: pass


# ---------------------------------------------------------------------------
# Public API: upload_file_via_core
# ---------------------------------------------------------------------------

async def upload_file_via_core(
    sender_username: str,
    sender_password: str,
    receiver_username: str,
    filename: str,
    filepath: str,
    chunk_size: int = CHUNK_SIZE,
) -> dict:
    """Async wrapper: resolve receiver credentials, then run blocking proxy in thread."""
    import asyncio
    from app.services.user_session import session_manager

    receiver_password = sender_password  # fallback (won't work if different)
    receiver_creds = await session_manager.get_credentials(receiver_username)
    if receiver_creds:
        _, receiver_password = receiver_creds
    else:
        logger.warning(f"PROXY: receiver '{receiver_username}' not online, cannot get credentials")
        return {"ok": False, "transfer_id": 0, "core_path": "",
                "error": f"Receiver '{receiver_username}' not online"}

    return await asyncio.to_thread(
        _upload_file_via_core_sync,
        sender_username, sender_password,
        receiver_username, receiver_password,
        filename, filepath, chunk_size,
    )


def _upload_file_via_core_sync(
    sender_username: str,
    sender_password: str,
    receiver_username: str,
    receiver_password: str,
    filename: str,
    filepath: str,
    chunk_size: int = CHUNK_SIZE,
) -> dict:
    """
    Upload a file through the C++ Core using the full binary protocol.

    Opens TWO TCP connections (sender + receiver), performs the full
    offer -> accept -> chunked upload -> done -> result handshake.

    Returns {"ok": bool, "transfer_id": int, "core_path": str, "error": str}.
    """
    file_size = os.path.getsize(filepath)
    sender_sock: Optional[socket.socket] = None
    receiver_sock: Optional[socket.socket] = None
    transfer_id = 0

    try:
        # 1. Connect as sender
        logger.info(f"PROXY_UPLOAD start sender={sender_username} receiver={receiver_username} "
                     f"file={filename} size={file_size}")
        sender_sock = _connect_and_login(sender_username, sender_password)

        # 2. Send FILE_OFFER_REQ
        offer_payload = _build_file_offer_req(receiver_username, filename, file_size, chunk_size)
        _send_msg(sender_sock, MSG_FILE_OFFER_REQ, offer_payload)

        # 3. Read FILE_OFFER_RESP
        mtype, data = _recv_msg(sender_sock)
        if mtype != MSG_FILE_OFFER_RESP:
            raise CoreProtocolError(f"Expected FILE_OFFER_RESP, got type={mtype}")
        offer_resp = _parse_file_offer_resp(data)
        if not offer_resp["ok"]:
            raise CoreProtocolError(f"FILE_OFFER rejected: {offer_resp['reason']}")

        transfer_id = offer_resp["transfer_id"]
        logger.info(f"PROXY_OFFER_OK transfer_id={transfer_id}")

        # 4. Connect as receiver and accept
        receiver_sock = _connect_and_login(receiver_username, receiver_password)

        accept_payload = _build_file_accept_req(transfer_id, True)
        _send_msg(receiver_sock, MSG_FILE_ACCEPT_REQ, accept_payload)

        # Read FILE_ACCEPT_RESP on receiver
        mtype, data = _recv_msg(receiver_sock)
        if mtype != MSG_FILE_ACCEPT_RESP:
            raise CoreProtocolError(f"Expected FILE_ACCEPT_RESP, got type={mtype}")
        accept_resp = _parse_file_accept_resp(data)
        if not accept_resp["ok"]:
            raise CoreProtocolError(f"FILE_ACCEPT rejected: {accept_resp['reason']}")

        # Also read FILE_ACCEPT_RESP on sender (server notifies sender)
        mtype2, data2 = _recv_msg(sender_sock)
        if mtype2 != MSG_FILE_ACCEPT_RESP:
            logger.warning(f"PROXY: expected FILE_ACCEPT_RESP on sender, got type={mtype2}")

        logger.info(f"PROXY_ACCEPT_OK transfer_id={transfer_id}")

        # 5. Send chunks with CRC32 + ACK/NAK
        total_chunks = 0
        with open(filepath, "rb") as f:
            chunk_index = 0
            while True:
                chunk_data = f.read(chunk_size)
                if not chunk_data:
                    break

                for attempt in range(MAX_RETRIES):
                    chunk_payload = _build_upload_chunk(transfer_id, chunk_index, chunk_data)
                    _send_msg(sender_sock, MSG_FILE_UPLOAD_CHUNK, chunk_payload)

                    # Wait for ACK or NAK
                    mtype, resp = _recv_msg(sender_sock)
                    if mtype == MSG_FILE_UPLOAD_ACK:
                        ack = _parse_upload_ack(resp)
                        if ack["chunk_index"] == chunk_index:
                            break  # success
                    elif mtype == MSG_FILE_UPLOAD_NAK:
                        nak = _parse_upload_nak(resp)
                        logger.warning(f"PROXY_NAK transfer_id={transfer_id} "
                                       f"chunk={nak['chunk_index']} attempt={attempt+1}")
                        if attempt == MAX_RETRIES - 1:
                            raise CoreProtocolError(
                                f"Chunk {chunk_index} NAK'd {MAX_RETRIES} times"
                            )
                    else:
                        raise CoreProtocolError(
                            f"Expected ACK/NAK, got type={mtype}"
                        )

                chunk_index += 1
            total_chunks = chunk_index

        # 6. Compute SHA-256 and send FILE_DONE
        file_sha = _sha256(filepath)
        done_payload = _build_file_done(transfer_id, total_chunks, file_size, file_sha)
        _send_msg(sender_sock, MSG_FILE_DONE, done_payload)

        # 7. Read FILE_RESULT
        mtype, data = _recv_msg(sender_sock)
        if mtype != MSG_FILE_RESULT:
            raise CoreProtocolError(f"Expected FILE_RESULT, got type={mtype}")
        result = _parse_file_result(data)

        logger.info(f"PROXY_UPLOAD_DONE transfer_id={transfer_id} ok={result['ok']} "
                     f"path={result['path_or_reason']}")

        return {
            "ok": result["ok"],
            "transfer_id": transfer_id,
            "core_path": result["path_or_reason"],
            "error": "" if result["ok"] else result["path_or_reason"],
        }

    except Exception as e:
        logger.error(f"PROXY_UPLOAD_ERROR sender={sender_username} file={filename} err={e}")

        # --- Attempt resume if we got far enough to have a transfer_id ---
        if transfer_id:
            logger.info(f"PROXY: attempting resume for transfer_id={transfer_id}")
            for attempt in range(MAX_RESUME_ATTEMPTS):
                time.sleep(1)  # brief pause before reconnect
                resume_info = _query_resume(sender_username, sender_password, transfer_id)
                if resume_info and resume_info["can_resume"]:
                    resume_result = _resume_upload(
                        sender_username, sender_password,
                        transfer_id, filepath, file_size,
                        resume_info["last_acked_chunk_index"],
                        resume_info.get("chunk_size", chunk_size),
                    )
                    if resume_result["ok"]:
                        logger.info(f"PROXY: resume succeeded for transfer_id={transfer_id}")
                        return resume_result
                    logger.warning(f"PROXY: resume attempt {attempt+1} failed: {resume_result['error']}")
                else:
                    logger.warning(f"PROXY: Core says cannot resume transfer_id={transfer_id}")
                    break

        return {
            "ok": False,
            "transfer_id": transfer_id if transfer_id else 0,
            "core_path": "",
            "error": str(e),
        }
    finally:
        if sender_sock:
            try: sender_sock.close()
            except: pass
        if receiver_sock:
            try: receiver_sock.close()
            except: pass


# ---------------------------------------------------------------------------
# Public API: download_file_from_core
# ---------------------------------------------------------------------------

async def download_file_from_core(
    username: str,
    password: str,
    transfer_id: int,
) -> Optional[Tuple[bytes, str]]:
    """Async wrapper that runs the blocking download in a thread pool."""
    import asyncio
    return await asyncio.to_thread(
        _download_file_from_core_sync, username, password, transfer_id
    )


def _download_file_from_core_sync(
    username: str,
    password: str,
    transfer_id: int,
) -> Optional[Tuple[bytes, str]]:
    """
    Download a file from the C++ Core using FILE_DOWNLOAD_REQ.

    Returns (file_bytes, filename) on success, or None on failure.
    """
    sock: Optional[socket.socket] = None
    try:
        sock = _connect_and_login(username, password)

        # Send FILE_DOWNLOAD_REQ: u64 transfer_id
        payload = struct.pack(">Q", transfer_id)
        _send_msg(sock, MSG_FILE_DOWNLOAD_REQ, payload)

        # Read FILE_DOWNLOAD_START (type 41)
        mtype, data = _recv_msg(sock)
        if mtype != MSG_FILE_DOWNLOAD_START:
            logger.error(f"PROXY_DOWNLOAD: expected FILE_DOWNLOAD_START(41), got {mtype}")
            return None

        if len(data) < 1:
            logger.error("PROXY_DOWNLOAD: empty response")
            return None

        status = data[0]
        if status != 1:
            logger.error(f"PROXY_DOWNLOAD: server returned status={status} (fail)")
            return None

        # Parse: u8 status(1) + u64 file_size + u16 filename_len + filename + file_data
        pos = 1
        if len(data) < pos + 8:
            logger.error("PROXY_DOWNLOAD: truncated file_size")
            return None
        file_size = struct.unpack(">Q", data[pos:pos+8])[0]
        pos += 8

        if len(data) < pos + 2:
            logger.error("PROXY_DOWNLOAD: truncated filename_len")
            return None
        fn_len = struct.unpack(">H", data[pos:pos+2])[0]
        pos += 2

        if len(data) < pos + fn_len:
            logger.error("PROXY_DOWNLOAD: truncated filename")
            return None
        filename = data[pos:pos+fn_len].decode(errors="replace")
        pos += fn_len

        file_data = data[pos:]
        if len(file_data) != file_size:
            logger.warning(f"PROXY_DOWNLOAD: size mismatch got={len(file_data)} expected={file_size}")

        logger.info(f"PROXY_DOWNLOAD_OK transfer_id={transfer_id} filename={filename} size={len(file_data)}")
        return (file_data, filename)

    except Exception as e:
        logger.error(f"PROXY_DOWNLOAD_ERROR transfer_id={transfer_id} err={e}")
        return None
    finally:
        if sock:
            try: sock.close()
            except: pass
