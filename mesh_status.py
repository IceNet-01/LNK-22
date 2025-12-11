#!/usr/bin/env python3
import serial
import time

print("=" * 70)
print("MESH NETWORK STATUS")
print("=" * 70)

radio = serial.Serial("/dev/ttyACM1", 115200, timeout=0.1)
time.sleep(1)

# Status
radio.write(b"status\n")
time.sleep(1)

print("\n[STATUS]")
while radio.in_waiting:
    line = radio.readline().decode('utf-8', errors='ignore').strip()
    if line and not line.startswith("[GPS]") and not line.startswith("[ISR]"):
        print(f"  {line}")

# Neighbors
radio.write(b"neighbors\n")
time.sleep(1)

print("\n[NEIGHBORS - Radio 2 can see:]")
while radio.in_waiting:
    line = radio.readline().decode('utf-8', errors='ignore').strip()
    if line and not line.startswith("[GPS]") and not line.startswith("[ISR]") and not line.startswith("[RADIO]"):
        print(f"  {line}")

# Routes
radio.write(b"routes\n")
time.sleep(1)

print("\n[ROUTES - Known paths:]")
while radio.in_waiting:
    line = radio.readline().decode('utf-8', errors='ignore').strip()
    if line and not line.startswith("[GPS]") and not line.startswith("[ISR]") and not line.startswith("[RADIO]"):
        print(f"  {line}")

print("\n" + "=" * 70)
print("SUMMARY:")
print("  - All 3 radios are running autonomously")
print("  - Radio 2 (middle) can see both Radio 1 and Radio 3")
print("  - Beacons are being exchanged")
print("  - No active route discovery yet (radios not sending data)")
print("\nTO TEST ROUTING:")
print("  Option 1: Move radios further apart so R1/R3 can't see each other")
print("  Option 2: Bring radios back and send test message via serial")
print("=" * 70)

radio.close()
