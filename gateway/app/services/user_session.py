"""
User Session Manager - Maintains TCP connections to Core for logged-in users
"""
import asyncio
import socket
import struct
import logging
from typing import Dict, Optional
from app.core_client import (
    CORE_TCP_HOST, CORE_TCP_PORT, HEADER_SIZE,
    _make_header, _parse_header, _make_auth_payload, _parse_login_resp,
    MSG_TYPE_LOGIN_REQ, MSG_TYPE_LOGIN_RESP, MSG_TYPE_ONLINE_LIST_REQ, MSG_TYPE_ONLINE_LIST_RESP,
    MSG_TYPE_PING,
    CoreConnectionError, CoreProtocolError
)

logger = logging.getLogger(__name__)


class UserSession:
    """Represents a user's TCP connection to Core"""
    def __init__(self, username: str, token: str, user_id: int, sock: socket.socket):
        self.username = username
        self.token = token
        self.user_id = user_id
        self.sock = sock
        self.connected = True
        self._ping_task = None

    def start_ping_loop(self):
        """Start sending PING every 5 seconds to keep connection alive"""
        async def ping_loop():
            while self.connected:
                try:
                    await asyncio.sleep(5)
                    if self.connected and self.sock:
                        # Send PING
                        ping_payload = b'ping'
                        header = _make_header(MSG_TYPE_PING, len(ping_payload))
                        self.sock.sendall(header + ping_payload)
                        logger.debug(f"USER_SESSION_PING username={self.username}")
                except Exception as e:
                    logger.warning(f"USER_SESSION_PING_ERROR username={self.username} error={e}")
                    self.connected = False
                    break
        
        try:
            loop = asyncio.get_running_loop()
        except RuntimeError:
            loop = asyncio.new_event_loop()
            asyncio.set_event_loop(loop)
        
        self._ping_task = loop.create_task(ping_loop())

    def close(self):
        """Close the TCP connection"""
        self.connected = False
        if self._ping_task:
            self._ping_task.cancel()
        if self.sock:
            try:
                self.sock.close()
            except:
                pass


class UserSessionManager:
    """Manages TCP connections to Core for logged-in users"""
    def __init__(self):
        self.sessions: Dict[str, UserSession] = {}  # username -> UserSession
        self._lock = asyncio.Lock()

    async def create_session(self, username: str, password: str) -> Optional[UserSession]:
        """
        Create a TCP connection to Core and login the user.
        Returns UserSession if successful, None otherwise.
        """
        async with self._lock:
            # Check if session already exists
            if username in self.sessions:
                session = self.sessions[username]
                if session.connected:
                    return session
                else:
                    # Clean up disconnected session
                    del self.sessions[username]

            try:
                # Create TCP connection
                sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                sock.settimeout(5.0)
                sock.connect((CORE_TCP_HOST, CORE_TCP_PORT))

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

                result = _parse_login_resp(resp_payload)

                if result["ok"]:
                    # Create session
                    session = UserSession(
                        username=result["username"],
                        token=result["token"],
                        user_id=result["user_id"],
                        sock=sock
                    )
                    self.sessions[username] = session
                    
                    # Start ping loop to keep connection alive (in background)
                    session.start_ping_loop()
                    
                    logger.info(f"USER_SESSION_CREATED username={username} user_id={result['user_id']}")
                    return session
                else:
                    sock.close()
                    logger.warning(f"USER_SESSION_LOGIN_FAILED username={username} reason={result['msg']}")
                    return None

            except Exception as e:
                logger.error(f"USER_SESSION_ERROR username={username} error={e}")
                try:
                    sock.close()
                except:
                    pass
                return None

    async def remove_session(self, username: str):
        """Remove and close a user session"""
        async with self._lock:
            if username in self.sessions:
                session = self.sessions[username]
                if session.connected and session.sock:
                    try:
                        # Close TCP connection (Core will detect disconnect and remove from online list)
                        session.sock.close()
                        logger.info(f"USER_SESSION_TCP_CLOSED username={username}")
                    except Exception as e:
                        logger.warning(f"USER_SESSION_TCP_CLOSE_ERROR username={username} error={e}")
                session.close()
                del self.sessions[username]
                logger.info(f"USER_SESSION_REMOVED username={username} count_after={len(self.sessions)}")
            else:
                logger.warning(f"USER_SESSION_REMOVE_NOT_FOUND username={username} available={list(self.sessions.keys())}")

    async def get_online_usernames(self) -> list:
        """Get list of usernames with active sessions"""
        async with self._lock:
            return [username for username, session in self.sessions.items() if session.connected]

    async def has_session(self, username: str) -> bool:
        """Check if user has an active session"""
        async with self._lock:
            return username in self.sessions and self.sessions[username].connected


# Global session manager instance
session_manager = UserSessionManager()

