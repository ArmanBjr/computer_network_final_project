# Phase 3 File Transfer - Comprehensive Test Guide

## Prerequisites

1. **Core server running:**
   ```bash
   docker compose -f docker/compose.yml up -d db core
   ```

2. **Verify services:**
   ```bash
   docker ps | grep -E "fsx_core|fsx_db"
   docker logs fsx_core --tail 5
   ```

3. **Build client (in WSL):**
   ```bash
   cd client
   rm -rf build  # Clean old build if exists
   cmake -S . -B build
   cmake --build build
   cd ..
   ```

## Test Scenario: Complete File Transfer

### Step 1: Register Users

**Terminal 1:**
```bash
./client/build/test_auth register testuser1 pass123 testuser1@example.com 127.0.0.1 9000
```

**Expected:**
```
Connected to 127.0.0.1:9000
Sending REGISTER_REQ: username=testuser1 email=testuser1@example.com
REGISTER_RESP: ok=true msg=User registered successfully
```

**Terminal 2:**
```bash
./client/build/test_auth register testuser2 pass123 testuser2@example.com 127.0.0.1 9000
```

**Expected:** Same as above with `testuser2`

### Step 2: Login Both Users

**Terminal 1:**
```bash
./client/build/test_auth login testuser1 pass123 127.0.0.1 9000
```

**Terminal 2:**
```bash
./client/build/test_auth login testuser2 pass123 127.0.0.1 9000
```

**Expected:**
```
Connected to 127.0.0.1:9000
Sending LOGIN_REQ: username=testuser1
LOGIN_RESP: ok=true token=... msg=Login successful
```

### Step 3: Create Test File

**Terminal 1:**
```bash
cat > test_file.txt << 'EOF'
This is a test file for FileShareX Phase 3.
It contains multiple lines to test file transfer functionality.

Line 3: Testing chunk-based transfer
Line 4: Testing server-mediated architecture
Line 5: Testing file integrity

End of test file.
EOF

wc -c test_file.txt  # Check file size
```

### Step 4: Send File (Sender)

**Terminal 1:**
```bash
./client/build/test_file_transfer send testuser1 pass123 testuser2 ./test_file.txt 127.0.0.1 9000
```

**Expected Output:**
```
Connected to 127.0.0.1:9000
[LOGIN] Sending LOGIN_REQ for testuser1
[LOGIN] Success
[SEND] File: test_file.txt (XXX bytes)
[SEND] Receiver: testuser2
[SEND] Sending FILE_OFFER_REQ...
[SEND] Transfer ID: 1
[SEND] >>> Receiver should run: recv testuser2 pass123 1 <output_path>
[SEND] Waiting for receiver to accept...
[SEND] Accepted! Sending chunks...
[SEND] Chunk 0: XXX bytes (total: XXX/XXX)
[SEND] Sending FILE_DONE (total_chunks=1)
[SEND] SUCCESS! File saved at: ./storage/transfers/1/test_file.txt
```

**⚠️ IMPORTANT: Copy the Transfer ID from output!**

### Step 5: Receive File (Receiver)

**Terminal 2 (use Transfer ID from Step 4):**
```bash
./client/build/test_file_transfer recv testuser2 pass123 1 ./received_file.txt 127.0.0.1 9000
```

**Expected Output:**
```
Connected to 127.0.0.1:9000
[LOGIN] Sending LOGIN_REQ for testuser2
[LOGIN] Success
[RECV] Accepting transfer_id=1
[RECV] Sending FILE_ACCEPT_REQ (accept=true)...
[RECV] Accepted! Waiting for chunks...
[RECV] Chunk 0: XXX bytes (total: XXX)
[RECV] FILE_DONE received: total_chunks=1 file_size=XXX bytes_received=XXX
[RECV] SUCCESS! File saved to: ./received_file.txt
```

### Step 6: Verify File Integrity

**Terminal 2:**
```bash
# Compare files
diff test_file.txt received_file.txt

# Or check sizes
wc -c test_file.txt received_file.txt

# Or view content
cat received_file.txt
```

**Expected:** No differences, same size, same content

### Step 7: Check Server Logs

```bash
docker logs fsx_core --tail 30 | grep -E "FILE_|TRANSFER"
```

**Expected Logs:**
```
FILE_OFFER_OK transfer_id=1 sender=testuser1 receiver=testuser2
FILE_ACCEPT_OK transfer_id=1 receiver=testuser2
FILE_CHUNK_RX transfer_id=1 chunk_index=0 bytes=XXX total_received=XXX
FILE_DONE_OK transfer_id=1 filename=test_file.txt saved_path=./storage/transfers/1/test_file.txt
```

### Step 8: Verify Server Storage

```bash
docker exec fsx_core ls -lh /src/storage/transfers/1/
```

**Expected:**
```
-rw-r--r-- 1 root root XXX Jan  4 22:XX test_file.txt
```

## Test Checklist

- [ ] Both users can register
- [ ] Both users can login
- [ ] Sender can offer file transfer
- [ ] Receiver can accept transfer
- [ ] File chunks are transmitted
- [ ] File is saved on server
- [ ] Receiver receives complete file
- [ ] File integrity verified (size matches)
- [ ] File integrity verified (content matches)
- [ ] Server logs show all events

## Troubleshooting

### Problem: "Receiver not found"
```bash
# Check if receiver is registered
docker exec fsx_db psql -U fsx -d fsx -c "SELECT username, email FROM users WHERE username='testuser2';"
```

### Problem: "Transfer not found"
- Make sure you're using the correct transfer_id from sender output
- Check if sender completed FILE_OFFER:
  ```bash
  docker logs fsx_core | grep "FILE_OFFER"
  ```

### Problem: Connection refused
```bash
# Check if core is running
docker ps | grep fsx_core

# Check if port is accessible
docker logs fsx_core --tail 5
```

### Problem: File size mismatch
- Check server logs for errors
- Verify all chunks were received:
  ```bash
  docker logs fsx_core | grep "FILE_CHUNK_RX"
  ```

## Success Criteria

✅ **All tests pass** when:
1. Both users register and login successfully
2. File transfer completes without errors
3. Received file matches original (size and content)
4. Server logs show all expected events
5. File is stored correctly on server

## Next Phase

After Phase 3 is verified:
- Phase 4: CRC32 + NAK/ACK + Retransmission
- Phase 5: Resume functionality
- Phase 6: Compression
- Phase 7: SHA-256 integrity
- Phase 8: Encryption

