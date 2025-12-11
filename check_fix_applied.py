#!/usr/bin/env python3
import serial
import time

print("Checking if DIO1 IRQ fix was applied...")
print("Resetting Radio 1 to capture boot messages...\n")

try:
    ser = serial.Serial("/dev/ttyACM0", 115200, timeout=1)

    # Reset via DTR toggle
    ser.setDTR(False)
    time.sleep(0.1)
    ser.setDTR(True)
    time.sleep(3)

    print("=== BOOT MESSAGES ===\n")
    found_fix = False
    for i in range(50):
        if ser.in_waiting:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            print(line)
            if "DIO1 IRQ configured" in line:
                found_fix = True
                print("\n✅ FIX CONFIRMED: DIO1 IRQ configuration found!\n")

    if not found_fix:
        print("\n❌ FIX NOT FOUND: 'DIO1 IRQ configured' message missing!\n")
        print("The firmware may not have the fix compiled in.")

    ser.close()

except Exception as e:
    print(f"Error: {e}")
