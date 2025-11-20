#!/usr/bin/env python3
"""
Quick verification that Python IPC client works
"""

import sys
sys.path.insert(0, '../../src/ai')

from ipc_client import IPCClient, MSG_QUERY, MSG_RESPONSE

print("="*70)
print("JARVIS AI-OS - Python IPC Client Verification")
print("="*70)

# Test 1: Create client
print("\n[1] Creating IPC client...")
try:
    client = IPCClient()
    print("[PASS] IPC client created")
    print(f"  Path: {client.shared_memory_path}")
    print(f"  Connected: {client.connected}")
except Exception as e:
    print(f"[FAIL] Failed: {e}")
    sys.exit(1)

# Test 2: Send messages
print("\n[2] Sending test messages...")
test_queries = ["help", "status", "cache stats"]
sent = 0

for query in test_queries:
    try:
        client.send_message(MSG_QUERY, query)
        print(f"[PASS] Sent: '{query}'")
        sent += 1
    except Exception as e:
        print(f"[FAIL] Failed to send '{query}': {e}")

print(f"  Result: {sent}/{len(test_queries)} messages sent")

# Test 3: Get statistics
print("\n[3] Checking IPC statistics...")
try:
    stats = client.get_statistics()
    print("[PASS] Statistics retrieved:")
    for key, value in stats.items():
        print(f"  {key}: {value}")
except Exception as e:
    print(f"[FAIL] Failed: {e}")

# Test 4: Receive (will timeout since no seL4)
print("\n[4] Testing receive (expect timeout/None)...")
try:
    msg = client.receive_message()
    if msg is None:
        print("[PASS] Correctly returns None (no seL4 running)")
    else:
        print(f"[WARN] Unexpected message: {msg}")
except Exception as e:
    print(f"[FAIL] Error: {e}")

print("\n" + "="*70)
print("Python IPC Client Verification Complete!")
print("="*70)
print("\nNext steps:")
print("  1. Launch seL4: wsl bash -c 'cd ~/jarvis-phase1/hello-world_build && ./simulate'")
print("  2. Run this script again to see if seL4 responds")
print("\n")
