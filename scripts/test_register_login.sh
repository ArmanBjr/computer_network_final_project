#!/bin/bash
# Test REGISTER and LOGIN via TCP

HOST=${1:-"127.0.0.1"}
PORT=${2:-"9000"}

echo "=========================================="
echo "Testing FileShareX Auth (REGISTER/LOGIN)"
echo "=========================================="
echo ""

# Check if test client exists
if [ ! -f "client/src/test_auth.cpp" ]; then
    echo "ERROR: test_auth.cpp not found"
    exit 1
fi

# Build test client
echo "[1/3] Building test client..."
cd client
if [ ! -d "build" ]; then
    mkdir build
fi

g++ -std=c++20 -I../core/include -o build/test_auth src/test_auth.cpp -lboost_system -lpthread 2>&1
if [ $? -ne 0 ]; then
    echo "ERROR: Failed to build test client"
    exit 1
fi
cd ..

echo "[2/3] Testing REGISTER..."
./client/build/test_auth register testuser testpass123 "$HOST" "$PORT"

echo ""
echo "[3/3] Testing LOGIN..."
./client/build/test_auth login testuser testpass123 "$HOST" "$PORT"

echo ""
echo "=========================================="
echo "Test complete!"
echo "=========================================="
echo ""
echo "Check server logs: docker logs fsx_core"

