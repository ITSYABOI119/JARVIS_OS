#!/usr/bin/env python3
"""
UART Loopback Test - Verify adapter TX works

SETUP:
1. Disconnect adapter from Pi completely
2. Short the green wire (TXD) to white wire (RXD) with paperclip/jumper
3. Run this script

If you see "LOOPBACK OK" - adapter TX works, issue is with Pi wiring
If you see "LOOPBACK FAILED" - adapter TX is not working
"""
import serial
import sys
import time

PORT = sys.argv[1] if len(sys.argv) > 1 else 'COM5'
BAUD = 115200

print("=" * 50)
print("UART LOOPBACK TEST")
print("=" * 50)
print(f"\nPort: {PORT}")
print(f"Baud: {BAUD}")
print("\nMAKE SURE:")
print("  1. Adapter is DISCONNECTED from Pi")
print("  2. Green wire (TXD) is shorted to White wire (RXD)")
print("  3. Press Enter when ready...")
input()

try:
    ser = serial.Serial(PORT, BAUD, timeout=1)
    ser.reset_input_buffer()
    ser.reset_output_buffer()
    print(f"Opened {PORT}")
except Exception as e:
    print(f"FAILED to open port: {e}")
    sys.exit(1)

# Test 1: Simple byte test
print("\n--- Test 1: Single byte ---")
ser.write(b'X')
time.sleep(0.1)
response = ser.read(10)
print(f"Sent: b'X'")
print(f"Received: {response}")
test1_ok = response == b'X'
print(f"Result: {'PASS' if test1_ok else 'FAIL'}")

# Test 2: Multiple bytes
print("\n--- Test 2: Multiple bytes ---")
ser.reset_input_buffer()
test_data = b'HELLO'
ser.write(test_data)
time.sleep(0.1)
response = ser.read(10)
print(f"Sent: {test_data}")
print(f"Received: {response}")
test2_ok = response == test_data
print(f"Result: {'PASS' if test2_ok else 'FAIL'}")

# Test 3: Binary data (including our sync bytes)
print("\n--- Test 3: Binary data (AA 55) ---")
ser.reset_input_buffer()
test_data = bytes([0xAA, 0x55, 0x01, 0x02, 0x03])
ser.write(test_data)
time.sleep(0.1)
response = ser.read(10)
print(f"Sent: {test_data.hex()}")
print(f"Received: {response.hex() if response else 'nothing'}")
test3_ok = response == test_data
print(f"Result: {'PASS' if test3_ok else 'FAIL'}")

# Summary
print("\n" + "=" * 50)
if test1_ok and test2_ok and test3_ok:
    print("LOOPBACK OK - Adapter TX works!")
    print("The issue is with Pi wiring or GPIO configuration.")
    print("\nNext steps:")
    print("  1. Double-check green wire is in GPIO15 (Pin 10)")
    print("  2. Check if red wire (5V) is accidentally connected")
    print("  3. Try different GND pin on Pi")
elif test1_ok or test2_ok:
    print("PARTIAL LOOPBACK - Some tests passed")
    print("Adapter may have issues with certain byte patterns.")
else:
    print("LOOPBACK FAILED - Adapter TX not working!")
    print("\nPossible causes:")
    print("  1. Green (TXD) and White (RXD) not properly shorted")
    print("  2. PL2303 driver issue - check Device Manager")
    print("  3. Faulty adapter")
print("=" * 50)

ser.close()
