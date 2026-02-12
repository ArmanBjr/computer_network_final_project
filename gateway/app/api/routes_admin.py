from fastapi import APIRouter, HTTPException, UploadFile, File, Form
from fastapi.responses import FileResponse
from pydantic import BaseModel
from typing import Optional
import logging
import asyncio
from app.core_client import (
    get_online_users, register_user, login_user,
    set_throttle as core_set_throttle,
    get_transfer_list as core_get_transfer_list,
    get_voice_sessions as core_get_voice_sessions,
    CoreConnectionError, CoreProtocolError, CoreClientError
)
from app.services.user_session import session_manager
from app.services.password_reset import create_reset_token, validate_reset_token, mark_token_used, change_user_password
from app.services.email_service import email_service
from app.services.file_sharing import (
    save_uploaded_file, get_files_for_user, get_files_between,
    get_file_path, get_file_info,
)
from app.services.messaging import (
    send_text_message, send_voice_message, send_file_message,
    get_conversation, get_new_messages_after,
)

router = APIRouter()
logger = logging.getLogger(__name__)


class RegisterRequest(BaseModel):
    username: str
    email: str
    password: str


class LoginRequest(BaseModel):
    username: str
    password: str


class ForgotPasswordRequest(BaseModel):
    email: str


class ResetPasswordRequest(BaseModel):
    token: str
    new_password: str


@router.get("/health")
async def health():
    return {"ok": True, "service": "fsx_gateway"}


@router.get("/online")
async def get_online():
    """
    Get list of online users - combines Core online users and Gateway sessions.
    
    Returns:
        JSON with count and list of usernames
    """
    try:
        # Get users from Core (C++ clients with TCP connections)
        core_usernames = set(get_online_users(timeout=5.0))
        
        # Get users from Gateway sessions (Browser logins with TCP connections)
        gateway_usernames = set(await session_manager.get_online_usernames())
        
        # Combine both lists (remove duplicates)
        all_usernames = sorted(list(core_usernames | gateway_usernames))
        count = len(all_usernames)
        
        logger.info(f"GATEWAY_ONLINE_LIST_OK count={count} core={len(core_usernames)} gateway={len(gateway_usernames)}")
        return {
            "count": count,
            "users": all_usernames
        }
    except CoreConnectionError as e:
        # If Core is down, still return Gateway sessions
        gateway_usernames = await session_manager.get_online_usernames()
        logger.warning(f"GATEWAY_ONLINE_LIST_CORE_DOWN gateway_count={len(gateway_usernames)}")
        return {
            "count": len(gateway_usernames),
            "users": gateway_usernames
        }
    except Exception as e:
        logger.error(f"GATEWAY_ONLINE_LIST_ERROR type=internal error={e}")
        # Fallback to Gateway sessions only
        gateway_usernames = await session_manager.get_online_usernames()
        return {
            "count": len(gateway_usernames),
            "users": gateway_usernames
        }


@router.post("/register")
async def register(req: RegisterRequest):
    """
    Register a new user.
    
    Returns:
        {"ok": bool, "msg": str}
    """
    try:
        result = register_user(req.username, req.email, req.password, timeout=8.0)
        if result["ok"]:
            logger.info(f"GATEWAY_REGISTER_OK username={req.username}")
        else:
            logger.warning(f"GATEWAY_REGISTER_FAIL username={req.username} reason={result['msg']}")
        return result
    except CoreConnectionError as e:
        logger.error(f"GATEWAY_REGISTER_ERROR type=connection error={e}")
        return {"ok": False, "msg": f"Core server unavailable: {e}"}
    except (CoreProtocolError, CoreClientError) as e:
        logger.error(f"GATEWAY_REGISTER_ERROR type=protocol error={e}")
        return {"ok": False, "msg": f"Protocol error: {e}"}
    except Exception as e:
        logger.error(f"GATEWAY_REGISTER_ERROR type=internal error={e}")
        return {"ok": False, "msg": f"Internal error: {e}"}


@router.post("/login")
async def login(req: LoginRequest):
    """
    Login a user and create a persistent TCP connection to Core.
    
    Returns:
        {"ok": bool, "token": str, "user_id": int, "username": str, "msg": str}
    """
    try:
        result = login_user(req.username, req.password, timeout=8.0)
        if result["ok"]:
            # Create persistent TCP session to Core
            session = await session_manager.create_session(req.username, req.password)
            if session:
                logger.info(f"GATEWAY_LOGIN_OK username={req.username} user_id={result.get('user_id', 0)} session_created=True")
            else:
                logger.warning(f"GATEWAY_LOGIN_OK username={req.username} but session_creation_failed")
        else:
            logger.warning(f"GATEWAY_LOGIN_FAIL username={req.username} reason={result['msg']}")
        return result
    except CoreConnectionError as e:
        logger.error(f"GATEWAY_LOGIN_ERROR type=connection error={e}")
        return {"ok": False, "msg": f"Core server unavailable: {e}"}
    except (CoreProtocolError, CoreClientError) as e:
        logger.error(f"GATEWAY_LOGIN_ERROR type=protocol error={e}")
        return {"ok": False, "msg": f"Protocol error: {e}"}
    except Exception as e:
        logger.error(f"GATEWAY_LOGIN_ERROR type=internal error={e}")
        return {"ok": False, "msg": f"Internal error: {e}"}


class LogoutRequest(BaseModel):
    username: str


@router.post("/logout")
async def logout(req: LogoutRequest):
    """
    Logout a user and close their TCP connection to Core.
    
    Returns:
        {"ok": bool, "msg": str}
    """
    try:
        await session_manager.remove_session(req.username)
        logger.info(f"GATEWAY_LOGOUT_OK username={req.username}")
        return {"ok": True, "msg": "Logged out successfully"}
    except Exception as e:
        logger.error(f"GATEWAY_LOGOUT_ERROR username={req.username} error={e}")
        return {"ok": False, "msg": str(e)}


@router.post("/forgot-password")
async def forgot_password(req: ForgotPasswordRequest):
    """
    Request password reset - creates token and sends email.
    
    Returns:
        {"ok": bool, "msg": str}
    """
    try:
        # Create reset token (returns None if user doesn't exist)
        token = create_reset_token(req.email)
        
        # Always return success (security: don't reveal if email exists)
        if token:
            # Get username from DB for email
            try:
                from app.services.password_reset import get_db_connection
                import psycopg2.extras
                conn = get_db_connection()
                cur = conn.cursor(cursor_factory=psycopg2.extras.RealDictCursor)
                cur.execute("SELECT username FROM users WHERE email = %s LIMIT 1", (req.email,))
                user_row = cur.fetchone()
                username = user_row['username'] if user_row else req.email
                conn.close()
            except Exception as e:
                logger.warning(f"GATEWAY_FORGOT_PASSWORD_USERNAME_FETCH_ERROR email={req.email} error={e}")
                username = req.email
            
            # Send email
            email_sent = email_service.send_password_reset(
                email=req.email,
                reset_token=token,
                username=username
            )
            
            if email_sent:
                logger.info(f"GATEWAY_FORGOT_PASSWORD_OK email={req.email} token={token[:8]}...")
            else:
                logger.warning(f"GATEWAY_FORGOT_PASSWORD_EMAIL_FAILED email={req.email}")
                # Still return success, but log the issue
        
        # Always return success message (security best practice)
        return {
            "ok": True,
            "msg": "If the email exists in our system, a password reset link has been sent."
        }
    except Exception as e:
        logger.error(f"GATEWAY_FORGOT_PASSWORD_ERROR email={req.email} error={e}")
        # Still return success (don't reveal errors to user)
        return {
            "ok": True,
            "msg": "If the email exists in our system, a password reset link has been sent."
        }


# ---------- Phase 9: Throttle & Transfer List ----------

class ThrottleRequest(BaseModel):
    scope: str = "global"              # "global" | "user"
    bytes_per_second: int = 0          # 0 = unlimited
    user_id: Optional[int] = 0


@router.post("/throttle")
async def set_throttle_endpoint(req: ThrottleRequest):
    """
    Set transfer throttle on the Core server.
    scope: "global" (all users) or "user" (specific user_id).
    bytes_per_second: 0 = unlimited.
    """
    try:
        result = core_set_throttle(
            scope=req.scope,
            bytes_per_second=req.bytes_per_second,
            user_id=req.user_id or 0,
            timeout=3.0,
        )
        bps = req.bytes_per_second
        logger.info(
            f"GATEWAY_THROTTLE_SET scope={req.scope} bps={bps} "
            f"user_id={req.user_id} ok={result.get('ok')}"
        )
        return result
    except CoreConnectionError as e:
        logger.error(f"GATEWAY_THROTTLE_ERROR connection: {e}")
        raise HTTPException(status_code=503, detail=f"Core unavailable: {e}")
    except Exception as e:
        logger.error(f"GATEWAY_THROTTLE_ERROR: {e}")
        raise HTTPException(status_code=500, detail=str(e))


@router.get("/transfers")
async def list_transfers():
    """
    List active file transfers from the Core server.
    """
    try:
        transfers = core_get_transfer_list(timeout=3.0)
        logger.info(f"GATEWAY_TRANSFER_LIST count={len(transfers)}")
        return {"count": len(transfers), "transfers": transfers}
    except CoreConnectionError as e:
        logger.error(f"GATEWAY_TRANSFER_LIST_ERROR connection: {e}")
        raise HTTPException(status_code=503, detail=f"Core unavailable: {e}")
    except Exception as e:
        logger.error(f"GATEWAY_TRANSFER_LIST_ERROR: {e}")
        raise HTTPException(status_code=500, detail=str(e))


@router.get("/voice-sessions")
async def list_voice_sessions():
    """
    List active voice chat sessions from the Core server.
    """
    try:
        sessions = core_get_voice_sessions(timeout=3.0)
        logger.info(f"GATEWAY_VOICE_SESSION_LIST count={len(sessions)}")
        return {"count": len(sessions), "sessions": sessions}
    except CoreConnectionError as e:
        logger.error(f"GATEWAY_VOICE_SESSION_LIST_ERROR connection: {e}")
        return {"count": 0, "sessions": []}
    except Exception as e:
        logger.error(f"GATEWAY_VOICE_SESSION_LIST_ERROR: {e}")
        return {"count": 0, "sessions": []}


@router.post("/reset-password")
async def reset_password(req: ResetPasswordRequest):
    """
    Reset password using a reset token.
    
    Returns:
        {"ok": bool, "msg": str}
    """
    try:
        # Validate token
        user_info = validate_reset_token(req.token)
        if not user_info:
            return {
                "ok": False,
                "msg": "Invalid or expired reset token."
            }
        
        # Validate new password
        if not req.new_password or len(req.new_password) < 6:
            return {
                "ok": False,
                "msg": "Password must be at least 6 characters."
            }
        
        # Change password in database
        success = change_user_password(user_info['user_id'], req.new_password)
        if not success:
            return {
                "ok": False,
                "msg": "Failed to update password. Please try again."
            }
        
        # Mark token as used
        mark_token_used(req.token)
        
        logger.info(f"GATEWAY_RESET_PASSWORD_OK username={user_info['username']} user_id={user_info['user_id']}")
        return {
            "ok": True,
            "msg": "Password has been reset successfully. You can now login with your new password."
        }
    except Exception as e:
        logger.error(f"GATEWAY_RESET_PASSWORD_ERROR error={e}")
        return {
            "ok": False,
            "msg": "An error occurred while resetting your password."
        }


# ============ Phase 11: Web File Sharing ============

MAX_UPLOAD_SIZE = 50 * 1024 * 1024  # 50 MB


@router.post("/upload")
async def upload_file(
    file: UploadFile = File(...),
    sender: str = Form(...),
    receiver: str = Form(...),
):
    """
    Upload a file to share with another user.
    The file is stored on the gateway and recorded in the database.
    """
    try:
        data = await file.read()
        if len(data) > MAX_UPLOAD_SIZE:
            raise HTTPException(status_code=413, detail="File too large (max 50 MB)")

        result = save_uploaded_file(
            sender=sender,
            receiver=receiver,
            filename=file.filename or "unnamed",
            data=data,
        )

        if result["ok"]:
            logger.info(f"UPLOAD_OK sender={sender} receiver={receiver} file={file.filename} size={len(data)}")
        else:
            logger.error(f"UPLOAD_FAIL sender={sender} receiver={receiver}")

        return result
    except HTTPException:
        raise
    except Exception as e:
        logger.error(f"UPLOAD_ERROR: {e}")
        raise HTTPException(status_code=500, detail=str(e))


@router.get("/files/{username}")
async def list_user_files(username: str):
    """List files sent to or by a user."""
    files = get_files_for_user(username)
    return {"count": len(files), "files": files}


@router.get("/files-between/{user_a}/{user_b}")
async def list_files_between(user_a: str, user_b: str):
    """List files shared between two specific users."""
    files = get_files_between(user_a, user_b)
    return {"count": len(files), "files": files}


@router.get("/download/{file_id}")
async def download_file(file_id: int, username: str = ""):
    """
    Download a shared file.
    Username is passed as query param for access control.
    """
    if not username:
        raise HTTPException(status_code=400, detail="Username required")

    info = get_file_info(file_id)
    if not info:
        raise HTTPException(status_code=404, detail="File not found")

    path = get_file_path(file_id, username)
    if not path:
        raise HTTPException(status_code=403, detail="Access denied or file missing")

    return FileResponse(
        path=path,
        filename=info["filename"],
        media_type="application/octet-stream",
    )


# ============ Phase 11: Messaging (text + voice + file) ============

class TextMessageRequest(BaseModel):
    sender: str
    receiver: str
    content: str


@router.post("/messages/send")
async def send_message(req: TextMessageRequest):
    """Send a text message."""
    if not req.content.strip():
        return {"ok": False, "msg": "Empty message"}
    result = send_text_message(req.sender, req.receiver, req.content.strip())
    return result


@router.post("/messages/voice")
async def send_voice_msg(
    file: UploadFile = File(...),
    sender: str = Form(...),
    receiver: str = Form(...),
):
    """Record and send a voice message (audio file upload + message entry)."""
    try:
        data = await file.read()
        if len(data) > 10 * 1024 * 1024:  # 10 MB max for voice
            raise HTTPException(status_code=413, detail="Voice message too large")

        # Save as file
        fname = file.filename or "voice.webm"
        file_result = save_uploaded_file(sender, receiver, fname, data)
        if not file_result["ok"]:
            return {"ok": False, "msg": "Failed to save voice message"}

        # Create voice message entry
        msg_result = send_voice_message(sender, receiver, file_result["file_id"])
        return msg_result
    except HTTPException:
        raise
    except Exception as e:
        logger.error(f"VOICE_MSG_ERROR: {e}")
        return {"ok": False, "msg": str(e)}


@router.post("/messages/file")
async def send_file_msg(
    file: UploadFile = File(...),
    sender: str = Form(...),
    receiver: str = Form(...),
):
    """Send a file as a message (upload + message entry)."""
    try:
        data = await file.read()
        if len(data) > MAX_UPLOAD_SIZE:
            raise HTTPException(status_code=413, detail="File too large")

        fname = file.filename or "file"
        file_result = save_uploaded_file(sender, receiver, fname, data)
        if not file_result["ok"]:
            return {"ok": False, "msg": "Failed to save file"}

        msg_result = send_file_message(sender, receiver, file_result["file_id"])
        msg_result["file_id"] = file_result["file_id"]
        msg_result["filename"] = fname
        msg_result["size"] = file_result["size"]
        return msg_result
    except HTTPException:
        raise
    except Exception as e:
        logger.error(f"FILE_MSG_ERROR: {e}")
        return {"ok": False, "msg": str(e)}


@router.get("/messages/{user_a}/{user_b}")
async def get_messages(user_a: str, user_b: str, after_id: int = 0):
    """
    Get conversation between two users.
    If after_id > 0, returns only messages newer than that ID (for polling).
    """
    if after_id > 0:
        msgs = get_new_messages_after(user_a, user_b, after_id)
    else:
        msgs = get_conversation(user_a, user_b, limit=100)
    return {"messages": msgs, "count": len(msgs)}
