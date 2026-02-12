# Phase 3: File Transfer MVP - Testing Guide

## Overview

Phase 3 implements a **server-mediated** file transfer system where:
- Sender uploads file to server
- Server stores file temporarily
- Receiver downloads file from server

## Prerequisites

1. **Core server running:**
   ```bash
   docker compose -f docker/compose.yml up -d core db
   ```

2. **Two users registered:**
   - User 1 (sender): `testuser1` / `pass123`
   - User 2 (receiver): `testuser2` / `pass123`

3. **Test file prepared:**
   - Create a small test file (e.g., `test.txt` with some content)

## Build Client

```bash
cd client
cmake -S . -B build
cmake --build build
cd ..
```

## Test Scenario

### Step 1: Register Users (if not already registered)

```bash
# Terminal 1: Register sender
./client/build/test_auth register testuser1 pass123 testuser1@example.com 127.0.0.1 9000

# Terminal 2: Register receiver
./client/build/test_auth register testuser2 pass123 testuser2@example.com 127.0.0.1 9000
```

### Step 2: Create Test File

```bash
echo "Hello, this is a test file for FileShareX Phase 3!" > test_file.txt
```

### Step 3: Send File

```bash
# Terminal 1: Sender
./client/build/test_file_transfer send testuser1 pass123 testuser2 ./test_file.txt 127.0.0.1 9000
```

**Output will show:**
```
[SEND] Transfer ID: <transfer_id>
[SEND] >>> Receiver should run: recv testuser2 pass123 <transfer_id> <output_path>
[SEND] Waiting for receiver to accept...
```

**Copy the transfer_id from the output!**

### Step 4: Receive File

```bash
# Terminal 2: Receiver (use transfer_id from Step 3)
./client/build/test_file_transfer recv testuser2 pass123 <transfer_id> ./received_file.txt 127.0.0.1 9000
```

### Step 5: Verify

```bash
# Compare files
diff test_file.txt received_file.txt
# Should show no differences
```

## Expected Behavior

### Sender Flow:
1. ✅ Login successful
2. ✅ FILE_OFFER_REQ sent
3. ✅ FILE_OFFER_RESP received with transfer_id
4. ✅ Waiting for receiver accept
5. ✅ FILE_ACCEPT_RESP received
6. ✅ Sending chunks (with progress)
7. ✅ FILE_DONE sent
8. ✅ FILE_RESULT received (success)

### Receiver Flow:
1. ✅ Login successful
2. ✅ FILE_ACCEPT_REQ sent
3. ✅ FILE_ACCEPT_RESP received
4. ✅ Receiving chunks (with progress)
5. ✅ FILE_DONE received
6. ✅ File saved to output path

### Server Logs:
```
FILE_OFFER_OK transfer_id=1 sender=testuser1 receiver=testuser2
FILE_ACCEPT_OK transfer_id=1 receiver=testuser2
FILE_CHUNK_RX transfer_id=1 chunk_index=0 bytes=...
FILE_DONE_OK transfer_id=1 filename=test_file.txt saved_path=...
```

## Troubleshooting

### Problem: "Receiver not found"
- **Solution:** Make sure receiver user is registered

### Problem: "Transfer not found" (in recv)
- **Solution:** Use the correct transfer_id from sender output

### Problem: "Not the receiver"
- **Solution:** Make sure receiver username matches the one used in FILE_OFFER_REQ

### Problem: File size mismatch
- **Solution:** Check server logs for errors. File might be corrupted.

### Problem: Connection timeout
- **Solution:** Make sure core server is running and accessible

## Wireshark Capture

For Phase 3 demo, capture:
1. **FILE_OFFER_REQ/RESP** - Initial offer
2. **FILE_ACCEPT_REQ/RESP** - Acceptance
3. **FILE_CHUNK** messages - Data transfer
4. **FILE_DONE** - Completion
5. **FILE_RESULT** - Final confirmation

**Filter:** `tcp.port == 9000`

## Next Steps (Phase 4+)

- CRC32 per chunk validation
- ACK/NAK retransmission
- Resume functionality
- Compression
- Encryption

