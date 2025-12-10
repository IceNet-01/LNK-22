# MeshNet Quick Start Guide

Get your MeshNet mesh network up and running in minutes!

## 1. Flash Your First Device

### Option A: Use Web Flasher (Easiest)

1. Build the firmware:
   ```bash
   cd firmware
   platformio run -e rak4631
   ```

2. Open `web-flasher/index.html` in Chrome/Edge
3. Click "Connect Device"
4. Select your serial port
5. Click "Flash Firmware"

### Option B: Use Command Line

```bash
cd firmware
platformio run -e rak4631 --target upload
```

## 2. Verify Device is Working

Open serial monitor:

```bash
platformio device monitor
```

You should see:
```
========================================
MeshNet Firmware v1.0.0
Board: RAK4631
========================================

[CRYPTO] Initializing cryptography...
[CRYPTO] Node address: 0x12345678
[RADIO] Initializing LoRa radio...
[RADIO] Radio initialized successfully
[MESH] Initializing mesh network...
[MESH] Mesh network initialized
[GPS] Initializing GPS module...
[SYSTEM] Boot complete. Ready to mesh!
```

## 3. Send Your First Message

In the serial monitor, type:

```
send FFFFFFFF Hello MeshNet!
```

This broadcasts "Hello MeshNet!" to all nodes.

## 4. Flash a Second Device

Repeat step 1 with a second device. The two devices will automatically discover each other and form a mesh network!

## 5. Use the Web Client

1. Start the web client:
   ```bash
   cd web-client
   npm install
   npm run dev
   ```

2. Open `http://localhost:3001` in your browser

3. Click "Connect" and select your device

4. Send messages via the UI!

## Common Commands

### Serial Commands

| Command | Description |
|---------|-------------|
| `help` | Show all commands |
| `status` | Show device status |
| `routes` | Show routing table |
| `neighbors` | Show neighbor list |
| `send <addr> <msg>` | Send message to address |

### Examples

```bash
# Check status
status

# Send to specific node (replace with actual address)
send 0x12345678 Hello Node!

# Broadcast to all nodes
send FFFFFFFF Broadcasting!

# Show neighbors
neighbors

# Show routes
routes
```

## Troubleshooting

### Device Won't Connect

1. Check USB cable (must support data, not just charging)
2. Install drivers if needed (CH340, CP2102, etc.)
3. Try different USB port
4. Check device appears in: `platformio device list`

### No Radio Communication

1. Check antenna is connected
2. Verify frequency matches (915 MHz US, 868 MHz EU)
3. Check devices are within range (start with <100m)
4. Ensure both devices have compatible firmware

### GPS Not Working

1. GPS requires outdoor use or clear sky view
2. Allow 30-60 seconds for first fix
3. Check GPS module is properly connected
4. Verify GPS is enabled for your board in `platformio.ini`

## What's Next?

### Build a Larger Network

Flash 3+ devices to test multi-hop routing:

```
Device A <---> Device B <---> Device C
```

Messages from A to C will automatically route through B!

### Customize Your Network

Edit `firmware/src/config.h` to change:
- Radio frequency
- TX power
- Beacon interval
- Routing parameters

### View GPS Positions

1. Connect GPS module to devices
2. Wait for GPS fix (outdoor)
3. Open web client Map tab
4. See node positions in real-time!

### Enable Encryption

Encryption is enabled by default! Change the network key in `firmware/src/crypto/crypto.cpp`:

```cpp
memset(networkKey, 0x42, KEY_SIZE);  // Your custom key
```

### Add More Nodes

The network supports:
- 100+ nodes
- 32 routes per node
- 16 neighbors per node
- 5-15 km range (depending on settings)

## Performance Tips

**Maximum Range**:
```cpp
// In config.h
#define LORA_SPREADING_FACTOR 12
#define LORA_TX_POWER 20
```

**Maximum Speed**:
```cpp
#define LORA_SPREADING_FACTOR 7
#define LORA_BANDWIDTH 500000
```

**Battery Optimization**:
```cpp
#define BEACON_INTERVAL 60000  // Less frequent beacons
// Enable duty cycling
```

## Example Network Scenarios

### Scenario 1: Farm Monitoring
- 5 nodes across 50-acre farm
- SF10, 20dBm TX power
- 30s beacon interval
- GPS on all nodes
- Range: ~2 km per hop

### Scenario 2: Urban Mesh
- 20 nodes across neighborhood
- SF7, 14dBm TX power
- 15s beacon interval
- Mix of stationary and mobile nodes
- Range: ~500m per hop

### Scenario 3: Hiking Group
- 8 nodes (hikers + base camp)
- SF9, 17dBm TX power
- GPS on all nodes
- 60s position broadcasts
- Range: ~5 km line-of-sight

## Getting Help

- Read [BUILD.md](BUILD.md) for detailed build instructions
- Check [PROTOCOL.md](PROTOCOL.md) for protocol details
- Review [README.md](README.md) for full documentation

Happy meshing! 🎉
