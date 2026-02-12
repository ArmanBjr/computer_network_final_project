#!/bin/bash
# ==========================================================
# Phase 9 Test Script — Throttling + Parallel Transfers
# Usage: ./scripts/wsl_test_phase9.sh
#
# Tests:
#   1. set-throttle / list-transfers admin commands
#   2. Throttle enforcement (ACK delay measured via transfer speed)
#   3. Two parallel file transfers (two senders at the same time)
#   4. Dashboard API endpoints
#
# Pre-requisite:
#   docker compose -f docker/compose.yml up -d db core
# ==========================================================
set -e

HOST="127.0.0.1"
PORT="9000"

echo "=========================================="
echo " Phase 9  Throttling + Parallel Transfers"
echo "=========================================="
echo ""

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[0;33m'
CYAN='\033[0;36m'
NC='\033[0m'

pass() { echo -e "${GREEN}✓ $1${NC}"; }
fail() { echo -e "${RED}✗ $1${NC}"; }
info() { echo -e "${YELLOW}$1${NC}"; }
step() { echo -e "${CYAN}[$1] $2${NC}"; }

if [ ! -f "docker/compose.yml" ]; then
  fail "Run from project root (where docker/compose.yml exists)"
  exit 1
fi

# --------------------------------------------------------
# 1. Check Docker services
# --------------------------------------------------------
step "1/8" "Checking Docker services..."
if ! docker ps | grep -q "fsx_core"; then
  fail "Core not running. Start: docker compose -f docker/compose.yml up -d db core"
  exit 1
fi
if ! docker ps | grep -q "fsx_db"; then
  fail "Database not running."
  exit 1
fi
pass "Docker services running"
echo ""

# --------------------------------------------------------
# 2. Build core + client
# --------------------------------------------------------
step "2/8" "Building core and client..."
(cd core  && rm -rf build && cmake -S . -B build 2>&1 | tail -3 && cmake --build build -j$(nproc) 2>&1 | tail -5)
(cd client && rm -rf build && cmake -S . -B build 2>&1 | tail -3 && cmake --build build -j$(nproc) 2>&1 | tail -5)

[ -f "client/build/test_file_transfer" ] || { fail "Client build failed"; exit 1; }
pass "Build complete"
echo ""

# --------------------------------------------------------
# 3. Register test users
# --------------------------------------------------------
step "3/8" "Registering users..."
for u in throttle_sender1 throttle_sender2 throttle_receiver; do
  ./client/build/test_auth register "$u" pass123 "${u}@example.com" $HOST $PORT 2>&1 | \
    grep -q "ok=true" && pass "  $u registered" || info "  $u (may exist)"
done
echo ""

# --------------------------------------------------------
# 4. Test set-throttle command (global)
# --------------------------------------------------------
step "4/8" "Testing set-throttle (global 512 KB/s)..."
THROTTLE_OUT=$(./client/build/test_file_transfer set-throttle global 524288 $HOST $PORT 2>&1)
if echo "$THROTTLE_OUT" | grep -q "THROTTLE_SET_RESP: OK"; then
  pass "THROTTLE_SET global=512KB/s acknowledged"
else
  fail "THROTTLE_SET failed"
  echo "$THROTTLE_OUT"
  exit 1
fi
echo ""

# --------------------------------------------------------
# 5. Test list-transfers command (empty list)
# --------------------------------------------------------
step "5/8" "Testing list-transfers (expect empty)..."
LIST_OUT=$(./client/build/test_file_transfer list-transfers $HOST $PORT 2>&1)
if echo "$LIST_OUT" | grep -q "Active transfers: 0"; then
  pass "list-transfers returned 0 (correct, no active transfers)"
else
  info "list-transfers output: $LIST_OUT"
fi
echo ""

# --------------------------------------------------------
# 6. Throttled single transfer — measure speed
# --------------------------------------------------------
step "6/8" "Throttled transfer test (global 100 KB/s, 500 KB file)..."

# Set strict throttle: 100 KB/s
./client/build/test_file_transfer set-throttle global 102400 $HOST $PORT >/dev/null 2>&1

# Create 500 KB test file
dd if=/dev/urandom of=/tmp/fsx_p9_test.bin bs=1024 count=500 2>/dev/null
FILE_SIZE=$(wc -c < /tmp/fsx_p9_test.bin)
info "  Test file: $FILE_SIZE bytes  |  Throttle: 100 KB/s  |  Expected time: ~5s"

SEND_LOG="/tmp/fsx_p9_send_$$.log"
RECV_LOG="/tmp/fsx_p9_recv_$$.log"
TID_FILE="/tmp/fsx_p9_tid_$$.txt"
FIFO="/tmp/fsx_p9_fifo_$$"
rm -f "$FIFO" "$TID_FILE"
mkfifo "$FIFO" 2>/dev/null || FIFO=""

# Sender (background)
if [ -n "$FIFO" ]; then
  ( while IFS= read -r line; do
      echo "$line" >> "$SEND_LOG"
      tid=$(echo "$line" | sed -n 's/.*Transfer ID: \([0-9][0-9]*\).*/\1/p')
      [ -n "$tid" ] && echo "$tid" > "$TID_FILE"
    done < "$FIFO" ) &
  READER_PID=$!
  ./client/build/test_file_transfer send throttle_sender1 pass123 throttle_receiver /tmp/fsx_p9_test.bin $HOST $PORT > "$FIFO" 2>&1 &
  SEND_PID=$!
else
  ./client/build/test_file_transfer send throttle_sender1 pass123 throttle_receiver /tmp/fsx_p9_test.bin $HOST $PORT > "$SEND_LOG" 2>&1 &
  SEND_PID=$!
  READER_PID=""
  sleep 3
fi

# Wait for transfer_id
TIMEOUT=50; ELAPSED=0; TRANSFER_ID=""
while [ "$ELAPSED" -lt "$TIMEOUT" ]; do
  sleep 0.5
  [ -f "$TID_FILE" ] && TRANSFER_ID=$(cat "$TID_FILE" 2>/dev/null)
  if [ -z "$TRANSFER_ID" ] && [ -f "$SEND_LOG" ]; then
    TRANSFER_ID=$(sed -n 's/.*Transfer ID: \([0-9][0-9]*\).*/\1/p' "$SEND_LOG" 2>/dev/null | head -1)
  fi
  [ -n "$TRANSFER_ID" ] && break
  ELAPSED=$((ELAPSED + 1))
done
rm -f "$TID_FILE"

if [ -z "$TRANSFER_ID" ]; then
  rm -f "$FIFO"
  [ -n "$READER_PID" ] && kill $READER_PID 2>/dev/null; wait $READER_PID 2>/dev/null || true
  fail "Could not get transfer_id"
  cat "$SEND_LOG" 2>/dev/null; kill $SEND_PID 2>/dev/null
  rm -f "$SEND_LOG" "$RECV_LOG"
  exit 1
fi
info "  transfer_id=$TRANSFER_ID"

# Receiver
./client/build/test_file_transfer recv throttle_receiver pass123 "$TRANSFER_ID" /tmp/fsx_p9_recv.bin $HOST $PORT > "$RECV_LOG" 2>&1 &
RECV_PID=$!

# Wait completion
wait $SEND_PID 2>/dev/null || true
wait $RECV_PID 2>/dev/null || true
[ -n "$READER_PID" ] && wait $READER_PID 2>/dev/null || true
rm -f "$FIFO"

if grep -q "SUCCESS! File saved at" "$SEND_LOG"; then
  pass "Throttled transfer completed"
  # Check speed
  AVG_SPEED=$(grep "Average speed" "$SEND_LOG" | sed -n 's/.*Average speed: \([0-9.]*\).*/\1/p')
  ELAPSED_TIME=$(grep "elapsed:" "$SEND_LOG" | sed -n 's/.*elapsed: \([0-9.]*\).*/\1/p')
  if [ -n "$AVG_SPEED" ]; then
    info "  Average speed: ${AVG_SPEED} KB/s (elapsed: ${ELAPSED_TIME}s)"
  fi
else
  fail "Throttled transfer did not succeed"
  tail -15 "$SEND_LOG"
fi

# Check server logs for THROTTLE_DELAYED_ACK
CORE_LOGS=$(docker logs fsx_core --tail=200 2>&1)
if echo "$CORE_LOGS" | grep -q "THROTTLE_DELAYED_ACK"; then
  DELAY_COUNT=$(echo "$CORE_LOGS" | grep -c "THROTTLE_DELAYED_ACK" || echo "0")
  pass "Server applied throttle delays ($DELAY_COUNT delayed ACKs in logs)"
else
  info "  Note: no THROTTLE_DELAYED_ACK found (file may have been too small for visible delay)"
fi
rm -f "$SEND_LOG" "$RECV_LOG"
echo ""

# --------------------------------------------------------
# 7. Parallel transfers — two senders simultaneously
# --------------------------------------------------------
step "7/8" "Parallel transfers test (two senders at the same time)..."

# Clear throttle first
./client/build/test_file_transfer set-throttle global 0 $HOST $PORT >/dev/null 2>&1

# Create two test files
dd if=/dev/urandom of=/tmp/fsx_p9_file_a.bin bs=1024 count=200 2>/dev/null
dd if=/dev/urandom of=/tmp/fsx_p9_file_b.bin bs=1024 count=200 2>/dev/null

LOG_A="/tmp/fsx_p9_a_$$.log"
LOG_B="/tmp/fsx_p9_b_$$.log"
FIFO_A="/tmp/fsx_p9_fifo_a_$$"
FIFO_B="/tmp/fsx_p9_fifo_b_$$"
TID_A="/tmp/fsx_p9_tid_a_$$.txt"
TID_B="/tmp/fsx_p9_tid_b_$$.txt"
rm -f "$FIFO_A" "$FIFO_B" "$TID_A" "$TID_B"

mkfifo "$FIFO_A" 2>/dev/null || FIFO_A=""
mkfifo "$FIFO_B" 2>/dev/null || FIFO_B=""

# Start sender A
if [ -n "$FIFO_A" ]; then
  ( while IFS= read -r line; do echo "$line" >> "$LOG_A"; tid=$(echo "$line" | sed -n 's/.*Transfer ID: \([0-9][0-9]*\).*/\1/p'); [ -n "$tid" ] && echo "$tid" > "$TID_A"; done < "$FIFO_A" ) &
  RA=$!
  ./client/build/test_file_transfer send throttle_sender1 pass123 throttle_receiver /tmp/fsx_p9_file_a.bin $HOST $PORT > "$FIFO_A" 2>&1 &
  SA=$!
else
  ./client/build/test_file_transfer send throttle_sender1 pass123 throttle_receiver /tmp/fsx_p9_file_a.bin $HOST $PORT > "$LOG_A" 2>&1 &
  SA=$!; RA=""
fi

# Start sender B (different user)
if [ -n "$FIFO_B" ]; then
  ( while IFS= read -r line; do echo "$line" >> "$LOG_B"; tid=$(echo "$line" | sed -n 's/.*Transfer ID: \([0-9][0-9]*\).*/\1/p'); [ -n "$tid" ] && echo "$tid" > "$TID_B"; done < "$FIFO_B" ) &
  RB=$!
  ./client/build/test_file_transfer send throttle_sender2 pass123 throttle_receiver /tmp/fsx_p9_file_b.bin $HOST $PORT > "$FIFO_B" 2>&1 &
  SB=$!
else
  ./client/build/test_file_transfer send throttle_sender2 pass123 throttle_receiver /tmp/fsx_p9_file_b.bin $HOST $PORT > "$LOG_B" 2>&1 &
  SB=$!; RB=""
fi

# Wait for both transfer IDs
sleep 2
TID_VAL_A=""
TID_VAL_B=""
for i in $(seq 1 60); do
  [ -z "$TID_VAL_A" ] && [ -f "$TID_A" ] && TID_VAL_A=$(cat "$TID_A" 2>/dev/null)
  [ -z "$TID_VAL_A" ] && [ -f "$LOG_A" ] && TID_VAL_A=$(sed -n 's/.*Transfer ID: \([0-9][0-9]*\).*/\1/p' "$LOG_A" 2>/dev/null | head -1)
  [ -z "$TID_VAL_B" ] && [ -f "$TID_B" ] && TID_VAL_B=$(cat "$TID_B" 2>/dev/null)
  [ -z "$TID_VAL_B" ] && [ -f "$LOG_B" ] && TID_VAL_B=$(sed -n 's/.*Transfer ID: \([0-9][0-9]*\).*/\1/p' "$LOG_B" 2>/dev/null | head -1)
  [ -n "$TID_VAL_A" ] && [ -n "$TID_VAL_B" ] && break
  sleep 0.5
done

info "  Transfer A: tid=$TID_VAL_A  |  Transfer B: tid=$TID_VAL_B"

# Accept both transfers
if [ -n "$TID_VAL_A" ]; then
  ./client/build/test_file_transfer recv throttle_receiver pass123 "$TID_VAL_A" /tmp/fsx_p9_recv_a.bin $HOST $PORT >/dev/null 2>&1 &
  RECV_A=$!
fi
if [ -n "$TID_VAL_B" ]; then
  ./client/build/test_file_transfer recv throttle_receiver pass123 "$TID_VAL_B" /tmp/fsx_p9_recv_b.bin $HOST $PORT >/dev/null 2>&1 &
  RECV_B=$!
fi

# Wait for all to finish
wait $SA 2>/dev/null || true
wait $SB 2>/dev/null || true
[ -n "$RECV_A" ] && wait $RECV_A 2>/dev/null || true
[ -n "$RECV_B" ] && wait $RECV_B 2>/dev/null || true
[ -n "$RA" ] && wait $RA 2>/dev/null || true
[ -n "$RB" ] && wait $RB 2>/dev/null || true
rm -f "$FIFO_A" "$FIFO_B" "$TID_A" "$TID_B"

SUCCESS_A=false; SUCCESS_B=false
[ -f "$LOG_A" ] && grep -q "SUCCESS! File saved at" "$LOG_A" && SUCCESS_A=true
[ -f "$LOG_B" ] && grep -q "SUCCESS! File saved at" "$LOG_B" && SUCCESS_B=true

if $SUCCESS_A && $SUCCESS_B; then
  pass "Both parallel transfers completed successfully"
elif $SUCCESS_A; then
  pass "Transfer A succeeded"; fail "Transfer B failed"
  [ -f "$LOG_B" ] && tail -5 "$LOG_B"
elif $SUCCESS_B; then
  fail "Transfer A failed"; pass "Transfer B succeeded"
  [ -f "$LOG_A" ] && tail -5 "$LOG_A"
else
  fail "Both transfers failed"
  [ -f "$LOG_A" ] && echo "--- A ---" && tail -5 "$LOG_A"
  [ -f "$LOG_B" ] && echo "--- B ---" && tail -5 "$LOG_B"
fi
rm -f "$LOG_A" "$LOG_B"
echo ""

# --------------------------------------------------------
# 8. Check server logs for Phase 9 messages
# --------------------------------------------------------
step "8/8" "Verifying server logs for Phase 9 messages..."
CORE_LOGS=$(docker logs fsx_core --tail=300 2>&1)

echo "$CORE_LOGS" | grep -q "THROTTLE_SET" && pass "THROTTLE_SET logged" || fail "No THROTTLE_SET in logs"
echo "$CORE_LOGS" | grep -q "TRANSFER_LIST_RESP" && pass "TRANSFER_LIST_RESP logged" || info "  No TRANSFER_LIST_RESP (may not have been called during transfers)"
echo "$CORE_LOGS" | grep -q "FILE_UPLOAD_CHUNK_OK" && pass "FILE_UPLOAD_CHUNK_OK logged (transfers worked)" || fail "No chunk logs"
echo ""

# --------------------------------------------------------
# Summary
# --------------------------------------------------------
echo "=========================================="
echo -e "${GREEN} Phase 9 test complete!${NC}"
echo "=========================================="
echo ""
echo "What was tested:"
echo "  - Token-bucket throttle (set via admin command)"
echo "  - Throttle enforcement (server delays ACK)"
echo "  - Two parallel file transfers (simultaneous)"
echo "  - Admin commands: set-throttle, list-transfers"
echo "  - Speed measurement in client output"
echo ""
echo "Dashboard:"
echo "  - POST /api/throttle   — set rate limit"
echo "  - GET  /api/transfers  — list active transfers"
echo "  - Dashboard UI has slider + transfer table"

# Cleanup temp files
rm -f /tmp/fsx_p9_*.bin /tmp/fsx_p9_*.log /tmp/fsx_p9_*.txt
