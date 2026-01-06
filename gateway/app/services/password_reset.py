"""
Password Reset Service - Manages reset tokens in database
"""
import secrets
import logging
from datetime import datetime, timedelta, timezone
from typing import Optional
import psycopg2
from psycopg2.extras import RealDictCursor
import os

logger = logging.getLogger(__name__)

# Database connection
DB_CONFIG = {
    'host': os.getenv("FSX_DB_HOST", "localhost"),
    'port': int(os.getenv("FSX_DB_PORT", "5432")),
    'user': os.getenv("FSX_DB_USER", "fsx"),
    'password': os.getenv("FSX_DB_PASSWORD", "fsxpass"),
    'dbname': os.getenv("FSX_DB_NAME", "fsx")
}


def get_db_connection():
    """Get a database connection"""
    return psycopg2.connect(**DB_CONFIG)


def create_reset_token(email: str) -> Optional[str]:
    """
    Create a password reset token for the given email.
    Returns token if user exists, None otherwise.
    """
    conn = None
    try:
        conn = get_db_connection()
        cur = conn.cursor(cursor_factory=RealDictCursor)
        
        # Find user by email
        cur.execute("""
            SELECT id, username, email FROM users WHERE email = %s LIMIT 1
        """, (email,))
        
        user = cur.fetchone()
        if not user:
            logger.warning(f"RESET_TOKEN_USER_NOT_FOUND email={email}")
            return None
        
        user_id = user['id']
        username = user['username']
        
        # Generate secure token
        token = secrets.token_urlsafe(32)
        
        # Expires in 1 hour
        expires_at = datetime.now(timezone.utc) + timedelta(hours=1)
        
        # Insert reset token
        cur.execute("""
            INSERT INTO password_reset_tokens (user_id, token, email, expires_at)
            VALUES (%s, %s, %s, %s)
            RETURNING token
        """, (user_id, token, email, expires_at))
        
        conn.commit()
        logger.info(f"RESET_TOKEN_CREATED email={email} user_id={user_id} token={token[:8]}...")
        return token
        
    except Exception as e:
        logger.error(f"RESET_TOKEN_CREATE_ERROR email={email} error={e}")
        if conn:
            conn.rollback()
        return None
    finally:
        if conn:
            conn.close()


def validate_reset_token(token: str) -> Optional[dict]:
    """
    Validate a reset token and return user info if valid.
    Returns None if token is invalid or expired.
    """
    conn = None
    try:
        conn = get_db_connection()
        cur = conn.cursor(cursor_factory=RealDictCursor)
        
        # Check token
        cur.execute("""
            SELECT prt.id, prt.user_id, prt.email, prt.expires_at, prt.used,
                   u.username
            FROM password_reset_tokens prt
            JOIN users u ON u.id = prt.user_id
            WHERE prt.token = %s
            LIMIT 1
        """, (token,))
        
        row = cur.fetchone()
        if not row:
            logger.warning(f"RESET_TOKEN_NOT_FOUND token={token[:8]}...")
            return None
        
        # Check if expired
        expires_at = row['expires_at']
        # Ensure timezone-aware comparison
        # psycopg2 returns TIMESTAMPTZ as timezone-aware datetime
        if expires_at.tzinfo is None:
            # If somehow naive, assume UTC
            expires_at = expires_at.replace(tzinfo=timezone.utc)
        else:
            # Convert to UTC for consistent comparison
            expires_at = expires_at.astimezone(timezone.utc)
        now = datetime.now(timezone.utc)
        # Compare timezone-aware datetimes
        if now > expires_at:
            logger.warning(f"RESET_TOKEN_EXPIRED token={token[:8]}... now={now.isoformat()} expires_at={expires_at.isoformat()}")
            return None
        
        # Check if already used
        if row['used']:
            logger.warning(f"RESET_TOKEN_ALREADY_USED token={token[:8]}...")
            return None
        
        return {
            'user_id': row['user_id'],
            'username': row['username'],
            'email': row['email']
        }
        
    except Exception as e:
        logger.error(f"RESET_TOKEN_VALIDATE_ERROR token={token[:8]}... error={e}")
        return None
    finally:
        if conn:
            conn.close()


def mark_token_used(token: str) -> bool:
    """Mark a reset token as used"""
    conn = None
    try:
        conn = get_db_connection()
        cur = conn.cursor()
        
        cur.execute("""
            UPDATE password_reset_tokens
            SET used = TRUE
            WHERE token = %s
        """, (token,))
        
        conn.commit()
        logger.info(f"RESET_TOKEN_MARKED_USED token={token[:8]}...")
        return True
        
    except Exception as e:
        logger.error(f"RESET_TOKEN_MARK_USED_ERROR token={token[:8]}... error={e}")
        if conn:
            conn.rollback()
        return False
    finally:
        if conn:
            conn.close()


def change_user_password(user_id: int, new_password: str) -> bool:
    """
    Change user password by updating pass_hash in database.
    Uses same PBKDF2-HMAC-SHA256 algorithm as Core (100000 iterations).
    Returns True if successful, False otherwise.
    """
    import hashlib
    import secrets
    import hmac
    
    conn = None
    try:
        # Hash the new password using same method as Core
        # Core uses PBKDF2-HMAC-SHA256 with 100000 iterations
        # Format: pbkdf2$iters$salt_hex$hash_hex
        
        iters = 100000
        salt_len = 16
        dk_len = 32  # 256-bit
        
        # Generate random salt
        salt = secrets.token_bytes(salt_len)
        salt_hex = salt.hex()
        
        # PBKDF2-HMAC-SHA256
        dk = hashlib.pbkdf2_hmac('sha256', new_password.encode('utf-8'), salt, iters, dk_len)
        dk_hex = dk.hex()
        
        # Format: pbkdf2$iters$salt_hex$hash_hex
        pass_hash_stored = f"pbkdf2${iters}${salt_hex}${dk_hex}"
        
        conn = get_db_connection()
        cur = conn.cursor()
        
        cur.execute("""
            UPDATE users
            SET pass_hash = %s
            WHERE id = %s
        """, (pass_hash_stored, user_id))
        
        conn.commit()
        logger.info(f"PASSWORD_CHANGED user_id={user_id}")
        return True
        
    except Exception as e:
        logger.error(f"PASSWORD_CHANGE_ERROR user_id={user_id} error={e}")
        if conn:
            conn.rollback()
        return False
    finally:
        if conn:
            conn.close()

