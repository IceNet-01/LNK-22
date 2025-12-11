#!/usr/bin/env python3
import serial
import time

radio = serial.Serial("/dev/ttyACM1", 115200, timeout=0.1)
time.sleep(1)

# Get status first
print("Getting radio status...")
radio.write(b"status\n")
time.sleep(1)

while radio.in_waiting:
    print(radio.readline().decode('utf-8', errors='ignore').strip())

# Get neighbors
print("\nGetting neighbors...")
radio.write(b"neighbors\n")
time.sleep(1)

while radio.in_waiting:
    line = radio.readline().decode('utf-8', errors='ignore').strip()
    if line and not line.startswith("[GPS]"):
        print(line)

# Monitor for 30 seconds
print("\nMonitoring for 30 seconds...")
print("=" * 60)

start = time.time()
while time.time() - start < 30:
    if radio.in_waiting:
        line = radio.readline().decode('utf-8', errors='ignore').strip()
        if line and not line.startswith("[GPS]"):
            print(line)

print("=" * 60)

# Final neighbors
print("\nFinal neighbor check...")
radio.write(b"neighbors\n")
time.sleep(1)

while radio.in_waiting:
    line = radio.readline().decode('utf-8', errors='ignore').strip()
    if line and not line.startswith("[GPS]"):
        print(line)

radio.close()
