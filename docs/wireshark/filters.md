# Wireshark Filters for FileShareX

## Quick Reference

### Basic Filters

**Core Server Traffic (Port 9000):**
```
tcp.port == 9000
```

**Gateway HTTP (Port 8000):**
```
tcp.port == 8000
```

**Admin Port (Port 9100, future):**
```
tcp.port == 9100
```

---

## Phase 2 Filters

### Authentication Messages

**Login Request (Type 12):**
```
tcp.port == 9000 && tcp.payload[4] == 0x0c
```

**Login Response (Type 13):**
```
tcp.port == 9000 && tcp.payload[4] == 0x0d
```

**Register Request (Type 10):**
```
tcp.port == 9000 && tcp.payload[4] == 0x0a
```

**Register Response (Type 11):**
```
tcp.port == 9000 && tcp.payload[4] == 0x0b
```

### Online List Messages

**Online List Request (Type 20):**
```
tcp.port == 9000 && tcp.payload[4] == 0x14
```

**Online List Response (Type 21):**
```
tcp.port == 9000 && tcp.payload[4] == 0x15
```

### Combined Filters

**All Auth Messages:**
```
tcp.port == 9000 && (tcp.payload[4] == 0x0a || tcp.payload[4] == 0x0b || tcp.payload[4] == 0x0c || tcp.payload[4] == 0x0d)
```

**All Online List Messages:**
```
tcp.port == 9000 && (tcp.payload[4] == 0x14 || tcp.payload[4] == 0x15)
```

**All Phase 2 Messages:**
```
tcp.port == 9000 && (tcp.payload[4] >= 0x0a && tcp.payload[4] <= 0x15)
```

---

## Protocol Header Structure

**Message Header (12 bytes):**
- Bytes 0-3: Magic (0x46535831 = "FSX1")
- Byte 4: Version (0x01)
- Byte 5: Message Type
- Bytes 6-9: Payload Length (big-endian)
- Bytes 10-11: Reserved (0x0000)

**Message Type Values:**
- 0x01: HELLO
- 0x02: PING
- 0x03: PONG
- 0x0a: REGISTER_REQ
- 0x0b: REGISTER_RESP
- 0x0c: LOGIN_REQ
- 0x0d: LOGIN_RESP
- 0x14: ONLINE_LIST_REQ (20)
- 0x15: ONLINE_LIST_RESP (21)

---

## Display Filters for Analysis

### Show TCP Stream
```
tcp.stream eq 0
```
(Replace 0 with stream number)

### Show Only Application Data
```
tcp.port == 9000 && tcp.len > 0
```

### Show TCP Handshake
```
tcp.port == 9000 && (tcp.flags.syn == 1 || tcp.flags.ack == 1)
```

### Show Connection Establishment
```
tcp.port == 9000 && tcp.flags.syn == 1
```

### Show Connection Close
```
tcp.port == 9000 && tcp.flags.fin == 1
```

---

## Capture Commands

### Capture Core Traffic
```bash
sudo tcpdump -i lo -nn -vvv tcp port 9000 -w fsx_phase2.pcap
```

### Capture All FileShareX Traffic
```bash
sudo tcpdump -i lo -nn -vvv 'tcp port 9000 or tcp port 8000 or tcp port 9100' -w fsx_all.pcap
```

### Capture with Display Filter
```bash
sudo tcpdump -i lo -nn -vvv -s 0 'tcp port 9000 and tcp[tcpflags] & tcp-syn != 0' -w fsx_handshake.pcap
```

---

## Analysis Tips

1. **Follow TCP Stream:**
   - Right-click packet → Follow → TCP Stream
   - Shows complete conversation

2. **Find Message Type:**
   - Look at byte 5 (offset 4) in TCP payload
   - Compare with message type values above

3. **Payload Length:**
   - Bytes 6-9 contain payload length (big-endian)
   - Use: `tcp.payload[6:10]` to inspect

4. **Magic Validation:**
   - First 4 bytes should be: `46 53 58 31` ("FSX1")
   - Filter: `tcp.payload[0:4] == 46:53:58:31`

---

## Phase 2 Specific

### Login Flow
1. Filter: `tcp.port == 9000 && tcp.payload[4] == 0x0c`
2. Follow TCP stream
3. Look for LOGIN_REQ (type 12) → LOGIN_RESP (type 13)

### Online List Flow
1. Filter: `tcp.port == 9000 && tcp.payload[4] == 0x14`
2. Follow TCP stream
3. Look for ONLINE_LIST_REQ (type 20) → ONLINE_LIST_RESP (type 21)

---

## Future Phases

### File Transfer (Phase 3+)
```
tcp.port == 9000 && tcp.payload[4] >= 0x1e && tcp.payload[4] <= 0x3f
```

### Voice (Phase 10+)
```
udp.port == 9001
```

### Admin (Phase 2+)
```
tcp.port == 9100
```

