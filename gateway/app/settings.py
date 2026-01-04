import os

GATEWAY_HOST = os.getenv("FSX_GATEWAY_HOST", "0.0.0.0")
GATEWAY_PORT = int(os.getenv("FSX_GATEWAY_PORT", "8000"))

# Core TCP connection (port 9000 for client protocol)
CORE_TCP_HOST = os.getenv("FSX_CORE_TCP_HOST", os.getenv("FSX_CORE_ADMIN_HOST", "127.0.0.1"))
CORE_TCP_PORT = int(os.getenv("FSX_CORE_TCP_PORT", "9000"))

# Core Admin connection (port 9100, for future use)
CORE_ADMIN_HOST = os.getenv("FSX_CORE_ADMIN_HOST", "127.0.0.1")
CORE_ADMIN_PORT = int(os.getenv("FSX_CORE_ADMIN_PORT", "9100"))
