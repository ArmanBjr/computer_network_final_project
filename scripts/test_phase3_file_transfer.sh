#!/bin/bash
# Phase 3 File Transfer - Comprehensive Test Script
# Tests: Register → Login → Send File → Receive File → Verify

set -e  # Exit on error

HOST="${1:-127.0.0.1}"
PORT="${2:-9000}"

echo "=========================================="
echo "Phase 3 File Transfer - Comprehensive Test"
echo "=========================================="
echo "Host: $HOST"
echo "Port: $PORT"
echo ""

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Test users
SENDER_USER="testuser1"
SENDER_PASS="pass123"
SENDER_EMAIL="testuser1@example.com"

RECEIVER_USER="testuser2"
RECEIVER_PASS="pass123"
RECEIVER_EMAIL="testuser2@example.com"

# Test file
TEST_FILE="test_phase3_file.txt"
RECEIVED_FILE="received_phase3_file.txt"
TRANSFER_ID_FILE="/tmp/fsx_transfer_id.txt"

# Cleanup
cleanup() {
    echo ""
    echo "Cleaning up..."
    rm -f "$TEST_FILE" "$RECEIVED_FILE" "$TRANSFER_ID_FILE"
    echo "Done."
}

trap cleanup EXIT

# Step 1: Create test file
echo -e "${YELLOW}[STEP 1] Creating test file...${NC}"
cat > "$TEST_FILE" << 'EOF'
This is a test file for FileShareX Phase 3.
It contains multiple lines to test file transfer functionality.

Line 3: Testing chunk-based transfer
Line 4: Testing server-mediated architecture
Line 5: Testing file integrity

End of test file.
EOF

FILE_SIZE=$(wc -c < "$TEST_FILE")
echo -e "${GREEN}✓${NC} Test file created: $TEST_FILE ($FILE_SIZE bytes)"
echo ""

# Step 2: Register sender
echo -e "${YELLOW}[STEP 2] Registering sender ($SENDER_USER)...${NC}"
if ./client/build/test_auth register "$SENDER_USER" "$SENDER_PASS" "$HOST" "$PORT" 2>&1 | grep -q "ok=true"; then
    echo -e "${GREEN}✓${NC} Sender registered"
else
    echo -e "${YELLOW}⚠${NC} Sender might already exist (continuing...)"
fi
echo ""

# Step 3: Register receiver
echo -e "${YELLOW}[STEP 3] Registering receiver ($RECEIVER_USER)...${NC}"
if ./client/build/test_auth register "$RECEIVER_USER" "$RECEIVER_PASS" "$HOST" "$PORT" 2>&1 | grep -q "ok=true"; then
    echo -e "${GREEN}✓${NC} Receiver registered"
else
    echo -e "${YELLOW}⚠${NC} Receiver might already exist (continuing...)"
fi
echo ""

# Step 4: Login sender
echo -e "${YELLOW}[STEP 4] Logging in sender...${NC}"
if ./client/build/test_auth login "$SENDER_USER" "$SENDER_PASS" "$HOST" "$PORT" 2>&1 | grep -q "ok=true"; then
    echo -e "${GREEN}✓${NC} Sender logged in"
else
    echo -e "${RED}✗${NC} Sender login failed"
    exit 1
fi
echo ""

# Step 5: Login receiver
echo -e "${YELLOW}[STEP 5] Logging in receiver...${NC}"
if ./client/build/test_auth login "$RECEIVER_USER" "$RECEIVER_PASS" "$HOST" "$PORT" 2>&1 | grep -q "ok=true"; then
    echo -e "${GREEN}✓${NC} Receiver logged in"
else
    echo -e "${RED}✗${NC} Receiver login failed"
    exit 1
fi
echo ""

# Step 6: Send file (background, capture transfer_id)
echo -e "${YELLOW}[STEP 6] Sending file from $SENDER_USER to $RECEIVER_USER...${NC}"
SEND_OUTPUT=$(./client/build/test_file_transfer send "$SENDER_USER" "$SENDER_PASS" "$RECEIVER_USER" "$TEST_FILE" "$HOST" "$PORT" 2>&1 || true)

# Extract transfer_id from output
TRANSFER_ID=$(echo "$SEND_OUTPUT" | grep -oP 'Transfer ID: \K[0-9]+' || echo "")
if [ -z "$TRANSFER_ID" ]; then
    echo -e "${RED}✗${NC} Failed to get transfer_id from send output"
    echo "Send output:"
    echo "$SEND_OUTPUT"
    exit 1
fi

echo -e "${GREEN}✓${NC} Transfer ID: $TRANSFER_ID"
echo "$TRANSFER_ID" > "$TRANSFER_ID_FILE"
echo ""

# Step 7: Receive file (in background, wait a bit for sender to be ready)
echo -e "${YELLOW}[STEP 7] Receiving file as $RECEIVER_USER...${NC}"
sleep 2  # Give sender time to send FILE_OFFER

# Run receiver in background
RECV_OUTPUT=$(timeout 30 ./client/build/test_file_transfer recv "$RECEIVER_USER" "$RECEIVER_PASS" "$TRANSFER_ID" "$RECEIVED_FILE" "$HOST" "$PORT" 2>&1 || true)

if echo "$RECV_OUTPUT" | grep -q "SUCCESS"; then
    echo -e "${GREEN}✓${NC} File received successfully"
else
    echo -e "${RED}✗${NC} File receive failed or incomplete"
    echo "Receive output:"
    echo "$RECV_OUTPUT"
    exit 1
fi
echo ""

# Step 8: Verify file integrity
echo -e "${YELLOW}[STEP 8] Verifying file integrity...${NC}"
if [ ! -f "$RECEIVED_FILE" ]; then
    echo -e "${RED}✗${NC} Received file not found"
    exit 1
fi

RECEIVED_SIZE=$(wc -c < "$RECEIVED_FILE")
if [ "$FILE_SIZE" != "$RECEIVED_SIZE" ]; then
    echo -e "${RED}✗${NC} Size mismatch: original=$FILE_SIZE, received=$RECEIVED_SIZE"
    exit 1
fi

if ! diff -q "$TEST_FILE" "$RECEIVED_FILE" > /dev/null; then
    echo -e "${RED}✗${NC} File content mismatch"
    echo "Diff:"
    diff "$TEST_FILE" "$RECEIVED_FILE" || true
    exit 1
fi

echo -e "${GREEN}✓${NC} File integrity verified: $FILE_SIZE bytes, content matches"
echo ""

# Step 9: Check server logs
echo -e "${YELLOW}[STEP 9] Checking server logs...${NC}"
if command -v docker > /dev/null; then
    echo "Recent server logs:"
    docker logs fsx_core --tail 20 2>&1 | grep -E "FILE_|TRANSFER" || echo "No file transfer logs found"
    echo ""
fi

# Summary
echo "=========================================="
echo -e "${GREEN}✓ ALL TESTS PASSED${NC}"
echo "=========================================="
echo "Test Summary:"
echo "  - Sender: $SENDER_USER"
echo "  - Receiver: $RECEIVER_USER"
echo "  - Transfer ID: $TRANSFER_ID"
echo "  - File size: $FILE_SIZE bytes"
echo "  - File integrity: ✓ Verified"
echo ""
echo "Phase 3 File Transfer MVP is working correctly!"

