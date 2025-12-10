# MeshNet

A modern, open-source mesh networking ecosystem for LoRa devices.

## Overview

MeshNet is a complete mesh networking solution designed for low-power, long-range communication using LoRa radios. Built from the ground up to be simple, secure, and extensible.

## Features

- **Long Range**: LoRa-based communication with ranges up to 10+ km
- **Mesh Networking**: Automatic message routing across multiple hops
- **Secure**: End-to-end encryption using modern cryptography
- **Low Power**: Optimized for battery-powered devices
- **Web-Based Tools**: Flash and configure devices directly from your browser
- **No Dependencies**: Clean, original implementation

## Architecture

### Protocol Stack

```
┌─────────────────────────────────┐
│      Application Layer          │  Messages, Telemetry, Commands
├─────────────────────────────────┤
│      Transport Layer            │  Reliable/Unreliable Delivery
├─────────────────────────────────┤
│      Network Layer              │  Mesh Routing, Path Discovery
├─────────────────────────────────┤
│      MAC Layer                  │  Medium Access Control
├─────────────────────────────────┤
│      Physical Layer             │  LoRa Radio (SX126x)
└─────────────────────────────────┘
```

## Components

### 1. Firmware (`/firmware`)
- **Target Hardware**: WisBlock (RAK4631, RAK11200, etc.)
- **Framework**: Arduino/PlatformIO
- **Features**: Mesh routing, encryption, power management

### 2. Web Flasher (`/web-flasher`)
- Flash firmware directly from browser using Web Serial API
- No drivers or software installation required
- Built with modern web technologies

### 3. Web Client (`/web-client`)
- Communicate with devices via USB/Serial
- Send and receive messages
- Configure device settings
- View mesh network topology

## Quick Start

### 1. Flash Firmware to Device

```bash
cd firmware
platformio run --target upload
```

Or use the web flasher:
1. Open `web-flasher/index.html` in Chrome/Edge
2. Connect your WisBlock device via USB
3. Click "Connect" and select your device
4. Click "Flash Firmware"

### 2. Connect via Web Client

1. Open `web-client/index.html` in your browser
2. Click "Connect Device"
3. Select your serial port
4. Start sending messages!

## Development

### Prerequisites
- Python 3.8+
- Node.js 18+
- PlatformIO Core

### Firmware Development

```bash
cd firmware

# Build for RAK4631 (nRF52840 + SX1262)
platformio run -e rak4631

# Build for RAK11200 (ESP32 + SX1262)
platformio run -e rak11200

# Upload to device
platformio run -e rak4631 --target upload

# Monitor serial output
platformio device monitor
```

### Web Applications

```bash
# Web Flasher
cd web-flasher
npm install
npm run dev

# Web Client
cd web-client
npm install
npm run dev
```

## Protocol

See [PROTOCOL.md](PROTOCOL.md) for detailed protocol specification including:
- Packet formats
- Routing algorithm
- Encryption scheme
- MAC layer design
- Power management

## Hardware Support

### Currently Supported
- RAK4631 (nRF52840 + SX1262)
- RAK11200 (ESP32 + SX1262)

### Planned Support
- RAK3172 (STM32WL)
- RAK11310 (RP2040 + SX1262)
- Heltec LoRa32 V3
- LilyGO T-Beam
- Custom WisBlock combinations

## Project Structure

```
MeshNet/
├── firmware/                 # Device firmware
│   ├── src/
│   │   ├── main.cpp         # Main application
│   │   ├── protocol/        # Protocol implementation
│   │   ├── radio/           # LoRa radio drivers
│   │   ├── crypto/          # Encryption/signing
│   │   └── mesh/            # Mesh routing
│   ├── lib/                 # Libraries
│   └── platformio.ini       # Build configuration
├── web-flasher/             # Web-based firmware flasher
│   ├── src/
│   ├── public/
│   └── package.json
├── web-client/              # Web-based device client
│   ├── src/
│   ├── public/
│   └── package.json
├── protocol/                # Protocol specifications
│   └── spec.md
├── docs/                    # Documentation
└── tools/                   # Development tools
```

## License

MIT License - see LICENSE file

## Contributing

Contributions welcome! Please read CONTRIBUTING.md first.

## Roadmap

- [x] Protocol specification
- [x] Project structure
- [ ] Core firmware implementation
- [ ] LoRa radio driver
- [ ] Mesh routing
- [ ] Encryption layer
- [ ] Web flasher
- [ ] Web client
- [ ] Mobile apps
- [ ] Desktop apps
