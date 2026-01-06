# Phase 3 File Transfer - Comprehensive Test Script (PowerShell)
# Tests: Register → Login → Send File → Receive File → Verify

param(
    [string]$Host = "127.0.0.1",
    [int]$Port = 9000
)

$ErrorActionPreference = "Stop"

Write-Host "==========================================" -ForegroundColor Cyan
Write-Host "Phase 3 File Transfer - Comprehensive Test" -ForegroundColor Cyan
Write-Host "==========================================" -ForegroundColor Cyan
Write-Host "Host: $Host"
Write-Host "Port: $Port"
Write-Host ""

# Test users
$SENDER_USER = "testuser1"
$SENDER_PASS = "pass123"
$SENDER_EMAIL = "testuser1@example.com"

$RECEIVER_USER = "testuser2"
$RECEIVER_PASS = "pass123"
$RECEIVER_EMAIL = "testuser2@example.com"

# Test file
$TEST_FILE = "test_phase3_file.txt"
$RECEIVED_FILE = "received_phase3_file.txt"
$CLIENT_DIR = "client/build"

# Step 1: Create test file
Write-Host "[STEP 1] Creating test file..." -ForegroundColor Yellow
@"
This is a test file for FileShareX Phase 3.
It contains multiple lines to test file transfer functionality.

Line 3: Testing chunk-based transfer
Line 4: Testing server-mediated architecture
Line 5: Testing file integrity

End of test file.
"@ | Out-File -FilePath $TEST_FILE -Encoding utf8

$FILE_SIZE = (Get-Item $TEST_FILE).Length
Write-Host "✓ Test file created: $TEST_FILE ($FILE_SIZE bytes)" -ForegroundColor Green
Write-Host ""

# Step 2: Register sender
Write-Host "[STEP 2] Registering sender ($SENDER_USER)..." -ForegroundColor Yellow
$regOutput = & "$CLIENT_DIR/test_auth" register $SENDER_USER $SENDER_PASS $SENDER_EMAIL $Host $Port 2>&1
if ($regOutput -match "ok=true") {
    Write-Host "✓ Sender registered" -ForegroundColor Green
} else {
    Write-Host "⚠ Sender might already exist (continuing...)" -ForegroundColor Yellow
}
Write-Host ""

# Step 3: Register receiver
Write-Host "[STEP 3] Registering receiver ($RECEIVER_USER)..." -ForegroundColor Yellow
$regOutput = & "$CLIENT_DIR/test_auth" register $RECEIVER_USER $RECEIVER_PASS $RECEIVER_EMAIL $Host $Port 2>&1
if ($regOutput -match "ok=true") {
    Write-Host "✓ Receiver registered" -ForegroundColor Green
} else {
    Write-Host "⚠ Receiver might already exist (continuing...)" -ForegroundColor Yellow
}
Write-Host ""

# Step 4: Login sender
Write-Host "[STEP 4] Logging in sender..." -ForegroundColor Yellow
$loginOutput = & "$CLIENT_DIR/test_auth" login $SENDER_USER $SENDER_PASS $Host $Port 2>&1
if ($loginOutput -match "ok=true") {
    Write-Host "✓ Sender logged in" -ForegroundColor Green
} else {
    Write-Host "✗ Sender login failed" -ForegroundColor Red
    Write-Host $loginOutput
    exit 1
}
Write-Host ""

# Step 5: Login receiver
Write-Host "[STEP 5] Logging in receiver..." -ForegroundColor Yellow
$loginOutput = & "$CLIENT_DIR/test_auth" login $RECEIVER_USER $RECEIVER_PASS $Host $Port 2>&1
if ($loginOutput -match "ok=true") {
    Write-Host "✓ Receiver logged in" -ForegroundColor Green
} else {
    Write-Host "✗ Receiver login failed" -ForegroundColor Red
    Write-Host $loginOutput
    exit 1
}
Write-Host ""

# Step 6: Send file (capture transfer_id)
Write-Host "[STEP 6] Sending file from $SENDER_USER to $RECEIVER_USER..." -ForegroundColor Yellow
$sendOutput = & "$CLIENT_DIR/test_file_transfer" send $SENDER_USER $SENDER_PASS $RECEIVER_USER $TEST_FILE $Host $Port 2>&1

# Extract transfer_id
$transferIdMatch = [regex]::Match($sendOutput, 'Transfer ID: (\d+)')
if (-not $transferIdMatch.Success) {
    Write-Host "✗ Failed to get transfer_id from send output" -ForegroundColor Red
    Write-Host "Send output:"
    Write-Host $sendOutput
    exit 1
}

$TRANSFER_ID = $transferIdMatch.Groups[1].Value
Write-Host "✓ Transfer ID: $TRANSFER_ID" -ForegroundColor Green
Write-Host ""

# Step 7: Receive file
Write-Host "[STEP 7] Receiving file as $RECEIVER_USER..." -ForegroundColor Yellow
Start-Sleep -Seconds 2  # Give sender time to send FILE_OFFER

$recvOutput = & "$CLIENT_DIR/test_file_transfer" recv $RECEIVER_USER $RECEIVER_PASS $TRANSFER_ID $RECEIVED_FILE $Host $Port 2>&1

if ($recvOutput -match "SUCCESS") {
    Write-Host "✓ File received successfully" -ForegroundColor Green
} else {
    Write-Host "✗ File receive failed or incomplete" -ForegroundColor Red
    Write-Host "Receive output:"
    Write-Host $recvOutput
    exit 1
}
Write-Host ""

# Step 8: Verify file integrity
Write-Host "[STEP 8] Verifying file integrity..." -ForegroundColor Yellow
if (-not (Test-Path $RECEIVED_FILE)) {
    Write-Host "✗ Received file not found" -ForegroundColor Red
    exit 1
}

$RECEIVED_SIZE = (Get-Item $RECEIVED_FILE).Length
if ($FILE_SIZE -ne $RECEIVED_SIZE) {
    Write-Host "✗ Size mismatch: original=$FILE_SIZE, received=$RECEIVED_SIZE" -ForegroundColor Red
    exit 1
}

# Compare files
$originalContent = Get-Content $TEST_FILE -Raw
$receivedContent = Get-Content $RECEIVED_FILE -Raw
if ($originalContent -ne $receivedContent) {
    Write-Host "✗ File content mismatch" -ForegroundColor Red
    exit 1
}

Write-Host "✓ File integrity verified: $FILE_SIZE bytes, content matches" -ForegroundColor Green
Write-Host ""

# Step 9: Check server logs
Write-Host "[STEP 9] Checking server logs..." -ForegroundColor Yellow
if (Get-Command docker -ErrorAction SilentlyContinue) {
    Write-Host "Recent server logs:"
    docker logs fsx_core --tail 20 2>&1 | Select-String -Pattern "FILE_|TRANSFER"
    Write-Host ""
}

# Summary
Write-Host "==========================================" -ForegroundColor Cyan
Write-Host "✓ ALL TESTS PASSED" -ForegroundColor Green
Write-Host "==========================================" -ForegroundColor Cyan
Write-Host "Test Summary:"
Write-Host "  - Sender: $SENDER_USER"
Write-Host "  - Receiver: $RECEIVER_USER"
Write-Host "  - Transfer ID: $TRANSFER_ID"
Write-Host "  - File size: $FILE_SIZE bytes"
Write-Host "  - File integrity: ✓ Verified"
Write-Host ""
Write-Host "Phase 3 File Transfer MVP is working correctly!" -ForegroundColor Green

# Cleanup
Write-Host ""
Write-Host "Cleaning up test files..." -ForegroundColor Yellow
Remove-Item -Path $TEST_FILE -ErrorAction SilentlyContinue
Remove-Item -Path $RECEIVED_FILE -ErrorAction SilentlyContinue
Write-Host "Done." -ForegroundColor Green

