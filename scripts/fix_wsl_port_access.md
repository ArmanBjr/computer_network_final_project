# Fix WSL Port Access Issue

## Problem
When running Docker in WSL, `http://localhost:8000` from Windows browser doesn't work because ports aren't automatically forwarded.

## Solutions

### Solution 1: Use WSL IP Address (Recommended)

1. Find your WSL IP:
```bash
# In WSL terminal:
hostname -I
# or
ip addr show eth0 | grep "inet " | awk '{print $2}' | cut -d/ -f1
```

2. Access from Windows browser:
```
http://<WSL_IP>:8000
```

Example: If WSL IP is `172.20.10.5`, use `http://172.20.10.5:8000`

### Solution 2: Use Docker Desktop (Easiest)

If you have Docker Desktop installed:
1. Open Docker Desktop
2. Go to Settings → Resources → WSL Integration
3. Enable integration with your WSL distro
4. Restart Docker Desktop
5. Now `localhost:8000` should work from Windows

### Solution 3: Port Forwarding (Manual)

Add port forwarding in Windows PowerShell (as Administrator):

```powershell
# Forward port 8000
netsh interface portproxy add v4tov4 listenport=8000 listenaddress=0.0.0.0 connectport=8000 connectaddress=<WSL_IP>

# Forward port 9000 (Core TCP)
netsh interface portproxy add v4tov4 listenport=9000 listenaddress=0.0.0.0 connectport=9000 connectaddress=<WSL_IP>

# Check existing rules
netsh interface portproxy show all

# Remove a rule (if needed)
netsh interface portproxy delete v4tov4 listenport=8000 listenaddress=0.0.0.0
```

### Solution 4: Access from WSL Browser

If you have a browser in WSL:
```bash
# Install a browser in WSL (optional)
sudo apt-get update
sudo apt-get install -y firefox

# Or use Windows browser with WSL IP
```

## Quick Test

1. Check if containers are running:
```bash
docker ps
```

2. Check if port is listening in WSL:
```bash
netstat -tuln | grep 8000
# or
ss -tuln | grep 8000
```

3. Test from WSL:
```bash
curl http://localhost:8000
```

4. Test from Windows (replace with your WSL IP):
```bash
# In PowerShell:
curl http://<WSL_IP>:8000
```

## Recommended Approach

**For development**: Use WSL IP address directly
**For production/demo**: Use Docker Desktop with WSL integration

