#!/bin/bash
# Manual Phase 5 Test (step by step)
# Usage: Run commands one by one

HOST="127.0.0.1"
PORT="9000"

echo "=== Step 1: Build client ==="
cd client
rm -rf build
cmake -S . -B build
cmake --build build
cd ..

echo ""
echo "=== Step 2: Create test file ==="
cat > test_phase5.txt << 'EOF'
This is a Phase 5 test file for FileShareX.
It is used to demonstrate transfer resume (RESUME_QUERY / RESUME_REPLY).

The file is intentionally a bit longer so that we can interrupt
the upload in the middle and let the sender resume from the last
ACKed chunk without starting from zero.

Line 10: Chunked upload with CRC32.
Line 11: Server saves resume state (transfer_resume table).
Line 12: Client reconnects and asks RESUME_QUERY.
Line 13: Client continues from last_acked_chunk_index / bytes_received.

End of Phase 5 test file.
EOF

echo "Test file created: test_phase5.txt ($(wc -c < test_phase5.txt) bytes)"

echo ""
echo "=== Step 3: Register users ==="
./client/build/test_auth register testuser1 pass123 testuser1@example.com $HOST $PORT
./client/build/test_auth register testuser2 pass123 testuser2@example.com $HOST $PORT

echo ""
echo "=== Step 4: First upload (will be interrupted) ==="
echo "Run this command and let it run for a few seconds, then press Ctrl+C:"
echo "./client/build/test_file_transfer send testuser1 pass123 testuser2 ./test_phase5.txt $HOST $PORT"
echo ""
echo "After interruption, note the TRANSFER_ID from the output (look for 'Transfer ID: XXXX')"
echo ""
read -p "Press Enter after you've noted the TRANSFER_ID..."

echo ""
echo "=== Step 5: Resume upload ==="
echo "Enter the TRANSFER_ID you noted:"
read TRANSFER_ID

echo "Running resume-send..."
./client/build/test_file_transfer resume-send testuser1 pass123 $TRANSFER_ID ./test_phase5.txt $HOST $PORT

echo ""
echo "=== Done! Check output above for [RESUME] messages ==="
