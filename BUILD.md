# MeshNet Build Guide

Complete guide to building and deploying the MeshNet mesh networking ecosystem.

## Prerequisites

### Required Software
- **Python 3.8+** (for PlatformIO)
- **Node.js 18+** (for web applications)
- **PlatformIO Core** (for firmware compilation)
- **Git** (for source control)

### Install PlatformIO

```bash
# Using pip
pip install platformio

# Or using the installer script
curl -fsSL https://raw.githubusercontent.com/platformio/platformio-core-installer/master/get-platformio.py -o get-platformio.py
python3 get-platformio.py

# Verify installation
pio --version
```

## Building Firmware

### 1. Navigate to firmware directory

```bash
cd firmware
```

### 2. Build for your target board

```bash
# RAK4631 (nRF52840 + SX1262) - Primary target
platformio run -e rak4631

# RAK11200 (ESP32 + SX1262)
platformio run -e rak11200

# Heltec LoRa32 V3 (ESP32-S3 + SX1262)
platformio run -e heltec_lora32_v3

# RAK3172 (STM32WL - built-in LoRa)
platformio run -e rak3172
```

### 3. Flash to device

```bash
# Connect your device via USB, then:
platformio run -e rak4631 --target upload

# Monitor serial output
platformio device monitor
```

### Build Outputs

Compiled firmware will be in:
- RAK4631: `.pio/build/rak4631/firmware.hex`
- RAK11200: `.pio/build/rak11200/firmware.bin`
- Heltec V3: `.pio/build/heltec_lora32_v3/firmware.bin`

## Web Applications

### Web Flasher

```bash
cd web-flasher

# Install dependencies
npm install

# Development server (with hot reload)
npm run dev

# Production build
npm run build

# Preview production build
npm run preview
```

Access at: `http://localhost:3000`

### Web Client

```bash
cd web-client

# Install dependencies
npm install

# Development server
npm run dev

# Production build
npm run build
```

Access at: `http://localhost:3001`

## Testing

### Unit Tests (Firmware)

```bash
cd firmware
platformio test -e rak4631
```

### Integration Testing

1. Flash firmware to at least 2 devices
2. Open Web Client
3. Connect to one device
4. Send test messages
5. Verify routing and mesh functionality

## Troubleshooting

### Firmware Build Issues

**Problem**: `Library not found`
```bash
# Clean and rebuild
platformio run -e rak4631 --target clean
platformio run -e rak4631
```

**Problem**: `Upload failed`
```bash
# List serial ports
platformio device list

# Specify port manually
platformio run -e rak4631 --target upload --upload-port /dev/ttyUSB0
```

### Web Application Issues

**Problem**: `Module not found`
```bash
# Delete node_modules and reinstall
rm -rf node_modules package-lock.json
npm install
```

**Problem**: Web Serial API not available
- Use Chrome, Edge, or Opera (90+)
- HTTPS required (or localhost for development)

## Production Deployment

### Firmware

1. Build optimized firmware:
   ```bash
   platformio run -e rak4631 --target clean
   platformio run -e rak4631
   ```

2. Flash to all devices
3. Configure network keys (if using custom keys)

### Web Applications

1. Build production bundles:
   ```bash
   cd web-flasher && npm run build
   cd ../web-client && npm run build
   ```

2. Deploy to web server:
   ```bash
   # web-flasher/dist -> https://your-domain.com/flasher
   # web-client/dist -> https://your-domain.com/client
   ```

3. Ensure HTTPS is enabled (required for Web Serial API)

## Continuous Integration

Example GitHub Actions workflow:

```yaml
name: Build Firmware

on: [push, pull_request]

jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      - uses: actions/setup-python@v4
        with:
          python-version: '3.x'
      - name: Install PlatformIO
        run: pip install platformio
      - name: Build RAK4631
        run: cd firmware && platformio run -e rak4631
      - name: Build RAK11200
        run: cd firmware && platformio run -e rak11200
```

## Custom Configuration

### Changing Network Key

Edit `firmware/src/crypto/crypto.cpp`:

```cpp
// In generateOrLoadKeys():
memset(networkKey, 0x42, KEY_SIZE);  // Change this
```

### Adjusting Radio Parameters

Edit `firmware/src/config.h`:

```cpp
#define LORA_FREQUENCY 915000000     // Change frequency
#define LORA_SPREADING_FACTOR 7      // Adjust SF (7-12)
#define LORA_TX_POWER 20             // Adjust TX power
```

### Modifying Timeouts

```cpp
#define BEACON_INTERVAL 30000        // Beacon every 30s
#define ROUTE_TIMEOUT 300000         // Routes expire after 5min
```

## Hardware Setup

### RAK4631 (WisBlock nRF52840)

1. **Core**: RAK4631
2. **Base**: RAK5005-O or RAK19007
3. **GPS** (optional): RAK1910 or RAK12500
4. **Power**: LiPo battery (3.7V, 2000mAh+)
5. **Antenna**: LoRa antenna (915/868 MHz)

### RAK11200 (WisBlock ESP32)

1. **Core**: RAK11200
2. **Base**: RAK5005-O
3. **GPS** (optional): RAK1910
4. **Power**: LiPo battery
5. **Antenna**: LoRa antenna

### Heltec LoRa32 V3

1. Heltec WiFi LoRa 32 V3 board
2. Built-in display
3. External GPS module (optional)
4. LiPo battery
5. LoRa antenna

## Performance Tuning

### Optimize for Range

```cpp
#define LORA_SPREADING_FACTOR 12     // Maximum range
#define LORA_BANDWIDTH 125000        // Narrower bandwidth
#define LORA_TX_POWER 20             // Maximum power
```

Range: 10-20 km line-of-sight

### Optimize for Speed

```cpp
#define LORA_SPREADING_FACTOR 7      // Minimum latency
#define LORA_BANDWIDTH 500000        // Wider bandwidth
#define LORA_TX_POWER 14             // Lower power
```

Throughput: ~50 kbps

### Optimize for Battery Life

```cpp
#define BEACON_INTERVAL 60000        // Reduce beacon frequency
// Enable duty cycling in power.cpp
powerManager.setDutyCycling(true, 30000, 1000);  // 30s sleep, 1s wake
```

Battery life: 7+ days on 2000mAh

## Next Steps

- Read [PROTOCOL.md](PROTOCOL.md) for protocol details
- Check [README.md](README.md) for usage guide
- Join the community and contribute!
