#!/usr/bin/env python3
"""Simple UART diagnostic - raw read/write test"""
import serial
import time
import sys

PORT = sys.argv[1] if len(sys.argv) > 1 else 'COM5'
BAUD = 115200

print(f"Opening {PORT} at {BAUD}...")
try:
    ser = serial.Serial(PORT, BAUD, timeout=0.1)
    ser.reset_input_buffer()
    ser.reset_output_buffer()
    print("Connected!")
except Exception as e:
    print(f"Failed: {e}")
    sys.exit(1)

# Read any pending data from seL4
print("\n=== Reading seL4 output (2 seconds) ===")
start = time.time()
total_rx = 0
while time.time() - start < 2:
    data = ser.read(256)
    if data:
        total_rx += len(data)
        # Show as text if printable, else hex
        try:
            text = data.decode('utf-8', errors='replace')
            print(text, end='', flush=True)
        except:
            print(f"[{data.hex()}]", end='', flush=True)
print(f"\n\nReceived {total_rx} bytes from seL4")

# Send sync bytes and see if seL4 responds
print("\n=== Sending 0xAA 0x55 (sync bytes) ===")
ser.write(bytes([0xAA, 0x55]))
print("Sent: AA 55")

# Read response
print("\n=== Reading response (3 seconds) ===")
start = time.time()
total_rx = 0
while time.time() - start < 3:
    data = ser.read(256)
    if data:
        total_rx += len(data)
        try:
            text = data.decode('utf-8', errors='replace')
            print(text, end='', flush=True)
        except:
            print(f"[{data.hex()}]", end='', flush=True)
print(f"\n\nReceived {total_rx} bytes after sending sync")

ser.close()
print("\nDone!")
