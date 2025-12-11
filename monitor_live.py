#!/usr/bin/env python3
"""Live monitor for mesh routing activity"""
import serial
import time
from datetime import datetime

def timestamp():
    return datetime.now().strftime("%H:%M:%S")

print("=" * 70)
print("LNK-22 Live Mesh Monitor")
print("=" * 70)

try:
    radio = serial.Serial("/dev/ttyACM1", 115200, timeout=0.1)
    time.sleep(1)

    # Get radio info
    radio.write(b"status\n")
    time.sleep(1)

    print("\n[INIT] Getting radio status...")
    addr = None
    while radio.in_waiting:
        line = radio.readline().decode('utf-8', errors='ignore').strip()
        if "Node Address:" in line:
            addr = line.split("0x")[1]
            print(f"[INIT] Monitoring Radio: 0x{addr}")
        elif "Neighbors:" in line:
            print(f"[INIT] {line}")
        elif "Routes:" in line:
            print(f"[INIT] {line}")

    # Check neighbors
    radio.write(b"neighbors\n")
    time.sleep(1)

    print("\n[INIT] Neighbor table:")
    while radio.in_waiting:
        line = radio.readline().decode('utf-8', errors='ignore').strip()
        if line and not line.startswith("[GPS]") and not line.startswith("[ISR]"):
            print(f"       {line}")

    print("\n" + "=" * 70)
    print("LIVE MESH ACTIVITY (Press Ctrl+C to stop)")
    print("=" * 70 + "\n")

    # Monitor continuously
    while True:
        if radio.in_waiting:
            line = radio.readline().decode('utf-8', errors='ignore').strip()
            if not line:
                continue

            # Filter out GPS spam
            if "[GPS]" in line:
                continue

            # Highlight important events
            if "ROUTE_REQ" in line:
                print(f"[{timestamp()}] 🔍 ROUTE DISCOVERY: {line}")
            elif "ROUTE_REP" in line:
                print(f"[{timestamp()}] ✅ ROUTE REPLY: {line}")
            elif "Forward" in line:
                print(f"[{timestamp()}] 📡 FORWARDING: {line}")
            elif "MESSAGE from" in line or "📨" in line:
                print(f"[{timestamp()}] 💬 MESSAGE: {line}")
            elif "Beacon from" in line:
                print(f"[{timestamp()}] 📍 BEACON: {line}")
            elif "ISR" in line and "OnRxDone" in line:
                print(f"[{timestamp()}] 📥 RX: {line}")
            elif "TX:" in line:
                print(f"[{timestamp()}] 📤 TX: {line}")
            elif "ACK" in line:
                print(f"[{timestamp()}] ✔️  ACK: {line}")
            elif "Neighbor discovered" in line:
                print(f"[{timestamp()}] 👋 NEW NEIGHBOR: {line}")
            elif "route to" in line or "Route" in line:
                print(f"[{timestamp()}] 🗺️  ROUTING: {line}")
            else:
                # Print other messages
                if line.startswith("["):
                    print(f"[{timestamp()}] {line}")

except KeyboardInterrupt:
    print("\n\n[STOP] Monitoring stopped by user")

    # Final status
    print("\n" + "=" * 70)
    print("FINAL STATUS")
    print("=" * 70)

    radio.write(b"neighbors\n")
    time.sleep(1)
    print("\nNeighbors:")
    while radio.in_waiting:
        line = radio.readline().decode('utf-8', errors='ignore').strip()
        if line and not line.startswith("[GPS]"):
            print(f"  {line}")

    radio.write(b"routes\n")
    time.sleep(1)
    print("\nRoutes:")
    while radio.in_waiting:
        line = radio.readline().decode('utf-8', errors='ignore').strip()
        if line and not line.startswith("[GPS]"):
            print(f"  {line}")

    radio.close()

except Exception as e:
    print(f"\n[ERROR] {e}")
    import traceback
    traceback.print_exc()
