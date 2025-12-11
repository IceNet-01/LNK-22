# LNK-22 Mesh Network - Quick Start Guide

**Get your mesh network up and running in 5 minutes!**

---

## 🎯 What You Have

A complete LoRa mesh networking system with:
- ✅ **Working firmware** - RadioLib-based, tested with 3 radios
- ✅ **Professional web interface** - Real-time monitoring and control
- ✅ **Autonomous operation** - No computer needed after setup
- ✅ **AODV routing** - Automatic multi-hop mesh networking

---

## 🚀 Quick Start

### 1. Flash Firmware to Radios

```bash
# Connect radio via USB
# Check which port it's on
ls /dev/ttyACM*

# Flash firmware (replace /dev/ttyACM0 with your port)
cd /home/mesh/LNK-22/firmware
platformio run -e rak4631_full -t upload --upload-port /dev/ttyACM0

# Wait for "SUCCESS" message
# Repeat for each radio
```

### 2. Deploy Radios

**Option A: Testing (All on Desk)**
- Power all 3 radios via USB
- They will auto-discover each other
- Beacons every 30 seconds

**Option B: Field Deployment**
- Radios run autonomously (no computer needed)
- Power via USB battery pack or solar
- Place radios with line-of-sight if possible
- SF10 gives ~10-15km range in open areas

### 3. Monitor with Web Interface

```bash
# Start the web server
cd /home/mesh/LNK-22/web-client
./start-server.sh

# Open in Chrome/Edge/Opera
# Go to: http://localhost:8080/index-enhanced.html

# Click "Connect Device"
# Select your LNK-22 radio

# Watch the mesh network live! 🎉
```

---

## 📊 What You'll See

### Dashboard Tab
- **Total packets** sent and received
- **Active neighbors** discovered
- **Live charts** of packet activity and signal quality

### Network Graph Tab
- **Visual mesh topology** showing all nodes
- **Drag nodes** to rearrange
- **Hover for details** (RSSI, SNR, hop count)

### Routing Table Tab
- **All routes** to reachable nodes
- **Hop count** for each route
- **Visual path** (This Node → Next Hop → Destination)

### Messages Tab
- **Send messages** to any node
- **Broadcast** to all nodes (use FFFFFFFF)
- **Message history** with timestamps

### GPS Map Tab
- **See node positions** on OpenStreetMap
- **Requires GPS fix** (may take 1-2 minutes)
- **Auto-centers** on your position

### Console Tab
- **Full serial access** to firmware
- **Type commands** directly
- **Command history** with up/down arrows

---

## 🎮 Try These Commands

In the web console, try:

```
status        - Show node info and statistics
neighbors     - List all discovered neighbors
routes        - Show routing table
beacon        - Send beacon packet now
radio         - Show radio configuration
help          - List all available commands

send 0xABCD1234 Hello!     - Send message to specific node
send FFFFFFFF Hi everyone! - Broadcast to all nodes
```

---

## 📡 Radio Addresses

Each radio has a unique address. Check yours with `status` command.

**Example from testing:**
- Radio 1: `0x1E34F1F2`
- Radio 2: `0x9B69311E`
- Radio 3: `0x1CE25673`

**Your radios will have different addresses** (based on chip ID).

---

## 🔍 Troubleshooting

### Radios not discovering neighbors
- **Wait 30-60 seconds** for first beacon cycle
- Check antennas are connected
- Try moving radios closer together
- Run `beacon` command to force a beacon

### Web interface won't connect
- Use **Chrome, Edge, or Opera** (not Firefox/Safari)
- Check radio is plugged in (`ls /dev/ttyACM*`)
- Try different USB port or cable
- Restart browser if needed

### No data in web interface
- Ensure **RadioLib firmware is flashed** (not old firmware)
- Check baud rate is 115200
- Try `status` command in console
- Disconnect and reconnect

### GPS not working
- **Wait 1-2 minutes** for initial satellite acquisition
- Ensure **antenna is connected**
- GPS works best **near window or outdoors**
- Check firmware has GPS enabled

---

## 📚 Documentation

- **COMPLETED_WORK.md** - Firmware implementation details
- **WEB_CLIENT_COMPLETE.md** - Web interface implementation
- **web-client/README.md** - Detailed web client usage guide
- **firmware/README.md** - Firmware documentation

---

## 🧪 Test Scenarios

### Test 1: Basic Communication (2 radios)
1. Flash 2 radios
2. Power both via USB
3. Connect web interface to one
4. Watch neighbors appear in ~30 seconds
5. See packet counts increase

### Test 2: Mesh Networking (3 radios)
1. Flash 3 radios
2. Power all three
3. Connect web interface to middle radio
4. Should see 2 neighbors
5. Watch beacons exchange

### Test 3: Message Sending
1. Connect to radio 1
2. Note address of radio 2 from neighbors
3. Send message: `send 0x[RADIO2] Hello!`
4. Check radio 2's serial output for message

### Test 4: Multi-Hop Routing (Advanced)
1. Place radios so R1 and R3 can't directly communicate
2. R2 should be in middle, reachable by both
3. Send message from R1 to R3
4. Watch R2 forward the packet
5. Check routing tables for multi-hop routes

---

## 🎯 Expected Performance

### Signal Quality
- **Excellent:** RSSI > -70 dBm, SNR > 5 dB
- **Good:** RSSI > -90 dBm, SNR > 0 dB
- **Fair:** RSSI > -110 dBm, SNR > -5 dB
- **Poor:** RSSI < -110 dBm, SNR < -5 dB

### Range (SF10, 22 dBm)
- **Indoor:** 100-500m (depends on walls)
- **Outdoor (urban):** 1-3 km
- **Outdoor (rural):** 5-15 km
- **Line-of-sight:** Up to 20 km

### Packet Rates
- **Beacons:** Every 30 seconds
- **Route discovery:** On-demand
- **Messages:** As sent
- **Typical traffic:** 2-4 packets/minute per radio

---

## 💡 Pro Tips

### Maximize Range
- Use **SF11 or SF12** for longer range (slower data rate)
- Ensure antennas are **vertically oriented**
- Place radios **high up** (roof, pole, hill)
- **Line-of-sight** is best for max range

### Improve Reliability
- Keep **firmware updated**
- Use **quality antennas** (not random wires)
- Ensure **good power supply** (not weak USB ports)
- Check for **loose connections**

### Debug Issues
- Always check **console output** first
- Use `status` command for quick health check
- Watch for `[ISR] OnRxDone!` (means RX working)
- RSSI values should be **> -120 dBm**

### Monitor Long-Term
- Leave web interface open to observe
- Watch for route table changes
- Monitor packet loss (check counts)
- GPS tracking over time (if enabled)

---

## 🎓 Understanding the System

### Mesh Networking Basics
1. **Beacons** announce presence to neighbors
2. **Neighbors** are nodes in direct radio range
3. **Routes** are paths to distant nodes via neighbors
4. **Forwarding** happens when intermediate nodes relay packets
5. **AODV** automatically discovers and maintains routes

### How Messages Flow
```
Radio 1 wants to send to Radio 3

Scenario A: Direct (Radio 3 is neighbor)
  R1 → R3 (direct transmission)

Scenario B: Multi-hop (Radio 3 not in range)
  R1 → R2 → R3 (R2 forwards packet)

Route discovery happens automatically!
```

### What Each Radio Does
- **Listens** for packets on LoRa frequency
- **Announces** itself via beacons
- **Discovers** neighbors from their beacons
- **Forwards** packets destined for others
- **Maintains** routing table
- **Reports** status via serial (if connected)

---

## 🏆 Success Criteria

You'll know everything is working when you see:

- ✅ Radios booting with **"LNK-22 starting..."**
- ✅ `[ISR] OnRxDone!` messages appearing
- ✅ Neighbors showing up in table
- ✅ RSSI/SNR values in reasonable range
- ✅ Beacon packets every ~30 seconds
- ✅ Web interface populating with data
- ✅ Network graph showing connections
- ✅ Messages successfully delivered

---

## 🚀 Ready to Deploy!

Your mesh network is **fully functional** and ready for:
- Emergency communications
- Remote sensing
- IoT connectivity
- Off-grid networking
- Research and experimentation

**Have fun exploring mesh networking!** 🎉

---

## 📞 Need Help?

1. Check **COMPLETED_WORK.md** for firmware details
2. Read **WEB_CLIENT_COMPLETE.md** for web interface help
3. Review console output for error messages
4. Check signal quality (RSSI/SNR)
5. Verify firmware version matches documentation

---

*Quick Start Guide - LNK-22 v1.0.0*
*Last Updated: December 10, 2024*
