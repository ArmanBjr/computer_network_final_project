"""
Web-based file sharing service.
Files are uploaded through the gateway HTTP API and stored on disk.
Recipients can download them via their browser.
"""
import os
import uuid
import logging
import psycopg2
import psycopg2.extras
from typing import List, Optional

logger = logging.getLogger(__name__)

UPLOAD_DIR = os.getenv("FSX_UPLOAD_DIR", "/tmp/fsx_uploads")
os.makedirs(UPLOAD_DIR, exist_ok=True)

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
    """Create table if it doesn't exist (for fresh DB without migration)."""
    try:
        conn = _get_conn()
        cur = conn.cursor()
        cur.execute("""
            CREATE TABLE IF NOT EXISTS shared_files (
                id BIGSERIAL PRIMARY KEY,
                sender_username TEXT NOT NULL,
                receiver_username TEXT NOT NULL,
                filename TEXT NOT NULL,
                file_size BIGINT NOT NULL DEFAULT 0,
                storage_path TEXT NOT NULL,
                status TEXT NOT NULL DEFAULT 'pending',
                created_at TIMESTAMPTZ NOT NULL DEFAULT now()
            )
        """)
        conn.commit()
        conn.close()
    except Exception as e:
        logger.warning(f"Could not ensure shared_files table: {e}")


# Ensure table exists on module load
_ensure_table()


def save_uploaded_file(sender: str, receiver: str, filename: str, data: bytes) -> dict:
    """
    Save an uploaded file and record it in the database.
    Returns: {"ok": bool, "file_id": int, "filename": str, "size": int}
    """
    try:
        # Save to disk with unique name
        uid = uuid.uuid4().hex[:12]
        safe_name = f"{uid}_{filename}"
        path = os.path.join(UPLOAD_DIR, safe_name)

        with open(path, "wb") as f:
            f.write(data)

        size = len(data)

        # Record in DB
        conn = _get_conn()
        cur = conn.cursor()
        cur.execute(
            """INSERT INTO shared_files (sender_username, receiver_username, filename, file_size, storage_path, status)
               VALUES (%s, %s, %s, %s, %s, 'pending') RETURNING id""",
            (sender, receiver, filename, size, path)
        )
        file_id = cur.fetchone()[0]
        conn.commit()
        conn.close()

        logger.info(f"FILE_UPLOAD sender={sender} receiver={receiver} filename={filename} size={size} id={file_id}")
        return {"ok": True, "file_id": file_id, "filename": filename, "size": size}

    except Exception as e:
        logger.error(f"FILE_UPLOAD_ERROR sender={sender} error={e}")
        return {"ok": False, "file_id": 0, "filename": filename, "size": 0}


def get_files_for_user(username: str) -> List[dict]:
    """Get all files shared with a user (incoming) or sent by a user (outgoing)."""
    try:
        conn = _get_conn()
        cur = conn.cursor(cursor_factory=psycopg2.extras.RealDictCursor)
        cur.execute(
            """SELECT id, sender_username, receiver_username, filename, file_size, status,
                      created_at AT TIME ZONE 'UTC' as created_at
               FROM shared_files
               WHERE receiver_username = %s OR sender_username = %s
               ORDER BY created_at DESC
               LIMIT 50""",
            (username, username)
        )
        rows = cur.fetchall()
        conn.close()

        files = []
        for r in rows:
            files.append({
                "id": r["id"],
                "sender": r["sender_username"],
                "receiver": r["receiver_username"],
                "filename": r["filename"],
                "size": r["file_size"],
                "status": r["status"],
                "created_at": str(r["created_at"]),
                "direction": "sent" if r["sender_username"] == username else "received",
            })
        return files

    except Exception as e:
        logger.error(f"FILE_LIST_ERROR username={username} error={e}")
        return []


def get_files_between(user_a: str, user_b: str) -> List[dict]:
    """Get files shared between two specific users."""
    try:
        conn = _get_conn()
        cur = conn.cursor(cursor_factory=psycopg2.extras.RealDictCursor)
        cur.execute(
            """SELECT id, sender_username, receiver_username, filename, file_size, status,
                      created_at AT TIME ZONE 'UTC' as created_at
               FROM shared_files
               WHERE (sender_username = %s AND receiver_username = %s)
                  OR (sender_username = %s AND receiver_username = %s)
               ORDER BY created_at DESC
               LIMIT 50""",
            (user_a, user_b, user_b, user_a)
        )
        rows = cur.fetchall()
        conn.close()

        files = []
        for r in rows:
            files.append({
                "id": r["id"],
                "sender": r["sender_username"],
                "receiver": r["receiver_username"],
                "filename": r["filename"],
                "size": r["file_size"],
                "status": r["status"],
                "created_at": str(r["created_at"]),
            })
        return files

    except Exception as e:
        logger.error(f"FILE_LIST_BETWEEN_ERROR error={e}")
        return []


def get_file_path(file_id: int, username: str) -> Optional[str]:
    """
    Get the disk path for a file, only if the user is the sender or receiver.
    Also marks it as 'downloaded' if it was pending.
    """
    try:
        conn = _get_conn()
        cur = conn.cursor(cursor_factory=psycopg2.extras.RealDictCursor)
        cur.execute(
            """SELECT id, sender_username, receiver_username, filename, storage_path, status
               FROM shared_files WHERE id = %s""",
            (file_id,)
        )
        row = cur.fetchone()
        if not row:
            conn.close()
            return None

        # Only sender or receiver can download
        if username not in (row["sender_username"], row["receiver_username"]):
            conn.close()
            return None

        # Mark as downloaded if pending
        if row["status"] == "pending" and username == row["receiver_username"]:
            cur.execute("UPDATE shared_files SET status = 'downloaded' WHERE id = %s", (file_id,))
            conn.commit()

        conn.close()

        path = row["storage_path"]
        if os.path.exists(path):
            return path
        return None

    except Exception as e:
        logger.error(f"FILE_DOWNLOAD_ERROR file_id={file_id} error={e}")
        return None


def get_file_info(file_id: int) -> Optional[dict]:
    """Get metadata for a file by ID."""
    try:
        conn = _get_conn()
        cur = conn.cursor(cursor_factory=psycopg2.extras.RealDictCursor)
        cur.execute(
            "SELECT id, sender_username, receiver_username, filename, file_size, status FROM shared_files WHERE id = %s",
            (file_id,)
        )
        row = cur.fetchone()
        conn.close()
        if row:
            return dict(row)
        return None
    except Exception as e:
        logger.error(f"FILE_INFO_ERROR file_id={file_id} error={e}")
        return None
