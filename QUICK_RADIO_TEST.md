# QUICK RADIO TEST - LNK-22

**Issue:** Both radios show TX but no RX/neighbors

## New Debug Firmware Available

**File:** `lnk22-DEBUG-v1.0.uf2`
**New Features:**
- ✅ `beacon` command - manually send beacon
- ✅ `radio` command - show radio config
- ✅ `channel <0-7>` command - switch channels
- ✅ Channel shown in `status` command

## STEP 1: Quick Test (Current Firmware)

**On Radio 1:**
```
> status
```

Look for:
```
Node Address: 0x[ADDR1]    ← Should be different
Channel: 0                  ← Should match Radio 2
Packets Sent: XX            ← Should be increasing
Packets Received: 0         ← PROBLEM!
Neighbors: 0                ← PROBLEM!
```

**On Radio 2:**
```
> status
```

Should show DIFFERENT node address but same problem.

## STEP 2: Manual Beacon Test

**On Radio 1:**
```
> beacon
```

Watch the serial output - you should see:
```
[CMD] Sending beacon now...
[MESH] TX packet ID XX to 0xFFFFFFFF
[CMD] Beacon sent!
```

**Now check Radio 2 serial - do you see ANYTHING?**

Expected (if working):
```
[MESH] RX packet type 8 from 0x[ADDR1]
[MESH] Neighbor 0x[ADDR1] updated, RSSI: -XX
```

## STEP 3: Broadcast Test

**On Radio 1:**
```
> send 0xFFFFFFFF test message
```

**Check Radio 2 - do you see:**
```
[MESH] RX packet ...
```

## STEP 4: Check Both Radios on Same Channel

**On BOTH radios:**
```
> status
```

Make sure "Channel: 0" on BOTH.

If different, fix with:
```
> channel 0
```

## STEP 5: Check Radio Config

**On Radio 1:**
```
> radio
```

Should show:
```
=== Radio Status ===
Frequency: 915 MHz, SF10, BW125 kHz
TX Power: 22 dBm
Sync Word: 0x12
===================
```

Check Radio 2 shows SAME config.

## Common Issues & Quick Fixes

### Issue 1: No RX at all
**Cause:** Antenna not connected or damaged
**Fix:** Check both antennas are firmly connected

### Issue 2: "Ignoring packet on channel X"
**Cause:** Radios on different channels
**Fix:**
```
# On both radios:
> channel 0
```

### Issue 3: TX works but no RX
**Cause:** Sync word mismatch or radio not initialized
**Check Serial for:**
```
[RADIO] SX1262 initialization FAILED
```

### Issue 4: Different firmware versions
**Cause:** Radios flashed with different UF2 files
**Fix:** Reflash BOTH with same `lnk22-FIXED-v1.0.uf2`

## Expected Behavior When Working

### Radio 1:
```
> status
Node Address: 0x12345678
Packets Sent: 15
Packets Received: 3        ← Should see packets!
Neighbors: 1                ← Should see Radio 2!
Channel: 0
```

### Radio 2:
```
> status
Node Address: 0x87654321   ← Different from Radio 1
Packets Sent: 12
Packets Received: 5         ← Should see packets!
Neighbors: 1                ← Should see Radio 1!
Channel: 0
```

## What to Report

If still not working, send me:

1. **Output from Radio 1:**
```
> status
[paste output]

> radio
[paste output]

> beacon
[paste output]
```

2. **Output from Radio 2:**
```
> status
[paste output]

> neighbors
[paste output]
```

3. **Hardware Setup:**
- Distance between radios: [X meters]
- Antennas connected: [Yes/No]
- Both powered on: [Yes/No]
- Serial shows "LNK-22 Firmware v1.0.0": [Yes/No]

## Advanced Debug (If Needed)

If you want to see WHY packets are being filtered, we can:

1. Temporarily disable channel filtering
2. Add verbose RX logging
3. Check if packets arrive but are rejected

But let's try the basic tests first!

## Most Likely Causes

Based on symptoms (TX works, RX doesn't):

1. **Antennas** (90% likely)
   - Not connected
   - Wrong frequency (need 915 MHz)
   - Damaged

2. **Channel mismatch** (5% likely)
   - Shouldn't happen with same firmware
   - But check with `status` command

3. **Radio not initialized** (3% likely)
   - Would see error in serial output
   - Both radios working suggests not this

4. **Too far apart** (2% likely)
   - SF10 should work 10+ km
   - Try within 5 meters first

## Quick Debug Test

**Absolute simplest test:**

1. Put radios 1 meter apart
2. Both plugged into computer via USB
3. Both serial terminals open
4. Type on Radio 1: `beacon`
5. Watch Radio 2 serial

If Radio 2 shows NOTHING → antenna/hardware issue
If Radio 2 shows packet but filters it → configuration issue

---

Try these tests and let me know what you see!
