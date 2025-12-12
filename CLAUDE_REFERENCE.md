# Claude Reference - LNK-22 Development

This file contains reference information for Claude to use when resuming development sessions.

## Project Structure

```
/home/mesh/LNK-22/
├── firmware/              # PlatformIO project for RAK4631
│   ├── platformio.ini     # Build configuration
│   └── src/
│       ├── main.cpp       # Main application
│       ├── radio/         # LoRa radio (RadioLib)
│       ├── mesh/          # AODV mesh networking
│       ├── mac/           # Hybrid TDMA/CSMA-CA
│       ├── display/       # OLED display (8 pages)
│       ├── ble/           # BLE service + relay
│       ├── crypto/        # ChaCha20-Poly1305
│       ├── gps/           # GPS support
│       └── naming/        # Node naming (planned)
├── ios-app/               # SwiftUI iOS/macOS app
│   └── LNK22/
│       ├── Services/      # BluetoothManager.swift
│       ├── Views/         # SwiftUI views
│       └── Models/        # Data models
└── web-client/            # Web serial client
    ├── index.html
    └── src/
        ├── main-enhanced.js
        └── style-enhanced.css
```

## Building Firmware

```bash
# Build only
cd /home/mesh/LNK-22/firmware
pio run

# Build output location
.pio/build/rak4631_full/firmware.hex
.pio/build/rak4631_full/firmware.uf2
```

## Flashing Firmware via Serial

The radios are RAK4631 (nRF52840) connected via USB-serial.

### Method 1: PlatformIO Upload (Recommended)

```bash
# Flash single radio
cd /home/mesh/LNK-22/firmware
pio run -t upload --upload-port /dev/ttyACM0

# Flash all three radios (run separately or in parallel)
pio run -t upload --upload-port /dev/ttyACM0
pio run -t upload --upload-port /dev/ttyACM1
pio run -t upload --upload-port /dev/ttyACM2
```

### Method 2: adafruit-nrfutil (if PlatformIO fails)

```bash
# Install if needed
pip install adafruit-nrfutil

# Flash using the ZIP package
adafruit-nrfutil dfu serial --package .pio/build/rak4631_full/firmware.zip -p /dev/ttyACM0 -b 115200
```

### Serial Monitor

```bash
# Monitor single radio
pio device monitor --port /dev/ttyACM0 --baud 115200

# Or with screen
screen /dev/ttyACM0 115200
```

## Serial Commands (Firmware)

```
help                    - Show all commands
status                  - Show device status
neighbors               - List mesh neighbors
routes                  - Show routing table
send <addr> <msg>       - Send message to address (hex or name)
broadcast <msg>         - Send broadcast message
channel <0-7>           - Switch channel
time <unix_timestamp>   - Set time from serial (for TDMA sync)
mac                     - Show MAC layer status
beacon                  - Send beacon
name                    - Show/set node name (if implemented)
```

## Time Sync for TDMA

To enable TDMA mode, the radio needs a time source. From serial:

```bash
# Send Unix timestamp to radio
echo "time $(date +%s)" > /dev/ttyACM0
```

Or via web client "Set Time from Host" button.

## iOS App Notes

### Build Requirements
- Xcode 15+
- iOS 17+ / macOS 14+
- `import UIKit` wrapped in `#if canImport(UIKit) && !os(macOS)`

### Key Classes
- `BluetoothManager` - Central BLE manager, handles both radio connections and standalone mesh
- `StandaloneMeshPeer` - Represents a discovered phone or radio in standalone mode
- `MeshMessage` - Message data model

### Standalone Mode
- Default mode on launch
- Scans for ALL BLE devices (not just specific UUIDs)
- Auto-connects to LNK-22 radios and subscribes to message notifications
- `sendMessage()` routes through `sendStandaloneMeshMessage()` when in standalone mode

## Web Client

```bash
# Start local server
cd /home/mesh/LNK-22/web-client
python3 -m http.server 8080

# Access at http://localhost:8080
```

Uses Web Serial API to connect to radios directly from browser.

## Common Issues

### "LNK-22 service not found"
The BLE service UUID must match between firmware and app:
- Firmware: `ble_service.h` defines UUIDs
- iOS: `LNK22BLEService` struct in `BluetoothManager.swift`

### Radio not discovered
- Check radio is advertising (should show "LNK-22-XXXX" name)
- In standalone mode, app scans for all devices with `nil` service filter

### Messages not received
- Verify subscribed to `messageTxUUID` notifications
- Check `didUpdateValueFor` handles the characteristic

### Build errors - optional unwrapping
Swift optionals need `??` or `if let` unwrapping:
```swift
// Wrong: peer.nodeName.contains("...")
// Right: (peer.nodeName ?? "").contains("...")
```

## Git Workflow

```bash
# Check status
git status

# Stage and commit
git add <files>
git commit -m "Message

🤖 Generated with [Claude Code](https://claude.com/claude-code)

Co-Authored-By: Claude Opus 4.5 <noreply@anthropic.com>"

# Push
git push origin main
```
