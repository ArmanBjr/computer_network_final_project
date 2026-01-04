from fastapi import APIRouter, HTTPException
from pydantic import BaseModel
import logging
import asyncio
from app.core_client import (
    get_online_users, register_user, login_user,
    CoreConnectionError, CoreProtocolError, CoreClientError
)
from app.services.user_session import session_manager
from app.services.password_reset import create_reset_token, validate_reset_token, mark_token_used, change_user_password
from app.services.email_service import email_service

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
        core_usernames = set(get_online_users(timeout=2.0))
        
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
        result = register_user(req.username, req.email, req.password, timeout=2.0)
        if result["ok"]:
            logger.info(f"GATEWAY_REGISTER_OK username={req.username}")
        else:
            logger.warning(f"GATEWAY_REGISTER_FAIL username={req.username} reason={result['msg']}")
        return result
    except CoreConnectionError as e:
        logger.error(f"GATEWAY_REGISTER_ERROR type=connection error={e}")
        raise HTTPException(status_code=503, detail=f"Core server unavailable: {e}")
    except (CoreProtocolError, CoreClientError) as e:
        logger.error(f"GATEWAY_REGISTER_ERROR type=protocol error={e}")
        raise HTTPException(status_code=502, detail=f"Core protocol error: {e}")
    except Exception as e:
        logger.error(f"GATEWAY_REGISTER_ERROR type=internal error={e}")
        raise HTTPException(status_code=500, detail=f"Internal error: {e}")


@router.post("/login")
async def login(req: LoginRequest):
    """
    Login a user and create a persistent TCP connection to Core.
    
    Returns:
        {"ok": bool, "token": str, "user_id": int, "username": str, "msg": str}
    """
    try:
        result = login_user(req.username, req.password, timeout=2.0)
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
        raise HTTPException(status_code=503, detail=f"Core server unavailable: {e}")
    except (CoreProtocolError, CoreClientError) as e:
        logger.error(f"GATEWAY_LOGIN_ERROR type=protocol error={e}")
        raise HTTPException(status_code=502, detail=f"Core protocol error: {e}")
    except Exception as e:
        logger.error(f"GATEWAY_LOGIN_ERROR type=internal error={e}")
        raise HTTPException(status_code=500, detail=f"Internal error: {e}")


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
