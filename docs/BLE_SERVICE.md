# LNK-22 BLE Service Specification

This document defines the Bluetooth Low Energy (BLE) GATT service for the LNK-22 LoRa mesh networking device.

## Service Overview

The LNK-22 BLE service enables iOS and Android applications to communicate with the LNK-22 radio device over Bluetooth Low Energy. The service provides capabilities for:

- Sending and receiving mesh messages
- Device configuration and control
- Network status monitoring
- GPS position sharing

## Service UUID

**LNK-22 Service UUID:** `4C4E4B32-0001-1000-8000-00805F9B34FB`

The base UUID `4C4E4B32` is derived from "LNK2" in ASCII (0x4C4E4B32).

## Characteristics

### Message RX (Write)
- **UUID:** `4C4E4B32-0002-1000-8000-00805F9B34FB`
- **Properties:** Write, Write Without Response
- **Description:** Send messages to the mesh network

**Format:**
```
[type:1][destination:4][channel:1][payload:N]
```
- `type` (1 byte): Message type (0x01=text, 0x02=position, 0x03=sensor, 0x04=command, 0x05=file)
- `destination` (4 bytes, little-endian): Destination address (0xFFFFFFFF for broadcast)
- `channel` (1 byte): Channel ID (0-7)
- `payload` (variable): Message content (UTF-8 for text)

### Message TX (Notify)
- **UUID:** `4C4E4B32-0003-1000-8000-00805F9B34FB`
- **Properties:** Notify
- **Description:** Receive messages from the mesh network

**Format:**
```
[type:1][source:4][channel:1][timestamp:4][payload:N]
```
- `type` (1 byte): Message type
- `source` (4 bytes, little-endian): Source node address
- `channel` (1 byte): Channel ID
- `timestamp` (4 bytes, little-endian): Unix timestamp
- `payload` (variable): Message content

### Command (Write)
- **UUID:** `4C4E4B32-0004-1000-8000-00805F9B34FB`
- **Properties:** Write
- **Description:** Send commands to the device

**Command Codes:**
| Code | Command | Parameters |
|------|---------|------------|
| 0x01 | Send Beacon | None |
| 0x02 | Switch Channel | channel (1 byte) |
| 0x03 | Set TX Power | power (1 byte, signed) |
| 0x10 | Request Status | None |
| 0x11 | Request Neighbors | None |
| 0x12 | Request Routes | None |
| 0x20 | Clear Routes | None |
| 0xFE | Reboot | None |
| 0xFF | Factory Reset | None |

### Status (Read/Notify)
- **UUID:** `4C4E4B32-0005-1000-8000-00805F9B34FB`
- **Properties:** Read, Notify
- **Description:** Device status information

**Format (24 bytes):**
```
[nodeAddr:4][txCount:4][rxCount:4][neighborCount:2][routeCount:2]
[channel:1][txPower:1][battery:1][flags:1][uptime:4]
```
- `nodeAddr` (4 bytes): Node address
- `txCount` (4 bytes): Transmitted packet count
- `rxCount` (4 bytes): Received packet count
- `neighborCount` (2 bytes): Number of neighbors
- `routeCount` (2 bytes): Number of routes
- `channel` (1 byte): Current channel
- `txPower` (1 byte, signed): TX power in dBm
- `battery` (1 byte): Battery percentage (0-100)
- `flags` (1 byte): Feature flags
  - Bit 0: Encryption enabled
  - Bit 1: GPS enabled
  - Bit 2: Display enabled
- `uptime` (4 bytes): Uptime in seconds

### Neighbors (Read/Notify)
- **UUID:** `4C4E4B32-0006-1000-8000-00805F9B34FB`
- **Properties:** Read, Notify
- **Description:** Neighbor list

**Format (16 bytes per neighbor):**
```
[address:4][rssi:2][snr:1][quality:1][lastSeen:4][packetCount:4]
```

### Routes (Read/Notify)
- **UUID:** `4C4E4B32-0007-1000-8000-00805F9B34FB`
- **Properties:** Read, Notify
- **Description:** Routing table

**Format (14 bytes per route):**
```
[destination:4][nextHop:4][hopCount:1][quality:1][timestamp:4]
```

### Configuration (Read/Write)
- **UUID:** `4C4E4B32-0008-1000-8000-00805F9B34FB`
- **Properties:** Read, Write
- **Description:** Device configuration

**Format (12 bytes):**
```
[channel:1][txPower:1][sf:1][reserved:1][bandwidth:4][flags:1][beaconInterval:2][reserved:1]
```

### GPS Position (Read/Notify)
- **UUID:** `4C4E4B32-0009-1000-8000-00805F9B34FB`
- **Properties:** Read, Notify
- **Description:** Current GPS position

**Format (22 bytes):**
```
[latitude:8][longitude:8][altitude:4][satellites:1][valid:1]
```

## Connection Parameters

Recommended connection parameters for optimal performance:
- **Connection Interval:** 15-30 ms
- **Slave Latency:** 0
- **Supervision Timeout:** 4000 ms
- **MTU:** 247 bytes (for iOS) / 512 bytes (for Android)

## Security

The BLE service supports bonding with MITM protection using numeric comparison or passkey entry. Encryption is required for all characteristics.

## Advertising

The device advertises with:
- **Local Name:** "LNK-22" or "LNK-22-XXXX" (last 4 hex digits of node address)
- **Service UUID:** Complete list containing the LNK-22 service UUID
- **Flags:** General Discoverable, BR/EDR Not Supported
- **TX Power Level:** Included for proximity detection

## Implementation Notes

### iOS
- Uses CoreBluetooth framework
- Background mode: `bluetooth-central` required in Info.plist
- State restoration supported for background connections

### Android
- Uses Android Bluetooth API (API level 21+)
- Companion device pairing supported (API level 26+)
- Foreground service required for reliable background operation

## Error Handling

Write operations return ATT error codes:
- `0x00`: Success
- `0x03`: Write Not Permitted
- `0x06`: Request Not Supported
- `0x07`: Invalid Offset
- `0x0D`: Invalid Attribute Length
- `0x80`: Device Busy
- `0x81`: Invalid Command
- `0x82`: Buffer Full
