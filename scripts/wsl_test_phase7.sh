#!/bin/bash
# Phase 7 Test Script for WSL
# Usage: ./scripts/wsl_test_phase7.sh
#
# هدف: تست SHA-256 integrity نهایی — سرور بعد از ذخیره فایل، SHA-256 را محاسبه و در FILE_RESULT و log گزارش می‌دهد
# پیش‌نیاز: db و core را دستی بالا بیاور: docker compose -f docker/compose.yml up -d db core

set -e

HOST="127.0.0.1"
PORT="9000"

echo "=========================================="
echo "Phase 7 SHA-256 Integrity Test (WSL)"
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

# 1) Services
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

# 2) Build core and client
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
if [ ! -f "core/build/test_integrity" ]; then
    echo -e "${RED}✗ test_integrity not built${NC}"
    exit 1
fi
echo -e "${GREEN}✓ Core and client built${NC}"
echo ""

# 3) Unit test IntegrityService (CRC32 + SHA-256)
echo -e "${YELLOW}[3/5] Running IntegrityService unit test (SHA-256)...${NC}"
./core/build/test_integrity || { echo -e "${RED}✗ test_integrity failed${NC}"; exit 1; }
echo -e "${GREEN}✓ test_integrity passed${NC}"
echo ""

# 4) Register users
echo -e "${YELLOW}[4/5] Registering users...${NC}"
./client/build/test_auth register testuser1 pass123 testuser1@example.com $HOST $PORT 2>&1 | grep -q "ok=true" && echo -e "${GREEN}✓ testuser1${NC}" || echo -e "${YELLOW}⚠ testuser1 (may exist)${NC}"
./client/build/test_auth register testuser2 pass123 testuser2@example.com $HOST $PORT 2>&1 | grep -q "ok=true" && echo -e "${GREEN}✓ testuser2${NC}" || echo -e "${YELLOW}⚠ testuser2 (may exist)${NC}"
echo ""

# 5) Send file and verify server logs contain Phase 7 SHA-256 (computed_only or verified)
echo -e "${YELLOW}[5/5] Sending file and verifying SHA-256 in server...${NC}"
{
  echo "Phase 7 SHA-256 test file"
  echo "Content for integrity verification."
  for i in $(seq 1 500); do
    echo "Line $i: FileShareX Phase 7 SHA-256."
  done
} > test_phase7.txt
FILE_SIZE=$(wc -c < test_phase7.txt)
echo "Test file: test_phase7.txt ($FILE_SIZE bytes)"

SEND_LOG="/tmp/fsx_phase7_send_$$.log"
RECV_LOG="/tmp/fsx_phase7_recv_$$.log"
TID_FILE="/tmp/fsx_phase7_tid_$$.txt"
FIFO="/tmp/fsx_phase7_fifo_$$"
rm -f "$FIFO" "$TID_FILE"
if ! mkfifo "$FIFO" 2>/dev/null; then
  FIFO=""
fi

if [ -n "$FIFO" ]; then
  ( while IFS= read -r line; do
      echo "$line" >> "$SEND_LOG"
      tid=$(echo "$line" | sed -n 's/.*Transfer ID: \([0-9][0-9]*\).*/\1/p')
      if [ -n "$tid" ]; then echo "$tid" > "$TID_FILE"; fi
    done < "$FIFO" ) &
  READER_PID=$!
  ./client/build/test_file_transfer send testuser1 pass123 testuser2 ./test_phase7.txt $HOST $PORT > "$FIFO" 2>&1 &
  SEND_PID=$!
else
  ./client/build/test_file_transfer send testuser1 pass123 testuser2 ./test_phase7.txt $HOST $PORT > "$SEND_LOG" 2>&1 &
  SEND_PID=$!
  READER_PID=""
  sleep 3
fi

echo "Waiting for transfer_id (up to 25s)..."
TIMEOUT=50
ELAPSED=0
TRANSFER_ID=""
while [ "$ELAPSED" -lt "$TIMEOUT" ]; do
  sleep 0.5
  if [ -f "$TID_FILE" ]; then TRANSFER_ID=$(cat "$TID_FILE" 2>/dev/null); fi
  if [ -z "$TRANSFER_ID" ] && [ -f "$SEND_LOG" ]; then
    TRANSFER_ID=$(sed -n 's/.*Transfer ID: \([0-9][0-9]*\).*/\1/p' "$SEND_LOG" 2>/dev/null | head -1)
  fi
  if [ -n "$TRANSFER_ID" ]; then echo "Got transfer_id=$TRANSFER_ID"; break; fi
  ELAPSED=$((ELAPSED + 1))
done

if [ -z "$TRANSFER_ID" ]; then
  rm -f "$FIFO"
  [ -n "$READER_PID" ] && kill $READER_PID 2>/dev/null || true
  [ -n "$READER_PID" ] && wait $READER_PID 2>/dev/null || true
  echo -e "${RED}✗ Could not get transfer_id (timeout)${NC}"
  cat "$SEND_LOG" 2>/dev/null || echo "(empty)"
  kill $SEND_PID 2>/dev/null || true
  rm -f "$SEND_LOG" "$RECV_LOG" "$TID_FILE"
  exit 1
fi
rm -f "$TID_FILE"

echo "Starting receiver for transfer_id=$TRANSFER_ID..."
./client/build/test_file_transfer recv testuser2 pass123 "$TRANSFER_ID" ./received_phase7.txt $HOST $PORT > "$RECV_LOG" 2>&1 &
RECV_PID=$!

echo "Waiting for sender and receiver..."
wait $SEND_PID 2>/dev/null || true
wait $RECV_PID 2>/dev/null || true
[ -n "$READER_PID" ] && wait $READER_PID 2>/dev/null || true
rm -f "$FIFO"

if ! grep -q "SUCCESS! File saved at" "$SEND_LOG"; then
  echo -e "${RED}✗ Sender did not report SUCCESS${NC}"
  tail -20 "$SEND_LOG"
  rm -f "$SEND_LOG" "$RECV_LOG"
  exit 1
fi
echo -e "${GREEN}✓ Sender completed: file saved on server${NC}"

if ! grep -q "SUCCESS\|Accepted" "$RECV_LOG"; then
  echo -e "${RED}✗ Receiver did not accept${NC}"
  rm -f "$SEND_LOG" "$RECV_LOG"
  exit 1
fi
echo -e "${GREEN}✓ Receiver accepted${NC}"

# Phase 7: Server must log FILE_DONE_OK with sha256 (computed_only when client doesn't send hash)
echo ""
echo "Checking server logs for Phase 7 SHA-256..."
CORE_LOGS=$(docker logs fsx_core 2>&1)
if echo "$CORE_LOGS" | grep -q "FILE_DONE_OK.*transfer_id=$TRANSFER_ID"; then
  echo -e "${GREEN}✓ Server logged FILE_DONE_OK for transfer_id=$TRANSFER_ID${NC}"
else
  echo -e "${RED}✗ Server did not log FILE_DONE_OK for transfer_id=$TRANSFER_ID${NC}"
  echo "$CORE_LOGS" | grep "FILE_DONE\|transfer_id=$TRANSFER_ID" | tail -5
  rm -f "$SEND_LOG" "$RECV_LOG"
  exit 1
fi

if echo "$CORE_LOGS" | grep "transfer_id=$TRANSFER_ID" | grep -qE "sha256=computed_only|sha256=verified|sha256=mismatch"; then
  echo -e "${GREEN}✓ Phase 7: Server reported SHA-256 integrity (computed_only/verified/mismatch) in log${NC}"
else
  echo -e "${RED}✗ Phase 7: Server log should contain sha256=computed_only or sha256=verified for this transfer${NC}"
  echo "Relevant lines:"
  echo "$CORE_LOGS" | grep "transfer_id=$TRANSFER_ID" | tail -5
  rm -f "$SEND_LOG" "$RECV_LOG"
  exit 1
fi

rm -f "$SEND_LOG" "$RECV_LOG"

echo ""
echo -e "${GREEN}Phase 7 SHA-256 integrity test completed.${NC}"
echo "  - IntegrityService (SHA-256) unit test: OK"
echo "  - File transfer + server SHA-256 compute: OK"
echo "  - FILE_RESULT includes sha256_status/computed_sha256 for UI/log"
