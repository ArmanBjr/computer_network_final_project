# Phase 6 — Compression (Roadmap)

## هدف
فشرده‌سازی chunkها قبل از ارسال با zlib؛ کاهش حجم روی شبکه و (در صورت bottleneck شبکه) بهبود سرعت.

## ToDo های فاز ۶

| # | کار | وضعیت |
|---|-----|--------|
| 1 | ماژول `ZlibCodec` (compress/decompress با zlib) | ✅ انجام شده |
| 2 | گسترش `FILE_UPLOAD_CHUNK`: فیلد `original_size` (u64)؛ وقتی >0 یعنی داده فشرده است | ✅ انجام شده |
| 3 | سرور: در `handle_file_upload_chunk` اگر `original_size > 0` ابتدا decompress، بعد CRC روی داده بازشده، سپس ذخیره | ✅ انجام شده |
| 4 | کلاینت: پرچم `--compress`؛ در ارسال، در صورت فعال بودن، هر chunk را فشرده و `original_size` را ست کند | ✅ انجام شده |
| 5 | تست واحد `test_zlib_codec` + اسکریپت WSL `scripts/wsl_test_phase6.sh` | ✅ انجام شده |

## تست‌ها (WSL)

### پیش‌نیاز
- داکر: فقط `db` و `core` (کانتینرها را دستی بالا بیاور).
- کلاینت و core را در WSL بیلد کن (نیاز به zlib روی سیستم).

### دستورات دستی برای بالا آوردن سرویس‌ها
```bash
# از ریشه پروژه
docker compose -f docker/compose.yml up -d db core
# صبر کن تا core و db آماده شوند (مثلاً ۵ ثانیه)
docker logs fsx_core --tail 5
```

### ۱) تست واحد ZlibCodec
```bash
cd core
cmake -S . -B build && cmake --build build
./build/test_zlib_codec
```

### ۲) تست یکپارچه فاز ۶ (ارسال با --compress + دریافت)
```bash
# از ریشه پروژه؛ فرض: db و core بالا هستند
dos2unix scripts/wsl_test_phase6.sh
chmod +x scripts/wsl_test_phase6.sh
./scripts/wsl_test_phase6.sh
```

اسکریپت انجام می‌دهد:
- چک کردن سرویس‌های داکر (در صورت نبود، db+core را استارت می‌کند)
- بیلد core (با zlib) و client
- اجرای `test_zlib_codec`
- ثبت کاربران و ایجاد فایل متنی فشرده‌پذیر
- ارسال با `--compress` و دریافت؛ مقایسه فایل دریافتی با اصل

### ۳) تست دستی ارسال با/بدون فشرده‌سازی
```bash
# بدون فشرده‌سازی
./client/build/test_file_transfer send testuser1 pass123 testuser2 ./myfile.txt 127.0.0.1 9000

# با فشرده‌سازی
./client/build/test_file_transfer send testuser1 pass123 testuser2 ./myfile.txt 127.0.0.1 9000 --compress
```

## پروتکل (خلاصه)

- **FILE_UPLOAD_CHUNK** (نسخه با فشرده‌سازی):
  - هدر قبلی: `transfer_id`, `chunk_index`, `data_size`, `crc32` (CRC روی دادهٔ **بازنشده**).
  - فیلد جدید: `original_size` (u64، اختیاری). اگر وجود داشته باشد و >0 باشد، `data` با zlib فشرده است و `original_size` اندازهٔ قبل از فشرده‌سازی است.
  - سازگاری عقب‌گرد: کلاینت/سرور قدیمی بدون `original_size` همچنان کار می‌کنند (داده خام، `original_size=0`).

## فایل‌های مرتبط

- `core/include/fsx/transfer/zlib_codec.h`, `core/src/transfer/zlib_codec.cpp`
- `core/include/fsx/protocol/file_messages.h` (ساختار `FileUploadChunk.original_size`)
- `core/src/net/tcp_session.cpp` (`handle_file_upload_chunk`: decompress وقتی `original_size > 0`)
- `client/src/test_file_transfer.cpp` (پرچم `--compress` و ساخت فریم با `original_size`)
- `core/tests/test_zlib_codec.cpp`, `scripts/wsl_test_phase6.sh`
