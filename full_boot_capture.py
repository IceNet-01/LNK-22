#!/usr/bin/env python3
import serial
import time

print("Capturing FULL boot sequence...")

try:
    ser = serial.Serial("/dev/ttyACM0", 115200, timeout=1)

    # Reset
    ser.setDTR(False)
    time.sleep(0.1)
    ser.setDTR(True)

    print("\n=== FULL BOOT LOG ===\n")

    # Capture everything for 10 seconds
    start_time = time.time()
    while (time.time() - start_time) < 10:
        if ser.in_waiting:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            if line:
                print(line)

    print("\n=== END BOOT LOG ===\n")

    ser.close()

except Exception as e:
    print(f"Error: {e}")
