# Email Setup Guide for FileShareX

## Forgot Password Email Configuration

To enable email sending for password reset, you need to configure SMTP settings.

### Option 1: Gmail (Recommended for Testing)

1. **Enable 2-Factor Authentication** on your Gmail account
2. **Generate App Password:**
   - Go to https://myaccount.google.com/apppasswords
   - Generate a new app password for "Mail"
   - Copy the 16-character password

3. **Update docker/compose.yml:**
   ```yaml
   gateway:
     environment:
       FSX_SMTP_USER: your-email@gmail.com
       FSX_SMTP_PASSWORD: xxxx xxxx xxxx xxxx  # App password (16 chars, no spaces)
       FSX_SMTP_FROM: your-email@gmail.com
       FSX_RESET_BASE_URL: http://172.24.30.74:8000  # Your WSL IP
   ```

4. **Restart Gateway:**
   ```bash
   docker compose -f docker/compose.yml up -d gateway
   ```

### Option 2: Other SMTP Servers

```yaml
gateway:
  environment:
    FSX_SMTP_HOST: smtp.example.com
    FSX_SMTP_PORT: 587  # or 465 for SSL
    FSX_SMTP_USER: your-email@example.com
    FSX_SMTP_PASSWORD: your-password
    FSX_SMTP_FROM: your-email@example.com
    FSX_SMTP_USE_TLS: "true"  # or "false" for SSL
    FSX_RESET_BASE_URL: http://172.24.30.74:8000
```

### Testing Without Email (Development)

If you don't want to set up email right now, you can:

1. **Check tokens in database:**
   ```bash
   docker exec fsx_db psql -U fsx -d fsx -c "SELECT token, email, expires_at FROM password_reset_tokens ORDER BY created_at DESC LIMIT 1;"
   ```

2. **Use the token directly:**
   ```
   http://172.24.30.74:8000/reset-password?token=<TOKEN_FROM_DB>
   ```

### Verification

After setting up SMTP, check logs:
```bash
docker logs fsx_gateway | grep EMAIL
```

You should see:
- `EMAIL_RESET_SENT email=...` (success)
- or `EMAIL_RESET_ERROR email=...` (error)

