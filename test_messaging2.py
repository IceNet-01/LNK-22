#!/usr/bin/env python3
"""
LNK-22 Messaging Test Script v2
More detailed test showing sender output
"""

import serial
import time
import threading

PORTS = ['/dev/ttyACM0', '/dev/ttyACM1', '/dev/ttyACM2']
BAUD = 115200

def reader_thread(ser, name, stop_event, output_list):
    """Continuously read from serial"""
    while not stop_event.is_set():
        try:
            if ser.in_waiting:
                line = ser.readline().decode('utf-8', errors='ignore').strip()
                if line and not line.startswith('[GPS]'):  # Filter GPS spam
                    print(f"[{name}] {line}")
                    output_list.append(line)
        except:
            pass
        time.sleep(0.01)

def main():
    print("=" * 60)
    print("LNK-22 Messaging Test v2")
    print("=" * 60)

    # Open serial ports
    radios = {}
    for port in PORTS:
        try:
            ser = serial.Serial(port, BAUD, timeout=0.1)
            ser.reset_input_buffer()
            radios[port] = ser
            print(f"✓ Opened {port}")
        except Exception as e:
            print(f"✗ Failed to open {port}: {e}")

    if len(radios) < 2:
        print("ERROR: Need 2 radios")
        return

    # Start reader threads
    stop_event = threading.Event()
    outputs = {port: [] for port in radios}
    threads = []
    for port, ser in radios.items():
        t = threading.Thread(target=reader_thread, args=(ser, port, stop_event, outputs[port]))
        t.daemon = True
        t.start()
        threads.append(t)

    time.sleep(1)

    # Get addresses first
    print("\n--- Getting addresses ---")
    for port, ser in radios.items():
        ser.write(b"status\n")
        time.sleep(1)

    addresses = {}
    for port, lines in outputs.items():
        for line in lines:
            if "Node:" in line and "0x" in line:
                import re
                match = re.search(r'\(0x([0-9A-Fa-f]+)\)', line)
                if match:
                    addresses[port] = "0x" + match.group(1).upper()
                    break

    print(f"\nAddresses: {addresses}")

    # Clear outputs
    for port in outputs:
        outputs[port].clear()

    # Test broadcast from ACM1
    print("\n" + "=" * 60)
    print("Sending BROADCAST from /dev/ttyACM1...")
    print("=" * 60)

    radios['/dev/ttyACM1'].write(b"send broadcast HELLO_BROADCAST_TEST\n")

    # Wait and observe
    time.sleep(5)

    # Check what the sender logged
    print("\n--- Sender (/dev/ttyACM1) output: ---")
    for line in outputs['/dev/ttyACM1']:
        print(f"  {line}")

    print("\n--- Receiver (/dev/ttyACM2) output: ---")
    for line in outputs['/dev/ttyACM2']:
        print(f"  {line}")

    # Clear outputs
    for port in outputs:
        outputs[port].clear()

    # Test direct message
    print("\n" + "=" * 60)
    print("Sending DIRECT message from /dev/ttyACM1 to /dev/ttyACM2...")
    print("=" * 60)

    dest = addresses.get('/dev/ttyACM2', '0xFFFFFFFF')
    cmd = f"send {dest} HELLO_DIRECT_TEST\n"
    print(f"Command: {cmd.strip()}")
    radios['/dev/ttyACM1'].write(cmd.encode())

    # Wait
    time.sleep(5)

    print("\n--- Sender (/dev/ttyACM1) output: ---")
    for line in outputs['/dev/ttyACM1']:
        print(f"  {line}")

    print("\n--- Receiver (/dev/ttyACM2) output: ---")
    for line in outputs['/dev/ttyACM2']:
        print(f"  {line}")

    # Check if MESSAGE was received
    received = any("MESSAGE" in line or "HELLO" in line for line in outputs['/dev/ttyACM2'])
    if received:
        print("\n✓ MESSAGE RECEIVED!")
    else:
        print("\n✗ MESSAGE NOT RECEIVED")

    # Cleanup
    stop_event.set()
    time.sleep(0.5)
    for ser in radios.values():
        ser.close()

if __name__ == "__main__":
    main()
