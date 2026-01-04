# FileShareX Demo Scenarios

## Phase 2 Demo Scenario: Authentication & Online List

### Prerequisites
- Docker and Docker Compose installed
- WSL2 (if on Windows) or Linux environment
- Browser for accessing dashboard
- Wireshark (optional, for packet capture)

---

## Step-by-Step Demo

### 1. Start Services

```bash
# From project root
docker compose -f docker/compose.yml up -d postgres core gateway
```

**Expected output:**
- PostgreSQL container running on port 5432
- Core server running on port 9000
- Gateway running on port 8000

**Verify:**
```bash
docker ps
# Should show: fsx_db, fsx_core, fsx_gateway
```

---

### 2. Open Dashboard

**WSL Users:** Get WSL IP first:
```bash
hostname -I
# Example: 172.24.30.74
```

**Access dashboard:**
- From Windows browser: `http://<WSL_IP>:8000`
- From WSL/Linux: `http://localhost:8000`

**Expected:**
- Gateway Status: "WS connected"
- Online Users table: Empty (count=0) or "No users online"

---

### 3. Start Wireshark Capture (Optional)

```bash
# In WSL/Linux terminal
sudo tcpdump -i lo -nn -vvv tcp port 9000 -w fsx_phase2.pcap
```

Or use Wireshark GUI:
- Filter: `tcp.port == 9000`
- Start capture on loopback interface

---

### 4. Register and Login Clients

**Terminal 1 - Client 1:**
```bash
cd client
# Build if needed
cmake -S . -B build
cmake --build build

# Register
./build/test_auth register alice pass123 127.0.0.1 9000

# Login
./build/test_auth login alice pass123 127.0.0.1 9000
```

**Terminal 2 - Client 2:**
```bash
cd client
# Register
./build/test_auth register bob pass456 127.0.0.1 9000

# Login
./build/test_auth login bob pass456 127.0.0.1 9000
```

**Terminal 3 - Client 3:**
```bash
cd client
# Only login (user already exists)
./build/test_auth login alice pass123 127.0.0.1 9000
```

**Expected in Dashboard:**
- After ~2 seconds, Online Users table shows:
  - alice
  - bob
  - Count: 2 (or 3 if client3 also logged in)

---

### 5. Verify Logs

**Core logs:**
```bash
docker logs fsx_core | grep -E "AUTH_|ONLINE_"
```

**Expected log entries:**
```
AUTH_REGISTER_OK username=alice from=127.0.0.1:xxxxx
AUTH_LOGIN_OK username=alice user_id=1 token=xxxxxxxx count=1
ONLINE_ADD username=alice user_id=1 count=1
AUTH_REGISTER_OK username=bob from=127.0.0.1:xxxxx
AUTH_LOGIN_OK username=bob user_id=2 token=xxxxxxxx count=2
ONLINE_ADD username=bob user_id=2 count=2
```

---

### 6. Test Disconnect

**Kill one client:**
```bash
# Find client process
ps aux | grep test_auth

# Kill it (or just Ctrl+C if still running)
kill <PID>
```

**Expected in Dashboard:**
- After ~2 seconds, Online Users count decreases
- One username disappears from table

**Expected in Core logs:**
```
ONLINE_REMOVE username=bob user_id=2 token=xxxxxxxx count=1
```

---

### 7. Test Online List Request

**From Gateway:**
```bash
curl http://localhost:8000/api/online
```

**Expected JSON:**
```json
{
  "count": 1,
  "users": ["alice"]
}
```

---

### 8. Wireshark Analysis (Optional)

**Open capture file:**
```bash
wireshark fsx_phase2.pcap
```

**Filters to use:**
- `tcp.port == 9000` - All Core traffic
- `tcp.stream eq 0` - First connection
- `tcp.flags.syn == 1` - TCP handshake
- `tcp.payload` - Application data

**What to show:**
1. TCP 3-way handshake
2. LOGIN_REQ packet (type=12 in header)
3. LOGIN_RESP packet (type=13 in header)
4. ONLINE_LIST_REQ packet (type=20 in header)
5. ONLINE_LIST_RESP packet (type=21 in header)

---

## Troubleshooting

### Dashboard shows "Core server unavailable"
- Check Core container: `docker logs fsx_core`
- Verify Core is listening: `netstat -tlnp | grep 9000`
- Check Gateway logs: `docker logs fsx_gateway`

### Clients can't connect
- Verify Core is running: `docker ps | grep fsx_core`
- Check firewall/port: `telnet localhost 9000`
- Check Core logs for errors

### Online list not updating
- Check Gateway auto-refresh (every 2 seconds)
- Verify Core logs show ONLINE_LIST_REQ/RESP
- Check browser console for JavaScript errors

---

## Success Criteria

✅ Dashboard shows online users in real-time
✅ Logs show AUTH_LOGIN_OK and ONLINE_ADD events
✅ Disconnect removes user from list
✅ Wireshark capture shows protocol messages
✅ All events are logged with timestamps and IP addresses
