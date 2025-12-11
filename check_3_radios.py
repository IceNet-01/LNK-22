#!/usr/bin/env python3
import serial
import time

print("Checking all 3 radios...")

try:
    r1 = serial.Serial("/dev/ttyACM0", 115200, timeout=0.1)
    r2 = serial.Serial("/dev/ttyACM1", 115200, timeout=0.1)
    r3 = serial.Serial("/dev/ttyACM2", 115200, timeout=0.1)

    time.sleep(2)

    # Clear buffers
    r1.reset_input_buffer()
    r2.reset_input_buffer()
    r3.reset_input_buffer()

    time.sleep(1)

    # Get status from all radios
    print("\n=== Getting Radio Status ===")
    r1.write(b"status\n")
    r2.write(b"status\n")
    r3.write(b"status\n")

    time.sleep(2)

    print("\n=== Radio 1 Status ===")
    while r1.in_waiting:
        line = r1.readline().decode('utf-8', errors='ignore').strip()
        if line and not line.startswith("[GPS]"):
            print(f"R1: {line}")

    print("\n=== Radio 2 Status ===")
    while r2.in_waiting:
        line = r2.readline().decode('utf-8', errors='ignore').strip()
        if line and not line.startswith("[GPS]"):
            print(f"R2: {line}")

    print("\n=== Radio 3 Status ===")
    while r3.in_waiting:
        line = r3.readline().decode('utf-8', errors='ignore').strip()
        if line and not line.startswith("[GPS]"):
            print(f"R3: {line}")

    # Wait for beacons to propagate
    print("\n\n=== Waiting 30 seconds for neighbor discovery ===")
    time.sleep(30)

    # Check neighbor tables
    r1.reset_input_buffer()
    r2.reset_input_buffer()
    r3.reset_input_buffer()

    print("\n=== Checking Neighbor Tables ===")
    r1.write(b"neighbors\n")
    time.sleep(1)
    print("\n--- Radio 1 Neighbors ---")
    while r1.in_waiting:
        line = r1.readline().decode('utf-8', errors='ignore').strip()
        if line and not line.startswith("[GPS]") and not line.startswith("[ISR]") and not line.startswith("[RADIO]"):
            print(line)

    r2.write(b"neighbors\n")
    time.sleep(1)
    print("\n--- Radio 2 Neighbors ---")
    while r2.in_waiting:
        line = r2.readline().decode('utf-8', errors='ignore').strip()
        if line and not line.startswith("[GPS]") and not line.startswith("[ISR]") and not line.startswith("[RADIO]"):
            print(line)

    r3.write(b"neighbors\n")
    time.sleep(1)
    print("\n--- Radio 3 Neighbors ---")
    while r3.in_waiting:
        line = r3.readline().decode('utf-8', errors='ignore').strip()
        if line and not line.startswith("[GPS]") and not line.startswith("[ISR]") and not line.startswith("[RADIO]"):
            print(line)

    r1.close()
    r2.close()
    r3.close()

    print("\n\n=== All 3 radios checked! ===")

except Exception as e:
    print(f"Error: {e}")
    import traceback
    traceback.print_exc()
