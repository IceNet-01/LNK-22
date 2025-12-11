#!/usr/bin/env python3
import serial
import time

print("Resetting Radio 1 and capturing boot output...")
try:
    # Open serial
    ser = serial.Serial("/dev/ttyACM0", 115200, timeout=1)

    # Reset by toggling DTR
    ser.setDTR(False)
    time.sleep(0.1)
    ser.setDTR(True)
    time.sleep(2)

    # Capture boot messages
    print("\n=== BOOT OUTPUT ===")
    start_time = time.time()
    while (time.time() - start_time) < 5:
        if ser.in_waiting:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            print(line)

    print("\n=== END BOOT OUTPUT ===\n")

    # Send status command
    ser.write(b"status\n")
    time.sleep(1)

    print("=== STATUS OUTPUT ===")
    while ser.in_waiting:
        line = ser.readline().decode('utf-8', errors='ignore').strip()
        print(line)

    ser.close()
except Exception as e:
    print(f"Error: {e}")
