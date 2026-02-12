# Troubleshooting Guide

## Docker Build Issues

### Problem: TLS handshake timeout when pulling images

**Error:**
```
failed to solve: failed to fetch oauth token: Post "https://auth.docker.io/token": net/http: TLS handshake timeout
```

**Solutions:**

1. **Retry the build** (network might be temporarily down):
   ```bash
   docker compose -f docker/compose.yml build core
   ```

2. **Pull the base image manually first:**
   ```bash
   docker pull ubuntu:22.04
   docker compose -f docker/compose.yml build core
   ```

3. **Use Docker's build cache** (if you've built before):
   ```bash
   docker compose -f docker/compose.yml build --no-cache core
   ```

4. **Check Docker daemon:**
   ```bash
   docker info
   ```

5. **If using WSL, restart Docker Desktop:**
   - Close Docker Desktop
   - Restart WSL: `wsl --shutdown` (in PowerShell)
   - Start Docker Desktop again
   - Try build again

6. **Use a different network/DNS:**
   ```bash
   # In WSL, try:
   sudo nano /etc/resolv.conf
   # Add: nameserver 8.8.8.8
   ```

### Problem: Build fails with compile errors

**Check:**
1. All source files are in `CMakeLists.txt`
2. All includes are correct
3. C++ standard is set correctly (C++20)

**Fix missing files in CMakeLists.txt:**
```cmake
add_executable(fsx_core
  # ... existing files ...
  src/transfer/transfer_manager.cpp
  src/storage/file_store.cpp
)
```

### Problem: `std::filesystem` not found

**Solution:** Ensure C++17 or higher:
```cmake
set(CMAKE_CXX_STANDARD 20)  # Already set, but verify
```

### Problem: `ssize_t` not found (Windows)

**Solution:** Already handled with `#ifdef _WIN32` in code, but if issues persist:
```cpp
#include <sys/types.h>  // Provides ssize_t on Linux
```

---

## Runtime Issues

### Problem: Core container exits immediately

**Check logs:**
```bash
docker logs fsx_core
```

**Common causes:**
- Database connection failed
- Port already in use
- Missing dependencies

### Problem: File transfer not working

**Check:**
1. Storage directory exists: `./storage/transfers`
2. Permissions are correct
3. Disk space available

**Verify:**
```bash
docker exec fsx_core ls -la /src/storage/transfers
```

---

## Network Issues

### Problem: Can't connect from Windows to WSL Docker

**Solution:** Use WSL IP address:
```bash
# Get WSL IP
ip addr show eth0 | grep inet
# Use that IP in browser: http://<WSL_IP>:8000
```

---

## Quick Fixes

### Rebuild everything from scratch:
```bash
docker compose -f docker/compose.yml down
docker compose -f docker/compose.yml build --no-cache
docker compose -f docker/compose.yml up -d
```

### Check all containers:
```bash
docker ps -a
docker logs fsx_core
docker logs fsx_gateway
docker logs fsx_db
```

