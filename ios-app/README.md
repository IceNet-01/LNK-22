# LNK-22 iOS App

Official iOS application for controlling LNK-22 LoRa mesh networking radios over Bluetooth Low Energy.

## Features

- **Bluetooth Connectivity**: Connect to LNK-22 radios via BLE
- **Messaging**: Send and receive mesh network messages
- **Network Monitoring**: View neighbors, routes, and signal quality
- **Map View**: See GPS positions of mesh nodes on a map
- **Device Configuration**: Configure radio settings, channels, and encryption
- **Multi-Channel Support**: Switch between 8 different communication channels

## Requirements

- iOS 17.0 or later
- iPhone with Bluetooth Low Energy support
- LNK-22 radio with BLE firmware

## Installation

### From Xcode

1. Open `LNK22.xcodeproj` in Xcode 15 or later
2. Select your development team in the project settings
3. Build and run on your device

### TestFlight

Coming soon.

## App Structure

```
LNK22/
├── LNK22App.swift          # App entry point
├── ContentView.swift       # Main tab navigation
├── Services/
│   └── BluetoothManager.swift    # BLE communication layer
├── Models/
│   └── Models.swift              # Data models
├── ViewModels/
│   └── MeshNetworkViewModel.swift # Network state management
├── Views/
│   ├── MessagesView.swift        # Message send/receive UI
│   ├── NetworkView.swift         # Network status & stats
│   ├── MapView.swift             # GPS map view
│   ├── DevicesView.swift         # BLE device discovery
│   └── SettingsView.swift        # Configuration
└── Assets.xcassets/              # App icons and images
```

## Usage

### Connecting to a Radio

1. Ensure your LNK-22 radio is powered on
2. Open the app and go to the **Devices** tab
3. Tap **Scan for Devices**
4. Select your radio from the list
5. Wait for the connection to complete

### Sending Messages

1. Go to the **Messages** tab
2. Select a destination (Broadcast or specific node)
3. Type your message and tap Send

### Viewing Network Status

The **Network** tab shows:
- Device status (address, TX/RX counts, battery)
- Neighbor list with signal strength
- Routing table with hop counts

### Map View

The **Map** tab displays:
- GPS positions of all nodes in the mesh
- Your current location
- Online/offline status of nodes

### Configuration

In **Settings**, you can:
- Configure radio parameters (TX power, spreading factor)
- Change channels
- Enable/disable encryption and GPS
- View device diagnostics

## BLE Service

The app communicates with the radio using a custom BLE GATT service:

- **Service UUID**: `4C4E4B32-0001-1000-8000-00805F9B34FB`
- **Message RX**: Write messages to send
- **Message TX**: Notifications for received messages
- **Status**: Device status and statistics
- **Neighbors**: Neighbor list with RSSI/SNR
- **Routes**: Routing table
- **Config**: Device configuration

See [BLE_SERVICE.md](../docs/BLE_SERVICE.md) for the full specification.

## Permissions

The app requires the following permissions:

- **Bluetooth**: To communicate with LNK-22 radios
- **Location** (optional): To display your position on the map

## Troubleshooting

### Radio Not Found

1. Ensure the radio is powered on
2. Check that Bluetooth is enabled on your iPhone
3. Move closer to the radio (within 10 meters)
4. Restart the radio

### Connection Drops

1. Stay within range of the radio
2. Reduce interference from other Bluetooth devices
3. Check the radio's battery level
4. Try disconnecting and reconnecting

### Messages Not Sending

1. Verify you're connected to the radio
2. Check that the destination is reachable
3. Ensure you're on the correct channel

## Development

### Building

```bash
# Open in Xcode
open LNK22.xcodeproj

# Or build from command line
xcodebuild -project LNK22.xcodeproj -scheme LNK22 -configuration Debug
```

### Testing

The app includes SwiftUI previews for rapid development. Use Xcode's canvas to preview views without running on a device.

### Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Submit a pull request

## License

MIT License - see [LICENSE](../LICENSE) for details.

## Related

- [LNK-22 Firmware](../firmware/) - Radio firmware
- [Web Client](../web-client/) - Browser-based interface
- [Protocol Specification](../PROTOCOL.md) - Mesh protocol details
