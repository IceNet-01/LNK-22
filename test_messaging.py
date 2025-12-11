#!/usr/bin/env python3
"""
LNK-22 Messaging Test Script
Tests sending messages between radios via serial
"""

import serial
import time
import sys
import threading

# Configuration
PORTS = ['/dev/ttyACM0', '/dev/ttyACM1', '/dev/ttyACM2']
BAUD = 115200
TIMEOUT = 1

def read_serial(ser, name, duration=5):
    """Read from serial port for a duration"""
    end_time = time.time() + duration
    output = []
    while time.time() < end_time:
        try:
            if ser.in_waiting:
                line = ser.readline().decode('utf-8', errors='ignore').strip()
                if line:
                    print(f"[{name}] {line}")
                    output.append(line)
        except:
            pass
        time.sleep(0.01)
    return output

def send_command(ser, cmd):
    """Send a command to serial port"""
    ser.write(f"{cmd}\n".encode())
    ser.flush()

def main():
    print("=" * 60)
    print("LNK-22 Messaging Test")
    print("=" * 60)

    # Open all available serial ports
    radios = {}
    for port in PORTS:
        try:
            ser = serial.Serial(port, BAUD, timeout=TIMEOUT)
            ser.reset_input_buffer()
            radios[port] = ser
            print(f"✓ Opened {port}")
        except Exception as e:
            print(f"✗ Failed to open {port}: {e}")

    if len(radios) < 2:
        print("ERROR: Need at least 2 radios for messaging test")
        return

    print(f"\nFound {len(radios)} radios")
    time.sleep(1)

    # Get status from each radio
    print("\n" + "=" * 60)
    print("Getting status from each radio...")
    print("=" * 60)

    addresses = {}
    for port, ser in radios.items():
        ser.reset_input_buffer()
        send_command(ser, "status")
        time.sleep(0.5)

        # Read response
        lines = []
        for _ in range(20):
            try:
                if ser.in_waiting:
                    line = ser.readline().decode('utf-8', errors='ignore').strip()
                    if line:
                        lines.append(line)
                        print(f"[{port}] {line}")
                        # Parse node address
                        if "Node:" in line and "0x" in line:
                            # Extract address like "Node: LNK-048F (0x4D77048F)"
                            import re
                            match = re.search(r'0x([0-9A-Fa-f]+)', line)
                            if match:
                                addresses[port] = "0x" + match.group(1).upper()
            except:
                pass
            time.sleep(0.05)
        print()

    print("\nRadio Addresses:")
    for port, addr in addresses.items():
        print(f"  {port}: {addr}")

    if len(addresses) < 2:
        print("ERROR: Couldn't get addresses from at least 2 radios")
        # Try neighbors command
        print("\nTrying neighbors command...")
        for port, ser in radios.items():
            ser.reset_input_buffer()
            send_command(ser, "neighbors")
            time.sleep(1)
            while ser.in_waiting:
                line = ser.readline().decode('utf-8', errors='ignore').strip()
                if line:
                    print(f"[{port}] {line}")
        return

    # Test 1: Broadcast message from radio 0
    print("\n" + "=" * 60)
    print("TEST 1: Broadcast message from first radio")
    print("=" * 60)

    ports = list(radios.keys())
    sender = ports[0]
    receivers = ports[1:]

    # Clear buffers
    for ser in radios.values():
        ser.reset_input_buffer()

    # Send broadcast
    test_msg = "Hello from broadcast test!"
    print(f"\nSending broadcast from {sender}: '{test_msg}'")
    send_command(radios[sender], f"send broadcast {test_msg}")

    # Wait and check for reception
    print("\nWaiting for message reception (5 seconds)...")
    time.sleep(5)

    received = False
    for port in receivers:
        ser = radios[port]
        while ser.in_waiting:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            if line:
                print(f"[{port}] {line}")
                if "MESSAGE" in line or test_msg in line:
                    received = True

    if received:
        print("\n✓ Broadcast message received!")
    else:
        print("\n✗ Broadcast message NOT received")

    # Test 2: Direct message if we have addresses
    if len(addresses) >= 2:
        print("\n" + "=" * 60)
        print("TEST 2: Direct message between radios")
        print("=" * 60)

        # Send from radio 0 to radio 1
        sender_port = ports[0]
        receiver_port = ports[1]

        if receiver_port in addresses:
            dest_addr = addresses[receiver_port]
            test_msg2 = "Direct message test!"

            # Clear buffers
            for ser in radios.values():
                ser.reset_input_buffer()

            print(f"\nSending direct message from {sender_port} to {dest_addr}: '{test_msg2}'")
            send_command(radios[sender_port], f"send {dest_addr} {test_msg2}")

            # Wait and check
            print("\nWaiting for message reception (5 seconds)...")
            time.sleep(5)

            received = False
            ser = radios[receiver_port]
            while ser.in_waiting:
                line = ser.readline().decode('utf-8', errors='ignore').strip()
                if line:
                    print(f"[{receiver_port}] {line}")
                    if "MESSAGE" in line or test_msg2 in line:
                        received = True

            if received:
                print("\n✓ Direct message received!")
            else:
                print("\n✗ Direct message NOT received")

    # Cleanup
    print("\n" + "=" * 60)
    print("Closing serial ports...")
    for ser in radios.values():
        ser.close()

    print("Test complete!")

if __name__ == "__main__":
    main()
