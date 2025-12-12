# LNK-22 Development Status

**Last Updated:** 2025-12-11

## Current State

### Firmware (v1.8.0)
- **Hybrid TDMA/CSMA-CA MAC Layer** - Fully implemented
  - Time source election: GPS(4) > NTP(3) > Serial(2) > Synced(1) > Crystal(0)
  - Frame structure: 1 second frames, 10 slots x 100ms each
  - Automatic fallback to CSMA-CA when time not synced (stratum >= 15)
  - PKT_TIME_SYNC (0x0A) packets for network time propagation

- **Display** - 8 pages implemented
  1. Info (node ID, firmware version)
  2. Network (neighbors, routes, TX/RX counts)
  3. Neighbors (list with signal quality)
  4. Signal (RSSI, SNR)
  5. GPS (coordinates, satellites)
  6. Messages (last received)
  7. Battery (voltage, percent)
  8. **MAC** (TDMA/CSMA mode, time source, frame/slot, TX stats)

- **BLE Service** - Full mesh relay capability
  - Message TX/RX characteristics
  - Status, neighbors, routes reporting
  - Node naming support

### iOS App
- **Standalone Mesh Mode** - Default mode on launch
  - Phone-to-phone BLE mesh networking
  - Auto-discovers both phones AND LNK-22 radios
  - Auto-connects to radios for message relay
  - Shows peers with 📻 (radio) or 📱 (phone) indicators

- **Network View** - Works in standalone mode
  - StandaloneStatusCard with node address and peer counts
  - StandalonePeersListView showing discovered devices
  - StandaloneStatsView with mesh statistics

- **Connection Behavior**
  - Auto-enters standalone mode on app launch
  - Exits standalone mode when connecting to a specific radio
  - Returns to standalone mode when disconnecting

### Web Client
- **TDMA/MAC Tab** - 10-slot visualization with time sync controls
- **ARP/Neighbor Table** - Signal quality indicators
- **Radio Status Tab** - Shows TDMA vs CSMA-CA mode

## Known Issues / TODO

1. **Message Relay** - Phone app sends to radios but receiving needs more testing
2. **Node Naming** - Plan exists in `.claude/plans/` but not yet implemented
3. **Store & Forward** - Models exist but not fully implemented

## Hardware Setup

Three RAK4631 radios connected via USB:
- `/dev/ttyACM0` - Radio 1
- `/dev/ttyACM1` - Radio 2
- `/dev/ttyACM2` - Radio 3

## Build & Flash Commands

See `CLAUDE_REFERENCE.md` for detailed commands.

## Recent Commits

- `3277025` - Fix optional String unwrapping in NetworkView
- `eb14a72` - Fix BLE message handling in standalone mesh mode
- `9a26511` - Add standalone mode views to NetworkView
- `650af79` - Fix standalone mesh mode to discover BLE radios
- `9d826ae` - Add standalone mesh mode UI, auto-enable by default
- `1f8284e` - Add MAC/TDMA status display page
- `a8f326c` - Add hybrid TDMA/CSMA-CA MAC layer, web client enhancements, iOS standalone mesh
