# LNK-22 Radio Debugging Guide

## Issue: Radios Not Seeing Each Other

**Symptoms:**
- Both radios show TX counts increasing
- Neither radio shows RX counts or neighbors
- Beacons are being sent but not received

## Diagnostic Steps

### Step 1: Verify Both Radios Are Transmitting

Connect to **Radio 1** via serial (115200 baud):
```bash
> status
```

Check output:
```
=== LNK-22 Status ===
Node Address: 0x[ADDRESS_1]      ← Should be different on each radio
Uptime: ...
Packets Sent: 10                  ← Should be increasing
Packets Received: 0               ← Problem: should see packets!
Neighbors: 0                      ← Problem: should see other radio!
Routes: 0
Channel: 0                        ← Both must be same!
```

Connect to **Radio 2** - should show **different** node address but same issue.

### Step 2: Check Radio Configuration

Both radios should show:
```
[RADIO] SX1262 configured for 915 MHz
[RADIO] SF10, BW125, CR4/5, 22dBm
[MESH] Default channel: 0
```

### Step 3: Force Manual Beacon

Try manually sending a beacon:
```bash
> beacon
```

Watch the OTHER radio's serial output for:
```
[MESH] RX packet type 8 from 0x[OTHER_ADDRESS]
```

### Step 4: Check for Channel Filtering

The issue might be channel filtering. Try this test:

On **Radio 1**:
```bash
> send 0xFFFFFFFF test broadcast
```

Watch **Radio 2** serial output - you should see packet received.

## Common Causes

### 1. Antennas Not Connected
- **Check:** Both radios have antennas attached
- **Fix:** Connect 915 MHz antennas to both radios

### 2. Distance Too Far for First Test
- **Check:** Radios are within 5-10 meters for initial test
- **Fix:** Move radios closer together

### 3. Sync Word Mismatch
- **Check:** Both radios compiled with same firmware
- **Value:** 0x12 (private network)
- **Fix:** Reflash both with same UF2 file

### 4. Radio Not Initializing
Check serial output for:
```
[RADIO] SX1262 initialization FAILED
```

If you see this:
- Check SPI connections
- Verify SX1262 module is properly seated
- Try pressing RESET button

### 5. RX Callback Not Set
The mesh.begin() should set up the RX callback.

Check for:
```
[MESH] LNK-22 initialized with address 0x...
```

## Quick Fix Commands

### Enable Debug Output
```bash
> debug radio 1
> debug mesh 1
```

This will show all radio RX/TX activity.

### Manual Test Send
```bash
> send 0xFFFFFFFF hello
```

This broadcasts to all radios - watch other radio's serial.

### Force Beacon
```bash
> beacon
```

Immediately sends a beacon packet.

## Expected Behavior (Working)

When working correctly, you should see:

**Radio 1 Serial:**
```
[MESH] TX packet ID 1 to 0xFFFFFFFF
[RADIO] TX complete
...30 seconds later...
[MESH] RX packet type 8 from 0x[RADIO_2_ADDR]
[MESH] Neighbor 0x[RADIO_2_ADDR] updated, RSSI: -45
```

**Radio 2 Serial:**
```
[MESH] RX packet type 8 from 0x[RADIO_1_ADDR]
[MESH] Neighbor 0x[RADIO_1_ADDR] updated, RSSI: -48
...30 seconds later...
[MESH] TX packet ID 1 to 0xFFFFFFFF
```

## Troubleshooting Matrix

| Symptom | Likely Cause | Solution |
|---------|--------------|----------|
| No TX activity | Radio init failed | Check antenna, reset device |
| TX but no RX | Antenna issues | Check both antennas connected |
| RX shows but no neighbors | Callback not working | Check mesh.begin() called |
| Different channels shown | Firmware mismatch | Reflash both with same UF2 |
| "Ignoring packet on channel X" | Channel mismatch | Both should be channel 0 |

## Advanced Debugging

### Check Raw Radio Reception

If you suspect the radio hardware, test raw RX:

1. Put one radio in continuous RX mode
2. Other radio transmits
3. Check RSSI values

### Packet Inspection

Enable verbose debugging:
```bash
> debug all 1
```

This shows:
- Every packet received
- RSSI/SNR values
- Channel info
- Why packets are filtered/ignored

## Hardware Checklist

- [ ] Both RAK4631 modules powered on
- [ ] Both have antennas connected
- [ ] Both showing "LNK-22 Firmware v1.0.0" in serial
- [ ] Both show same channel (0)
- [ ] Both show increasing TX counts
- [ ] Radios are within 10 meters for test
- [ ] No metal/concrete barriers between radios

## Firmware Checklist

- [ ] Both flashed with lnk22-FIXED-v1.0.uf2
- [ ] Both show "LNK-22" branding (not MeshNet)
- [ ] Both show "Professional LoRa Mesh Networking"
- [ ] Same MD5 hash: 8875382696f0b74e2d7e0b999e50552b

## Next Steps

1. **If still not working after these checks:**
   - Share serial output from BOTH radios
   - Include output from `status` command
   - Include output when typing `beacon`

2. **Quick test to try:**
   ```bash
   # On Radio 1:
   > debug mesh 1
   > beacon

   # Watch Radio 2 serial - should see:
   # [MESH] RX packet type 8 from 0x...
   ```

3. **If you see "Ignoring packet on channel X":**
   - This means packets ARE being received!
   - But channel filtering is rejecting them
   - Both radios need to be on same channel

## Success Indicators

When working, within 30-60 seconds you should see:

✅ Radio 1: "Neighbors: 1"
✅ Radio 2: "Neighbors: 1"
✅ Both showing RX counts increasing
✅ RSSI values around -30 to -80 dBm (close range)

