#!/bin/bash
# Quick client connection test
# Usage: ./test_client.sh [client_name] [host] [port]

CLIENT_NAME=${1:-"test_client"}
HOST=${2:-"127.0.0.1"}
PORT=${3:-"9000"}

echo "Testing client connection to ${HOST}:${PORT}..."
echo "Client name: ${CLIENT_NAME}"
echo ""

# Check if client binary exists
if [ ! -f "client/build/fsx_client" ]; then
    echo "Client binary not found. Building..."
    cd client
    cmake -S . -B build
    cmake --build build
    cd ..
fi

# Run client
echo "Connecting..."
./client/build/fsx_client "${CLIENT_NAME}" "${HOST}" "${PORT}"

echo ""
echo "Connection test complete!"
echo "Check server logs: docker logs fsx_core"

