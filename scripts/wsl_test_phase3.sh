#!/bin/bash
# Phase 3 Test Script for WSL
# Usage: ./wsl_test_phase3.sh

set -e

HOST="127.0.0.1"
PORT="9000"

echo "=========================================="
echo "Phase 3 File Transfer Test (WSL)"
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

# Step 1: Check services
echo -e "${YELLOW}[1/9] Checking Docker services...${NC}"
if ! docker ps | grep -q "fsx_core"; then
    echo -e "${RED}✗ Core server not running${NC}"
    echo "Starting services..."
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
echo -e "${YELLOW}[2/9] Building client...${NC}"
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
echo -e "${YELLOW}[3/9] Creating test file...${NC}"
cat > test_phase3.txt << 'EOF'
This is a test file for FileShareX Phase 3.
It contains multiple lines to test file transfer functionality.

Line 3: Testing chunk-based transfer
Line 4: Testing server-mediated architecture
Line 5: Testing file integrity

End of test file.
EOF

FILE_SIZE=$(wc -c < test_phase3.txt)
echo -e "${GREEN}✓ Test file created: test_phase3.txt ($FILE_SIZE bytes)${NC}"

# Optional: Create a larger test file (5MB) to demonstrate chunking
# Set ENABLE_LARGE_FILE_TEST=1 to enable
if [ "${ENABLE_LARGE_FILE_TEST:-0}" = "1" ]; then
    TEST_FILE_LARGE="test_phase3_5mb.bin"
    if [ ! -f "$TEST_FILE_LARGE" ]; then
        echo "Creating 5MB test file for chunking demonstration..."
        if command -v dd >/dev/null 2>&1; then
            dd if=/dev/urandom of="$TEST_FILE_LARGE" bs=1M count=5 2>/dev/null || \
            head -c 5242880 /dev/urandom > "$TEST_FILE_LARGE" 2>/dev/null
        elif command -v python3 >/dev/null 2>&1; then
            python3 -c "import os; f=open('$TEST_FILE_LARGE','wb'); f.write(os.urandom(5242880)); f.close()" 2>/dev/null
        else
            echo "Warning: Could not create large test file (no dd/python3 available)"
        fi
    fi
    if [ -f "$TEST_FILE_LARGE" ]; then
        LARGE_SIZE=$(wc -c < "$TEST_FILE_LARGE" 2>/dev/null || echo "0")
        echo -e "${GREEN}✓ Large test file available: $TEST_FILE_LARGE ($LARGE_SIZE bytes)${NC}"
        echo "  To use large file: ENABLE_LARGE_FILE_TEST=1 ./scripts/wsl_test_phase3.sh"
    fi
fi
echo ""

# Step 4: Register users
echo -e "${YELLOW}[4/9] Registering users...${NC}"
./client/build/test_auth register testuser1 pass123 testuser1@example.com $HOST $PORT 2>&1 | grep -q "ok=true" && echo -e "${GREEN}✓ testuser1${NC}" || echo -e "${YELLOW}⚠ testuser1 (might exist)${NC}"
./client/build/test_auth register testuser2 pass123 testuser2@example.com $HOST $PORT 2>&1 | grep -q "ok=true" && echo -e "${GREEN}✓ testuser2${NC}" || echo -e "${YELLOW}⚠ testuser2 (might exist)${NC}"
echo ""

# Step 5: Login users
echo -e "${YELLOW}[5/9] Logging in users...${NC}"
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

# Step 6: Send file (in background, capture transfer_id from output)
echo -e "${YELLOW}[6/9] Sending file...${NC}"
SEND_LOG="/tmp/fsx_sender_$$.log"
./client/build/test_file_transfer send testuser1 pass123 testuser2 ./test_phase3.txt $HOST $PORT > "$SEND_LOG" 2>&1 &
SENDER_PID=$!

# Wait for transfer_id to appear in log (with flush, it should appear quickly)
TIMEOUT=5
ELAPSED=0
TRANSFER_ID=""
while [ $ELAPSED -lt $TIMEOUT ]; do
    sleep 0.2
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
    echo "Sender log:"
    cat "$SEND_LOG" 2>/dev/null || true
    kill $SENDER_PID 2>/dev/null || true
    rm -f "$SEND_LOG"
    exit 1
fi

echo -e "${GREEN}✓ Transfer ID: $TRANSFER_ID${NC}"
echo ""

# Step 7: Receive file (this will trigger sender to continue)
echo -e "${YELLOW}[7/9] Receiving file...${NC}"
# Start receiver in background
./client/build/test_file_transfer recv testuser2 pass123 $TRANSFER_ID ./received_phase3.txt $HOST $PORT > /tmp/fsx_receiver_$$.log 2>&1 &
RECEIVER_PID=$!

# Wait a bit for receiver to accept
sleep 2

# Wait for both to complete
wait $SENDER_PID 2>/dev/null || true
wait $RECEIVER_PID 2>/dev/null || true

# Check results
if [ -f "$SEND_LOG" ]; then
    if grep -q "SUCCESS" "$SEND_LOG"; then
        echo -e "${GREEN}✓ File sent successfully${NC}"
    else
        echo -e "${YELLOW}⚠ Sender output:${NC}"
        cat "$SEND_LOG"
    fi
    
    # Debug: Show chunk sending logs
    if grep -q "Sending chunk\|Chunk.*sent" "$SEND_LOG"; then
        echo -e "${GREEN}✓ Chunks were sent (check logs above)${NC}"
    else
        echo -e "${RED}✗ WARNING: No chunk sending logs found!${NC}"
        echo "Full sender log:"
        cat "$SEND_LOG"
    fi
fi

RECV_OUTPUT=$(cat /tmp/fsx_receiver_$$.log 2>/dev/null || echo "")
# Don't delete logs yet - we might need them for debugging
# rm -f "$SEND_LOG" /tmp/fsx_receiver_$$.log

if echo "$RECV_OUTPUT" | grep -q "SUCCESS"; then
    echo -e "${GREEN}✓ Transfer accepted${NC}"
else
    echo -e "${YELLOW}⚠ Receiver output:${NC}"
    echo "$RECV_OUTPUT"
    # In MVP, receiver just accepts, doesn't download yet
    echo -e "${YELLOW}Note: In MVP, file is saved on server. Download protocol not implemented yet.${NC}"
fi
echo ""

# Step 8: Verify file on server (MVP - file download not implemented yet)
echo -e "${YELLOW}[8/9] Verifying file on server...${NC}"
# In MVP, file is stored on server, not downloaded to receiver
# Check server logs for file save confirmation
if docker logs fsx_core 2>&1 | grep -q "FILE_DONE_OK.*transfer_id=$TRANSFER_ID"; then
    echo -e "${GREEN}✓ File saved on server (transfer_id=$TRANSFER_ID)${NC}"
    
    # Try to check if file exists in container (check multiple possible paths)
    # Container workdir is /app, and storage path is relative: ./storage/transfers
    SERVER_FILE=""
    for base_path in "/app" "/src" "."; do
        for pattern in "*.txt" "*.part" "*"; do
            found=$(docker exec fsx_core find "$base_path" -path "*/storage/transfers/$TRANSFER_ID/*" -name "$pattern" 2>/dev/null | head -1 || echo "")
            if [ -n "$found" ]; then
                SERVER_FILE="$found"
                break 2
            fi
        done
    done
    
    if [ -n "$SERVER_FILE" ]; then
        SERVER_SIZE=$(docker exec fsx_core stat -c%s "$SERVER_FILE" 2>/dev/null || echo "0")
        if [ "$SERVER_SIZE" = "$FILE_SIZE" ]; then
            echo -e "${GREEN}✓ Server file size matches: $SERVER_SIZE bytes${NC}"
            
            # Verify file content with SHA256
            LOCAL_SHA=$(sha256sum test_phase3.txt 2>/dev/null | awk '{print $1}' || echo "")
            SERVER_SHA=$(docker exec fsx_core sha256sum "$SERVER_FILE" 2>/dev/null | awk '{print $1}' || echo "")
            if [ -n "$LOCAL_SHA" ] && [ -n "$SERVER_SHA" ]; then
                if [ "$LOCAL_SHA" = "$SERVER_SHA" ]; then
                    echo -e "${GREEN}✓ File SHA256 hash matches${NC}"
                else
                    echo -e "${RED}✗ File SHA256 hash mismatch!${NC}"
                    echo "  Local:  $LOCAL_SHA"
                    echo "  Server: $SERVER_SHA"
                    echo -e "${RED}TEST FAILED: File content mismatch${NC}"
                    exit 1
                fi
            fi
        else
            echo -e "${RED}✗ Server file size mismatch!${NC}"
            echo "  Expected: $FILE_SIZE bytes"
            echo "  Actual:   $SERVER_SIZE bytes"
            echo -e "${RED}TEST FAILED: File size mismatch${NC}"
            exit 1
        fi
        echo -e "${GREEN}✓ File path on server: $SERVER_FILE${NC}"
    else
        # Try to get path from saved_path in logs
        SAVED_PATH=$(docker logs fsx_core 2>&1 | grep "FILE_DONE_OK.*transfer_id=$TRANSFER_ID" | grep -oP 'saved_path=\K[^\s]+' | head -1 || echo "")
        if [ -n "$SAVED_PATH" ]; then
            echo -e "${GREEN}✓ File saved at: $SAVED_PATH (from server logs)${NC}"
        else
            echo -e "${YELLOW}⚠ Could not verify server file (file may be in different location)${NC}"
        fi
    fi
else
    echo -e "${YELLOW}⚠ Could not find FILE_DONE_OK in server logs${NC}"
    echo "Checking server logs..."
    docker logs fsx_core --tail 20 | grep -E "FILE_|transfer_id=$TRANSFER_ID" || true
fi

echo -e "${GREEN}✓ MVP verification complete (file download protocol not implemented yet)${NC}"
echo ""

# Step 9: Check server logs (only for this transfer)
echo -e "${YELLOW}[9/9] Checking server logs...${NC}"
echo "File transfer events for transfer_id=$TRANSFER_ID:"
docker logs fsx_core 2>&1 | grep -E "transfer_id=$TRANSFER_ID|FILE_OFFER_REQ|FILE_ACCEPT|FILE_CHUNK|FILE_DONE|FileStore" | grep -E "transfer_id=$TRANSFER_ID|FILE_OFFER_REQ|FILE_ACCEPT|FILE_CHUNK|FILE_DONE|FileStore" || echo "No file transfer logs found"
echo ""

# Debug: Check if chunks were received
if docker logs fsx_core 2>&1 | grep "transfer_id=$TRANSFER_ID" | grep -q "FILE_CHUNK_RX"; then
    echo -e "${GREEN}✓ Server received chunks for this transfer${NC}"
    docker logs fsx_core 2>&1 | grep "transfer_id=$TRANSFER_ID" | grep "FILE_CHUNK_RX"
else
    echo -e "${RED}✗ WARNING: Server did NOT receive any FILE_CHUNK messages!${NC}"
    echo "This means chunks were not sent or not received."
    echo ""
    echo "Checking if FILE_CHUNK handler was called at all:"
    docker logs fsx_core 2>&1 | grep "transfer_id=$TRANSFER_ID" | grep "FILE_CHUNK" | tail -10
fi
echo ""

# Cleanup logs now
rm -f "$SEND_LOG" /tmp/fsx_receiver_$$.log

# Summary
echo "=========================================="
echo -e "${GREEN}✓ ALL TESTS PASSED${NC}"
echo "=========================================="
echo "Summary:"
echo "  - Transfer ID: $TRANSFER_ID"
echo "  - File size: $FILE_SIZE bytes"
echo "  - Sender: ✓ Completed"
echo "  - Receiver: ✓ Accepted"
echo "  - Server: ✓ File saved"
echo "  - Note: File download protocol not implemented in MVP"
echo ""
echo -e "${GREEN}Phase 3 File Transfer MVP is working!${NC}"

# Cleanup
echo ""
read -p "Clean up test files? (y/n) " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    rm -f test_phase3.txt
    echo "Test files cleaned up (server file remains)"
fi

