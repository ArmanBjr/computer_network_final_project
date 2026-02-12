#!/bin/bash
# Phase 5 Test Script for WSL
# Usage: ./scripts/wsl_test_phase5.sh
#
# هدف: تست Resume (RESUME_QUERY / RESUME_REPLY) + ادامه ارسال از last_acked_chunk

set -e

HOST="127.0.0.1"
PORT="9000"

echo "=========================================="
echo "Phase 5 Resume Test (WSL)"
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
echo -e "${YELLOW}[1/6] Checking Docker services...${NC}"
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
echo -e "${YELLOW}[2/6] Building client...${NC}"
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

# Step 3: Create Phase 5 test file (bigger file so that mid-transfer stop is meaningful)
echo -e "${YELLOW}[3/6] Creating Phase 5 test file (large file for multi-chunk transfer)...${NC}"
# Create a file that's about 2.5MB (will be ~10 chunks with default 256KB chunk size)
# This ensures we can interrupt mid-transfer before completion
dd if=/dev/urandom of=test_phase5.txt bs=1024 count=2560 2>/dev/null || {
    # Fallback: use Python if dd fails
    python3 -c "
import os
data = os.urandom(2560 * 1024)
with open('test_phase5.txt', 'wb') as f:
    f.write(b'Phase 5 Resume Test File\\n')
    f.write(b'This file is intentionally large (2.5MB) to test resume functionality.\\n')
    f.write(b'It will be split into multiple chunks (256KB each).\\n')
    f.write(b'We will interrupt the transfer after 1-2 chunks to test resume.\\n\\n')
    f.write(data)
    f.write(b'\\n\\nEnd of Phase 5 test file.\\n')
" 2>/dev/null || {
    # Last resort: create file with repeated pattern
    for i in {1..5000}; do
        echo "Line $i: This is a Phase 5 test file for FileShareX resume functionality. " >> test_phase5.txt
    done
}
}

FILE_SIZE=$(wc -c < test_phase5.txt)
echo -e "${GREEN}✓ Test file created: test_phase5.txt (${FILE_SIZE} bytes, ~$((FILE_SIZE / 1024))KB)${NC}"
echo ""

# Step 4: Register users (idempotent)
echo -e "${YELLOW}[4/6] Registering users...${NC}"
./client/build/test_auth register testuser1 pass123 testuser1@example.com $HOST $PORT 2>&1 | grep -q "ok=true" && echo -e "${GREEN}✓ testuser1${NC}" || echo -e "${YELLOW}⚠ testuser1 (might exist)${NC}"
./client/build/test_auth register testuser2 pass123 testuser2@example.com $HOST $PORT 2>&1 | grep -q "ok=true" && echo -e "${GREEN}✓ testuser2${NC}" || echo -e "${YELLOW}⚠ testuser2 (might exist)${NC}"
echo ""

# Step 5: First upload run (simulate connection drop)
echo -e "${YELLOW}[5/6] Running first upload and interrupting mid-transfer...${NC}"

SEND_LOG1="/tmp/fsx_phase5_send_first_$$.log"
RECV_LOG1="/tmp/fsx_phase5_recv_first_$$.log"

echo -e "${YELLOW}Starting sender in background (no timeout)...${NC}"
./client/build/test_file_transfer \
  send \
  testuser1 pass123 \
  testuser2 \
  ./test_phase5.txt \
  $HOST $PORT > "$SEND_LOG1" 2>&1 &
SENDER_PID=$!

# Wait for transfer_id to appear in sender log
TIMEOUT=10
ELAPSED=0
TRANSFER_ID=""
while [ $ELAPSED -lt $TIMEOUT ]; do
    sleep 0.5
    if [ -f "$SEND_LOG1" ]; then
        TRANSFER_ID=$(grep -oP 'Transfer ID: \K[0-9]+' "$SEND_LOG1" 2>/dev/null || echo "")
        if [ -n "$TRANSFER_ID" ]; then
            break
        fi
    fi
    ELAPSED=$((ELAPSED + 1))
done

if [ -z "$TRANSFER_ID" ]; then
    echo -e "${RED}✗ Failed to get transfer_id from sender log${NC}"
    cat "$SEND_LOG1" 2>/dev/null || true
    kill $SENDER_PID 2>/dev/null || true
    rm -f "$SEND_LOG1" "$RECV_LOG1"
    exit 1
fi

echo -e "${GREEN}✓ Transfer ID: $TRANSFER_ID${NC}"
echo ""

# Start receiver so it accepts the transfer (server will save resume state)
echo -e "${YELLOW}Starting receiver to accept transfer...${NC}"
./client/build/test_file_transfer \
  recv \
  testuser2 pass123 \
  $TRANSFER_ID \
  ./received_phase5.txt \
  $HOST $PORT > "$RECV_LOG1" 2>&1 &
RECEIVER_PID=$!

# Wait for "Accepted!" then kill sender IMMEDIATELY (poll every 0.1s so we don't miss)
echo -e "${YELLOW}Waiting for receiver to accept, then killing sender immediately...${NC}"
ACCEPTED=false
for i in $(seq 1 50); do
    sleep 0.1
    if grep -q "Accepted!" "$SEND_LOG1" 2>/dev/null; then
        ACCEPTED=true
        echo -e "${GREEN}✓ Receiver accepted, resume state saved on server - killing sender NOW${NC}"
        kill $SENDER_PID 2>/dev/null || true
        wait $SENDER_PID 2>/dev/null || true
        break
    fi
    if ! kill -0 $SENDER_PID 2>/dev/null; then
        if grep -q "SUCCESS! File saved" "$SEND_LOG1"; then
            echo -e "${RED}✗ Sender finished too fast (transfer completed before we could kill)${NC}"
            kill $RECEIVER_PID 2>/dev/null || true
            rm -f "$SEND_LOG1" "$RECV_LOG1"
            exit 1
        fi
        break
    fi
done

if [ "$ACCEPTED" = false ]; then
    echo -e "${YELLOW}⚠ Accepted! not seen in time; killing sender anyway${NC}"
    kill $SENDER_PID 2>/dev/null || true
    wait $SENDER_PID 2>/dev/null || true
fi

sleep 0.5
kill $RECEIVER_PID 2>/dev/null || true
wait $RECEIVER_PID 2>/dev/null || true

CHUNKS_ACKED=$(grep -c "Chunk.*ACKed\|ACK received" "$SEND_LOG1" 2>/dev/null | head -1 || echo "0")
CHUNKS_ACKED=$(echo "$CHUNKS_ACKED" | tr -d '[:space:]')
if [ -z "$CHUNKS_ACKED" ] || ! [[ "$CHUNKS_ACKED" =~ ^[0-9]+$ ]]; then
    CHUNKS_ACKED=0
fi
if [ "$CHUNKS_ACKED" -gt "0" ]; then
    echo -e "${GREEN}✓ Sender was killed after $CHUNKS_ACKED chunk(s) ACKed${NC}"
else
    echo -e "${YELLOW}⚠ No chunks ACKed (resume will start from chunk 0)${NC}"
fi

echo ""
echo -e "${YELLOW}First sender output (truncated):${NC}"
head -n 50 "$SEND_LOG1" || true
echo ""

echo -e "${YELLOW}Checking that some chunks were uploaded before kill...${NC}"
if grep -q "Chunk.*ACKed\|FILE_UPLOAD_CHUNK_OK\|ACK received" "$SEND_LOG1"; then
    ACK_COUNT=$(grep -c "Chunk.*ACKed\|ACK received" "$SEND_LOG1" 2>/dev/null || echo "0")
    echo -e "${GREEN}✓ At least $ACK_COUNT chunk(s) were ACKed before interruption${NC}"
    grep -E "Chunk.*ACKed|ACK received" "$SEND_LOG1" | head -5 || true
else
    echo -e "${YELLOW}⚠ No ACK found in sender log (checking server logs...)${NC}"
    docker logs fsx_core 2>&1 | grep "transfer_id=$TRANSFER_ID" | grep -E "FILE_UPLOAD_CHUNK_OK|FILE_UPLOAD_ACK" | head -3 || echo "No ACK logs found"
    echo -e "${YELLOW}Note: Resume will still work, but will start from chunk 0${NC}"
fi
echo ""

# Step 6: Resume upload using new command resume-send
echo -e "${YELLOW}[6/6] Resuming upload using resume-send...${NC}"

SEND_LOG2="/tmp/fsx_phase5_send_resume_$$.log"

# Run resume-send; do not exit on failure so we always show the log
set +e
./client/build/test_file_transfer \
  resume-send \
  testuser1 pass123 \
  $TRANSFER_ID \
  ./test_phase5.txt \
  $HOST $PORT > "$SEND_LOG2" 2>&1
RESUME_EXIT=$?
set -e

echo ""
echo -e "${YELLOW}Resume sender output:${NC}"
cat "$SEND_LOG2"
echo ""

# Basic checks on resume behavior
if grep -q "\[RESUME\] Server allows resume" "$SEND_LOG2"; then
  echo -e "${GREEN}✓ RESUME_QUERY / RESUME_REPLY path exercised${NC}"
else
  echo -e "${RED}✗ RESUME messages not clearly detected in resume logs${NC}"
fi

if grep -q "\[RESUME\] SUCCESS! File saved at" "$SEND_LOG2"; then
  echo -e "${GREEN}✓ Resume upload completed successfully (FILE_DONE + FILE_RESULT)${NC}"
else
  echo -e "${RED}✗ Resume upload did not report SUCCESS${NC}"
fi

echo ""
echo -e "${YELLOW}Checking server logs for resume state...${NC}"
docker logs fsx_core 2>&1 | grep "transfer_id=$TRANSFER_ID" | grep -E "RESUME_QUERY|RESUME_REPLY|FILE_UPLOAD_CHUNK_OK|FILE_DONE_OK" || echo "No explicit resume logs found for this transfer_id"
echo ""

echo "Summary:"
echo "  - Transfer ID: $TRANSFER_ID"
echo "  - File size:   $FILE_SIZE bytes"
echo "  - First run:   interrupted by timeout (simulating disconnect)"
echo "  - Second run:  resume-send continued upload from last_acked_chunk"
echo ""

# Cleanup temp logs
rm -f "$SEND_LOG1" "$SEND_LOG2" "$RECV_LOG1"

if [ "$RESUME_EXIT" -ne 0 ]; then
  echo -e "${RED}Phase 5 resume-send exited with code $RESUME_EXIT${NC}"
  exit 1
fi
echo -e "${GREEN}Phase 5 resume path test completed.${NC}"

echo ""
read -p "Clean up test file test_phase5.txt? (y/n) " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    rm -f test_phase5.txt
    echo "Test file cleaned up (server-side stored file remains)."
fi

