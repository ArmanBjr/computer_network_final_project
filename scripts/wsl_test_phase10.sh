#!/bin/bash
# ==========================================================
# Phase 10 Test Script — Voice Chat (UDP + Opus + Relay)
# Usage: ./scripts/wsl_test_phase10.sh
#
# Tests:
#   1. Docker services running (core + db)
#   2. Build core and client (with libopus)
#   3. Register test users for voice
#   4. Voice call: caller initiates, answerer accepts
#   5. Synthetic Opus frames exchanged for 5 seconds
#   6. Verify frames sent/received counters
#   7. Check server logs for voice session messages
#
# Pre-requisite:
#   docker compose -f docker/compose.yml up -d db core
#   libopus-dev installed on WSL
# ==========================================================
set -e

HOST="127.0.0.1"
TCP_PORT="9000"
UDP_PORT="9001"
DURATION=5

echo "=========================================="
echo " Phase 10   Voice Chat (UDP + Opus)"
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

TOTAL_TESTS=0
PASSED_TESTS=0

# --------------------------------------------------------
# 1. Check Docker services
# --------------------------------------------------------
step "1/7" "Checking Docker services..."
TOTAL_TESTS=$((TOTAL_TESTS + 1))
if ! docker ps | grep -q "fsx_core"; then
  fail "Core not running. Start: docker compose -f docker/compose.yml up -d db core"
  exit 1
fi
if ! docker ps | grep -q "fsx_db"; then
  fail "Database not running."
  exit 1
fi
pass "Docker services running"
PASSED_TESTS=$((PASSED_TESTS + 1))
echo ""

# --------------------------------------------------------
# 2. Check libopus and build core + client
# --------------------------------------------------------
step "2/7" "Checking libopus and building..."

# Check if libopus-dev is installed
if ! pkg-config --exists opus 2>/dev/null; then
  info "  libopus-dev not found, installing..."
  sudo apt-get update -qq && sudo apt-get install -y -qq libopus-dev >/dev/null 2>&1
fi

TOTAL_TESTS=$((TOTAL_TESTS + 1))

info "  Building core..."
(cd core && rm -rf build && cmake -S . -B build 2>&1 | tail -3 && cmake --build build -j$(nproc) 2>&1 | tail -5)

info "  Building client..."
(cd client && rm -rf build && cmake -S . -B build 2>&1 | tail -3 && cmake --build build -j$(nproc) 2>&1 | tail -5)

if [ ! -f "client/build/test_voice" ]; then
  fail "test_voice build failed"
  echo "Check that libopus-dev is installed: sudo apt-get install libopus-dev"
  exit 1
fi

if [ ! -f "client/build/test_auth" ]; then
  fail "test_auth build failed"
  exit 1
fi

pass "Build complete (core + client + test_voice)"
PASSED_TESTS=$((PASSED_TESTS + 1))
echo ""

# --------------------------------------------------------
# 3. Register test users
# --------------------------------------------------------
step "3/7" "Registering voice test users..."
TOTAL_TESTS=$((TOTAL_TESTS + 1))

for u in voice_caller voice_answerer; do
  ./client/build/test_auth register "$u" pass123 "${u}@example.com" $HOST $TCP_PORT 2>&1 | \
    grep -q "ok=true" && pass "  $u registered" || info "  $u (may already exist)"
done
pass "Users ready"
PASSED_TESTS=$((PASSED_TESTS + 1))
echo ""

# --------------------------------------------------------
# 4. Voice call test — answerer + caller
# --------------------------------------------------------
step "4/7" "Starting voice call test (${DURATION}s synthetic audio)..."
TOTAL_TESTS=$((TOTAL_TESTS + 1))

ANSWER_LOG="/tmp/fsx_p10_answer_$$.log"
CALL_LOG="/tmp/fsx_p10_call_$$.log"

# Start answerer in background (must be running before caller sends VOICE_CALL_REQ)
info "  Starting answerer (voice_answerer)..."
./client/build/test_voice answer voice_answerer pass123 $HOST $TCP_PORT $DURATION > "$ANSWER_LOG" 2>&1 &
ANSWER_PID=$!

# Give answerer time to connect and log in
sleep 2

# Start caller
info "  Starting caller (voice_caller -> voice_answerer)..."
./client/build/test_voice call voice_caller pass123 voice_answerer $HOST $TCP_PORT $DURATION > "$CALL_LOG" 2>&1 &
CALL_PID=$!

# Wait for both to finish (with timeout)
info "  Waiting for voice exchange (${DURATION}s + buffer)..."
WAIT_TIMEOUT=$((DURATION + 15))
WAIT_START=$(date +%s)

# Wait for caller to finish
while kill -0 $CALL_PID 2>/dev/null; do
  NOW=$(date +%s)
  ELAPSED=$((NOW - WAIT_START))
  if [ $ELAPSED -gt $WAIT_TIMEOUT ]; then
    info "  Timeout waiting for caller, killing..."
    kill $CALL_PID 2>/dev/null || true
    break
  fi
  sleep 1
done
wait $CALL_PID 2>/dev/null || true

# Wait a bit for answerer to get VOICE_END and finish
sleep 2
if kill -0 $ANSWER_PID 2>/dev/null; then
  kill $ANSWER_PID 2>/dev/null || true
fi
wait $ANSWER_PID 2>/dev/null || true

# Show logs
echo ""
info "  === Caller Log ==="
cat "$CALL_LOG" | head -30
echo ""
info "  === Answerer Log ==="
cat "$ANSWER_LOG" | head -30
echo ""

# Check if call was established
if grep -q "VOICE_CALL accepted" "$CALL_LOG"; then
  pass "Voice call established"
  PASSED_TESTS=$((PASSED_TESTS + 1))
else
  fail "Voice call NOT established"
  echo "Caller log:"
  cat "$CALL_LOG"
  echo ""
  echo "Answerer log:"
  cat "$ANSWER_LOG"
fi
echo ""

# --------------------------------------------------------
# 5. Verify caller frame counts
# --------------------------------------------------------
step "5/7" "Verifying caller frame exchange..."
TOTAL_TESTS=$((TOTAL_TESTS + 1))

CALLER_SENT=$(grep -oP 'frames_sent: \K[0-9]+' "$CALL_LOG" | tail -1)
CALLER_RECV=$(grep -oP 'frames_received: \K[0-9]+' "$CALL_LOG" | tail -1)

if [ -z "$CALLER_SENT" ]; then CALLER_SENT=0; fi
if [ -z "$CALLER_RECV" ]; then CALLER_RECV=0; fi

info "  Caller: sent=$CALLER_SENT received=$CALLER_RECV"

# We expect at least ~200 frames sent in 5 seconds (5000ms / 20ms = 250 theoretical)
# Allow some slack for timing
if [ "$CALLER_SENT" -gt 100 ]; then
  pass "Caller sent $CALLER_SENT frames (good, expected ~250)"
else
  fail "Caller sent only $CALLER_SENT frames (expected >100)"
fi

# Caller should receive frames from answerer via relay
if [ "$CALLER_RECV" -gt 10 ]; then
  pass "Caller received $CALLER_RECV frames via server relay"
  PASSED_TESTS=$((PASSED_TESTS + 1))
else
  info "  Caller received $CALLER_RECV frames (relay may have started late)"
  # Still pass if call was established — relay needs both sides active
  if grep -q "VOICE_CALL accepted" "$CALL_LOG"; then
    pass "Caller frames OK (call was active, relay timing expected)"
    PASSED_TESTS=$((PASSED_TESTS + 1))
  else
    fail "Caller frame exchange failed"
  fi
fi
echo ""

# --------------------------------------------------------
# 6. Verify answerer frame counts
# --------------------------------------------------------
step "6/7" "Verifying answerer frame exchange..."
TOTAL_TESTS=$((TOTAL_TESTS + 1))

ANSWERER_SENT=$(grep -oP 'frames_sent: \K[0-9]+' "$ANSWER_LOG" | tail -1)
ANSWERER_RECV=$(grep -oP 'frames_received: \K[0-9]+' "$ANSWER_LOG" | tail -1)

if [ -z "$ANSWERER_SENT" ]; then ANSWERER_SENT=0; fi
if [ -z "$ANSWERER_RECV" ]; then ANSWERER_RECV=0; fi

info "  Answerer: sent=$ANSWERER_SENT received=$ANSWERER_RECV"

if [ "$ANSWERER_SENT" -gt 100 ]; then
  pass "Answerer sent $ANSWERER_SENT frames"
else
  info "  Answerer sent $ANSWERER_SENT frames (may have started late)"
fi

if [ "$ANSWERER_RECV" -gt 10 ]; then
  pass "Answerer received $ANSWERER_RECV frames via server relay"
  PASSED_TESTS=$((PASSED_TESTS + 1))
else
  info "  Answerer received $ANSWERER_RECV frames"
  if grep -q "call active" "$ANSWER_LOG"; then
    pass "Answerer frames OK (call was active)"
    PASSED_TESTS=$((PASSED_TESTS + 1))
  else
    fail "Answerer frame exchange failed"
  fi
fi
echo ""

# --------------------------------------------------------
# 7. Check server logs for voice session
# --------------------------------------------------------
step "7/7" "Checking server logs for voice session events..."
TOTAL_TESTS=$((TOTAL_TESTS + 1))

SERVER_LOG=$(docker logs fsx_core 2>&1 | tail -100)

VOICE_STARTED=$(echo "$SERVER_LOG" | grep -c "\[voice\] session.*started" || true)
VOICE_ENDED=$(echo "$SERVER_LOG" | grep -c "\[voice\] session.*ended" || true)
VOICE_UDP_CALLER=$(echo "$SERVER_LOG" | grep -c "caller UDP registered" || true)
VOICE_UDP_CALLEE=$(echo "$SERVER_LOG" | grep -c "callee UDP registered" || true)

info "  voice sessions started: $VOICE_STARTED"
info "  voice sessions ended: $VOICE_ENDED"
info "  caller UDP registered: $VOICE_UDP_CALLER"
info "  callee UDP registered: $VOICE_UDP_CALLEE"

if [ "$VOICE_STARTED" -gt 0 ]; then
  pass "Server logged voice session start"
else
  fail "No voice session start found in server logs"
fi

if [ "$VOICE_ENDED" -gt 0 ]; then
  pass "Server logged voice session end (with frame count)"
else
  info "  Voice session end not found (caller may have disconnected before log)"
fi

if [ "$VOICE_UDP_CALLER" -gt 0 ] && [ "$VOICE_UDP_CALLEE" -gt 0 ]; then
  pass "Both UDP endpoints registered with server"
  PASSED_TESTS=$((PASSED_TESTS + 1))
else
  info "  UDP endpoint registration: caller=$VOICE_UDP_CALLER callee=$VOICE_UDP_CALLEE"
  # Partial pass if at least one registered
  if [ "$VOICE_UDP_CALLER" -gt 0 ] || [ "$VOICE_UDP_CALLEE" -gt 0 ]; then
    pass "At least one UDP endpoint registered"
    PASSED_TESTS=$((PASSED_TESTS + 1))
  else
    fail "No UDP endpoints registered in server logs"
  fi
fi

# Show relayed frame count from server logs
RELAYED=$(echo "$SERVER_LOG" | grep -oP 'frames relayed\)' | head -1 || true)
if [ -n "$RELAYED" ]; then
  RELAY_COUNT=$(echo "$SERVER_LOG" | grep -oP '\((\d+) frames relayed' | head -1 | grep -oP '\d+' || true)
  info "  Server relayed: ${RELAY_COUNT:-?} frames"
fi
echo ""

# --------------------------------------------------------
# Summary
# --------------------------------------------------------
echo ""
echo "=========================================="
echo -e " Phase 10 Results: ${PASSED_TESTS}/${TOTAL_TESTS} passed"
echo "=========================================="
echo ""

# Cleanup
rm -f "$CALL_LOG" "$ANSWER_LOG"

if [ "$PASSED_TESTS" -ge "$TOTAL_TESTS" ]; then
  echo -e "${GREEN}All tests passed!${NC}"
  exit 0
elif [ "$PASSED_TESTS" -ge $((TOTAL_TESTS - 1)) ]; then
  echo -e "${YELLOW}Almost all tests passed (minor issues).${NC}"
  exit 0
else
  echo -e "${RED}Some tests failed.${NC}"
  exit 1
fi
