"""
Messaging service — text messages, voice messages, and file-reference messages.
Voice messages are stored as uploaded audio files with a message entry linking to them.
"""
import os
import logging
import psycopg2
import psycopg2.extras
from typing import List, Optional

logger = logging.getLogger(__name__)

DB_HOST = os.getenv("FSX_DB_HOST", "127.0.0.1")
DB_PORT = os.getenv("FSX_DB_PORT", "5432")
DB_USER = os.getenv("FSX_DB_USER", "fsx")
DB_PASS = os.getenv("FSX_DB_PASSWORD", "fsxpass")
DB_NAME = os.getenv("FSX_DB_NAME", "fsx")


def _get_conn():
    return psycopg2.connect(
        host=DB_HOST, port=DB_PORT,
        user=DB_USER, password=DB_PASS,
        dbname=DB_NAME,
    )


def _ensure_table():
    try:
        conn = _get_conn()
        cur = conn.cursor()
        cur.execute("""
            CREATE TABLE IF NOT EXISTS messages (
                id BIGSERIAL PRIMARY KEY,
                sender_username TEXT NOT NULL,
                receiver_username TEXT NOT NULL,
                msg_type TEXT NOT NULL DEFAULT 'text',
                content TEXT NOT NULL DEFAULT '',
                file_id BIGINT,
                created_at TIMESTAMPTZ NOT NULL DEFAULT now()
            )
        """)
        conn.commit()
        conn.close()
    except Exception as e:
        logger.warning(f"Could not ensure messages table: {e}")


_ensure_table()


def send_text_message(sender: str, receiver: str, content: str) -> dict:
    """Send a text message."""
    try:
        conn = _get_conn()
        cur = conn.cursor()
        cur.execute(
            """INSERT INTO messages (sender_username, receiver_username, msg_type, content)
               VALUES (%s, %s, 'text', %s) RETURNING id, created_at""",
            (sender, receiver, content)
        )
        row = cur.fetchone()
        conn.commit()
        conn.close()
        logger.info(f"MSG_TEXT sender={sender} receiver={receiver} len={len(content)}")
        return {"ok": True, "id": row[0], "created_at": str(row[1])}
    except Exception as e:
        logger.error(f"MSG_TEXT_ERROR: {e}")
        return {"ok": False, "id": 0}


def send_voice_message(sender: str, receiver: str, file_id: int) -> dict:
    """Send a voice message (audio file already uploaded, just link it)."""
    try:
        conn = _get_conn()
        cur = conn.cursor()
        cur.execute(
            """INSERT INTO messages (sender_username, receiver_username, msg_type, content, file_id)
               VALUES (%s, %s, 'voice', '', %s) RETURNING id, created_at""",
            (sender, receiver, file_id)
        )
        row = cur.fetchone()
        conn.commit()
        conn.close()
        logger.info(f"MSG_VOICE sender={sender} receiver={receiver} file_id={file_id}")
        return {"ok": True, "id": row[0], "created_at": str(row[1])}
    except Exception as e:
        logger.error(f"MSG_VOICE_ERROR: {e}")
        return {"ok": False, "id": 0}


def send_file_message(sender: str, receiver: str, file_id: int) -> dict:
    """Send a file-sharing message (file already uploaded, link it)."""
    try:
        conn = _get_conn()
        cur = conn.cursor()
        cur.execute(
            """INSERT INTO messages (sender_username, receiver_username, msg_type, content, file_id)
               VALUES (%s, %s, 'file', '', %s) RETURNING id, created_at""",
            (sender, receiver, file_id)
        )
        row = cur.fetchone()
        conn.commit()
        conn.close()
        logger.info(f"MSG_FILE sender={sender} receiver={receiver} file_id={file_id}")
        return {"ok": True, "id": row[0], "created_at": str(row[1])}
    except Exception as e:
        logger.error(f"MSG_FILE_ERROR: {e}")
        return {"ok": False, "id": 0}


def get_conversation(user_a: str, user_b: str, limit: int = 100, before_id: Optional[int] = None) -> List[dict]:
    """Get messages between two users, newest first."""
    try:
        conn = _get_conn()
        cur = conn.cursor(cursor_factory=psycopg2.extras.RealDictCursor)

        if before_id:
            cur.execute(
                """SELECT m.id, m.sender_username, m.receiver_username, m.msg_type,
                          m.content, m.file_id, m.created_at AT TIME ZONE 'UTC' as created_at,
                          sf.filename as file_name, sf.file_size as file_size
                   FROM messages m
                   LEFT JOIN shared_files sf ON m.file_id = sf.id
                   WHERE ((m.sender_username = %s AND m.receiver_username = %s)
                       OR (m.sender_username = %s AND m.receiver_username = %s))
                     AND m.id < %s
                   ORDER BY m.created_at DESC LIMIT %s""",
                (user_a, user_b, user_b, user_a, before_id, limit)
            )
        else:
            cur.execute(
                """SELECT m.id, m.sender_username, m.receiver_username, m.msg_type,
                          m.content, m.file_id, m.created_at AT TIME ZONE 'UTC' as created_at,
                          sf.filename as file_name, sf.file_size as file_size
                   FROM messages m
                   LEFT JOIN shared_files sf ON m.file_id = sf.id
                   WHERE (m.sender_username = %s AND m.receiver_username = %s)
                      OR (m.sender_username = %s AND m.receiver_username = %s)
                   ORDER BY m.created_at DESC LIMIT %s""",
                (user_a, user_b, user_b, user_a, limit)
            )

        rows = cur.fetchall()
        conn.close()

        messages = []
        for r in rows:
            messages.append({
                "id": r["id"],
                "sender": r["sender_username"],
                "receiver": r["receiver_username"],
                "type": r["msg_type"],
                "content": r["content"],
                "file_id": r["file_id"],
                "file_name": r["file_name"],
                "file_size": r["file_size"],
                "created_at": str(r["created_at"]),
            })
        # Return in chronological order (oldest first)
        messages.reverse()
        return messages

    except Exception as e:
        logger.error(f"MSG_CONVERSATION_ERROR: {e}")
        return []


def get_new_messages_after(user_a: str, user_b: str, after_id: int) -> List[dict]:
    """Get messages newer than a given ID (for polling)."""
    try:
        conn = _get_conn()
        cur = conn.cursor(cursor_factory=psycopg2.extras.RealDictCursor)
        cur.execute(
            """SELECT m.id, m.sender_username, m.receiver_username, m.msg_type,
                      m.content, m.file_id, m.created_at AT TIME ZONE 'UTC' as created_at,
                      sf.filename as file_name, sf.file_size as file_size
               FROM messages m
               LEFT JOIN shared_files sf ON m.file_id = sf.id
               WHERE ((m.sender_username = %s AND m.receiver_username = %s)
                   OR (m.sender_username = %s AND m.receiver_username = %s))
                 AND m.id > %s
               ORDER BY m.created_at ASC LIMIT 50""",
            (user_a, user_b, user_b, user_a, after_id)
        )
        rows = cur.fetchall()
        conn.close()

        return [{
            "id": r["id"],
            "sender": r["sender_username"],
            "receiver": r["receiver_username"],
            "type": r["msg_type"],
            "content": r["content"],
            "file_id": r["file_id"],
            "file_name": r["file_name"],
            "file_size": r["file_size"],
            "created_at": str(r["created_at"]),
        } for r in rows]

    except Exception as e:
        logger.error(f"MSG_NEW_ERROR: {e}")
        return []
