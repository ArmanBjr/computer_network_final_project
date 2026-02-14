CREATE TABLE IF NOT EXISTS users (
  id BIGSERIAL PRIMARY KEY,
  username TEXT UNIQUE NOT NULL,
  email TEXT UNIQUE NOT NULL,
  pass_hash TEXT NOT NULL,
  created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS idx_users_email ON users(email);

CREATE TABLE IF NOT EXISTS sessions (
  id BIGSERIAL PRIMARY KEY,
  user_id BIGINT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  token TEXT UNIQUE NOT NULL,
  created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
  expires_at TIMESTAMPTZ NOT NULL,
  last_seen_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS idx_sessions_user_id ON sessions(user_id);
CREATE INDEX IF NOT EXISTS idx_sessions_token ON sessions(token);

-- Password reset tokens
CREATE TABLE IF NOT EXISTS password_reset_tokens (
  id BIGSERIAL PRIMARY KEY,
  user_id BIGINT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  token TEXT UNIQUE NOT NULL,
  email TEXT NOT NULL,
  created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
  expires_at TIMESTAMPTZ NOT NULL,
  used BOOLEAN NOT NULL DEFAULT FALSE
);

CREATE INDEX IF NOT EXISTS idx_reset_tokens_user_id ON password_reset_tokens(user_id);
CREATE INDEX IF NOT EXISTS idx_reset_tokens_token ON password_reset_tokens(token);
CREATE INDEX IF NOT EXISTS idx_reset_tokens_email ON password_reset_tokens(email);

-- Phase 5: Transfer resume state
CREATE TABLE IF NOT EXISTS transfer_resume (
  id BIGSERIAL PRIMARY KEY,
  transfer_id BIGINT NOT NULL UNIQUE,
  sender_user_id BIGINT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  receiver_user_id BIGINT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  filename TEXT NOT NULL,
  file_size BIGINT NOT NULL,
  chunk_size INTEGER NOT NULL,
  last_acked_chunk_index INTEGER NOT NULL DEFAULT 0,
  bytes_received BIGINT NOT NULL DEFAULT 0,
  state TEXT NOT NULL DEFAULT 'RECEIVING', -- OFFERED, ACCEPTED, RECEIVING, COMPLETED, FAILED
  temp_file_path TEXT,
  created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
  updated_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS idx_transfer_resume_transfer_id ON transfer_resume(transfer_id);
CREATE INDEX IF NOT EXISTS idx_transfer_resume_sender ON transfer_resume(sender_user_id);
CREATE INDEX IF NOT EXISTS idx_transfer_resume_receiver ON transfer_resume(receiver_user_id);
CREATE INDEX IF NOT EXISTS idx_transfer_resume_state ON transfer_resume(state);

-- Phase 11: Web file sharing (gateway-based uploads, proxied through C++ Core)
CREATE TABLE IF NOT EXISTS shared_files (
  id BIGSERIAL PRIMARY KEY,
  sender_username TEXT NOT NULL,
  receiver_username TEXT NOT NULL,
  filename TEXT NOT NULL,
  file_size BIGINT NOT NULL DEFAULT 0,
  storage_path TEXT NOT NULL,
  status TEXT NOT NULL DEFAULT 'pending',  -- pending, accepted, downloaded, expired
  core_transfer_id BIGINT DEFAULT NULL,   -- links to C++ Core transfer_id
  created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS idx_shared_files_receiver ON shared_files(receiver_username);
CREATE INDEX IF NOT EXISTS idx_shared_files_sender ON shared_files(sender_username);

-- Phase 11: Chat messages (text, voice-message, file-ref)
CREATE TABLE IF NOT EXISTS messages (
  id BIGSERIAL PRIMARY KEY,
  sender_username TEXT NOT NULL,
  receiver_username TEXT NOT NULL,
  msg_type TEXT NOT NULL DEFAULT 'text',   -- text | voice | file
  content TEXT NOT NULL DEFAULT '',        -- text content or empty for voice/file
  file_id BIGINT REFERENCES shared_files(id) ON DELETE SET NULL,
  created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS idx_messages_pair ON messages(sender_username, receiver_username);
CREATE INDEX IF NOT EXISTS idx_messages_created ON messages(created_at);