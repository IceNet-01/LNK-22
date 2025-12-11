#!/usr/bin/env python3
import serial
import time
import sys

print("Testing RadioLib Communication...")
print("\nOpening both radios...")

try:
    # Open both radios
    r1 = serial.Serial("/dev/ttyACM0", 115200, timeout=0.1)
    r2 = serial.Serial("/dev/ttyACM1", 115200, timeout=0.1)

    # Clear buffers
    r1.reset_input_buffer()
    r2.reset_input_buffer()
    time.sleep(0.5)

    # Send beacon from Radio 1
    print("\n[TEST] Sending beacon from Radio 1...")
    r1.write(b"beacon\n")
    time.sleep(2)

    # Monitor both radios for 10 seconds
    print("\n=== Monitoring for RX activity ===\n")
    start_time = time.time()
    r1_rx_count = 0
    r2_rx_count = 0

    while (time.time() - start_time) < 10:
        # Check Radio 1
        if r1.in_waiting:
            line = r1.readline().decode('utf-8', errors='ignore').strip()
            if line:
                print(f"R1: {line}")
                if "[ISR] OnRxDone!" in line or "RX:" in line:
                    r1_rx_count += 1

        # Check Radio 2
        if r2.in_waiting:
            line = r2.readline().decode('utf-8', errors='ignore').strip()
            if line:
                print(f"R2: {line}")
                if "[ISR] OnRxDone!" in line or "RX:" in line:
                    r2_rx_count += 1

    print(f"\n=== Test Results ===")
    print(f"Radio 1 RX count: {r1_rx_count}")
    print(f"Radio 2 RX count: {r2_rx_count}")

    if r2_rx_count > 0:
        print("\n✅ SUCCESS! Radio 2 received packets from Radio 1!")
        print("🎉 RadioLib migration successful!")
    else:
        print("\n❌ No packets received on Radio 2")
        print("Checking radio status...")
        r1.write(b"radio\n")
        r2.write(b"radio\n")
        time.sleep(1)

        print("\n=== Radio 1 Status ===")
        while r1.in_waiting:
            print(r1.readline().decode('utf-8', errors='ignore').strip())

        print("\n=== Radio 2 Status ===")
        while r2.in_waiting:
            print(r2.readline().decode('utf-8', errors='ignore').strip())

    r1.close()
    r2.close()

except Exception as e:
    print(f"Error: {e}")
    import traceback
    traceback.print_exc()
