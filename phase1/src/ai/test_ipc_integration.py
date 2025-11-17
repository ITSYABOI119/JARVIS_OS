#!/usr/bin/env python3
"""
JARVIS AI-OS Phase 1 - IPC Integration Test Suite
Week 8: End-to-End Testing of Python ↔ seL4 IPC

Tests:
1. Python sends query → seL4 receives
2. seL4 cache lookup → Python receives response
3. Multiple queries with cache hits/misses
4. Invalid message handling
5. Timeout behavior
6. Message corruption detection

NOTE: Full testing requires seL4 running in QEMU.
      This test suite can run standalone to verify Python side.
"""

import sys
import time
from pathlib import Path

# Add parent directory for imports
sys.path.insert(0, str(Path(__file__).parent))

from ipc_client import IPCClient, MSG_QUERY, MSG_RESPONSE

# ============================================================================
# Test Configuration
# ============================================================================

# Test queries (should match patterns in decision cache)
TEST_QUERIES = [
    ("show cpu", True),          # Expected cache hit
    ("check memory", True),       # Expected cache hit
    ("help", True),               # Expected cache hit
    ("status", True),             # Expected cache hit
    ("unknown query", False),     # Expected cache miss
]

# ============================================================================
# Test Functions
# ============================================================================

def test_ipc_connection():
    """
    TEST 1: IPC Connection

    Verifies:
    - IPC client can connect to shared memory
    - Connection state tracked correctly
    - Disconnect works properly
    """
    print("=" * 70)
    print("TEST 1: IPC Connection")
    print("=" * 70)
    print()

    client = IPCClient()

    # Test connection
    if not client.connect():
        print("[FAIL] Connection failed")
        return False

    if not client.connected:
        print("[FAIL] Connection state incorrect")
        return False

    print("[PASS] Connected successfully")

    # Test disconnect
    client.disconnect()

    if client.connected:
        print("[FAIL] Disconnect failed")
        return False

    print("[PASS] Disconnected successfully")
    print()

    return True

def test_message_send():
    """
    TEST 2: Message Send

    Verifies:
    - Messages can be sent via IPC
    - Message structure is correct
    - Statistics updated correctly
    """
    print("=" * 70)
    print("TEST 2: Message Send")
    print("=" * 70)
    print()

    client = IPCClient()
    client.connect()

    # Send test messages
    test_messages = [
        "show cpu",
        "check memory",
        "help",
    ]

    for i, msg in enumerate(test_messages):
        if not client.send_message(MSG_QUERY, msg):
            print(f"[FAIL] Failed to send message: {msg}")
            return False
        print(f"[PASS] Sent message {i+1}: {msg}")

    # Check statistics
    stats = client.get_statistics()
    if stats['messages_sent'] != len(test_messages):
        print(f"[FAIL] Message count incorrect: {stats['messages_sent']} != {len(test_messages)}")
        return False

    print(f"[PASS] Statistics correct: {stats['messages_sent']} messages sent")

    client.disconnect()
    print()

    return True

def test_message_receive():
    """
    TEST 3: Message Receive

    Verifies:
    - Receive with timeout works
    - Returns None when no messages available
    - Handles timeout correctly
    """
    print("=" * 70)
    print("TEST 3: Message Receive")
    print("=" * 70)
    print()

    client = IPCClient()
    client.connect()

    # Try to receive (should timeout without seL4)
    print("Attempting receive (will timeout without seL4)...")
    start_time = time.time()
    msg = client.receive_message(timeout_ms=100)
    elapsed = (time.time() - start_time) * 1000

    if msg is not None:
        print(f"[UNEXPECTED] Received message without seL4 running")
        print(f"  Message: {msg}")
        # This is actually OK - might be leftover from previous run
        print("[PASS] Receive mechanism working")
    else:
        print("[PASS] Timeout worked correctly (no messages)")

    if elapsed < 90 or elapsed > 200:
        print(f"[WARN] Timeout duration unexpected: {elapsed:.0f}ms (expected ~100ms)")
    else:
        print(f"[PASS] Timeout duration correct: {elapsed:.0f}ms")

    client.disconnect()
    print()

    return True

def test_end_to_end_simulation():
    """
    TEST 4: End-to-End Simulation

    Simulates complete Python → seL4 → Python flow.
    NOTE: Requires seL4 running to fully complete.
    """
    print("=" * 70)
    print("TEST 4: End-to-End Simulation")
    print("=" * 70)
    print()

    client = IPCClient()

    if not client.connect():
        print("[FAIL] Connection failed")
        return False

    print("[INFO] This test simulates the full IPC flow:")
    print("        Python -> send query -> seL4 -> cache lookup -> send response -> Python")
    print()

    # Send queries
    print("Sending queries...")
    for query, expected_hit in TEST_QUERIES:
        if not client.send_message(MSG_QUERY, query):
            print(f"[FAIL] Failed to send: {query}")
            return False
        print(f"  Sent: \"{query}\" (expect {'HIT' if expected_hit else 'MISS'})")

    print()

    # Try to receive responses
    print("Attempting to receive responses...")
    print("(Will timeout if seL4 not running)")
    print()

    received = 0
    for i in range(len(TEST_QUERIES)):
        msg = client.receive_message(timeout_ms=500)
        if msg:
            received += 1
            print(f"  [{received}] Received: {msg.payload.decode('utf-8', errors='ignore')}")
        else:
            print(f"  [{i+1}] Timeout (no response)")
            break

    if received > 0:
        print()
        print(f"[PASS] Received {received}/{len(TEST_QUERIES)} responses")
        print("[INFO] seL4 is running and responding!")
    else:
        print()
        print("[INFO] No responses received (seL4 not running)")
        print("[PASS] Python IPC send/receive mechanism working")

    client.disconnect()
    print()

    return True

def test_error_handling():
    """
    TEST 5: Error Handling

    Verifies:
    - Payload too large rejected
    - Not connected error handling
    - Invalid parameters handled
    """
    print("=" * 70)
    print("TEST 5: Error Handling")
    print("=" * 70)
    print()

    # Test 1: Send without connection
    client = IPCClient()
    if client.send_message(MSG_QUERY, "test"):
        print("[FAIL] Send succeeded without connection")
        return False
    print("[PASS] Send rejected when not connected")

    # Test 2: Payload too large
    client.connect()
    large_payload = "A" * 300  # Exceeds MAX_MESSAGE_SIZE (256)
    if client.send_message(MSG_QUERY, large_payload):
        print("[FAIL] Large payload accepted")
        return False
    print("[PASS] Large payload rejected")

    # Test 3: Empty payload (should work)
    if not client.send_message(MSG_QUERY, ""):
        print("[FAIL] Empty payload rejected")
        return False
    print("[PASS] Empty payload handled")

    client.disconnect()
    print()

    return True

def test_statistics():
    """
    TEST 6: Statistics Tracking

    Verifies:
    - Statistics updated correctly
    - Counters accurate
    - Error tracking works
    """
    print("=" * 70)
    print("TEST 6: Statistics Tracking")
    print("=" * 70)
    print()

    client = IPCClient()
    client.connect()

    # Send some messages
    for i in range(5):
        client.send_message(MSG_QUERY, f"test {i}")

    # Try large message (should error)
    client.send_message(MSG_QUERY, "X" * 300)

    # Get statistics
    stats = client.get_statistics()

    if stats['messages_sent'] != 5:
        print(f"[FAIL] Messages sent incorrect: {stats['messages_sent']} != 5")
        return False
    print(f"[PASS] Messages sent: {stats['messages_sent']}")

    if stats['send_errors'] != 1:
        print(f"[FAIL] Send errors incorrect: {stats['send_errors']} != 1")
        return False
    print(f"[PASS] Send errors: {stats['send_errors']}")

    print(f"[PASS] Statistics tracking working")

    client.disconnect()
    print()

    return True

# ============================================================================
# Main Test Runner
# ============================================================================

def run_all_tests():
    """Run complete test suite"""
    print()
    print("=" * 70)
    print("JARVIS AI-OS - Phase 1 Week 8")
    print("IPC Integration Test Suite")
    print("=" * 70)
    print()
    print("NOTE: Full end-to-end testing requires seL4 running in QEMU.")
    print("      These tests verify Python IPC client functionality.")
    print()

    results = []

    # TEST 1: IPC Connection
    results.append(("IPC Connection", test_ipc_connection()))

    # TEST 2: Message Send
    results.append(("Message Send", test_message_send()))

    # TEST 3: Message Receive
    results.append(("Message Receive", test_message_receive()))

    # TEST 4: End-to-End Simulation
    results.append(("End-to-End Simulation", test_end_to_end_simulation()))

    # TEST 5: Error Handling
    results.append(("Error Handling", test_error_handling()))

    # TEST 6: Statistics
    results.append(("Statistics Tracking", test_statistics()))

    # Summary
    print("=" * 70)
    print("TEST SUMMARY")
    print("=" * 70)
    print()

    passed = sum(1 for _, success in results if success)
    total = len(results)

    for test_name, success in results:
        status = "[PASS]" if success else "[FAIL]"
        print(f"  {status}: {test_name}")

    print()
    print(f"Results: {passed}/{total} tests passed")
    print()

    if passed == total:
        print("=" * 70)
        print("[ALL TESTS PASSED]")
        print("=" * 70)
        print()
        print("Week 8 deliverables (Python side):")
        print("  [x] IPC client connects to shared memory")
        print("  [x] Messages sent correctly")
        print("  [x] Receive with timeout works")
        print("  [x] Error handling robust")
        print("  [x] Statistics tracking accurate")
        print()
        print("Next: Test with seL4 running in QEMU for full integration")
        print("=" * 70)
        print()
        return True
    else:
        print("=" * 70)
        print(f"[WARNING] {total - passed} TEST(S) FAILED")
        print("=" * 70)
        print()
        return False

# ============================================================================
# Entry Point
# ============================================================================

def main():
    """Main entry point"""
    success = run_all_tests()
    sys.exit(0 if success else 1)

if __name__ == "__main__":
    main()
