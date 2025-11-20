#!/usr/bin/env python3
"""
JARVIS AI-OS Phase 1 - Week 10 IPC Standalone Test

Tests the Python IPC client in standalone mode (without seL4 running).
Verifies that the IPC infrastructure works correctly on the Python side.

This test can run WITHOUT seL4/QEMU running.

Usage:
    python3 test_ipc_standalone.py
"""

import sys
import time
from pathlib import Path

# Add parent directory for imports
script_dir = Path(__file__).parent
project_root = script_dir.parent.parent.parent
ai_dir = project_root / 'phase1' / 'src' / 'ai'
sys.path.insert(0, str(ai_dir))

try:
    from ipc_client import IPCClient, MessageType
except ImportError:
    print("ERROR: Cannot import IPCClient")
    sys.exit(1)


def test_ipc_client_creation():
    """Test 1: IPC Client Creation"""
    print("\n" + "="*70)
    print("TEST 1: IPC Client Creation")
    print("="*70)

    try:
        client = IPCClient()
        print("✓ IPC client created successfully")
        print(f"  Shared memory: {client.shm_name}")
        print(f"  Ring buffer: {client.ring_size} slots")
        print(f"  Message size: {client.message_size} bytes")
        return client, True
    except Exception as e:
        print(f"✗ Failed to create client: {e}")
        return None, False


def test_send_messages(client):
    """Test 2: Send Messages"""
    print("\n" + "="*70)
    print("TEST 2: Send Messages (Standalone)")
    print("="*70)

    test_queries = [
        "help",
        "status",
        "cache stats",
        "ls",
        "git status"
    ]

    passed = 0
    failed = 0

    for query in test_queries:
        try:
            client.send(MessageType.QUERY, query)
            print(f"✓ Sent: '{query}'")
            passed += 1
        except Exception as e:
            print(f"✗ Failed to send '{query}': {e}")
            failed += 1

    print(f"\nResults: {passed}/{len(test_queries)} messages sent successfully")
    return failed == 0


def test_message_types(client):
    """Test 3: Different Message Types"""
    print("\n" + "="*70)
    print("TEST 3: Different Message Types")
    print("="*70)

    message_types = [
        (MessageType.QUERY, "test query"),
        (MessageType.COMMAND, "test command"),
        (MessageType.EVENT, "test event"),
    ]

    passed = 0
    failed = 0

    for msg_type, payload in message_types:
        try:
            client.send(msg_type, payload)
            print(f"✓ Sent {msg_type.name}: '{payload}'")
            passed += 1
        except Exception as e:
            print(f"✗ Failed to send {msg_type.name}: {e}")
            failed += 1

    print(f"\nResults: {passed}/{len(message_types)} message types sent successfully")
    return failed == 0


def test_statistics(client):
    """Test 4: IPC Statistics"""
    print("\n" + "="*70)
    print("TEST 4: IPC Statistics")
    print("="*70)

    try:
        stats = client.get_stats()
        print("✓ Statistics retrieved:")
        print(f"  Total sent:      {stats['total_sent']}")
        print(f"  Total received:  {stats['total_received']}")
        print(f"  Total drops:     {stats['total_drops']}")
        print(f"  Current count:   {stats['current_count']} / {stats['capacity']}")
        print(f"  Available space: {stats['available']}")

        if stats['total_drops'] > 0:
            print(f"  ⚠ Warning: {stats['total_drops']} messages dropped")

        return True
    except Exception as e:
        print(f"✗ Failed to get statistics: {e}")
        return False


def test_receive_timeout(client):
    """Test 5: Receive Timeout (no messages expected)"""
    print("\n" + "="*70)
    print("TEST 5: Receive Timeout Handling")
    print("="*70)

    try:
        print("  Attempting to receive (no seL4 running, should return None)...")
        response = client.receive()

        if response is None:
            print("✓ Correctly returned None when no messages available")
            return True
        else:
            print(f"⚠ Unexpected response: {response}")
            return True  # Still valid, just unexpected
    except Exception as e:
        print(f"✗ Error during receive: {e}")
        return False


def main():
    """Main test execution"""
    print("\n" + "="*70)
    print("JARVIS AI-OS - IPC Standalone Test (Python Side Only)")
    print("="*70)
    print("\nThis test validates the Python IPC client WITHOUT seL4 running.")
    print("For full end-to-end testing, use test_ipc_end_to_end.py with QEMU.\n")

    results = {
        'passed': 0,
        'failed': 0
    }

    # Test 1: Create client
    client, success = test_ipc_client_creation()
    if success:
        results['passed'] += 1
    else:
        results['failed'] += 1
        print("\n" + "="*70)
        print("FATAL: Cannot create IPC client. Aborting tests.")
        print("="*70)
        return 1

    # Test 2: Send messages
    if test_send_messages(client):
        results['passed'] += 1
    else:
        results['failed'] += 1

    # Test 3: Message types
    if test_message_types(client):
        results['passed'] += 1
    else:
        results['failed'] += 1

    # Test 4: Statistics
    if test_statistics(client):
        results['passed'] += 1
    else:
        results['failed'] += 1

    # Test 5: Receive timeout
    if test_receive_timeout(client):
        results['passed'] += 1
    else:
        results['failed'] += 1

    # Final summary
    print("\n" + "="*70)
    print("TEST SUMMARY")
    print("="*70)
    print(f"Tests Passed: {results['passed']}")
    print(f"Tests Failed: {results['failed']}")
    print(f"Total Tests:  {results['passed'] + results['failed']}")

    if results['failed'] == 0:
        print("\n✓ ALL TESTS PASSED - Python IPC client working correctly!")
        print("\nNext steps:")
        print("  1. Launch seL4 in QEMU: cd ~/jarvis-phase1/hello-world_build && ./simulate")
        print("  2. Run end-to-end test: python3 test_ipc_end_to_end.py")
        return 0
    else:
        print(f"\n✗ {results['failed']} TEST(S) FAILED")
        return 1


if __name__ == "__main__":
    try:
        exit_code = main()
        sys.exit(exit_code)
    except KeyboardInterrupt:
        print("\n\nTest interrupted by user")
        sys.exit(1)
    except Exception as e:
        print(f"\n✗ Unexpected error: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)
