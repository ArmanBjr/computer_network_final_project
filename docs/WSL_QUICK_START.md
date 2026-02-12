# 🚀 راهنمای سریع تست Phase 3 در WSL

## ✅ وضعیت فعلی
- ✅ Core server: در حال اجرا
- ✅ Database: در حال اجرا
- ✅ Gateway: در حال اجرا

---

## 🎯 دو روش تست

### روش 1: تست خودکار (پیشنهادی)

```bash
# در WSL
cd /mnt/e/University/University_Subjects/5th/Computer_Networks/Projects/finl_project/filesharex

# اجرای اسکریپت تست
./scripts/wsl_test_phase3.sh
```

این اسکریپت:
- ✅ سرویس‌ها را بررسی می‌کند
- ✅ Client را build می‌کند
- ✅ کاربران را register می‌کند
- ✅ فایل را ارسال و دریافت می‌کند
- ✅ صحت فایل را بررسی می‌کند
- ✅ خلاصه نتایج را نمایش می‌دهد

---

### روش 2: تست دستی (گام‌به‌گام)

**برای راهنمای کامل:** `docs/WSL_TEST_GUIDE.md`

**خلاصه دستورات:**

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
echo "Test content" > test_file.txt

# 4. Terminal 1: Send
./client/build/test_file_transfer send testuser1 pass123 testuser2 ./test_file.txt 127.0.0.1 9000
# کپی کردن Transfer ID

# 5. Terminal 2: Receive (استفاده از Transfer ID)
./client/build/test_file_transfer recv testuser2 pass123 <TRANSFER_ID> ./received.txt 127.0.0.1 9000

# 6. Verify
diff test_file.txt received.txt
```

---

## 📋 چک‌لیست سریع

- [ ] WSL باز است
- [ ] در دایرکتوری پروژه هستی (`cd /mnt/e/.../filesharex`)
- [ ] Docker containers در حال اجرا هستند (`docker ps | grep fsx`)
- [ ] Client build شده (`ls client/build/test_file_transfer`)

---

## 🔍 بررسی وضعیت

```bash
# بررسی سرویس‌ها
docker ps | grep fsx

# بررسی لاگ‌های Core
docker logs fsx_core --tail 5

# بررسی build client
ls -lh client/build/test_file_transfer
```

---

## ⚠️ مشکلات رایج

### Build failed
```bash
cd client
rm -rf build
cmake -S . -B build
cmake --build build
```

### Connection refused
```bash
# بررسی سرویس‌ها
docker ps | grep fsx_core

# اگر نیست، اجرا کن:
docker compose -f docker/compose.yml up -d core
```

### Permission denied
```bash
chmod +x client/build/test_file_transfer
chmod +x client/build/test_auth
```

---

**برای راهنمای کامل:** `docs/WSL_TEST_GUIDE.md`

**موفق باشی! 🎉**

