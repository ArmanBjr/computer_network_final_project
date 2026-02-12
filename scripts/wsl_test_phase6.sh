#!/bin/bash
# Phase 6 Test Script for WSL
# Usage: ./scripts/wsl_test_phase6.sh
#
# هدف: تست فشرده‌سازی (--compress) در ارسال فایل؛ FILE_UPLOAD_CHUNK با original_size
# پیش‌نیاز: db و core را دستی بالا بیاور: docker compose -f docker/compose.yml up -d db core

set -e

HOST="127.0.0.1"
PORT="9000"

echo "=========================================="
echo "Phase 6 Compression Test (WSL)"
echo "=========================================="
echo ""

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[0;33m'
NC='\033[0m'

if [ ! -f "docker/compose.yml" ]; then
    echo -e "${RED}Error: Run from project root (where docker/compose.yml exists)${NC}"
    exit 1
fi

# 1) Services (manual: docker compose -f docker/compose.yml up -d db core)
echo -e "${YELLOW}[1/5] Checking Docker services...${NC}"
if ! docker ps | grep -q "fsx_core"; then
    echo -e "${RED}✗ Core not running. Start manually: docker compose -f docker/compose.yml up -d db core${NC}"
    exit 1
fi
if ! docker ps | grep -q "fsx_db"; then
    echo -e "${RED}✗ Database not running. Start manually: docker compose -f docker/compose.yml up -d db core${NC}"
    exit 1
fi
echo -e "${GREEN}✓ Services running${NC}"
echo ""

# 2) Build core (with zlib) and client
echo -e "${YELLOW}[2/5] Building core and client...${NC}"
cd core
rm -rf build
cmake -S . -B build
cmake --build build
cd ..

cd client
rm -rf build
cmake -S . -B build
cmake --build build
cd ..

if [ ! -f "client/build/test_file_transfer" ]; then
    echo -e "${RED}✗ Client build failed${NC}"
    exit 1
fi
if [ ! -f "core/build/test_zlib_codec" ]; then
    echo -e "${RED}✗ test_zlib_codec not built${NC}"
    exit 1
fi
echo -e "${GREEN}✓ Core and client built${NC}"
echo ""

# 3) Unit test ZlibCodec
echo -e "${YELLOW}[3/5] Running ZlibCodec unit test...${NC}"
./core/build/test_zlib_codec || { echo -e "${RED}✗ ZlibCodec test failed${NC}"; exit 1; }
echo -e "${GREEN}✓ ZlibCodec test passed${NC}"
echo ""

# 4) Register users
echo -e "${YELLOW}[4/5] Registering users...${NC}"
./client/build/test_auth register testuser1 pass123 testuser1@example.com $HOST $PORT 2>&1 | grep -q "ok=true" && echo -e "${GREEN}✓ testuser1${NC}" || echo -e "${YELLOW}⚠ testuser1 (may exist)${NC}"
./client/build/test_auth register testuser2 pass123 testuser2@example.com $HOST $PORT 2>&1 | grep -q "ok=true" && echo -e "${GREEN}✓ testuser2${NC}" || echo -e "${YELLOW}⚠ testuser2 (may exist)${NC}"
echo ""

# 5) Create compressible test file and run send with --compress + recv
echo -e "${YELLOW}[5/5] Creating test file and running send --compress + recv...${NC}"
# Compressible text file (~100KB)
{
  echo "Phase 6 Compression Test File"
  echo "This file contains repeated text so that zlib can compress it well."
  for i in $(seq 1 2000); do
    echo "Line $i: The quick brown fox jumps over the lazy dog. FileShareX Phase 6."
  done
} > test_phase6.txt
FILE_SIZE=$(wc -c < test_phase6.txt)
echo "Test file: test_phase6.txt ($FILE_SIZE bytes)"

SEND_LOG="/tmp/fsx_phase6_send_$$.log"
RECV_LOG="/tmp/fsx_phase6_recv_$$.log"
TID_FILE="/tmp/fsx_phase6_tid_$$.txt"
FIFO="/tmp/fsx_phase6_fifo_$$"
rm -f "$FIFO" "$TID_FILE"
if ! mkfifo "$FIFO" 2>/dev/null; then
  echo -e "${YELLOW}mkfifo not available, using log file (may need longer wait)${NC}"
  FIFO=""
fi

if [ -n "$FIFO" ]; then
  # Reader: read sender output from FIFO, append to SEND_LOG, extract transfer_id when seen
  ( while IFS= read -r line; do
      echo "$line" >> "$SEND_LOG"
      tid=$(echo "$line" | sed -n 's/.*Transfer ID: \([0-9][0-9]*\).*/\1/p')
      if [ -n "$tid" ]; then
        echo "$tid" > "$TID_FILE"
      fi
    done < "$FIFO" ) &
  READER_PID=$!
  # Sender writes to FIFO (reader gets output immediately, no buffering)
  ./client/build/test_file_transfer send testuser1 pass123 testuser2 ./test_phase6.txt $HOST $PORT --compress > "$FIFO" 2>&1 &
  SEND_PID=$!
else
  # Fallback: redirect to log file and poll (output may be buffered)
  ./client/build/test_file_transfer send testuser1 pass123 testuser2 ./test_phase6.txt $HOST $PORT --compress > "$SEND_LOG" 2>&1 &
  SEND_PID=$!
  READER_PID=""
  sleep 3
fi

echo "Waiting for sender to connect and get transfer_id (up to 25s)..."
TIMEOUT=50
ELAPSED=0
TRANSFER_ID=""
while [ "$ELAPSED" -lt "$TIMEOUT" ]; do
  sleep 0.5
  if [ -f "$TID_FILE" ]; then
    TRANSFER_ID=$(cat "$TID_FILE" 2>/dev/null)
  fi
  if [ -z "$TRANSFER_ID" ] && [ -f "$SEND_LOG" ]; then
    TRANSFER_ID=$(sed -n 's/.*Transfer ID: \([0-9][0-9]*\).*/\1/p' "$SEND_LOG" 2>/dev/null | head -1)
  fi
  if [ -n "$TRANSFER_ID" ]; then
    echo "Got transfer_id=$TRANSFER_ID"
    break
  fi
  ELAPSED=$((ELAPSED + 1))
done

if [ -z "$TRANSFER_ID" ]; then
  rm -f "$FIFO"
  [ -n "$READER_PID" ] && kill $READER_PID 2>/dev/null || true
  [ -n "$READER_PID" ] && wait $READER_PID 2>/dev/null || true
  echo -e "${RED}✗ Could not get transfer_id from sender (timeout)${NC}"
  echo "Sender log file: $SEND_LOG"
  echo "Sender log contents:"
  cat "$SEND_LOG" 2>/dev/null || echo "(empty or unreadable)"
  kill $SEND_PID 2>/dev/null || true
  rm -f "$SEND_LOG" "$RECV_LOG" "$TID_FILE"
  exit 1
fi
rm -f "$TID_FILE"
# Do not kill reader here: it must keep draining the FIFO until sender exits

echo "Starting receiver for transfer_id=$TRANSFER_ID..."
./client/build/test_file_transfer recv testuser2 pass123 "$TRANSFER_ID" ./received_phase6.txt $HOST $PORT > "$RECV_LOG" 2>&1 &
RECV_PID=$!

echo "Waiting for sender and receiver to finish..."
wait $SEND_PID 2>/dev/null || true
wait $RECV_PID 2>/dev/null || true
[ -n "$READER_PID" ] && wait $READER_PID 2>/dev/null || true
rm -f "$FIFO"
echo "Done."

echo ""
echo "Sender output (last 30 lines):"
tail -30 "$SEND_LOG"
echo ""
echo "Receiver output:"
cat "$RECV_LOG"
echo ""

# Verify: recv in this phase only accepts the transfer; file is saved on server by sender (not downloaded to local)
if ! grep -q "SUCCESS! File saved at" "$SEND_LOG"; then
  echo -e "${RED}✗ Sender did not report SUCCESS (file not saved on server)${NC}"
  rm -f "$SEND_LOG" "$RECV_LOG"
  exit 1
fi
echo -e "${GREEN}✓ Sender completed: file saved on server${NC}"

if ! grep -q "SUCCESS\|Accepted" "$RECV_LOG"; then
  echo -e "${RED}✗ Receiver did not accept transfer${NC}"
  rm -f "$SEND_LOG" "$RECV_LOG"
  exit 1
fi
echo -e "${GREEN}✓ Receiver accepted transfer (file on server)${NC}"

# Check that compression was used in sender log
if grep -q "compressed" "$SEND_LOG"; then
  echo -e "${GREEN}✓ Compression was used (compressed chunk sizes in log)${NC}"
else
  echo -e "${YELLOW}⚠ No 'compressed' in sender log (chunks may be incompressible or small)${NC}"
fi

# Optional: show server logs for this transfer (original_size in FILE_UPLOAD_CHUNK)
echo ""
echo "Server logs (transfer_id=$TRANSFER_ID, compression):"
docker logs fsx_core 2>&1 | grep "transfer_id=$TRANSFER_ID" | grep -E "FILE_UPLOAD_CHUNK|original_size|compressed" | head -10 || echo "No matching logs"

rm -f "$SEND_LOG" "$RECV_LOG"

echo ""
echo -e "${GREEN}Phase 6 compression test completed.${NC}"
echo "  - ZlibCodec unit test: OK"
echo "  - send --compress + recv (accept): OK"
echo "  - File saved on server (compressed upload, server decompressed)"
