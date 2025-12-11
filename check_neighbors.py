#!/usr/bin/env python3
import serial
import time

print("Checking neighbor tables...")

r1 = serial.Serial("/dev/ttyACM0", 115200, timeout=0.1)
r2 = serial.Serial("/dev/ttyACM1", 115200, timeout=0.1)

r1.reset_input_buffer()
r2.reset_input_buffer()
time.sleep(1)

print("\n=== Radio 1 Neighbors ===")
r1.write(b"neighbors\n")
time.sleep(1)
while r1.in_waiting:
    print(r1.readline().decode('utf-8', errors='ignore').strip())

print("\n=== Radio 2 Neighbors ===")
r2.write(b"neighbors\n")
time.sleep(1)
while r2.in_waiting:
    print(r2.readline().decode('utf-8', errors='ignore').strip())

r1.close()
r2.close()
