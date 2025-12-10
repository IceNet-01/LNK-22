# LNK-22 Messaging Guide

## How Messages Work

### Architecture Overview

```
Radio 1 (USB Port A)          Radio 2 (USB Port B)
     │                              │
     │ Serial/USB                   │ Serial/USB
     │ (Commands In)                │ (Messages Out)
     ▼                              ▼
  Firmware                       Firmware
     │                              │
     │ LoRa 915 MHz                 │ LoRa 915 MHz
     │ (Messages Out)               │ (Messages In)
     └──────── 📡 ───────────────► 📡

     Physical radio waves in the air!
```

**Key Points:**
- Each radio is a SEPARATE physical device
- Serial/USB is ONLY for human interaction with that specific radio
- Messages travel via **LoRa radio waves** (915 MHz) through the air
- Serial console shows what YOUR radio sends/receives

---

## Sending Messages (Via Serial Console)

### Broadcast Message (All Radios)
```bash
> send 0xFFFFFFFF Hello everyone!
```

### Direct Message (Specific Radio)
```bash
> send 0x12345678 Private message
```

---

## Receiving Messages (Automatic Display)

When a message arrives via radio, you'll see:

```
========================================
📨 MESSAGE from 0xAABBCCDD (BROADCAST)
========================================
Hello everyone!
========================================
```

**Or for direct messages:**
```
========================================
📨 MESSAGE from 0x11223344 (DIRECT)
========================================
Private message
========================================
```

**This prints automatically** - no commands needed!

---

## Message Flow Example

### Scenario: Radio 1 sends to Radio 2

**Step 1: On Radio 1 serial console:**
```
> send 0xFFFFFFFF test message
```

**Radio 1 shows:**
```
Sending to 0xFFFFFFFF: test message
[MESH] Sent packet ID 5 to 0xFFFFFFFF
```

**Step 2: Radio waves travel through air (915 MHz)**

**Step 3: Radio 2 serial console shows:**
```
========================================
📨 MESSAGE from 0xAABBCCDD (BROADCAST)
========================================
test message
========================================
```

---

## Why Use Radio Waves?

**Q: Why not just send via USB/Serial?**
**A:** Because the radios are **physically separate devices!**

- Your computer has TWO USB ports
- Radio 1 plugged into USB Port A
- Radio 2 plugged into USB Port B
- No physical connection between them!

**The whole point of mesh networking:**
- Radios can be kilometers apart
- No wires needed
- Messages hop through multiple radios
- Works in disaster scenarios (no internet/cell)

---

## Current Message Display

### NEW Firmware (lnk22-MESSAGES-v1.0.uf2)

**When you SEND a message:**
```
> send 0xFFFFFFFF hello
Sending to 0xFFFFFFFF: hello
[MESH] Sent packet ID 10 to 0xFFFFFFFF
```

**When you RECEIVE a message:**
```
========================================
📨 MESSAGE from 0x12345678 (BROADCAST)
========================================
hello
========================================
```

**Much more visible!** The box formatting makes it obvious.

---

## Debugging Message Delivery

### Check if message was sent:
```
> status
Packets Sent: 15        ← Should increment after send
```

### Check if messages received:
```
> status
Packets Received: 3     ← Should increment when messages arrive
```

### Check neighbor discovery:
```
> neighbors
Neighbor 0x12345678, RSSI: -45 dBm, SNR: 8, Last seen: 5s ago
```

If neighbors = 0, messages won't be received!

---

## Troubleshooting

### "I sent a message but nothing happened"

**On the RECEIVING radio, you should see:**
```
========================================
📨 MESSAGE from 0x...
========================================
```

**If you don't see this:**
1. Check RX count: `> status` (should be > 0)
2. Check neighbors: `> neighbors` (should see other radio)
3. Check both on same channel: `> status` (Channel: 0)
4. Check antennas connected on BOTH radios

### "I see TX count but no RX count"

This means:
- ✅ Your radio CAN transmit
- ❌ Your radio CANNOT receive
- Likely antenna or hardware issue

### "Messages show but look garbled"

Could be:
- Interference
- Wrong baud rate (should be 115200)
- Terminal encoding issue

---

## Web UI Coming Soon!

**Currently:** Messages only show in serial console

**Coming next (Web UI):**
- Browser-based interface
- Message history
- Click to send messages
- See all conversations
- Network visualization

**But for now:** Serial console works perfectly for testing!

---

## Example Conversation

**Radio 1:**
```
> send 0xFFFFFFFF Radio 1 here!
Sending to 0xFFFFFFFF: Radio 1 here!
[MESH] Sent packet ID 1 to 0xFFFFFFFF

[Wait a moment...]

========================================
📨 MESSAGE from 0x22334455 (BROADCAST)
========================================
Radio 2 responding!
========================================
```

**Radio 2:**
```
========================================
📨 MESSAGE from 0x11223344 (BROADCAST)
========================================
Radio 1 here!
========================================

> send 0xFFFFFFFF Radio 2 responding!
Sending to 0xFFFFFFFF: Radio 2 responding!
[MESH] Sent packet ID 1 to 0xFFFFFFFF
```

---

## Quick Test Commands

### Basic messaging test:
```bash
# Radio 1:
> send 0xFFFFFFFF ping

# Radio 2 (should auto-display):
========================================
📨 MESSAGE from 0x... (BROADCAST)
========================================
ping
========================================

# Radio 2 reply:
> send 0xFFFFFFFF pong

# Radio 1 (should auto-display):
========================================
📨 MESSAGE from 0x... (BROADCAST)
========================================
pong
========================================
```

---

## Summary

✅ **Messages are sent**: Via LoRa radio (915 MHz)
✅ **Messages are received**: Auto-display in serial console
✅ **No app needed**: Serial terminal works for now
✅ **Web UI coming**: Better interface being built
✅ **Physical radios**: Must use radio waves to communicate

The serial console is your "chat app" for now - it shows both what you send and what arrives via radio!

---

**Try it now:**
1. Radio 1: `> send 0xFFFFFFFF test`
2. Radio 2: Should auto-display the message in a box
3. Radio 2: `> send 0xFFFFFFFF reply`
4. Radio 1: Should auto-display the reply

If messages don't appear on the OTHER radio, you have an RX problem (likely antennas).
