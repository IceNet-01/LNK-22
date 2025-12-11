# LNK-22 Radio Communication Debugging - Complete Summary

## Session Date
2025-12-10

## Initial Problem
Both RAK4631 radios could transmit packets but could not receive any packets. Automatic neighbor discovery was not working despite beacons being sent every 30 seconds.

## Hardware Verification
- ✅ RAK4631 modules with 915 MHz antennas
- ✅ Antennas properly connected
- ✅ Radios previously worked with Meshtastic firmware
- ✅ TX functionality confirmed working (packets being sent)
- ❌ RX functionality completely broken (zero packets received)

**Conclusion:** Hardware is fine, issue is in LNK-22 firmware configuration.

## Investigation Steps Taken

### 1. Callback Chain Analysis
Added debug output to trace RX packet flow through the firmware:
- `OnRxDone()` ISR (hardware interrupt handler)
- `Radio::handleRxDone()` (forward to radio instance)
- `Mesh::handleReceivedPacket()` (process mesh packet)

**Result:** NO [ISR] messages ever appeared - the hardware interrupt was NEVER being called.

### 2. DIO1 Pin Monitoring
Added code to check SX1262's DIO1 interrupt pin state:
```cpp
int dio1State = digitalRead(LORA_DIO1_PIN);
Serial.println(dio1State);
```

**Result:** DIO1 pin was ALWAYS LOW (0) - never asserted by the SX1262 chip.

### 3. Root Cause Identified
The SX1262 chip was NOT configured to assert the DIO1 pin on RX_DONE events!

Missing function call: `SX126xSetDioIrqParams()`

While we properly called:
- ✅ `::Radio.Init(&radioEvents)` - Registered callbacks
- ✅ `::Radio.SetRxConfig(...)` - Configured RX parameters
- ✅ `::Radio.Rx(0)` - Started continuous RX mode

We were MISSING:
- ❌ `SX126xSetDioIrqParams()` - Configure which events assert DIO1

## Fix Attempted

### Code Changes Made

**File:** `firmware/src/radio/radio.cpp`

**Added Include:**
```cpp
#include "radio/sx126x/sx126x.h"  // For IRQ constants and SX126xSetDioIrqParams()
```

**Added Configuration (in Radio::begin() after Radio.Init()):**
```cpp
// **CRITICAL FIX**: Configure DIO1 to trigger on RX/TX events
uint16_t irqMask = IRQ_RX_DONE | IRQ_TX_DONE | IRQ_RX_TX_TIMEOUT | IRQ_CRC_ERROR;
SX126xSetDioIrqParams(irqMask, irqMask, IRQ_RADIO_NONE, IRQ_RADIO_NONE);
Serial.println("[RADIO] DIO1 IRQ configured for RX/TX events");
```

### Build & Flash Results
- ✅ Code compiles successfully
- ✅ Fix verified in source code
- ✅ Fix verified in compiled binary (`strings firmware.elf`)
- ✅ Both radios flashed with fixed firmware
- ❌ Radios STILL not receiving packets
- ❌ DIO1 pin STILL always LOW
- ❌ NO [ISR] OnRxDone messages

## Current Status: Issue Persists

Despite all fixes:
- DIO1 pin remains LOW
- No RX interrupts firing
- No packets being received
- No neighbor discovery
- TX still working perfectly

## Possible Remaining Issues

### 1. Function Call Timing
`SX126xSetDioIrqParams()` might need to be called:
- Before `Radio.Init()`
- After `Radio.Rx()` is started
- Multiple times (once for TX, once for RX)

### 2. Function Signature/Parameters
The function might require:
- Different parameter format
- Pointer to struct instead of direct values
- Different IRQ mask values

### 3. Library Integration Issue
The SX126x-Arduino library might:
- Override our IRQ settings internally
- Require different initialization sequence
- Have incompatible API between versions

### 4. Missing Prerequisite
There might be another required call:
- `SX126xSetDio2AsRfSwitchCtrl()`
- Interrupt attachment with `attachInterrupt()`
- Timer/scheduler configuration for interrupt processing

### 5. Hardware Level Issue
Possible hardware problems:
- DIO1 pin not properly connected on RAK4631
- SPI communication issue preventing config writes
- Chip in wrong state/mode

## Data Collected

### Test Results (Latest)
```
Radio 1:
- Node Address: 0xBADA1B9D
- Packets Sent: 5+
- Packets Received: 0
- Neighbors: 0
- DIO1 State: Always 0

Radio 2:
- Node Address: 0xD1C1C4FC
- Packets Sent: 4+
- Packets Received: 0
- Neighbors: 0
- DIO1 State: Always 0
```

### Firmware Size
```
RAM:   11.7% (29,236 / 248,832 bytes)
Flash: 20.0% (163,200 / 815,104 bytes)
```

## Comparison: Meshtastic vs LNK-22

### What Meshtastic Does Differently
Since Meshtastic worked on the same hardware, they must be:
1. Calling `SX126xSetDioIrqParams()` correctly
2. Using different SX126x library version/fork
3. Having additional initialization steps
4. Configuring interrupts through different API

### Next Steps to Try

#### Option A: Reference Meshtastic Code
1. Download Meshtastic firmware source
2. Find their RAK4631 radio initialization
3. Compare line-by-line with our Radio::begin()
4. Identify missing steps

#### Option B: SX126x Library Deep Dive
1. Read SX126x-Arduino library documentation
2. Find working examples for RAK4631
3. Check if library has built-in IRQ configuration
4. Verify we're using library correctly

#### Option C: Hardware Interrupt Verification
1. Manually attach interrupt to DIO1 pin:
   ```cpp
   pinMode(LORA_DIO1_PIN, INPUT);
   attachInterrupt(digitalPinToInterrupt(LORA_DIO1_PIN), isrHandler, RISING);
   ```
2. See if manually attached interrupt fires
3. If yes: library issue
4. If no: hardware/config issue

#### Option D: Direct SX1262 Register Access
1. Bypass library, write directly to SX1262 registers
2. Use datasheet to configure DIO1 IRQ mask
3. Verify with logic analyzer or oscilloscope

#### Option E: Community Help
1. Post issue to:
   - RAK Wireless forums
   - Meshtastic Discord
   - SX126x-Arduino GitHub issues
2. Share our debugging findings
3. Get expert guidance

## Files Modified

- `firmware/src/radio/radio.cpp` - Added IRQ configuration
  - Line 10: Added include for sx126x.h
  - Lines 150-154: Added SX126xSetDioIrqParams() call

## Test Scripts Created

- `quick_test.py` - Tests beacon transmission/reception
- `monitor_dio1.py` - Monitors DIO1 pin state
- `check_fix_applied.py` - Verifies fix in firmware
- `full_boot_capture.py` - Captures complete boot sequence

## Documentation Created

- `RX_FIX_SUMMARY.md` - Detailed fix documentation
- `DEBUGGING_SUMMARY.md` - This file
- `RADIO_DEBUG_GUIDE.md` - Diagnostic procedures

## Time Invested
~3-4 hours of intensive debugging

## Conclusion

**Root Cause Identified:** DIO1 interrupt pin not configured for RX events

**Fix Implemented:** Added `SX126xSetDioIrqParams()` call with proper IRQ masks

**Current Status:** Fix not yet working, issue persists

**Confidence Level:** 95% that DIO1 IRQ configuration is the issue

**Success Criteria:**
- DIO1 pin goes HIGH when packet received
- [ISR] OnRxDone! messages appear in serial output
- Neighbors discovered within 30-60 seconds
- Messages successfully exchanged between radios

**Recommendation:** Need deeper investigation into SX126x library usage or comparison with working Meshtastic code to identify missing initialization step.

---

## For Future Reference

When working with SX126x LoRa chips:
1. ✅ Always verify DIO pin configuration
2. ✅ Monitor actual pin state with `digitalRead()`
3. ✅ Add debug output to ISR handlers
4. ✅ Verify interrupts are firing before debugging higher layers
5. ✅ Compare with known-working code (like Meshtastic)
6. ✅ Use `strings firmware.elf` to verify code is compiled in
7. ✅ Do clean builds when in doubt

The debugging methodology used (working backwards from ISR → pin state → chip config) was sound and led us to the right area. The remaining issue is likely a subtle API usage or timing problem.
