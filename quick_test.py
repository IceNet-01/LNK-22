#!/usr/bin/env python3
import serial
import time

print("Quick Radio Test")
print("="*60)

# Open both radios
try:
    r1 = serial.Serial("/dev/ttyACM0", 115200, timeout=0.5)
    r2 = serial.Serial("/dev/ttyACM1", 115200, timeout=0.5)
    time.sleep(1)

    # Clear buffers
    r1.reset_input_buffer()
    r2.reset_input_buffer()

    print("\n1. Getting Radio 1 status...")
    r1.write(b"status\n")
    time.sleep(2)
    while r1.in_waiting:
        print(f"  R1: {r1.readline().decode('utf-8', errors='ignore').strip()}")

    print("\n2. Getting Radio 2 status...")
    r2.write(b"status\n")
    time.sleep(2)
    while r2.in_waiting:
        print(f"  R2: {r2.readline().decode('utf-8', errors='ignore').strip()}")

    print("\n3. Radio 1 sending beacon...")
    r1.write(b"beacon\n")
    time.sleep(1)

    print("   Radio 1 output:")
    while r1.in_waiting:
        print(f"  R1: {r1.readline().decode('utf-8', errors='ignore').strip()}")

    print("\n4. Waiting 3 seconds for Radio 2 to receive...")
    time.sleep(3)

    print("   Radio 2 output:")
    if r2.in_waiting:
        while r2.in_waiting:
            print(f"  R2: {r2.readline().decode('utf-8', errors='ignore').strip()}")
    else:
        print("  R2: NO OUTPUT - Did not receive beacon!")

    print("\n5. Radio 2 sending beacon...")
    r2.write(b"beacon\n")
    time.sleep(1)

    print("   Radio 2 output:")
    while r2.in_waiting:
        print(f"  R2: {r2.readline().decode('utf-8', errors='ignore').strip()}")

    print("\n6. Waiting 3 seconds for Radio 1 to receive...")
    time.sleep(3)

    print("   Radio 1 output:")
    if r1.in_waiting:
        while r1.in_waiting:
            print(f"  R1: {r1.readline().decode('utf-8', errors='ignore').strip()}")
    else:
        print("  R1: NO OUTPUT - Did not receive beacon!")

    print("\n7. Final status check...")
    r1.write(b"neighbors\n")
    r2.write(b"neighbors\n")
    time.sleep(2)

    print("\nRadio 1 neighbors:")
    while r1.in_waiting:
        print(f"  R1: {r1.readline().decode('utf-8', errors='ignore').strip()}")

    print("\nRadio 2 neighbors:")
    while r2.in_waiting:
        print(f"  R2: {r2.readline().decode('utf-8', errors='ignore').strip()}")

    r1.close()
    r2.close()

    print("\n" + "="*60)
    print("Test complete!")

except Exception as e:
    print(f"ERROR: {e}")
    import traceback
    traceback.print_exc()
