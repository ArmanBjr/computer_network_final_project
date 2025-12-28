# FileShareX

Phase 0: project skeleton (C++ core + C++ client + FastAPI gateway + Docker compose)

## Ports
- Core TCP: 9000
- Core Admin TCP: 9100 (Phase 1)
- Gateway HTTP: 8000
- Postgres: 5432

## Run (Docker)
From repo root:
```bash
docker compose -f docker/compose.yml up --build
Gateway: http://localhost:8000
Core: TCP 9000 (echo test)

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
