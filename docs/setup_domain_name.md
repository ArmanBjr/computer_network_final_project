# Setting Up Domain Name for FileShareX

Instead of accessing the application via IP address (`172.24.30.74:8000`), you can use a domain name like `filesharex.local`.

## Method 1: Windows Hosts File (Recommended for Local Development)

### Step 1: Edit Windows Hosts File

1. Open Notepad **as Administrator**:
   - Press `Win + X`
   - Select "Windows Terminal (Admin)" or "Command Prompt (Admin)"
   - Or right-click Notepad → "Run as administrator"

2. Open the hosts file:
   - File → Open
   - Navigate to: `C:\Windows\System32\drivers\etc\`
   - Change file filter to "All Files (*.*)"
   - Open `hosts`

3. Add this line at the end:
   ```
   172.24.30.74    filesharex.local
   ```

4. Save the file (Ctrl+S)

### Step 2: Update Docker Compose Configuration

Update `docker/compose.yml` to use the domain name in email reset URLs:

```yaml
FSX_RESET_BASE_URL: http://filesharex.local:8000
```

### Step 3: Access via Domain

Now you can access:
- Login: `http://filesharex.local:8000/login`
- Dashboard: `http://filesharex.local:8000/messenger`
- Reset Password: `http://filesharex.local:8000/reset-password?token=...`

---

## Method 2: WSL Hosts File (If accessing from WSL)

If you're accessing from within WSL:

1. Edit WSL hosts file:
   ```bash
   sudo nano /etc/hosts
   ```

2. Add this line:
   ```
   172.24.30.74    filesharex.local
   ```

3. Save (Ctrl+O, Enter, Ctrl+X)

---

## Method 3: Use localhost (If on Same Machine)

If you're accessing from the same machine where Docker is running:

1. Update `docker/compose.yml`:
   ```yaml
   FSX_RESET_BASE_URL: http://localhost:8000
   ```

2. Access via: `http://localhost:8000`

**Note:** This only works if you're accessing from the same machine. For network access, use Method 1.

---

## Method 4: Dynamic DNS (For Public Access)

If you want to access from anywhere on the internet:

1. **Get a free domain** from services like:
   - [No-IP](https://www.noip.com/) - Free dynamic DNS
   - [DuckDNS](https://www.duckdns.org/) - Free subdomain
   - [FreeDNS](https://freedns.afraid.org/) - Free DNS hosting

2. **Configure your router** to update the DNS record when your IP changes

3. **Update `docker/compose.yml`**:
   ```yaml
   FSX_RESET_BASE_URL: http://yourdomain.ddns.net:8000
   ```

---

## Testing

After setting up, test the domain:

```bash
# Windows PowerShell
ping filesharex.local

# Should resolve to 172.24.30.74
```

Then access: `http://filesharex.local:8000/login`

---

## Troubleshooting

### Domain not resolving?

1. **Flush DNS cache** (Windows):
   ```powershell
   ipconfig /flushdns
   ```

2. **Check hosts file syntax**:
   - No extra spaces
   - IP address first, then domain
   - One entry per line

3. **Restart browser** after editing hosts file

### Still using IP in emails?

Make sure to:
1. Update `FSX_RESET_BASE_URL` in `docker/compose.yml`
2. Restart the gateway container:
   ```bash
   docker restart fsx_gateway
   ```

---

## Recommended Domain Names

For local development, you can use:
- `filesharex.local`
- `fsx.local`
- `filesharex.test`
- `fsx.test`

**Note:** `.local` and `.test` are reserved TLDs that won't conflict with real domains.

