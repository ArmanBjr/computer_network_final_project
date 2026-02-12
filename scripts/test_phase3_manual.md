# Phase 3 File Transfer - Manual Test Guide

## Prerequisites

1. **Core server running:**
   ```bash
   docker compose -f docker/compose.yml up -d db core
   ```

2. **Build client:**
   ```bash
   cd client
   cmake -S . -B build
   cmake --build build
   cd ..
   ```

## Test Steps

### Step 1: Register Users

**Terminal 1:**
```bash
./client/build/test_auth register testuser1 pass123 testuser1@example.com 127.0.0.1 9000
```

**Terminal 2:**
```bash
./client/build/test_auth register testuser2 pass123 testuser2@example.com 127.0.0.1 9000
```

**Expected:** Both should show `ok=true`

### Step 2: Login Both Users

**Terminal 1:**
```bash
./client/build/test_auth login testuser1 pass123 127.0.0.1 9000
```

**Terminal 2:**
```bash
./client/build/test_auth login testuser2 pass123 127.0.0.1 9000
```

**Expected:** Both should show `ok=true` with token

### Step 3: Create Test File

**Terminal 1:**
```bash
echo "Hello, this is a test file for FileShareX Phase 3!" > test_file.txt
cat test_file.txt
```

### Step 4: Send File

**Terminal 1 (Sender):**
```bash
./client/build/test_file_transfer send testuser1 pass123 testuser2 ./test_file.txt 127.0.0.1 9000
```

**Expected Output:**
```
Connected to 127.0.0.1:9000
[LOGIN] Sending LOGIN_REQ for testuser1
[LOGIN] Success
[SEND] File: test_file.txt (XX bytes)
[SEND] Receiver: testuser2
[SEND] Sending FILE_OFFER_REQ...
[SEND] Transfer ID: 1
[SEND] >>> Receiver should run: recv testuser2 pass123 1 <output_path>
[SEND] Waiting for receiver to accept...
[SEND] Accepted! Sending chunks...
[SEND] Chunk 0: XX bytes (total: XX/XX)
[SEND] Sending FILE_DONE (total_chunks=1)
[SEND] SUCCESS! File saved at: ./storage/transfers/1/test_file.txt
```

**Copy the Transfer ID from output!**

### Step 5: Receive File

**Terminal 2 (Receiver):**
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
[RECV] Chunk 0: XX bytes (total: XX)
[RECV] FILE_DONE received: total_chunks=1 file_size=XX bytes_received=XX
[RECV] SUCCESS! File saved to: ./received_file.txt
```

### Step 6: Verify File

**Terminal 2:**
```bash
diff test_file.txt received_file.txt
# Should show no differences

# Or compare:
cat received_file.txt
```

**Expected:** Files should be identical

### Step 7: Check Server Logs

```bash
docker logs fsx_core --tail 30 | grep -E "FILE_|TRANSFER"
```

**Expected logs:**
```
FILE_OFFER_OK transfer_id=1 sender=testuser1 receiver=testuser2
FILE_ACCEPT_OK transfer_id=1 receiver=testuser2
FILE_CHUNK_RX transfer_id=1 chunk_index=0 bytes=XX total_received=XX
FILE_DONE_OK transfer_id=1 filename=test_file.txt saved_path=...
```

## Troubleshooting

### Problem: "Receiver not found"
- Make sure receiver is registered
- Check username spelling

### Problem: "Transfer not found"
- Use the correct transfer_id from sender output
- Make sure sender has completed FILE_OFFER

### Problem: "Not the receiver"
- Receiver username must match the one in FILE_OFFER_REQ

### Problem: Connection timeout
- Check core server is running: `docker ps | grep fsx_core`
- Check port 9000 is accessible

### Problem: File size mismatch
- Check server logs for errors
- Verify chunk transmission completed

## Success Criteria

✅ Both users can register and login  
✅ Sender can offer file transfer  
✅ Receiver can accept transfer  
✅ File chunks are transmitted  
✅ File is saved correctly on server  
✅ Receiver receives complete file  
✅ File integrity verified (size and content match)

