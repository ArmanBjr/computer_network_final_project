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
from fastapi import UploadFile

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
                core_transfer_id BIGINT DEFAULT NULL,
                created_at TIMESTAMPTZ NOT NULL DEFAULT now()
            )
        """)
        # Add column if missing (for existing DBs)
        cur.execute("""
            DO $$ BEGIN
                ALTER TABLE shared_files ADD COLUMN core_transfer_id BIGINT DEFAULT NULL;
            EXCEPTION WHEN duplicate_column THEN NULL;
            END $$;
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


async def save_uploaded_file_streaming(
    file: UploadFile,
    sender: str,
    receiver: str,
    max_size: int = 500 * 1024 * 1024,
) -> dict:
    """
    Stream-write an uploaded file to disk in chunks (supports large files).
    Returns: {"ok": bool, "file_id": int, "filename": str, "size": int}
    """
    filename = file.filename or "unnamed"
    uid = uuid.uuid4().hex[:12]
    safe_name = f"{uid}_{filename}"
    path = os.path.join(UPLOAD_DIR, safe_name)

    CHUNK = 1024 * 1024  # 1 MB read chunks
    total = 0

    try:
        with open(path, "wb") as f:
            while True:
                chunk = await file.read(CHUNK)
                if not chunk:
                    break
                total += len(chunk)
                if total > max_size:
                    f.close()
                    os.remove(path)
                    return {
                        "ok": False,
                        "file_id": 0,
                        "filename": filename,
                        "size": 0,
                        "msg": f"File too large (max {max_size // (1024*1024)} MB)",
                    }
                f.write(chunk)

        # Record in DB
        conn = _get_conn()
        cur = conn.cursor()
        cur.execute(
            """INSERT INTO shared_files (sender_username, receiver_username, filename, file_size, storage_path, status)
               VALUES (%s, %s, %s, %s, %s, 'pending') RETURNING id""",
            (sender, receiver, filename, total, path),
        )
        file_id = cur.fetchone()[0]
        conn.commit()
        conn.close()

        logger.info(f"FILE_UPLOAD_STREAM sender={sender} receiver={receiver} filename={filename} size={total} id={file_id}")
        return {"ok": True, "file_id": file_id, "filename": filename, "size": total, "storage_path": path}

    except Exception as e:
        # Cleanup partial file
        if os.path.exists(path):
            try:
                os.remove(path)
            except OSError:
                pass
        logger.error(f"FILE_UPLOAD_STREAM_ERROR sender={sender} error={e}")
        return {"ok": False, "file_id": 0, "filename": filename, "size": 0, "msg": str(e)}


def update_core_transfer_id(file_id: int, core_transfer_id: int) -> bool:
    """Update a shared_files row with the Core transfer_id."""
    try:
        conn = _get_conn()
        cur = conn.cursor()
        cur.execute(
            "UPDATE shared_files SET core_transfer_id = %s WHERE id = %s",
            (core_transfer_id, file_id),
        )
        conn.commit()
        conn.close()
        return True
    except Exception as e:
        logger.error(f"UPDATE_CORE_TRANSFER_ID_ERROR file_id={file_id} err={e}")
        return False


def get_file_core_transfer_id(file_id: int) -> Optional[int]:
    """Get the core_transfer_id for a shared file (None if not proxied)."""
    try:
        conn = _get_conn()
        cur = conn.cursor()
        cur.execute("SELECT core_transfer_id FROM shared_files WHERE id = %s", (file_id,))
        row = cur.fetchone()
        conn.close()
        if row and row[0]:
            return int(row[0])
        return None
    except Exception as e:
        logger.error(f"GET_CORE_TRANSFER_ID_ERROR file_id={file_id} err={e}")
        return None


def mark_file_downloaded(file_id: int, username: str) -> bool:
    """Mark a shared file as downloaded (by the receiver)."""
    try:
        conn = _get_conn()
        cur = conn.cursor()
        cur.execute(
            "UPDATE shared_files SET status = 'downloaded' WHERE id = %s AND receiver_username = %s AND status = 'pending'",
            (file_id, username),
        )
        conn.commit()
        conn.close()
        return True
    except Exception as e:
        logger.error(f"MARK_DOWNLOADED_ERROR file_id={file_id} err={e}")
        return False


def get_all_files(limit: int = 200) -> List[dict]:
    """Get all shared files for admin dashboard."""
    try:
        conn = _get_conn()
        cur = conn.cursor(cursor_factory=psycopg2.extras.RealDictCursor)
        cur.execute(
            """SELECT id, sender_username, receiver_username, filename, file_size,
                      status, core_transfer_id,
                      created_at AT TIME ZONE 'UTC' as created_at
               FROM shared_files
               ORDER BY created_at DESC
               LIMIT %s""",
            (limit,)
        )
        rows = cur.fetchall()
        conn.close()

        return [{
            "id": r["id"],
            "sender": r["sender_username"],
            "receiver": r["receiver_username"],
            "filename": r["filename"],
            "size": r["file_size"],
            "status": r["status"],
            "core_transfer_id": r.get("core_transfer_id"),
            "created_at": str(r["created_at"]),
        } for r in rows]

    except Exception as e:
        logger.error(f"FILE_ALL_LIST_ERROR: {e}")
        return []


def get_storage_stats() -> dict:
    """Get file storage statistics."""
    try:
        conn = _get_conn()
        cur = conn.cursor(cursor_factory=psycopg2.extras.RealDictCursor)
        cur.execute("""
            SELECT
                COUNT(*) as total_files,
                COALESCE(SUM(file_size), 0) as total_bytes,
                COUNT(CASE WHEN status = 'pending' THEN 1 END) as pending,
                COUNT(CASE WHEN status = 'downloaded' THEN 1 END) as downloaded
            FROM shared_files
        """)
        row = cur.fetchone()
        conn.close()
        return dict(row) if row else {"total_files": 0, "total_bytes": 0, "pending": 0, "downloaded": 0}
    except Exception as e:
        logger.error(f"STORAGE_STATS_ERROR: {e}")
        return {"total_files": 0, "total_bytes": 0, "pending": 0, "downloaded": 0}
