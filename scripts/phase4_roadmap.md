# Phase 4: CRC32 + ACK/NAK + Retransmission - Roadmap

## وضعیت فعلی

### ✅ پیاده‌سازی شده:
- **CRC32 Implementation**: `IntegrityService::crc32()` کامل است
- **Protocol Messages**: همه پیام‌های Phase 4 تعریف شده‌اند
- **Server Handler**: `handle_file_upload_chunk()` با ACK/NAK logic کار می‌کند
- **Test Script**: `wsl_test_phase4.sh` آماده است

### ❌ نیاز به تکمیل:
- **Client Upload Logic**: هنوز از Phase 3 استفاده می‌کند
- **Retransmission**: منطق retry وجود ندارد
- **Stop-and-Wait**: کلاینت منتظر ACK/NAK نمی‌ماند

---

## نقشه راه (Roadmap)

### Task 1: Upgrade Client Upload Function
**هدف**: تغییر `do_send()` از `FILE_CHUNK` به `FILE_UPLOAD_CHUNK`

**کارها**:
1. جایگزینی `make_file_chunk()` با `make_file_upload_chunk()`
2. محاسبه CRC32 برای هر chunk قبل از ارسال
3. استفاده از `IntegrityService::crc32()` در client

**فایل**: `client/src/test_file_transfer.cpp`
**تابع**: `do_send()`

---

### Task 2: Implement Stop-and-Wait Pattern
**هدف**: کلاینت بعد از هر chunk منتظر ACK/NAK بماند

**کارها**:
1. بعد از ارسال هر chunk، منتظر پاسخ بماند
2. استفاده از `read_upload_ack_nak()` که از قبل وجود دارد
3. فقط بعد از دریافت ACK به chunk بعدی برود

**فایل**: `client/src/test_file_transfer.cpp`
**تابع**: `do_send()` - داخل loop ارسال chunks

---

### Task 3: Add Retry Logic
**هدف**: در صورت دریافت NAK، chunk را دوباره ارسال کند

**کارها**:
1. اگر NAK دریافت شد، همان chunk را دوباره ارسال کند
2. حداکثر 3-5 بار retry
3. اگر بعد از retry‌ها موفق نشد، خطا بدهد

**فایل**: `client/src/test_file_transfer.cpp`
**تابع**: `do_send()` - داخل loop

---

### Task 4: Add Logging and Counters
**هدف**: لاگ و شمارنده برای retry‌ها

**کارها**:
1. شمارش تعداد retry برای هر chunk
2. لاگ کردن NAK دریافت شده
3. لاگ کردن retry attempts
4. گزارش نهایی تعداد retry‌ها

**فایل**: `client/src/test_file_transfer.cpp`

---

### Task 5: Testing
**هدف**: تست کامل Phase 4

**تست‌ها**:
1. **Normal Transfer**: انتقال فایل عادی (باید بدون retry کار کند)
2. **Fault Injection**: با `--corrupt-upload-once` (باید NAK و retry ببیند)
3. **Wireshark Verification**: بررسی ACK/NAK در Wireshark

**اسکریپت**: `scripts/wsl_test_phase4.sh`

---

## ترتیب اجرا

1. ✅ **Start Containers** - بالا آوردن کانتینرها
2. 🔄 **Task 1** - Upgrade client upload function
3. 🔄 **Task 2** - Implement stop-and-wait
4. 🔄 **Task 3** - Add retry logic
5. 🔄 **Task 4** - Add logging
6. 🔄 **Task 5** - Testing

---

## نکات مهم

- **Stop-and-Wait**: فقط یک chunk در هر زمان در حال انتقال است
- **Retry Limit**: حداکثر 3-5 بار retry برای جلوگیری از infinite loop
- **CRC32**: باید روی data محاسبه شود، نه header
- **Error Handling**: اگر retry limit رسید، خطا بدهد و transfer را متوقف کند

---

## خروجی مورد انتظار

بعد از تکمیل، باید:
- ✅ Client از `FILE_UPLOAD_CHUNK` استفاده کند
- ✅ CRC32 برای هر chunk محاسبه شود
- ✅ Client منتظر ACK/NAK بماند
- ✅ در صورت NAK، retry انجام شود
- ✅ لاگ‌های مناسب نمایش داده شود
- ✅ تست‌ها موفق باشند

