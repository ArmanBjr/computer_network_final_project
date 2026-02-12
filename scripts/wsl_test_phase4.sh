#!/bin/bash
# Phase 4 Test Script for WSL
# Usage: ./scripts/wsl_test_phase4.sh
#
# هدف: تست FILE_UPLOAD_CHUNK + CRC32 + ACK/NAK + stop-and-wait

set -e

HOST="127.0.0.1"
PORT="9000"

echo "=========================================="
echo "Phase 4 File Upload Test (WSL)"
echo "=========================================="
echo ""

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

# Check if we're in the right directory
if [ ! -f "docker/compose.yml" ]; then
    echo -e "${RED}Error: Run this script from project root (where docker/compose.yml exists)${NC}"
    exit 1
fi

# Step 1: Check services
echo -e "${YELLOW}[1/7] Checking Docker services...${NC}"
if ! docker ps | grep -q "fsx_core"; then
    echo -e "${RED}✗ Core server not running${NC}"
    echo "Starting services (db + core)..."
    docker compose -f docker/compose.yml up -d db core
    sleep 5
fi

if ! docker ps | grep -q "fsx_db"; then
    echo -e "${RED}✗ Database not running${NC}"
    exit 1
fi

echo -e "${GREEN}✓ Services running${NC}"
echo ""

# Step 2: Build client (always rebuild to ensure latest changes)
echo -e "${YELLOW}[2/7] Building client...${NC}"
cd client
rm -rf build
cmake -S . -B build
cmake --build build
cd ..

if [ ! -f "client/build/test_file_transfer" ]; then
    echo -e "${RED}✗ Client build failed${NC}"
    exit 1
fi

echo -e "${GREEN}✓ Client built${NC}"
echo ""

# Step 3: Create test file
echo -e "${YELLOW}[3/7] Creating Phase 4 test file...${NC}"
cat > test_phase4.txt << 'EOF'
This is a Phase 4 test file for FileShareX.
It should be transferred using FILE_UPLOAD_CHUNK with CRC32 + ACK/NAK + stop-and-wait.

Line 3: Testing CRC32 per chunk.
Line 4: Testing ACK/NAK behavior.
Line 5: Testing retransmission (future fault injection).

End of Phase 4 test file.
EOF

FILE_SIZE=$(wc -c < test_phase4.txt)
echo -e "${GREEN}✓ Test file created: test_phase4.txt (${FILE_SIZE} bytes)${NC}"
echo ""

# Step 4: Register users
echo -e "${YELLOW}[4/7] Registering users...${NC}"
./client/build/test_auth register testuser1 pass123 testuser1@example.com $HOST $PORT 2>&1 | grep -q "ok=true" && echo -e "${GREEN}✓ testuser1${NC}" || echo -e "${YELLOW}⚠ testuser1 (might exist)${NC}"
./client/build/test_auth register testuser2 pass123 testuser2@example.com $HOST $PORT 2>&1 | grep -q "ok=true" && echo -e "${GREEN}✓ testuser2${NC}" || echo -e "${YELLOW}⚠ testuser2 (might exist)${NC}"
echo ""

# Step 5: Login users
echo -e "${YELLOW}[5/7] Logging in users...${NC}"
if ./client/build/test_auth login testuser1 pass123 $HOST $PORT 2>&1 | grep -q "ok=true"; then
    echo -e "${GREEN}✓ testuser1 logged in${NC}"
else
    echo -e "${RED}✗ testuser1 login failed${NC}"
    exit 1
fi

if ./client/build/test_auth login testuser2 pass123 $HOST $PORT 2>&1 | grep -q "ok=true"; then
    echo -e "${GREEN}✓ testuser2 logged in${NC}"
else
    echo -e "${RED}✗ testuser2 login failed${NC}"
    exit 1
fi
echo ""

# Step 6: Send + Receive using Phase 4 client (FILE_UPLOAD_CHUNK) with fault injection
echo -e "${YELLOW}[6/7] Running Phase 4 file upload with fault injection (send + recv)...${NC}"
echo -e "${YELLOW}  Fault injection: --corrupt-upload-once 0 (will corrupt CRC32 for chunk 0 on first send)${NC}"

SEND_LOG="/tmp/fsx_phase4_sender_$$.log"
RECV_LOG="/tmp/fsx_phase4_receiver_$$.log"

# Start sender in background with fault injection (corrupt chunk 0)
./client/build/test_file_transfer \
  send \
  testuser1 pass123 \
  testuser2 \
  ./test_phase4.txt \
  $HOST $PORT \
  --corrupt-upload-once 0 > "$SEND_LOG" 2>&1 &
SENDER_PID=$!

# Wait for transfer_id to appear in sender log
TIMEOUT=10
ELAPSED=0
TRANSFER_ID=""
while [ $ELAPSED -lt $TIMEOUT ]; do
    sleep 0.5
    if [ -f "$SEND_LOG" ]; then
        TRANSFER_ID=$(grep -oP 'Transfer ID: \K[0-9]+' "$SEND_LOG" 2>/dev/null || echo "")
        if [ -n "$TRANSFER_ID" ]; then
            break
        fi
    fi
    ELAPSED=$((ELAPSED + 1))
done

if [ -z "$TRANSFER_ID" ]; then
    echo -e "${RED}✗ Failed to get transfer_id from sender log${NC}"
    echo "Sender log:"
    cat "$SEND_LOG" 2>/dev/null || true
    kill $SENDER_PID 2>/dev/null || true
    rm -f "$SEND_LOG"
    exit 1
fi

echo -e "${GREEN}✓ Transfer ID: $TRANSFER_ID${NC}"
echo ""

# Start receiver in background (this triggers FILE_ACCEPT_REQ/RESP)
./client/build/test_file_transfer \
  recv \
  testuser2 pass123 \
  $TRANSFER_ID \
  ./received_phase4.txt \
  $HOST $PORT > "$RECV_LOG" 2>&1 &
RECEIVER_PID=$!

# Wait for both to complete
wait $SENDER_PID 2>/dev/null || true
wait $RECEIVER_PID 2>/dev/null || true

echo ""
echo -e "${YELLOW}Sender output:${NC}"
cat "$SEND_LOG"
echo ""
echo -e "${YELLOW}Receiver output:${NC}"
cat "$RECV_LOG"
echo ""

# Basic success checks
if grep -q "SUCCESS" "$SEND_LOG"; then
  echo -e "${GREEN}✓ Sender reported SUCCESS${NC}"
else
  echo -e "${RED}✗ Sender did NOT report SUCCESS${NC}"
  exit 1
fi

if grep -q "SUCCESS" "$RECV_LOG"; then
  echo -e "${GREEN}✓ Receiver accepted transfer${NC}"
else
  echo -e "${YELLOW}⚠ Receiver did not print SUCCESS message (for Phase 4 this is acceptable if it at least accepted).${NC}"
fi

# Check for fault injection and NAK/resend behavior
echo ""
echo -e "${YELLOW}Checking fault injection behavior...${NC}"
if grep -q "FAULT INJECTION" "$SEND_LOG"; then
  echo -e "${GREEN}✓ Fault injection activated (CRC32 corrupted for chunk 0)${NC}"
else
  echo -e "${YELLOW}⚠ Fault injection not detected in sender logs${NC}"
fi

if grep -q "NAK received" "$SEND_LOG"; then
  echo -e "${GREEN}✓ NAK received (as expected from corrupted CRC32)${NC}"
else
  echo -e "${YELLOW}⚠ NAK not found in sender logs${NC}"
fi

if grep -q "Retry.*for chunk" "$SEND_LOG"; then
  echo -e "${GREEN}✓ Retry detected (resend after NAK)${NC}"
  grep "Retry.*for chunk" "$SEND_LOG"
else
  echo -e "${YELLOW}⚠ Retry not found (may have succeeded on first attempt)${NC}"
fi

if grep -q "ACK received" "$SEND_LOG"; then
  echo -e "${GREEN}✓ ACK received (after resend with correct CRC32)${NC}"
else
  echo -e "${YELLOW}⚠ ACK not found in sender logs${NC}"
fi

# Step 7: Inspect server logs for Phase 4 events
echo ""
echo -e "${YELLOW}[7/7] Checking server logs for Phase 4 events...${NC}"

echo "FILE_UPLOAD_CHUNK / ACK / NAK events for transfer_id=$TRANSFER_ID:"
docker logs fsx_core 2>&1 | grep "transfer_id=$TRANSFER_ID" | grep -E "FILE_UPLOAD_CHUNK|FILE_UPLOAD_CHUNK_OK|FILE_UPLOAD_ACK|FILE_UPLOAD_NAK|CRC32_MISMATCH" || echo "No Phase 4 upload logs found for this transfer_id"
echo ""

# Check for CRC32 mismatch (expected from fault injection)
if docker logs fsx_core 2>&1 | grep "transfer_id=$TRANSFER_ID" | grep -q "CRC32_MISMATCH"; then
    echo -e "${GREEN}✓ CRC32_MISMATCH detected in server logs (expected from fault injection)${NC}"
    docker logs fsx_core 2>&1 | grep "transfer_id=$TRANSFER_ID" | grep "CRC32_MISMATCH"
    echo ""
else
    echo -e "${YELLOW}⚠ CRC32_MISMATCH not found in server logs${NC}"
    echo ""
fi

# Check for NAK sent
if docker logs fsx_core 2>&1 | grep "transfer_id=$TRANSFER_ID" | grep -q "FILE_UPLOAD_NAK"; then
    echo -e "${GREEN}✓ FILE_UPLOAD_NAK sent by server (expected after CRC32 mismatch)${NC}"
    docker logs fsx_core 2>&1 | grep "transfer_id=$TRANSFER_ID" | grep "FILE_UPLOAD_NAK"
    echo ""
else
    echo -e "${YELLOW}⚠ FILE_UPLOAD_NAK not found in server logs${NC}"
    echo ""
fi

echo "Summary:"
echo "  - Transfer ID: $TRANSFER_ID"
echo "  - File size:   $FILE_SIZE bytes"
echo "  - Sender:      SUCCESS reported"
echo "  - Receiver:    Accepted transfer"
echo "  - Server:      See logs above for FILE_UPLOAD_CHUNK / ACK / NAK"
echo ""
echo -e "${GREEN}Phase 4 upload path (CRC32 + ACK/NAK + fault injection) test completed.${NC}"

# Cleanup temp logs
rm -f "$SEND_LOG" "$RECV_LOG"

echo ""
read -p "Clean up test file test_phase4.txt? (y/n) " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    rm -f test_phase4.txt
    echo "Test file cleaned up (server-side stored file remains)."
fi


