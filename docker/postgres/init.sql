-- Phase 0: فقط اسکلت دیتابیس
CREATE TABLE IF NOT EXISTS users (
  id SERIAL PRIMARY KEY,
  username TEXT UNIQUE NOT NULL,
  password_hash TEXT NOT NULL,
  created_at TIMESTAMP NOT NULL DEFAULT NOW()
);

CREATE TABLE IF NOT EXISTS transfers (
  id SERIAL PRIMARY KEY,
  transfer_id TEXT UNIQUE NOT NULL,
  sender TEXT NOT NULL,
  receiver TEXT NOT NULL,
  filename TEXT NOT NULL,
  total_bytes BIGINT NOT NULL DEFAULT 0,
  status TEXT NOT NULL DEFAULT 'CREATED',
  created_at TIMESTAMP NOT NULL DEFAULT NOW()
);
