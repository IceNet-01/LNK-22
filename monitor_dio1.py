#!/usr/bin/env python3
import serial
import time

print("Monitoring both radios for DIO1 state...")
print("Waiting 15 seconds for boot...\n")
time.sleep(15)

try:
    r1 = serial.Serial("/dev/ttyACM0", 115200, timeout=0.5)
    r2 = serial.Serial("/dev/ttyACM1", 115200, timeout=0.5)

    # Send beacon on R1
    print("=" * 60)
    print("Sending beacon from Radio 1...")
    print("=" * 60)
    r1.write(b"beacon\n")
    time.sleep(1)

    # Monitor for 15 seconds
    print("\nMonitoring for 15 seconds...\n")
    start_time = time.time()
    while (time.time() - start_time) < 15:
        if r1.in_waiting:
            line = r1.readline().decode('utf-8', errors='ignore').strip()
            if "[RADIO] DIO1" in line or "[ISR]" in line or "beacon" in line.lower():
                print(f"R1: {line}")

        if r2.in_waiting:
            line = r2.readline().decode('utf-8', errors='ignore').strip()
            if "[RADIO] DIO1" in line or "[ISR]" in line or "beacon" in line.lower():
                print(f"R2: {line}")

        time.sleep(0.1)

    r1.close()
    r2.close()

    print("\n" + "=" * 60)
    print("Done!")

except Exception as e:
    print(f"Error: {e}")
