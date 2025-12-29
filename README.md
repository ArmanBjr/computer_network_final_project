# FileShareX

Phase 0: project skeleton (C++ core + C++ client + FastAPI gateway + Docker compose)

## Ports
- Core TCP: 9000
- Core Admin TCP: 9100 (Phase 1)
- Gateway HTTP: 8000
- Postgres: 5432

## ⚠️ WSL Docker Build Issue Fix

If you get `SIGBUS: bus error` when building Docker images in WSL from Windows filesystem (`/mnt/e/...`):

### ✅ **RECOMMENDED SOLUTION**: Move project to Linux filesystem

```bash
# Copy project to Linux filesystem
cp -r /mnt/e/University/University_Subjects/5th/Computer_Networks/Projects/finl_project/filesharex ~/filesharex

# Change directory
cd ~/filesharex

# Now build works normally
docker compose -f docker/compose.yml build
```

**Why?** Docker builds on Windows filesystem (`/mnt/...`) can cause SIGBUS errors due to filesystem translation issues.

### Alternative: Stay on Windows filesystem

If you must stay on `/mnt/e/...`, try:
1. Use `.dockerignore` (already created) to reduce context size
2. Build with verbose output: `docker compose -f docker/compose.yml build --progress=plain core`

## 🚀 Quick Start & Testing

### 1. Start all services
```bash
docker compose -f docker/compose.yml up -d
```

### 2. Test system health
```bash
# Make scripts executable (first time only)
chmod +x scripts/*.sh

# Run comprehensive system test
./scripts/test_system.sh
```

This will check:
- ✅ Docker containers are running
- ✅ Database connection
- ✅ Core TCP server (port 9000)
- ✅ Gateway HTTP (port 8000)
- ✅ Core Admin port (port 9100)

### 3. Test client connection

#### Test Auth (REGISTER/LOGIN)
```bash
# Build and run auth test
chmod +x scripts/test_register_login.sh
./scripts/test_register_login.sh 127.0.0.1 9000
```

Or manually:
```bash
# Build test client
cd client
g++ -std=c++20 -I../core/include -o build/test_auth src/test_auth.cpp -lboost_system -lpthread
cd ..

# Test REGISTER
./client/build/test_auth register testuser testpass123 127.0.0.1 9000

# Test LOGIN
./client/build/test_auth login testuser testpass123 127.0.0.1 9000
```

#### Test basic connection (old)
```bash
# Build client (if not already built)
cd client
cmake -S . -B build
cmake --build build
cd ..

# Run client test
./scripts/test_client.sh test_client 127.0.0.1 9000
```

### 4. Access dashboard

**⚠️ WSL Users:** If running in WSL, `localhost:8000` won't work from Windows browser.

**Quick Solution:** Use WSL IP address instead:

```bash
# Get WSL IP (run in WSL)
./scripts/get_wsl_ip.sh
# or simply:
hostname -I
```

Then access from **Windows browser**:
- **http://<WSL_IP>:8000** (e.g., `http://172.24.30.74:8000`)

**Alternative Solutions:**

1. **Docker Desktop** (if installed):
   - Settings → Resources → WSL Integration
   - Enable your WSL distro
   - Restart Docker Desktop
   - Now `localhost:8000` works from Windows

2. **From WSL terminal:**
   - `curl http://localhost:8000` works fine
   - Or use a browser inside WSL

**Note:** WSL IP may change after restart. Run `hostname -I` again if needed.

### 5. View logs
```bash
# Core server logs
docker logs -f fsx_core

# Gateway logs
docker logs -f fsx_gateway

# Database logs
docker logs -f fsx_db
```

### 6. Manual TCP test (using netcat/telnet)
```bash
# Test Core TCP connection
echo "test" | nc 127.0.0.1 9000
# or
telnet 127.0.0.1 9000
```

### 7. Stop services
```bash
docker compose -f docker/compose.yml down
```

## Run (Docker)
From repo root:
```bash
docker compose -f docker/compose.yml up --build
```

**Access URLs:**
- Gateway: `http://localhost:8000` (or `http://<WSL_IP>:8000` from Windows)
- Core TCP: `localhost:9000`
- Admin: `localhost:9100`

Run (Local, without Docker)
Core:

bash
Copy code
cmake -S core -B core/build
cmake --build core/build -j
./core/build/fsx_core_server
Client:

bash
Copy code
cmake -S client -B client/build
cmake --build client/build -j
./client/build/fsx_client 127.0.0.1 9000
Gateway:

bash
Copy code
cd gateway
python -m venv .venv && source .venv/bin/activate
pip install -e .
uvicorn app.main:app --host 0.0.0.0 --port 8000
yaml
Copy code
```"# computer_network_final_project" 
