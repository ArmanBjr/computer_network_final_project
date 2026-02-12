# راهنمای تست Phase 3 در WSL

## مرحله 1: بررسی سرویس‌ها

```bash
# بررسی اینکه Docker containers در حال اجرا هستند
docker ps | grep -E "fsx_core|fsx_db"

# باید این خروجی را ببینی:
# fsx_core      Up X minutes
# fsx_db        Up X minutes
```

اگر اجرا نیستند:
```bash
cd /mnt/e/University/University_Subjects/5th/Computer_Networks/Projects/finl_project/filesharex
docker compose -f docker/compose.yml up -d db core
```

بررسی لاگ‌های Core:
```bash
docker logs fsx_core --tail 5
```

**باید ببینی:**
```
[DB] connected
[storage] initialized
[core] listening on 0.0.0.0:9000
[core] server started on port 9000, running...
```

---

## مرحله 2: Build کردن Client

```bash
# برو به دایرکتوری پروژه
cd /mnt/e/University/University_Subjects/5th/Computer_Networks/Projects/finl_project/filesharex

# برو به دایرکتوری client
cd client

# پاک کردن build قبلی (اگر وجود دارد)
rm -rf build

# Build کردن
cmake -S . -B build
cmake --build build

# بررسی اینکه فایل‌های اجرایی ساخته شدند
ls -lh build/test_auth build/test_file_transfer
```

**باید ببینی:**
```
-rwxr-xr-x 1 user user XXX build/test_auth
-rwxr-xr-x 1 user user XXX build/test_file_transfer
```

---

## مرحله 3: تست Register و Login

### Terminal 1: Register User 1 (Sender)

```bash
cd /mnt/e/University/University_Subjects/5th/Computer_Networks/Projects/finl_project/filesharex

./client/build/test_auth register testuser1 pass123 testuser1@example.com 127.0.0.1 9000
```

**خروجی مورد انتظار:**
```
Connected to 127.0.0.1:9000
Sending REGISTER_REQ: username=testuser1 email=testuser1@example.com
REGISTER_RESP: ok=true msg=User registered successfully
```

### Terminal 2: Register User 2 (Receiver)

```bash
cd /mnt/e/University/University_Subjects/5th/Computer_Networks/Projects/finl_project/filesharex

./client/build/test_auth register testuser2 pass123 testuser2@example.com 127.0.0.1 9000
```

**خروجی مورد انتظار:** همانند بالا با `testuser2`

### Login هر دو کاربر

**Terminal 1:**
```bash
./client/build/test_auth login testuser1 pass123 127.0.0.1 9000
```

**Terminal 2:**
```bash
./client/build/test_auth login testuser2 pass123 127.0.0.1 9000
```

**خروجی مورد انتظار:**
```
Connected to 127.0.0.1:9000
Sending LOGIN_REQ: username=testuser1
LOGIN_RESP: ok=true token=... msg=Login successful
```

---

## مرحله 4: ایجاد فایل تست

**Terminal 1:**
```bash
cd /mnt/e/University/University_Subjects/5th/Computer_Networks/Projects/finl_project/filesharex

cat > test_file.txt << 'EOF'
This is a test file for FileShareX Phase 3.
It contains multiple lines to test file transfer functionality.

Line 3: Testing chunk-based transfer
Line 4: Testing server-mediated architecture
Line 5: Testing file integrity

End of test file.
EOF

# بررسی اندازه فایل
wc -c test_file.txt
cat test_file.txt
```

---

## مرحله 5: ارسال فایل (Sender)

**Terminal 1:**
```bash
./client/build/test_file_transfer send testuser1 pass123 testuser2 ./test_file.txt 127.0.0.1 9000
```

**خروجی مورد انتظار:**
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

**⚠️ مهم: Transfer ID را کپی کن! (مثلاً `1`)**

---

## مرحله 6: دریافت فایل (Receiver)

**Terminal 2 (از Transfer ID استفاده کن):**
```bash
./client/build/test_file_transfer recv testuser2 pass123 1 ./received_file.txt 127.0.0.1 9000
```

**خروجی مورد انتظار:**
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

---

## مرحله 7: بررسی صحت فایل

**Terminal 2:**
```bash
# مقایسه فایل‌ها
diff test_file.txt received_file.txt

# یا بررسی اندازه
wc -c test_file.txt received_file.txt

# یا نمایش محتوا
cat received_file.txt
```

**باید ببینی:**
- `diff` هیچ تفاوتی نشان ندهد
- اندازه‌ها یکسان باشند
- محتوا یکسان باشد

---

## مرحله 8: بررسی لاگ‌های سرور

```bash
docker logs fsx_core --tail 30 | grep -E "FILE_|TRANSFER"
```

**باید ببینی:**
```
FILE_OFFER_OK transfer_id=1 sender=testuser1 receiver=testuser2
FILE_ACCEPT_OK transfer_id=1 receiver=testuser2
FILE_CHUNK_RX transfer_id=1 chunk_index=0 bytes=XXX total_received=XXX
FILE_DONE_OK transfer_id=1 filename=test_file.txt saved_path=./storage/transfers/1/test_file.txt
```

---

## مرحله 9: بررسی فایل روی سرور

```bash
docker exec fsx_core ls -lh /src/storage/transfers/1/
```

**باید ببینی:**
```
-rw-r--r-- 1 root root XXX Jan  4 22:XX test_file.txt
```

---

## ✅ معیارهای موفقیت

- [ ] هر دو کاربر register شدند
- [ ] هر دو کاربر login شدند
- [ ] فایل ارسال شد (Transfer ID دریافت شد)
- [ ] فایل دریافت شد (SUCCESS message)
- [ ] فایل‌ها یکسان هستند (diff هیچ تفاوتی نشان نمی‌دهد)
- [ ] لاگ‌های سرور همه eventها را نشان می‌دهند
- [ ] فایل روی سرور ذخیره شده است

---

## 🔧 Troubleshooting

### مشکل: "Connection refused"
```bash
# بررسی اینکه core در حال اجرا است
docker ps | grep fsx_core

# بررسی لاگ‌ها
docker logs fsx_core --tail 10

# بررسی پورت
netstat -tuln | grep 9000
```

### مشکل: "Receiver not found"
```bash
# بررسی database
docker exec fsx_db psql -U fsx -d fsx -c "SELECT username FROM users;"
```

### مشکل: "Transfer not found"
- مطمئن شو که Transfer ID درست است
- بررسی کن که sender FILE_OFFER را کامل کرده:
  ```bash
  docker logs fsx_core | grep "FILE_OFFER"
  ```

### مشکل: Build failed
```bash
# پاک کردن build و دوباره build کردن
cd client
rm -rf build
cmake -S . -B build
cmake --build build
```

---

## 📝 خلاصه دستورات (Copy-Paste Ready)

```bash
# 1. Build client
cd /mnt/e/University/University_Subjects/5th/Computer_Networks/Projects/finl_project/filesharex/client
rm -rf build
cmake -S . -B build
cmake --build build

# 2. Register users
cd ..
./client/build/test_auth register testuser1 pass123 testuser1@example.com 127.0.0.1 9000
./client/build/test_auth register testuser2 pass123 testuser2@example.com 127.0.0.1 9000

# 3. Create test file
echo "Test file content" > test_file.txt

# 4. Send file (Terminal 1)
./client/build/test_file_transfer send testuser1 pass123 testuser2 ./test_file.txt 127.0.0.1 9000
# کپی کردن Transfer ID از خروجی

# 5. Receive file (Terminal 2 - استفاده از Transfer ID)
./client/build/test_file_transfer recv testuser2 pass123 <TRANSFER_ID> ./received_file.txt 127.0.0.1 9000

# 6. Verify
diff test_file.txt received_file.txt
```

---

**موفق باشی! 🚀**

