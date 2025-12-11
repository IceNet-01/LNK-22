#!/usr/bin/env python3
"""
Monitor both LNK-22 radios in real-time
"""
import serial
import time
import threading

def monitor_radio(port, name):
    """Monitor a single radio and print all output"""
    try:
        ser = serial.Serial(port, 115200, timeout=0.1)
        print(f"[{name}] Connected to {port}")

        # Send initial status
        time.sleep(1)
        ser.write(b"status\n")

        while True:
            if ser.in_waiting:
                line = ser.readline().decode('utf-8', errors='ignore').strip()
                if line:
                    print(f"[{name}] {line}")
            time.sleep(0.1)

    except KeyboardInterrupt:
        pass
    except Exception as e:
        print(f"[{name}] ERROR: {e}")
    finally:
        if 'ser' in locals():
            ser.close()

def send_commands():
    """Send test commands to both radios"""
    time.sleep(3)  # Wait for monitoring to start

    commands = [
        ("status", "Check status"),
        ("beacon", "Force beacon"),
        ("neighbors", "Check neighbors"),
    ]

    try:
        for cmd, desc in commands:
            print(f"\n{'='*60}")
            print(f"Sending: {cmd} - {desc}")
            print(f"{'='*60}")

            # Send to both radios
            for port in ["/dev/ttyACM0", "/dev/ttyACM1"]:
                try:
                    ser = serial.Serial(port, 115200, timeout=1)
                    ser.write(f"{cmd}\n".encode())
                    ser.close()
                except:
                    pass

            time.sleep(3)  # Wait for responses

    except KeyboardInterrupt:
        pass

if __name__ == "__main__":
    print("="*60)
    print("LNK-22 Dual Radio Monitor")
    print("Press Ctrl+C to stop")
    print("="*60)

    # Start monitoring threads
    t1 = threading.Thread(target=monitor_radio, args=("/dev/ttyACM0", "Radio1"), daemon=True)
    t2 = threading.Thread(target=monitor_radio, args=("/dev/ttyACM1", "Radio2"), daemon=True)
    t3 = threading.Thread(target=send_commands, daemon=True)

    t1.start()
    t2.start()
    t3.start()

    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print("\nStopping...")
