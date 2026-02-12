# Phase 3 File Transfer - Test Execution Guide

## Current Status

✅ **Core Server:** Running and ready  
✅ **Database:** Connected  
✅ **Storage:** Initialized  
✅ **File Transfer Protocol:** Implemented  
✅ **Client Commands:** Ready (need build in WSL)

## Quick Test (Recommended)

### Prerequisites Check

```bash
# 1. Check services
docker ps | grep -E "fsx_core|fsx_db"
# Should show both running

# 2. Check core logs
docker logs fsx_core --tail 5
# Should show: "[core] server started on port 9000, running..."
```

### Build Client (in WSL)

```bash
cd /mnt/e/University/University_Subjects/5th/Computer_Networks/Projects/finl_project/filesharex

# Clean old build
cd client
rm -rf build

# Build
cmake -S . -B build
cmake --build build

# Verify executables
ls -lh build/test_auth build/test_file_transfer
cd ..
```

### Execute Test

**Option 1: Manual Step-by-Step (Recommended for first test)**

See: `docs/PHASE3_COMPREHENSIVE_TEST.md`

**Option 2: Quick Script (in WSL)**

```bash
chmod +x scripts/quick_test_phase3.sh
./scripts/quick_test_phase3.sh 127.0.0.1 9000
```

## Expected Test Flow

### 1. Register Users
```bash
./client/build/test_auth register testuser1 pass123 testuser1@example.com 127.0.0.1 9000
./client/build/test_auth register testuser2 pass123 testuser2@example.com 127.0.0.1 9000
```

**Expected:** `ok=true` for both

### 2. Login Users
```bash
./client/build/test_auth login testuser1 pass123 127.0.0.1 9000
./client/build/test_auth login testuser2 pass123 127.0.0.1 9000
```

**Expected:** `ok=true token=...` for both

### 3. Create Test File
```bash
echo "Hello FileShareX Phase 3!" > test.txt
```

### 4. Send File
```bash
./client/build/test_file_transfer send testuser1 pass123 testuser2 ./test.txt 127.0.0.1 9000
```

**Expected Output:**
- Transfer ID displayed
- Chunks sent
- SUCCESS message

**⚠️ Copy Transfer ID!**

### 5. Receive File
```bash
./client/build/test_file_transfer recv testuser2 pass123 <TRANSFER_ID> ./received.txt 127.0.0.1 9000
```

**Expected Output:**
- File accepted
- Chunks received
- SUCCESS message

### 6. Verify
```bash
diff test.txt received.txt
# Should show no differences
```

## Server Log Verification

```bash
docker logs fsx_core --tail 50 | grep -E "FILE_|TRANSFER"
```

**Expected Events:**
1. `FILE_OFFER_OK transfer_id=X sender=... receiver=...`
2. `FILE_ACCEPT_OK transfer_id=X receiver=...`
3. `FILE_CHUNK_RX transfer_id=X chunk_index=0 bytes=...`
4. `FILE_DONE_OK transfer_id=X filename=... saved_path=...`

## Troubleshooting

### Client Build Issues

**Problem:** CMake cache mismatch (WSL vs Windows paths)
```bash
cd client
rm -rf build
cmake -S . -B build
cmake --build build
```

### Connection Issues

**Problem:** Can't connect to server
```bash
# Check if core is running
docker ps | grep fsx_core

# Check if port is accessible
docker logs fsx_core --tail 10

# Try from WSL
telnet 127.0.0.1 9000
```

### Transfer Issues

**Problem:** "Receiver not found"
- Verify receiver is registered: Check database or try registering again

**Problem:** "Transfer not found"
- Use correct transfer_id from sender output
- Check sender completed FILE_OFFER

**Problem:** File size mismatch
- Check server logs for errors
- Verify all chunks were sent/received

## Success Indicators

✅ **All these should work:**
- [ ] Users register successfully
- [ ] Users login successfully  
- [ ] FILE_OFFER creates transfer
- [ ] FILE_ACCEPT opens file for writing
- [ ] FILE_CHUNK writes data
- [ ] FILE_DONE finalizes file
- [ ] FILE_RESULT confirms success
- [ ] Received file matches original
- [ ] Server logs show all events

## Next Steps After Successful Test

1. **Document results** in project report
2. **Capture Wireshark** screenshots (4 required)
3. **Prepare demo** scenario
4. **Move to Phase 4** (CRC32 + Retransmission)

