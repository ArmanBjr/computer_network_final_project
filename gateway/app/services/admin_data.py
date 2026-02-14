"""
Admin data service — aggregated queries for the admin dashboard.
Provides user management, system stats, and transfer history.
"""
import os
import logging
import psycopg2
import psycopg2.extras
from typing import List

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


def get_all_users() -> List[dict]:
    """Get all registered users with stats."""
    try:
        conn = _get_conn()
        cur = conn.cursor(cursor_factory=psycopg2.extras.RealDictCursor)
        cur.execute("""
            SELECT
                u.id, u.username, u.email,
                u.created_at AT TIME ZONE 'UTC' as created_at,
                COALESCE(msg_sent.cnt, 0) as messages_sent,
                COALESCE(files_sent.cnt, 0) as files_sent,
                COALESCE(files_recv.cnt, 0) as files_received
            FROM users u
            LEFT JOIN (
                SELECT sender_username, COUNT(*) as cnt FROM messages GROUP BY sender_username
            ) msg_sent ON msg_sent.sender_username = u.username
            LEFT JOIN (
                SELECT sender_username, COUNT(*) as cnt FROM shared_files GROUP BY sender_username
            ) files_sent ON files_sent.sender_username = u.username
            LEFT JOIN (
                SELECT receiver_username, COUNT(*) as cnt FROM shared_files GROUP BY receiver_username
            ) files_recv ON files_recv.receiver_username = u.username
            ORDER BY u.created_at DESC
        """)
        rows = cur.fetchall()
        conn.close()

        return [{
            "id": r["id"],
            "username": r["username"],
            "email": r["email"],
            "created_at": str(r["created_at"]),
            "messages_sent": int(r["messages_sent"]),
            "files_sent": int(r["files_sent"]),
            "files_received": int(r["files_received"]),
        } for r in rows]

    except Exception as e:
        logger.error(f"ADMIN_USERS_ERROR: {e}")
        return []


def get_system_stats() -> dict:
    """Get comprehensive system statistics."""
    try:
        conn = _get_conn()
        cur = conn.cursor(cursor_factory=psycopg2.extras.RealDictCursor)

        # Users
        cur.execute("SELECT COUNT(*) as total_users FROM users")
        users = cur.fetchone()

        # Messages
        cur.execute("""
            SELECT
                COUNT(*) as total_messages,
                COUNT(CASE WHEN msg_type = 'text' THEN 1 END) as text_messages,
                COUNT(CASE WHEN msg_type = 'voice' THEN 1 END) as voice_messages,
                COUNT(CASE WHEN msg_type = 'file' THEN 1 END) as file_messages
            FROM messages
        """)
        msgs = cur.fetchone()

        # Files
        cur.execute("""
            SELECT
                COUNT(*) as total_files,
                COALESCE(SUM(file_size), 0) as total_bytes,
                COUNT(CASE WHEN status = 'pending' THEN 1 END) as pending_files,
                COUNT(CASE WHEN status = 'downloaded' THEN 1 END) as downloaded_files
            FROM shared_files
        """)
        files = cur.fetchone()

        # Transfers (C++ core)
        cur.execute("""
            SELECT
                COUNT(*) as total_transfers,
                COUNT(CASE WHEN state = 'COMPLETED' THEN 1 END) as completed_transfers,
                COUNT(CASE WHEN state = 'FAILED' THEN 1 END) as failed_transfers,
                COUNT(CASE WHEN state = 'RECEIVING' THEN 1 END) as active_transfers,
                COALESCE(SUM(file_size), 0) as total_transfer_bytes
            FROM transfer_resume
        """)
        transfers = cur.fetchone()

        # Sessions
        cur.execute("SELECT COUNT(*) as active_sessions FROM sessions WHERE expires_at > now()")
        sessions = cur.fetchone()

        conn.close()

        return {
            "total_users": users["total_users"] if users else 0,
            "total_messages": msgs["total_messages"] if msgs else 0,
            "text_messages": msgs["text_messages"] if msgs else 0,
            "voice_messages": msgs["voice_messages"] if msgs else 0,
            "file_messages": msgs["file_messages"] if msgs else 0,
            "total_files": files["total_files"] if files else 0,
            "total_file_bytes": int(files["total_bytes"]) if files else 0,
            "pending_files": files["pending_files"] if files else 0,
            "downloaded_files": files["downloaded_files"] if files else 0,
            "total_transfers": transfers["total_transfers"] if transfers else 0,
            "completed_transfers": transfers["completed_transfers"] if transfers else 0,
            "failed_transfers": transfers["failed_transfers"] if transfers else 0,
            "active_transfers": transfers["active_transfers"] if transfers else 0,
            "total_transfer_bytes": int(transfers["total_transfer_bytes"]) if transfers else 0,
            "active_sessions": sessions["active_sessions"] if sessions else 0,
        }

    except Exception as e:
        logger.error(f"ADMIN_STATS_ERROR: {e}")
        return {}


def get_all_transfers_history(limit: int = 200) -> List[dict]:
    """Get transfer history from database."""
    try:
        conn = _get_conn()
        cur = conn.cursor(cursor_factory=psycopg2.extras.RealDictCursor)
        cur.execute("""
            SELECT
                tr.id, tr.transfer_id, tr.filename, tr.file_size,
                tr.chunk_size, tr.last_acked_chunk_index, tr.bytes_received,
                tr.state, tr.created_at AT TIME ZONE 'UTC' as created_at,
                tr.updated_at AT TIME ZONE 'UTC' as updated_at,
                su.username as sender_username,
                ru.username as receiver_username
            FROM transfer_resume tr
            LEFT JOIN users su ON su.id = tr.sender_user_id
            LEFT JOIN users ru ON ru.id = tr.receiver_user_id
            ORDER BY tr.updated_at DESC
            LIMIT %s
        """, (limit,))
        rows = cur.fetchall()
        conn.close()

        result = []
        for r in rows:
            total_chunks = (r["file_size"] // r["chunk_size"] + 1) if r["chunk_size"] > 0 else 0
            progress = round((r["last_acked_chunk_index"] / total_chunks * 100) if total_chunks > 0 else 0, 1)
            result.append({
                "id": r["id"],
                "transfer_id": r["transfer_id"],
                "sender": r["sender_username"] or "unknown",
                "receiver": r["receiver_username"] or "unknown",
                "filename": r["filename"],
                "file_size": r["file_size"],
                "bytes_received": r["bytes_received"],
                "progress": progress,
                "state": r["state"],
                "created_at": str(r["created_at"]),
                "updated_at": str(r["updated_at"]),
            })
        return result

    except Exception as e:
        logger.error(f"ADMIN_TRANSFERS_ERROR: {e}")
        return []
