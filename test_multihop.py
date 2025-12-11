#!/usr/bin/env python3
"""
Multi-hop routing test for LNK-22
Requires 3 radios positioned so R1 can't reach R3 directly
"""
import serial
import time
import sys

print("=" * 60)
print("LNK-22 Multi-Hop Routing Test")
print("=" * 60)

# Radio addresses (will be auto-detected)
r1_addr = None
r2_addr = None
r3_addr = None

try:
    # Open all 3 radios
    r1 = serial.Serial("/dev/ttyACM0", 115200, timeout=0.1)
    r2 = serial.Serial("/dev/ttyACM1", 115200, timeout=0.1)
    r3 = serial.Serial("/dev/ttyACM2", 115200, timeout=0.1)

    time.sleep(2)

    # Get addresses
    print("\n[1/5] Detecting radio addresses...")
    r1.reset_input_buffer()
    r2.reset_input_buffer()
    r3.reset_input_buffer()

    r1.write(b"status\n")
    time.sleep(0.5)
    while r1.in_waiting:
        line = r1.readline().decode('utf-8', errors='ignore')
        if "Node Address:" in line:
            r1_addr = line.split("0x")[1].strip()
            print(f"  Radio 1: 0x{r1_addr}")

    r2.write(b"status\n")
    time.sleep(0.5)
    while r2.in_waiting:
        line = r2.readline().decode('utf-8', errors='ignore')
        if "Node Address:" in line:
            r2_addr = line.split("0x")[1].strip()
            print(f"  Radio 2: 0x{r2_addr}")

    r3.write(b"status\n")
    time.sleep(0.5)
    while r3.in_waiting:
        line = r3.readline().decode('utf-8', errors='ignore')
        if "Node Address:" in line:
            r3_addr = line.split("0x")[1].strip()
            print(f"  Radio 3: 0x{r3_addr}")

    if not all([r1_addr, r2_addr, r3_addr]):
        print("ERROR: Could not detect all radio addresses!")
        sys.exit(1)

    # Check neighbor tables
    print("\n[2/5] Checking neighbor tables...")
    r1.reset_input_buffer()
    r2.reset_input_buffer()
    r3.reset_input_buffer()

    r1.write(b"neighbors\n")
    time.sleep(1)
    r1_neighbors = []
    while r1.in_waiting:
        line = r1.readline().decode('utf-8', errors='ignore')
        if "0x" in line and "Address" not in line and "===" not in line:
            addr = line.split()[0]
            r1_neighbors.append(addr)
    print(f"  Radio 1 neighbors: {r1_neighbors}")

    r2.write(b"neighbors\n")
    time.sleep(1)
    r2_neighbors = []
    while r2.in_waiting:
        line = r2.readline().decode('utf-8', errors='ignore')
        if "0x" in line and "Address" not in line and "===" not in line:
            addr = line.split()[0]
            r2_neighbors.append(addr)
    print(f"  Radio 2 neighbors: {r2_neighbors}")

    r3.write(b"neighbors\n")
    time.sleep(1)
    r3_neighbors = []
    while r3.in_waiting:
        line = r3.readline().decode('utf-8', errors='ignore')
        if "0x" in line and "Address" not in line and "===" not in line:
            addr = line.split()[0]
            r3_neighbors.append(addr)
    print(f"  Radio 3 neighbors: {r3_neighbors}")

    # Check if multi-hop is needed
    if f"0x{r3_addr}" in r1_neighbors:
        print("\n  ⚠️  WARNING: Radio 1 can see Radio 3 directly!")
        print("  For multi-hop testing, radios should be separated so R1 can't reach R3")
        print("  Continuing anyway - route discovery will still be tested...")
    else:
        print("\n  ✅ Good! Radio 1 cannot see Radio 3 directly - multi-hop required!")

    # Clear buffers
    r1.reset_input_buffer()
    r2.reset_input_buffer()
    r3.reset_input_buffer()

    # Send message from Radio 1 to Radio 3
    print(f"\n[3/5] Sending message from Radio 1 (0x{r1_addr}) to Radio 3 (0x{r3_addr})...")
    test_message = "Hello from Radio 1! Testing multi-hop routing."
    send_cmd = f"send {r3_addr} {test_message}\n"

    print(f"  Command: {send_cmd.strip()}")
    r1.write(send_cmd.encode())

    # Monitor all radios for 10 seconds
    print("\n[4/5] Monitoring all radios for routing activity...")
    print("=" * 60)

    start_time = time.time()
    route_req_seen = False
    route_rep_seen = False
    forward_seen = False
    message_received = False

    while (time.time() - start_time) < 10:
        # Radio 1
        if r1.in_waiting:
            line = r1.readline().decode('utf-8', errors='ignore').strip()
            if line:
                if "ROUTE_REQ" in line:
                    route_req_seen = True
                    print(f"R1: {line}")
                elif "ROUTE_REP" in line:
                    route_rep_seen = True
                    print(f"R1: {line}")
                elif "Sent packet" in line or "TX:" in line:
                    print(f"R1: {line}")

        # Radio 2
        if r2.in_waiting:
            line = r2.readline().decode('utf-8', errors='ignore').strip()
            if line:
                if "ROUTE_REQ" in line:
                    route_req_seen = True
                    print(f"R2: {line}")
                elif "ROUTE_REP" in line:
                    route_rep_seen = True
                    print(f"R2: {line}")
                elif "Forward" in line:
                    forward_seen = True
                    print(f"R2: ✅ {line}")

        # Radio 3
        if r3.in_waiting:
            line = r3.readline().decode('utf-8', errors='ignore').strip()
            if line:
                if "MESSAGE from" in line or "📨" in line:
                    message_received = True
                    print(f"R3: ✅ {line}")
                elif "ROUTE_REQ" in line:
                    route_req_seen = True
                    print(f"R3: {line}")
                elif "ROUTE_REP" in line:
                    route_rep_seen = True
                    print(f"R3: {line}")
                elif test_message in line:
                    print(f"R3: ✅ MESSAGE CONTENT: {line}")

    print("=" * 60)

    # Results
    print("\n[5/5] Test Results:")
    print("-" * 60)
    print(f"  Route Discovery (RREQ): {'✅ YES' if route_req_seen else '❌ NO'}")
    print(f"  Route Reply (RREP):     {'✅ YES' if route_rep_seen else '❌ NO'}")
    print(f"  Packet Forwarding:      {'✅ YES' if forward_seen else '❌ NO'}")
    print(f"  Message Received:       {'✅ YES' if message_received else '❌ NO'}")
    print("-" * 60)

    if message_received:
        print("\n🎉 SUCCESS! Multi-hop routing is working!")
    elif route_req_seen:
        print("\n⚠️  Route discovery started but message may not have arrived")
        print("    Check if radios are positioned correctly")
    else:
        print("\n❌ Multi-hop routing did not activate")
        print("    Radios may be too close together (all direct neighbors)")

    r1.close()
    r2.close()
    r3.close()

except Exception as e:
    print(f"\n❌ Error: {e}")
    import traceback
    traceback.print_exc()
