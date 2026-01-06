#!/bin/bash
# Quick Phase 3 Test - Minimal steps

HOST="${1:-127.0.0.1}"
PORT="${2:-9000}"

echo "=== Quick Phase 3 Test ==="
echo ""

# Create test file
echo "Creating test file..."
echo "Test content for Phase 3" > test_quick.txt
FILE_SIZE=$(wc -c < test_quick.txt)
echo "✓ Test file: test_quick.txt ($FILE_SIZE bytes)"
echo ""

# Register users (ignore errors if already exist)
echo "Registering users..."
./client/build/test_auth register testuser1 pass123 testuser1@example.com $HOST $PORT 2>&1 | grep -q "ok=true" && echo "✓ testuser1" || echo "⚠ testuser1 (might exist)"
./client/build/test_auth register testuser2 pass123 testuser2@example.com $HOST $PORT 2>&1 | grep -q "ok=true" && echo "✓ testuser2" || echo "⚠ testuser2 (might exist)"
echo ""

# Send file and capture transfer_id
echo "Sending file..."
SEND_OUT=$(./client/build/test_file_transfer send testuser1 pass123 testuser2 ./test_quick.txt $HOST $PORT 2>&1)
TRANSFER_ID=$(echo "$SEND_OUT" | grep -oP 'Transfer ID: \K[0-9]+' || echo "")

if [ -z "$TRANSFER_ID" ]; then
    echo "✗ Failed to get transfer_id"
    echo "$SEND_OUT"
    exit 1
fi

echo "✓ Transfer ID: $TRANSFER_ID"
echo ""

# Receive file
echo "Receiving file..."
sleep 2
RECV_OUT=$(timeout 30 ./client/build/test_file_transfer recv testuser2 pass123 $TRANSFER_ID ./received_quick.txt $HOST $PORT 2>&1)

if echo "$RECV_OUT" | grep -q "SUCCESS"; then
    echo "✓ File received"
else
    echo "✗ Receive failed"
    echo "$RECV_OUT"
    exit 1
fi
echo ""

# Verify
echo "Verifying..."
if diff -q test_quick.txt received_quick.txt > /dev/null; then
    echo "✓ Files match!"
    echo ""
    echo "=== TEST PASSED ==="
    rm -f test_quick.txt received_quick.txt
    exit 0
else
    echo "✗ Files don't match"
    exit 1
fi

