#!/bin/bash
# Phase 4 Step-by-Step Test Script
# Usage: ./scripts/test_phase4_step_by_step.sh [step]
#   step: 1=normal, 2=fault_injection, 3=wireshark (optional)

set -e

HOST="127.0.0.1"
PORT="9000"

echo "=========================================="
echo "Phase 4 Step-by-Step Test"
echo "=========================================="
echo ""

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

# Check if we're in the right directory
if [ ! -f "docker/compose.yml" ]; then
    echo -e "${RED}Error: Run this script from project root${NC}"
    exit 1
fi

# Check containers
echo -e "${YELLOW}[Pre-check] Verifying containers...${NC}"
if ! docker ps | grep -q "fsx_core"; then
    echo -e "${RED}✗ Core server not running. Run: ./scripts/start_containers.sh${NC}"
    exit 1
fi
if ! docker ps | grep -q "fsx_db"; then
    echo -e "${RED}✗ Database not running. Run: ./scripts/start_containers.sh${NC}"
    exit 1
fi
echo -e "${GREEN}✓ Containers running${NC}"
echo ""

# Build client
echo -e "${YELLOW}[Build] Building client...${NC}"
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

# Create test file
echo -e "${YELLOW}[Setup] Creating test file...${NC}"
cat > test_phase4.txt << 'EOF'
This is a Phase 4 test file.
Line 2: Testing CRC32 + ACK/NAK + retransmission.
Line 3: This file will be transferred using FILE_UPLOAD_CHUNK.
Line 4: Each chunk includes CRC32 checksum.
Line 5: Receiver validates and sends ACK or NAK.
Line 6: Sender retries on NAK.
End of test file.
EOF
FILE_SIZE=$(wc -c < test_phase4.txt)
echo -e "${GREEN}✓ Test file created: test_phase4.txt (${FILE_SIZE} bytes)${NC}"
echo ""

# Register users
echo -e "${YELLOW}[Auth] Registering users...${NC}"
./client/build/test_auth register testuser1 pass123 testuser1@example.com $HOST $PORT 2>&1 | grep -q "ok=true" && echo -e "${GREEN}✓ testuser1${NC}" || echo -e "${YELLOW}⚠ testuser1 (might exist)${NC}"
./client/build/test_auth register testuser2 pass123 testuser2@example.com $HOST $PORT 2>&1 | grep -q "ok=true" && echo -e "${GREEN}✓ testuser2${NC}" || echo -e "${YELLOW}⚠ testuser2 (might exist)${NC}"
echo ""

# Login users
echo -e "${YELLOW}[Auth] Logging in users...${NC}"
if ! ./client/build/test_auth login testuser1 pass123 $HOST $PORT 2>&1 | grep -q "ok=true"; then
    echo -e "${RED}✗ testuser1 login failed${NC}"
    exit 1
fi
echo -e "${GREEN}✓ testuser1 logged in${NC}"

if ! ./client/build/test_auth login testuser2 pass123 $HOST $PORT 2>&1 | grep -q "ok=true"; then
    echo -e "${RED}✗ testuser2 login failed${NC}"
    exit 1
fi
echo -e "${GREEN}✓ testuser2 logged in${NC}"
echo ""

# Determine test step
STEP=${1:-1}

if [ "$STEP" = "1" ] || [ -z "$1" ]; then
    echo "=========================================="
    echo "Test 1: Normal Transfer (No Fault Injection)"
    echo "=========================================="
    echo ""
    
    SEND_LOG="/tmp/fsx_phase4_normal_send_$$.log"
    RECV_LOG="/tmp/fsx_phase4_normal_recv_$$.log"
    
    # Start sender
    echo -e "${YELLOW}[Send] Starting sender...${NC}"
    ./client/build/test_file_transfer \
      send \
      testuser1 pass123 \
      testuser2 \
      ./test_phase4.txt \
      $HOST $PORT > "$SEND_LOG" 2>&1 &
    SENDER_PID=$!
    
    # Wait for transfer_id
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
        echo -e "${RED}✗ Failed to get transfer_id${NC}"
        cat "$SEND_LOG"
        kill $SENDER_PID 2>/dev/null || true
        rm -f "$SEND_LOG" "$RECV_LOG"
        exit 1
    fi
    
    echo -e "${GREEN}✓ Transfer ID: $TRANSFER_ID${NC}"
    
    # Start receiver
    echo -e "${YELLOW}[Recv] Starting receiver...${NC}"
    ./client/build/test_file_transfer \
      recv \
      testuser2 pass123 \
      $TRANSFER_ID \
      ./received_phase4.txt \
      $HOST $PORT > "$RECV_LOG" 2>&1 &
    RECEIVER_PID=$!
    
    # Wait for completion
    wait $SENDER_PID 2>/dev/null || true
    wait $RECEIVER_PID 2>/dev/null || true
    
    echo ""
    echo -e "${YELLOW}Results:${NC}"
    echo "--- Sender Output ---"
    cat "$SEND_LOG"
    echo ""
    echo "--- Receiver Output ---"
    cat "$RECV_LOG"
    echo ""
    
    # Check success
    if grep -q "SUCCESS" "$SEND_LOG"; then
        echo -e "${GREEN}✓ Sender reported SUCCESS${NC}"
    else
        echo -e "${RED}✗ Sender did NOT report SUCCESS${NC}"
    fi
    
    # Check for Phase 4 messages
    if grep -q "FILE_UPLOAD_CHUNK\|ACK received\|NAK received" "$SEND_LOG"; then
        echo -e "${GREEN}✓ Phase 4 messages detected${NC}"
    else
        echo -e "${YELLOW}⚠ Phase 4 messages not found (might still be using Phase 3)${NC}"
    fi
    
    # Check server logs
    echo ""
    echo -e "${YELLOW}Server logs for transfer_id=$TRANSFER_ID:${NC}"
    docker logs fsx_core 2>&1 | grep "transfer_id=$TRANSFER_ID" | grep -E "FILE_UPLOAD_CHUNK|FILE_UPLOAD_ACK|FILE_UPLOAD_NAK|CRC32" | head -20 || echo "No Phase 4 logs found"
    
    rm -f "$SEND_LOG" "$RECV_LOG"
    
elif [ "$STEP" = "2" ]; then
    echo "=========================================="
    echo "Test 2: Fault Injection (Corrupted CRC32)"
    echo "=========================================="
    echo ""
    echo -e "${YELLOW}This test requires --corrupt-upload-once flag in client${NC}"
    echo -e "${YELLOW}Run this after implementing fault injection support${NC}"
    echo ""
    
elif [ "$STEP" = "3" ]; then
    echo "=========================================="
    echo "Test 3: Wireshark Capture Instructions"
    echo "=========================================="
    echo ""
    echo "1. Start Wireshark and capture on loopback interface"
    echo "2. Apply filter: tcp.port == 9000"
    echo "3. Run Test 1 in another terminal"
    echo "4. Look for:"
    echo "   - FILE_UPLOAD_CHUNK messages (type 37)"
    echo "   - FILE_UPLOAD_ACK messages (type 38)"
    echo "   - FILE_UPLOAD_NAK messages (type 39)"
    echo "5. Verify CRC32 field in FILE_UPLOAD_CHUNK"
    echo ""
fi

echo ""
echo "=========================================="
echo "Test completed"
echo "=========================================="

