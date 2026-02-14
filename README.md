# FileShareX

A secure, real-time file sharing and communication platform built with C++ (Boost.Asio), Python (FastAPI), and PostgreSQL. Features encrypted file transfers, voice chat, text messaging, and a modern admin dashboard.

## Architecture

```
Browser (Web UI)          C++ Clients (CLI)
     |                         |
     v                         v
FastAPI Gateway (8000)    Core C++ Server (TCP 9000)
     |                         |
     +--------> PostgreSQL <---+
                (5432)

Voice: Browser WebSocket (Gateway) / UDP 9001 (C++ Opus)
```

**Components:**
- **Core** (C++) -- TCP server handling authentication, file transfers (chunked, encrypted, compressed), voice relay (UDP/Opus), session management
- **Gateway** (Python/FastAPI) -- Web UI, REST API, WebSocket dashboard, browser voice calls, file upload/download
- **Client** (C++) -- CLI test clients for file transfer, authentication, voice calls
- **PostgreSQL** -- User accounts, sessions, file metadata, chat messages

## Features

| Feature | Details |
|---------|---------|
| Authentication | Register/Login, PBKDF2 password hashing, session tokens |
| File Transfer | Chunked transfer, CRC32 per chunk, ACK/NAK retransmission |
| Resume | Interrupt and resume transfers from last acknowledged chunk |
| Compression | zlib compression before transfer |
| Encryption | AES-256-GCM payload encryption, RSA key exchange |
| Integrity | SHA-256 final file verification |
| Throttling | Token bucket rate limiting (global and per-user) |
| Voice Chat | UDP relay with Opus codec (C++), WebSocket relay (browser) |
| Text Chat | Real-time text messaging between users |
| Voice Messages | Record and send audio clips from browser |
| File Sharing | Drag-and-drop file send with download for receiver |
| Admin Dashboard | Real-time monitoring: online users, transfers, voice sessions, throttle control |
| Password Reset | Email-based password reset with token expiry |

## Ports

| Port | Protocol | Service |
|------|----------|---------|
| 9000 | TCP | Core server (client protocol) |
| 9001 | UDP | Voice relay (Opus frames) |
| 9100 | TCP | Admin port |
| 8000 | HTTP/WS | Gateway (Web UI + API) |
| 5432 | TCP | PostgreSQL |

## Quick Start (Docker)

### Prerequisites

- Docker and Docker Compose
- WSL2 (if on Windows)

### 1. Clone and start

```bash
git clone <repo-url> filesharex
cd filesharex

# Build and start all services
docker compose -f docker/compose.yml up -d --build
```

### 2. Verify containers are running

```bash
docker ps
```

You should see three containers: `fsx_db`, `fsx_core`, `fsx_gateway`.

If `fsx_core` is missing (crashed on startup before DB was ready):
```bash
docker compose -f docker/compose.yml restart core
```

### 3. Access the Web UI

| Page | URL |
|------|-----|
| Admin Dashboard | http://localhost:8000 |
| Login / Register | http://localhost:8000/login |
| Messenger | http://localhost:8000/messenger |

**WSL users:** If `localhost` doesn't work from Windows browser, use the WSL IP:
```bash
hostname -I    # get WSL IP
```
Then open `http://<WSL_IP>:8000` in your Windows browser.

### 4. Register and use

1. Open http://localhost:8000/login
2. Click "Sign up here" and create an account
3. Sign in -- you'll be redirected to the Messenger
4. Open a second browser/incognito window, register a second user
5. Both users appear in each other's "Online Users" sidebar

### 5. Features to try

**Text Chat:**
- Select a user from the sidebar
- Type a message and press Enter

**Send Files:**
- Click the paperclip icon in chat header
- Select a file -- it uploads and appears as a message
- The receiver can click the download button

**Voice Messages:**
- Hold the microphone button (bottom left of chat input)
- Speak while holding
- Release to send -- appears as an audio player in chat

**Voice Call:**
- Click the purple phone icon in chat header
- The other user sees an incoming call overlay
- Accept to start a full-duplex voice call
- Call timer shows duration
- Red button to hang up

**Admin Dashboard:**
- Go to http://localhost:8000
- See real-time online users, active transfers, voice sessions
- Set throttle limits (global or per-user)

### 6. View logs

```bash
docker logs -f fsx_core       # C++ server logs
docker logs -f fsx_gateway    # Python gateway logs
docker logs -f fsx_db         # PostgreSQL logs
```

### 7. Stop / Reset

```bash
# Stop all containers
docker compose -f docker/compose.yml down

# Stop and delete all data (fresh start)
docker compose -f docker/compose.yml down -v
```

## Local Development (without Docker)

### Core (C++)

Requirements: `cmake`, `g++`, `libboost-dev`, `libpqxx-dev`, `libssl-dev`, `zlib1g-dev`, `libopus-dev`

```bash
# Install dependencies (Ubuntu/Debian)
sudo apt-get install -y build-essential cmake libboost-all-dev libpqxx-dev \
  libssl-dev zlib1g-dev libopus-dev pkg-config

# Build
cmake -S core -B core/build -DCMAKE_BUILD_TYPE=Release
cmake --build core/build -j$(nproc)

# Run (needs PostgreSQL running)
export FSX_DB_HOST=127.0.0.1 FSX_DB_PORT=5432 FSX_DB_USER=fsx \
       FSX_DB_PASSWORD=fsxpass FSX_DB_NAME=fsx
./core/build/fsx_core
```

### Client (C++)

```bash
# Install dependencies
sudo apt-get install -y build-essential cmake libboost-all-dev libopus-dev pkg-config

# Build
cmake -S client -B client/build
cmake --build client/build -j$(nproc)

# Run tests
./client/build/test_auth register testuser testpass 127.0.0.1 9000
./client/build/test_file_transfer 127.0.0.1 9000
./client/build/test_voice call 127.0.0.1 9000
```

### Gateway (Python)

Requirements: Python 3.10+

```bash
cd gateway
python -m venv .venv && source .venv/bin/activate
pip install -e .
uvicorn app.main:app --host 0.0.0.0 --port 8000 --reload
```

## Project Structure

```
filesharex/
├── core/                      # C++ server
│   ├── CMakeLists.txt
│   ├── include/fsx/           # Headers
│   │   ├── net/               # TCP server, session, UDP voice
│   │   ├── protocol/          # Message types, serialization
│   │   ├── auth/              # Authentication handler
│   │   ├── crypto/            # AES-GCM, RSA
│   │   ├── transfer/          # Integrity, compression, throttle
│   │   ├── storage/           # File store, resume store
│   │   └── voice/             # Opus codec, jitter buffer, voice manager
│   └── src/                   # Implementation
├── client/                    # C++ test clients
│   ├── CMakeLists.txt
│   └── src/
│       ├── test_auth.cpp      # Auth test client
│       ├── test_file_transfer.cpp  # File transfer test
│       └── test_voice.cpp     # Voice call test
├── gateway/                   # Python FastAPI gateway
│   ├── pyproject.toml
│   └── app/
│       ├── main.py            # FastAPI app
│       ├── core_client.py     # TCP client to Core
│       ├── settings.py        # Configuration
│       ├── api/
│       │   ├── routes_admin.py    # REST endpoints
│       │   ├── ws_dashboard.py    # Dashboard WebSocket
│       │   └── ws_voice.py        # Voice call WebSocket relay
│       ├── services/
│       │   ├── user_session.py    # Persistent TCP sessions
│       │   ├── file_sharing.py    # File upload/download
│       │   ├── messaging.py       # Text/voice/file messages
│       │   ├── password_reset.py  # Reset token management
│       │   └── email_service.py   # SMTP email
│       ├── templates/         # Jinja2 HTML
│       │   ├── dashboard.html
│       │   ├── messenger.html
│       │   ├── login.html
│       │   └── reset_password.html
│       └── static/            # CSS + JS
│           ├── css/
│           └── js/
├── docker/
│   ├── compose.yml            # Docker Compose
│   ├── core.Dockerfile        # C++ build
│   ├── gateway.Dockerfile     # Python build
│   └── postgres/
│       └── init.sql           # DB schema
├── scripts/                   # Test scripts (WSL)
└── .gitignore
```

## Protocol Overview

The Core server uses a custom binary protocol over TCP:

**Header (12 bytes):**
```
| Magic (4B) | Version (1B) | Type (1B) | Length (4B) | Reserved (2B) |
| "FSX1"     | 0x01         | msg_type  | payload_len | 0x0000        |
```

**Message Types:**
| Type | Name | Direction |
|------|------|-----------|
| 2/3 | PING/PONG | Both |
| 10/11 | REGISTER_REQ/RESP | Client → Server |
| 12/13 | LOGIN_REQ/RESP | Client → Server |
| 20/21 | ONLINE_LIST_REQ/RESP | Both |
| 30-36 | FILE_OFFER/ACCEPT/CHUNK/ACK/NAK/COMPLETE/HASH | Both |
| 40/41 | RESUME_QUERY/REPLY | Client → Server |
| 60/61 | KEY_EXCHANGE (RSA) | Both |
| 70-73 | THROTTLE_SET/RESP, TRANSFER_LIST | Admin |
| 80-85 | VOICE_CALL/RESP/NOTIFY/END, SESSION_LIST | Both |

## WSL Docker Build Issues

If you get `SIGBUS` errors building Docker images from Windows filesystem (`/mnt/e/...`):

```bash
# Copy to Linux filesystem (recommended)
cp -r /mnt/e/.../filesharex ~/filesharex
cd ~/filesharex
docker compose -f docker/compose.yml build
```

## Testing with Scripts

WSL test scripts are provided for automated testing of each phase:

```bash
chmod +x scripts/*.sh

# Phase-specific tests
bash scripts/wsl_test_phase9.sh      # Throttling + parallel transfers
bash scripts/wsl_test_phase10.sh     # Voice chat (UDP + Opus)
```

## Tech Stack

- **C++17** -- Boost.Asio, OpenSSL, zlib, libpqxx, libopus
- **Python 3.12** -- FastAPI, Uvicorn, Jinja2, psycopg2
- **PostgreSQL 16** -- User data, sessions, messages, file metadata
- **Docker** -- Multi-stage builds, Docker Compose orchestration
