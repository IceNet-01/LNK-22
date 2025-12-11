#!/usr/bin/env python3
"""
Test both LNK-22 radios to see what's happening
"""
import serial
import time
import sys

def test_radio(port, name):
    print(f"\n{'='*60}")
    print(f"Testing {name} on {port}")
    print(f"{'='*60}")

    try:
        ser = serial.Serial(port, 115200, timeout=1)
        time.sleep(0.5)

        # Clear buffer
        ser.reset_input_buffer()

        # Send status command
        ser.write(b"status\n")
        time.sleep(1)

        # Read response
        output = []
        while ser.in_waiting:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            if line:
                output.append(line)

        print("\n".join(output))

        ser.close()
        return output
    except Exception as e:
        print(f"ERROR: {e}")
        return []

if __name__ == "__main__":
    print("LNK-22 Radio Status Test")
    print("="*60)

    radio1 = test_radio("/dev/ttyACM0", "Radio 1")
    radio2 = test_radio("/dev/ttyACM1", "Radio 2")

    print(f"\n{'='*60}")
    print("Summary:")
    print(f"{'='*60}")
    print(f"Radio 1 lines: {len(radio1)}")
    print(f"Radio 2 lines: {len(radio2)}")
