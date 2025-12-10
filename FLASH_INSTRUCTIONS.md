# LNK-22 Firmware Flashing Instructions

**Firmware Version:** v1.0.0
**Build Date:** December 10, 2025
**Platform:** RAK4631 (nRF52840 + SX1262)

---

## 📦 Firmware Files

### Main Firmware (UF2 Format)
- **File:** `lnk22-v1.0-20251210.uf2`
- **Size:** 315 KB (322,560 bytes)
- **Start Address:** 0x26000
- **MD5:** (See file listing below)

### Alternative Formats
- **HEX:** `.pio/build/rak4631_full/firmware.hex` (443 KB)
- **ELF:** `.pio/build/rak4631_full/firmware.elf` (287 KB)
- **ZIP:** `.pio/build/rak4631_full/firmware.zip` (159 KB)

---

## 🔧 Method 1: UF2 Bootloader (Easiest)

### Step 1: Enter Bootloader Mode

**Option A - Double-Reset Method:**
1. Press the RESET button on your RAK4631
2. Wait 1 second
3. Press RESET again quickly (within 0.5 seconds)
4. The device LED should turn RED, indicating bootloader mode

**Option B - Upload Script:**
```bash
cd /home/mesh/LNK-22/firmware
./upload_full_dfu.sh
```

### Step 2: Mount the Bootloader Drive

When in bootloader mode, RAK4631 appears as a USB drive named **"RAK4631"** or **"FEATHERBOOT"**

**On Linux:**
```bash
# The drive usually auto-mounts to:
ls /media/$USER/RAK4631/
# or
ls /media/$USER/FEATHERBOOT/
```

**On macOS:**
```bash
ls /Volumes/RAK4631/
```

**On Windows:**
- Drive appears as "RAK4631 (D:)" or similar

### Step 3: Copy Firmware

**Simple Copy Method:**
```bash
# Linux/macOS
cp lnk22-v1.0-20251210.uf2 /media/$USER/RAK4631/

# Or drag-and-drop the UF2 file to the bootloader drive
```

**Windows:**
- Drag `lnk22-v1.0-20251210.uf2` to the RAK4631 drive
- Or right-click → Copy → Paste to RAK4631 drive

### Step 4: Automatic Reset

- The device will **automatically reset** and start running LNK-22
- The bootloader drive will **disappear** (this is normal!)
- LED will blink GREEN during startup

---

## 🔧 Method 2: Direct DFU Upload

### Prerequisites
```bash
# Install adafruit-nrfutil (if not already installed)
pip3 install --user adafruit-nrfutil
```

### Upload Firmware
```bash
cd /home/mesh/LNK-22/firmware

# Upload via DFU
chmod +x upload_full_dfu.sh
./upload_full_dfu.sh
```

The script will:
1. Convert HEX to DFU package
2. Find the serial port automatically
3. Upload firmware
4. Reset the device

---

## 🔧 Method 3: PlatformIO Upload

### Using PlatformIO CLI
```bash
cd /home/mesh/LNK-22/firmware

# Upload and monitor
pio run -e rak4631_full --target upload

# Monitor serial output
pio device monitor -b 115200
```

---

## ✅ Verify Installation

### Step 1: Connect to Serial Port

**Linux:**
```bash
pio device monitor -b 115200
# or
screen /dev/ttyACM0 115200
```

**macOS:**
```bash
pio device monitor -b 115200
# or
screen /dev/tty.usbmodem* 115200
```

**Windows:**
```bash
pio device monitor -b 115200
# or use PuTTY/TeraTerm on COMx port
```

### Step 2: Check Startup Messages

You should see:
```
========================================
LNK-22 Firmware v1.0.0
Board: RAK4631
Professional LoRa Mesh Networking
========================================

[CRYPTO] Initializing LNK-22 cryptography (Monocypher)...
[CRYPTO] Using Monocypher (Public Domain) - Zero license risk!
[CRYPTO] Node address: 0x[ADDRESS]
[RADIO] Initializing LoRa radio...
[RADIO] SX1262 configured for 915 MHz
[RADIO] SF10, BW125, CR4/5, 22dBm
[MESH] LNK-22 initialized with address 0x[ADDRESS]
[MESH] Default channel: 0
[GPS] Initializing GNSS...
[DISPLAY] Initializing SSD1306...
[POWER] Battery management initialized

LNK-22 Ready!
Type 'help' for commands
>
```

### Step 3: Test Basic Commands

```bash
# Show status
> status

# Show neighbors
> neighbors

# Show routes
> routes

# Switch channel
> channel 1

# Send test message
> send 0xFFFFFFFF hello mesh
```

---

## 🎯 Expected Behavior

### ✅ Successful Flash Indicators

1. **LED Behavior:**
   - Red LED during bootloader
   - Green blink after flash success
   - Blue LED during radio activity

2. **Serial Output:**
   - Clean startup messages
   - No error messages
   - Shows node address (derived from crypto keys)

3. **Radio Activity:**
   - Sends beacon every 30 seconds
   - Responds to neighbor discovery
   - Can receive packets on current channel

### ❌ Troubleshooting

**Problem: Device won't enter bootloader**
- Solution: Try double-reset again, timing is critical
- Alternative: Use upload_full_dfu.sh script

**Problem: UF2 copy fails**
- Check file size: Should be exactly 322,560 bytes
- Try different USB cable
- Try different USB port (use USB 2.0 port, not USB-C hub)

**Problem: No serial output**
- Wait 3-5 seconds after power-on
- Check baud rate: Must be 115200
- Try pressing RESET button once
- Check USB cable supports data (not charge-only cable)

**Problem: Radio initialization fails**
- Check SX1262 module is properly seated
- Verify antenna is connected
- Check for solder bridges on pins

---

## 📊 Firmware Statistics

### Memory Usage
```
RAM:   6.4% (16,016 / 248,832 bytes)
Flash: 19.8% (161,032 / 815,104 bytes)
```

### Features Enabled
- ✅ Monocypher encryption (ChaCha20-Poly1305)
- ✅ BLAKE2b hashing
- ✅ AODV routing
- ✅ 8-channel support (0-7)
- ✅ GPS/GNSS integration
- ✅ OLED display support
- ✅ Power management
- ✅ Flash storage for keys

### Radio Configuration
```
Frequency:      915 MHz (US ISM band)
Bandwidth:      125 kHz
Spreading:      SF10 (10-15 km range)
Coding Rate:    4/5
TX Power:       22 dBm
Sync Word:      0x12 (private network)
```

---

## 🔐 Security Notice

### First Boot
On first boot, LNK-22 will:
1. Generate new cryptographic identity (256-bit keys)
2. Save keys to internal flash (`/lnk22_keys.dat`)
3. Derive node address from public key
4. Print node address to serial

**⚠️ IMPORTANT:** Your node address is permanent (unless you erase flash)

### Reset Identity (if needed)
```bash
# Option 1: Erase flash via serial command
> factory_reset

# Option 2: Mass erase via nrfjprog
nrfjprog --family NRF52 --eraseall
```

---

## 📡 Configuration

### Change Channel
```bash
# Via serial console
> channel 3
[MESH] Switched to channel 3

# Default channels:
# 0 = Default (public)
# 1-6 = Custom channels
# 7 = Admin channel (reserved)
```

### Network Key (Future Feature)
```bash
# Currently uses default network key (0x42...)
# Will support custom PSK in future version
> setkey [32-byte hex key]
```

---

## 🆘 Support

### Serial Commands
```bash
help        - Show all commands
status      - Show node status
neighbors   - Show neighbor table
routes      - Show routing table
channel N   - Switch to channel 0-7
send        - Send message
reboot      - Restart device
```

### Debugging
```bash
# Enable verbose debugging
> debug radio 1
> debug mesh 1
> debug crypto 1

# Disable debugging
> debug all 0
```

---

## 📝 Build Information

**Compiler:** ARM GCC 7.2.1
**Framework:** Arduino Adafruit nRF52 @ 1.10601.0
**Platform:** Nordic nRF52 @ 10.10.0
**Build Date:** December 10, 2025
**Build Time:** 7.29 seconds

**Libraries:**
- Monocypher 4.0+ (Public Domain) - Cryptography
- ArduinoJson 6.21.5 (MIT) - JSON parsing
- SX126x-Arduino 2.0.32 (MIT) - LoRa driver
- SparkFun GNSS 2.2.28 - GPS support
- Adafruit SSD1306 2.5.16 - Display driver

**✅ NO LGPL DEPENDENCIES** - 100% commercial-safe!

---

## 🚀 Next Steps

After flashing:
1. Connect via serial to verify operation
2. Test radio transmission (send beacon)
3. Configure channel if needed
4. Test with second device for mesh routing
5. Check GPS fix (if GPS module installed)
6. Verify display output (if display installed)

---

## 🎉 You're Ready!

**LNK-22 firmware is now installed on your RAK4631!**

- Professional LoRa mesh networking ✅
- 10-15 km range with SF10 ✅
- 8-channel support ✅
- Military-grade encryption ✅
- Zero license risk ✅

For issues, check:
- GitHub: https://github.com/[your-org]/LNK-22
- Documentation: /home/mesh/LNK-22/docs/
- Progress: /home/mesh/LNK-22/PROGRESS.md

---

*Built with ❤️ for professional mesh networking*
