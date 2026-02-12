"""
Core TCP Client for Gateway
Connects to Core server on TCP port 9000 and sends protocol messages.
"""
import socket
import struct
from typing import List, Optional
from app.settings import CORE_TCP_HOST, CORE_TCP_PORT

# Protocol constants (must match core/include/fsx/protocol/message.h)
MAGIC = 0x46535831  # "FSX1" in hex
VERSION = 1
HEADER_SIZE = 12

# Message types (must match core/include/fsx/protocol/message.h)
MSG_TYPE_ONLINE_LIST_REQ = 20
MSG_TYPE_ONLINE_LIST_RESP = 21
MSG_TYPE_REGISTER_REQ = 10
MSG_TYPE_REGISTER_RESP = 11
MSG_TYPE_LOGIN_REQ = 12
MSG_TYPE_LOGIN_RESP = 13
MSG_TYPE_PING = 2
MSG_TYPE_PONG = 3


class CoreClientError(Exception):
    """Base exception for Core client errors"""
    pass


class CoreConnectionError(CoreClientError):
    """Failed to connect to Core server"""
    pass


class CoreProtocolError(CoreClientError):
    """Protocol-level error (bad response)"""
    pass


def _make_header(msg_type: int, payload_len: int) -> bytes:
    """Create a message header (12 bytes)"""
    # Format: magic (u32 BE), version (u8), type (u8), len (u32 BE), reserved (u16 BE)
    return struct.pack(">IBBIH",
                       MAGIC,      # magic_be
                       VERSION,    # version
                       msg_type,   # type
                       payload_len, # len_be
                       0)          # reserved_be


def _parse_header(data: bytes) -> tuple[int, int]:
    """Parse message header, return (msg_type, payload_len)"""
    if len(data) < HEADER_SIZE:
        raise CoreProtocolError(f"Header too short: {len(data)} < {HEADER_SIZE}")
    
    magic, version, msg_type, payload_len, reserved = struct.unpack(">IBBIH", data)
    
    if magic != MAGIC:
        raise CoreProtocolError(f"Bad magic: 0x{magic:08x} != 0x{MAGIC:08x}")
    if version != VERSION:
        raise CoreProtocolError(f"Bad version: {version} != {VERSION}")
    
    return msg_type, payload_len


def _parse_online_list_resp(payload: bytes) -> List[str]:
    """Parse ONLINE_LIST_RESP payload: u16 count + (u16 len + username)*"""
    if len(payload) < 2:
        return []
    
    pos = 0
    count = struct.unpack(">H", payload[pos:pos+2])[0]
    pos += 2
    
    usernames = []
    for _ in range(count):
        if pos + 2 > len(payload):
            break
        username_len = struct.unpack(">H", payload[pos:pos+2])[0]
        pos += 2
        if pos + username_len > len(payload):
            break
        username = payload[pos:pos+username_len].decode('utf-8', errors='replace')
        usernames.append(username)
        pos += username_len
    
    return usernames


def get_online_users(timeout: float = 5.0) -> List[str]:
    """
    Connect to Core server, send ONLINE_LIST_REQ, and return list of online usernames.
    
    Args:
        timeout: Connection timeout in seconds
        
    Returns:
        List of online usernames
        
    Raises:
        CoreConnectionError: If cannot connect to Core
        CoreProtocolError: If protocol error occurs
    """
    sock = None
    try:
        # Connect to Core
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(timeout)
        sock.connect((CORE_TCP_HOST, CORE_TCP_PORT))

        _consume_key_exchange_pubkey(sock)
        
        # Send ONLINE_LIST_REQ (empty payload)
        header = _make_header(MSG_TYPE_ONLINE_LIST_REQ, 0)
        sock.sendall(header)
        
        # Read response header
        header_data = sock.recv(HEADER_SIZE)
        if len(header_data) < HEADER_SIZE:
            raise CoreProtocolError("Incomplete response header")
        
        msg_type, payload_len = _parse_header(header_data)
        
        if msg_type != MSG_TYPE_ONLINE_LIST_RESP:
            raise CoreProtocolError(f"Unexpected message type: {msg_type} (expected {MSG_TYPE_ONLINE_LIST_RESP})")
        
        # Read payload
        if payload_len > 0:
            payload = b""
            while len(payload) < payload_len:
                chunk = sock.recv(payload_len - len(payload))
                if not chunk:
                    raise CoreProtocolError("Incomplete payload")
                payload += chunk
        else:
            payload = b""
        
        # Parse payload
        usernames = _parse_online_list_resp(payload)
        return usernames
        
    except socket.timeout:
        raise CoreConnectionError(f"Timeout connecting to Core at {CORE_TCP_HOST}:{CORE_TCP_PORT}")
    except socket.error as e:
        raise CoreConnectionError(f"Cannot connect to Core at {CORE_TCP_HOST}:{CORE_TCP_PORT}: {e}")
    except CoreProtocolError:
        raise
    except Exception as e:
        raise CoreClientError(f"Unexpected error: {e}")
    finally:
        if sock:
            sock.close()


def _make_auth_payload(username: str, password: str, email: str = "") -> bytes:
    """Create REGISTER_REQ or LOGIN_REQ payload: 
    REGISTER_REQ: u16 username_len + username + u16 email_len + email + u16 password_len + password
    LOGIN_REQ: u16 username_len + username + u16 password_len + password (no email)
    """
    payload = bytearray()
    
    # Username
    username_bytes = username.encode('utf-8')
    if len(username_bytes) > 65535:
        raise ValueError("Username too long")
    payload.extend(struct.pack(">H", len(username_bytes)))
    payload.extend(username_bytes)
    
    # Email (only for REGISTER_REQ)
    if email:
        email_bytes = email.encode('utf-8')
        if len(email_bytes) > 65535:
            raise ValueError("Email too long")
        payload.extend(struct.pack(">H", len(email_bytes)))
        payload.extend(email_bytes)
    
    # Password
    password_bytes = password.encode('utf-8')
    if len(password_bytes) > 65535:
        raise ValueError("Password too long")
    payload.extend(struct.pack(">H", len(password_bytes)))
    payload.extend(password_bytes)
    
    return bytes(payload)


def _parse_register_resp(payload: bytes) -> dict:
    """Parse REGISTER_RESP: u8 ok + u16 msg_len + msg"""
    if len(payload) < 3:
        raise CoreProtocolError("REGISTER_RESP too short")
    
    ok = payload[0] != 0
    msg_len = struct.unpack(">H", payload[1:3])[0]
    
    if len(payload) < 3 + msg_len:
        raise CoreProtocolError("REGISTER_RESP incomplete")
    
    msg = payload[3:3+msg_len].decode('utf-8', errors='replace')
    
    return {"ok": ok, "msg": msg}


def _parse_login_resp(payload: bytes) -> dict:
    """Parse LOGIN_RESP: u8 ok + (if ok: u16 token_len + token + u64 user_id + u16 username_len + username) + u16 msg_len + msg"""
    if len(payload) < 3:
        raise CoreProtocolError("LOGIN_RESP too short")
    
    ok = payload[0] != 0
    pos = 1
    
    token = ""
    user_id = 0
    username = ""
    
    if ok:
        # Token
        if pos + 2 > len(payload):
            raise CoreProtocolError("LOGIN_RESP: missing token_len")
        token_len = struct.unpack(">H", payload[pos:pos+2])[0]
        pos += 2
        if pos + token_len > len(payload):
            raise CoreProtocolError("LOGIN_RESP: incomplete token")
        token = payload[pos:pos+token_len].decode('utf-8', errors='replace')
        pos += token_len
        
        # User ID (u64)
        if pos + 8 > len(payload):
            raise CoreProtocolError("LOGIN_RESP: missing user_id")
        user_id = struct.unpack(">Q", payload[pos:pos+8])[0]
        pos += 8
        
        # Username
        if pos + 2 > len(payload):
            raise CoreProtocolError("LOGIN_RESP: missing username_len")
        username_len = struct.unpack(">H", payload[pos:pos+2])[0]
        pos += 2
        if pos + username_len > len(payload):
            raise CoreProtocolError("LOGIN_RESP: incomplete username")
        username = payload[pos:pos+username_len].decode('utf-8', errors='replace')
        pos += username_len
    
    # Message
    if pos + 2 > len(payload):
        raise CoreProtocolError("LOGIN_RESP: missing msg_len")
    msg_len = struct.unpack(">H", payload[pos:pos+2])[0]
    pos += 2
    if pos + msg_len > len(payload):
        raise CoreProtocolError("LOGIN_RESP: incomplete msg")
    msg = payload[pos:pos+msg_len].decode('utf-8', errors='replace')
    
    return {
        "ok": ok,
        "token": token,
        "user_id": user_id,
        "username": username,
        "msg": msg
    }


def register_user(username: str, email: str, password: str, timeout: float = 8.0) -> dict:
    """
    Register a new user.
    
    Returns:
        {"ok": bool, "msg": str}
    """
    sock = None
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(timeout)
        sock.connect((CORE_TCP_HOST, CORE_TCP_PORT))

        _consume_key_exchange_pubkey(sock)
        
        # Send REGISTER_REQ
        payload = _make_auth_payload(username, password, email)
        header = _make_header(MSG_TYPE_REGISTER_REQ, len(payload))
        sock.sendall(header + payload)
        
        # Read response
        header_data = sock.recv(HEADER_SIZE)
        if len(header_data) < HEADER_SIZE:
            raise CoreProtocolError("Incomplete response header")
        
        msg_type, payload_len = _parse_header(header_data)
        if msg_type != MSG_TYPE_REGISTER_RESP:
            raise CoreProtocolError(f"Unexpected message type: {msg_type}")
        
        resp_payload = b""
        while len(resp_payload) < payload_len:
            chunk = sock.recv(payload_len - len(resp_payload))
            if not chunk:
                raise CoreProtocolError("Incomplete payload")
            resp_payload += chunk
        
        return _parse_register_resp(resp_payload)
        
    except socket.timeout:
        raise CoreConnectionError(f"Timeout connecting to Core")
    except socket.error as e:
        raise CoreConnectionError(f"Cannot connect to Core: {e}")
    finally:
        if sock:
            sock.close()


def _recv_full(sock, length: int) -> bytes:
    """Receive exactly `length` bytes."""
    data = b""
    while len(data) < length:
        chunk = sock.recv(length - len(data))
        if not chunk:
            raise CoreProtocolError("Connection closed while reading")
        data += chunk
    return data


def _consume_key_exchange_pubkey(sock) -> None:
    """Read and discard the KEY_EXCHANGE_PUBKEY (type 60) the server sends on connect."""
    header_data = _recv_full(sock, HEADER_SIZE)
    msg_type, payload_len = _parse_header(header_data)
    if msg_type == 60:  # KEY_EXCHANGE_PUBKEY
        if payload_len > 0:
            _recv_full(sock, payload_len)
    # else: not a pubkey — we'll just continue


# ---------- Phase 9: Throttle & Transfer List ----------

MSG_TYPE_THROTTLE_SET      = 70
MSG_TYPE_THROTTLE_SET_RESP = 71
MSG_TYPE_TRANSFER_LIST_REQ  = 72
MSG_TYPE_TRANSFER_LIST_RESP = 73
MSG_TYPE_VOICE_SESSION_LIST_REQ  = 84
MSG_TYPE_VOICE_SESSION_LIST_RESP = 85


def set_throttle(scope: str = "global", bytes_per_second: int = 0,
                 user_id: int = 0, timeout: float = 3.0) -> dict:
    """
    Set transfer throttle on the Core server.

    Args:
        scope: "global" or "user"
        bytes_per_second: rate limit (0 = unlimited)
        user_id: target user (only when scope="user")

    Returns:
        {"ok": bool, "bps": int}
    """
    sock = None
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(timeout)
        sock.connect((CORE_TCP_HOST, CORE_TCP_PORT))

        _consume_key_exchange_pubkey(sock)

        # Build payload: u8 scope + [u64 user_id] + u64 bps
        payload = bytearray()
        scope_byte = 0 if scope == "global" else 1
        payload.append(scope_byte)
        if scope_byte == 1:
            payload.extend(struct.pack(">Q", user_id))
        payload.extend(struct.pack(">Q", bytes_per_second))

        header = _make_header(MSG_TYPE_THROTTLE_SET, len(payload))
        sock.sendall(header + bytes(payload))

        # Read response
        resp_header = _recv_full(sock, HEADER_SIZE)
        resp_type, resp_len = _parse_header(resp_header)
        if resp_type != MSG_TYPE_THROTTLE_SET_RESP:
            raise CoreProtocolError(f"Expected THROTTLE_SET_RESP (71), got {resp_type}")
        resp_payload = _recv_full(sock, resp_len) if resp_len > 0 else b""

        ok = resp_payload[0] == 1 if len(resp_payload) > 0 else False
        bps = struct.unpack(">Q", resp_payload[1:9])[0] if len(resp_payload) >= 9 else 0

        return {"ok": ok, "bps": bps}

    except socket.timeout:
        raise CoreConnectionError("Timeout connecting to Core")
    except socket.error as e:
        raise CoreConnectionError(f"Cannot connect to Core: {e}")
    finally:
        if sock:
            sock.close()


def get_transfer_list(timeout: float = 3.0) -> list:
    """
    Get list of active file transfers from Core server.

    Returns:
        List of dicts with keys: transfer_id, state, sender, receiver, filename,
        file_size, bytes_received, speed
    """
    sock = None
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(timeout)
        sock.connect((CORE_TCP_HOST, CORE_TCP_PORT))

        _consume_key_exchange_pubkey(sock)

        header = _make_header(MSG_TYPE_TRANSFER_LIST_REQ, 0)
        sock.sendall(header)

        resp_header = _recv_full(sock, HEADER_SIZE)
        resp_type, resp_len = _parse_header(resp_header)
        if resp_type != MSG_TYPE_TRANSFER_LIST_RESP:
            raise CoreProtocolError(f"Expected TRANSFER_LIST_RESP (73), got {resp_type}")

        payload = _recv_full(sock, resp_len) if resp_len > 0 else b""
        if len(payload) < 2:
            return []

        pos = 0
        count = struct.unpack(">H", payload[pos:pos+2])[0]; pos += 2

        state_names = ["OFFERED", "ACCEPTED", "RECEIVING", "COMPLETED", "FAILED"]
        transfers = []

        for _ in range(count):
            if pos + 8 > len(payload):
                break
            tid = struct.unpack(">Q", payload[pos:pos+8])[0]; pos += 8

            if pos + 1 > len(payload):
                break
            state_val = payload[pos]; pos += 1
            state_str = state_names[state_val] if state_val < len(state_names) else "UNKNOWN"

            # sender
            slen = struct.unpack(">H", payload[pos:pos+2])[0]; pos += 2
            sender = payload[pos:pos+slen].decode("utf-8", errors="replace"); pos += slen

            # receiver
            rlen = struct.unpack(">H", payload[pos:pos+2])[0]; pos += 2
            receiver = payload[pos:pos+rlen].decode("utf-8", errors="replace"); pos += rlen

            # filename
            flen = struct.unpack(">H", payload[pos:pos+2])[0]; pos += 2
            filename = payload[pos:pos+flen].decode("utf-8", errors="replace"); pos += flen

            # file_size, bytes_received, speed
            file_size = struct.unpack(">Q", payload[pos:pos+8])[0]; pos += 8
            bytes_rx  = struct.unpack(">Q", payload[pos:pos+8])[0]; pos += 8
            speed     = struct.unpack(">Q", payload[pos:pos+8])[0]; pos += 8

            progress = (bytes_rx / file_size * 100) if file_size > 0 else 0

            transfers.append({
                "transfer_id": tid,
                "state": state_str,
                "sender": sender,
                "receiver": receiver,
                "filename": filename,
                "file_size": file_size,
                "bytes_received": bytes_rx,
                "progress": round(progress, 1),
                "speed": speed,
                "speed_kbs": round(speed / 1024, 1) if speed > 0 else 0,
            })

        return transfers

    except socket.timeout:
        raise CoreConnectionError("Timeout connecting to Core")
    except socket.error as e:
        raise CoreConnectionError(f"Cannot connect to Core: {e}")
    finally:
        if sock:
            sock.close()


def get_voice_sessions(timeout: float = 3.0) -> list:
    """
    Get list of active voice sessions from Core server.

    Returns:
        List of dicts with keys: session_id, caller, callee, frames_relayed
    """
    sock = None
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(timeout)
        sock.connect((CORE_TCP_HOST, CORE_TCP_PORT))

        _consume_key_exchange_pubkey(sock)

        header = _make_header(MSG_TYPE_VOICE_SESSION_LIST_REQ, 0)
        sock.sendall(header)

        resp_header = _recv_full(sock, HEADER_SIZE)
        resp_type, resp_len = _parse_header(resp_header)
        if resp_type != MSG_TYPE_VOICE_SESSION_LIST_RESP:
            raise CoreProtocolError(f"Expected VOICE_SESSION_LIST_RESP (85), got {resp_type}")

        payload = _recv_full(sock, resp_len) if resp_len > 0 else b""
        if len(payload) < 2:
            return []

        pos = 0
        count = struct.unpack(">H", payload[pos:pos+2])[0]; pos += 2

        sessions = []
        for _ in range(count):
            if pos + 4 > len(payload):
                break
            session_id = struct.unpack(">I", payload[pos:pos+4])[0]; pos += 4

            # caller
            clen = struct.unpack(">H", payload[pos:pos+2])[0]; pos += 2
            caller = payload[pos:pos+clen].decode("utf-8", errors="replace"); pos += clen

            # callee
            elen = struct.unpack(">H", payload[pos:pos+2])[0]; pos += 2
            callee = payload[pos:pos+elen].decode("utf-8", errors="replace"); pos += elen

            # frames_relayed
            frames = struct.unpack(">Q", payload[pos:pos+8])[0]; pos += 8

            sessions.append({
                "session_id": session_id,
                "caller": caller,
                "callee": callee,
                "frames_relayed": frames,
            })

        return sessions

    except socket.timeout:
        raise CoreConnectionError("Timeout connecting to Core")
    except socket.error as e:
        raise CoreConnectionError(f"Cannot connect to Core: {e}")
    finally:
        if sock:
            sock.close()


def login_user(username: str, password: str, timeout: float = 8.0) -> dict:
    """
    Login a user.
    
    Returns:
        {"ok": bool, "token": str, "user_id": int, "username": str, "msg": str}
    """
    sock = None
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(timeout)
        sock.connect((CORE_TCP_HOST, CORE_TCP_PORT))

        _consume_key_exchange_pubkey(sock)
        
        # Send LOGIN_REQ
        payload = _make_auth_payload(username, password)
        header = _make_header(MSG_TYPE_LOGIN_REQ, len(payload))
        sock.sendall(header + payload)
        
        # Read response
        header_data = sock.recv(HEADER_SIZE)
        if len(header_data) < HEADER_SIZE:
            raise CoreProtocolError("Incomplete response header")
        
        msg_type, payload_len = _parse_header(header_data)
        if msg_type != MSG_TYPE_LOGIN_RESP:
            raise CoreProtocolError(f"Unexpected message type: {msg_type}")
        
        resp_payload = b""
        while len(resp_payload) < payload_len:
            chunk = sock.recv(payload_len - len(resp_payload))
            if not chunk:
                raise CoreProtocolError("Incomplete payload")
            resp_payload += chunk
        
        return _parse_login_resp(resp_payload)
        
    except socket.timeout:
        raise CoreConnectionError(f"Timeout connecting to Core")
    except socket.error as e:
        raise CoreConnectionError(f"Cannot connect to Core: {e}")
    finally:
        if sock:
            sock.close()

