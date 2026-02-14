from fastapi import APIRouter, HTTPException, UploadFile, File, Form
from fastapi.responses import FileResponse, Response
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
    save_uploaded_file, save_uploaded_file_streaming,
    get_files_for_user, get_files_between,
    get_file_path, get_file_info,
    get_all_files, get_storage_stats,
    update_core_transfer_id, get_file_core_transfer_id,
    mark_file_downloaded,
)
from app.services.messaging import (
    send_text_message, send_voice_message, send_file_message,
    get_conversation, get_new_messages_after,
    get_message_stats, get_recent_messages,
)
from app.services.admin_data import (
    get_all_users, get_system_stats,
    get_all_transfers_history,
)
from app.services.file_transfer_proxy import upload_file_via_core, download_file_from_core

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

MAX_UPLOAD_SIZE = 500 * 1024 * 1024  # 500 MB


@router.post("/upload")
async def upload_file(
    file: UploadFile = File(...),
    sender: str = Form(...),
    receiver: str = Form(...),
):
    """
    Upload a file to share with another user.
    Saves locally first, then proxies through C++ Core for full protocol processing.
    """
    try:
        # Step 1: stream-save to local disk + DB
        result = await save_uploaded_file_streaming(
            file=file,
            sender=sender,
            receiver=receiver,
            max_size=MAX_UPLOAD_SIZE,
        )

        if not result["ok"]:
            logger.error(f"UPLOAD_FAIL sender={sender} receiver={receiver} msg={result.get('msg','')}")
            return result

        file_id = result["file_id"]
        storage_path = result.get("storage_path", "")

        # Step 2: proxy through C++ Core
        if storage_path:
            creds = await session_manager.get_credentials(sender)
            if creds:
                _, sender_pw = creds
                proxy_result = await upload_file_via_core(
                    sender_username=sender,
                    sender_password=sender_pw,
                    receiver_username=receiver,
                    filename=result["filename"],
                    filepath=storage_path,
                )
                if proxy_result["ok"]:
                    update_core_transfer_id(file_id, proxy_result["transfer_id"])
                    logger.info(f"UPLOAD_PROXY_OK file_id={file_id} core_tid={proxy_result['transfer_id']}")
                else:
                    logger.warning(f"UPLOAD_PROXY_FAIL file_id={file_id} err={proxy_result['error']} (local copy kept)")
            else:
                logger.warning(f"UPLOAD_PROXY_SKIP sender={sender} (no credentials, local copy kept)")

        logger.info(f"UPLOAD_OK sender={sender} receiver={receiver} file={file.filename} size={result['size']}")
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
    Tries Core protocol download first; falls back to local disk.
    """
    if not username:
        raise HTTPException(status_code=400, detail="Username required")

    info = get_file_info(file_id)
    if not info:
        raise HTTPException(status_code=404, detail="File not found")

    # Try downloading from Core (if file was proxied)
    core_tid = get_file_core_transfer_id(file_id)
    if core_tid:
        creds = await session_manager.get_credentials(username)
        if creds:
            _, pw = creds
            core_result = await download_file_from_core(username, pw, core_tid)
            if core_result:
                file_bytes, core_filename = core_result
                mark_file_downloaded(file_id, username)
                logger.info(f"DOWNLOAD_VIA_CORE file_id={file_id} core_tid={core_tid} size={len(file_bytes)}")
                return Response(
                    content=file_bytes,
                    media_type="application/octet-stream",
                    headers={
                        "Content-Disposition": f'attachment; filename="{info["filename"]}"'
                    },
                )
            else:
                logger.warning(f"DOWNLOAD_CORE_FAIL file_id={file_id} core_tid={core_tid}, falling back to local")

    # Fallback: local disk
    path = get_file_path(file_id, username)
    if not path:
        raise HTTPException(status_code=403, detail="Access denied or file missing")

    return FileResponse(
        path=path,
        filename=info["filename"],
        media_type="application/octet-stream",
    )


# ============ Resume / Retry upload via Core ============

@router.post("/retry-upload/{file_id}")
async def retry_upload_via_core(file_id: int, username: str = Form(...)):
    """
    Retry proxying a locally-stored file through the C++ Core.
    Used when the initial Core proxy failed but the file was saved locally.
    Supports resume: if partial data was already sent to Core, continues from
    the last ACK'd chunk instead of re-uploading from scratch.
    """
    info = get_file_info(file_id)
    if not info:
        raise HTTPException(status_code=404, detail="File not found")

    # Already routed through Core?
    existing_tid = get_file_core_transfer_id(file_id)
    if existing_tid:
        return {"ok": True, "msg": f"Already routed through Core (transfer #{existing_tid})"}

    # Get local path
    path = get_file_path(file_id, info.get("sender", username))
    if not path:
        raise HTTPException(status_code=404, detail="Local file not found on disk")

    sender = info.get("sender", "")
    receiver = info.get("receiver", "")
    if not sender or not receiver:
        raise HTTPException(status_code=400, detail="Cannot determine sender/receiver")

    # Sender credentials
    creds = await session_manager.get_credentials(sender)
    if not creds:
        return {"ok": False, "msg": f"Sender '{sender}' not online — cannot authenticate with Core"}

    _, sender_pw = creds

    proxy_result = await upload_file_via_core(
        sender_username=sender,
        sender_password=sender_pw,
        receiver_username=receiver,
        filename=info.get("filename", "unknown"),
        filepath=path,
    )

    if proxy_result["ok"]:
        update_core_transfer_id(file_id, proxy_result["transfer_id"])
        logger.info(f"RETRY_UPLOAD_OK file_id={file_id} core_tid={proxy_result['transfer_id']}")
        return {"ok": True, "msg": f"Routed through Core (transfer #{proxy_result['transfer_id']})"}
    else:
        logger.warning(f"RETRY_UPLOAD_FAIL file_id={file_id} err={proxy_result['error']}")
        return {"ok": False, "msg": f"Core proxy failed: {proxy_result['error']}"}


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
    """Send a file as a message (upload + message entry). Proxied through C++ Core."""
    try:
        # Step 1: stream-save locally
        file_result = await save_uploaded_file_streaming(
            file=file,
            sender=sender,
            receiver=receiver,
            max_size=MAX_UPLOAD_SIZE,
        )
        if not file_result["ok"]:
            return {"ok": False, "msg": file_result.get("msg", "Failed to save file")}

        file_id = file_result["file_id"]
        storage_path = file_result.get("storage_path", "")

        # Step 2: proxy through C++ Core
        if storage_path:
            creds = await session_manager.get_credentials(sender)
            if creds:
                _, sender_pw = creds
                proxy_result = await upload_file_via_core(
                    sender_username=sender,
                    sender_password=sender_pw,
                    receiver_username=receiver,
                    filename=file_result["filename"],
                    filepath=storage_path,
                )
                if proxy_result["ok"]:
                    update_core_transfer_id(file_id, proxy_result["transfer_id"])
                    logger.info(f"FILE_MSG_PROXY_OK file_id={file_id} core_tid={proxy_result['transfer_id']}")
                else:
                    logger.warning(f"FILE_MSG_PROXY_FAIL file_id={file_id} err={proxy_result['error']}")
            else:
                logger.warning(f"FILE_MSG_PROXY_SKIP sender={sender} (no credentials)")

        # Step 3: create message entry
        msg_result = send_file_message(sender, receiver, file_id)
        msg_result["file_id"] = file_id
        msg_result["filename"] = file_result["filename"]
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


# ============ Admin Data Endpoints ============

@router.get("/admin/users")
async def admin_all_users():
    """Get all registered users with stats."""
    users = get_all_users()
    return {"count": len(users), "users": users}


@router.get("/admin/files")
async def admin_all_files():
    """Get complete file sharing history."""
    files = get_all_files()
    stats = get_storage_stats()
    return {"count": len(files), "files": files, "stats": stats}


@router.get("/admin/messages")
async def admin_message_stats():
    """Get message statistics and recent messages."""
    stats = get_message_stats()
    recent = get_recent_messages(limit=50)
    return {"stats": stats, "recent": recent, "count": len(recent)}


@router.get("/admin/transfers")
async def admin_transfer_history():
    """Get transfer history from database (completed/failed/in-progress)."""
    transfers = get_all_transfers_history()
    return {"count": len(transfers), "transfers": transfers}


@router.get("/admin/stats")
async def admin_system_stats():
    """Get comprehensive system statistics."""
    stats = get_system_stats()
    return stats


@router.get("/admin/voice-calls")
async def admin_voice_calls():
    """Get active browser voice calls + Core voice sessions."""
    from app.api.ws_voice import get_active_browser_voice_calls
    browser_calls = get_active_browser_voice_calls()

    core_sessions = []
    try:
        core_sessions = core_get_voice_sessions(timeout=2.0)
        # Add source tag
        for s in core_sessions:
            s["source"] = "core"
    except Exception:
        pass

    all_calls = browser_calls + core_sessions
    return {"count": len(all_calls), "calls": all_calls}
