#!/usr/bin/env python3
import serial
import time

print("Capturing detailed boot sequence...")

try:
    ser = serial.Serial("/dev/ttyACM0", 115200, timeout=1)

    # Hard reset by toggling DTR
    print("\nResetting radio...")
    ser.setDTR(False)
    time.sleep(0.5)
    ser.setDTR(True)
    time.sleep(2)

    print("\n=== COMPLETE BOOT LOG ===\n")

    # Capture for 15 seconds
    start_time = time.time()
    line_count = 0
    while (time.time() - start_time) < 15:
        if ser.in_waiting:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            if line:
                print(line)
                line_count += 1

    print(f"\n=== END BOOT LOG ({line_count} lines) ===\n")

    ser.close()

except Exception as e:
    print(f"Error: {e}")
