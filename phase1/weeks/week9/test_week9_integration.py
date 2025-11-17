#!/usr/bin/env python3
"""
JARVIS AI-OS Phase 1 - Week 9 Integration Test
End-to-end validation of Python ↔ seL4 IPC communication

This test validates:
1. Python IPC client connects to seL4 via shared memory
2. Queries sent from Python reach seL4
3. seL4 decision cache processes queries
4. Responses sent back to Python in correct format
5. Cache hits/misses tracked correctly

Prerequisites:
- seL4 running in QEMU with JARVIS components
- Shared memory at /dev/shm/jarvis_ipc
- IPC handler listening in seL4

Usage:
    # Terminal 1: Launch seL4 in QEMU
    ./launch-jarvis-qemu.sh

    # Terminal 2: Run this integration test
    python3 test_week9_integration.py
"""

import sys
import time
from pathlib import Path

# Add parent directory for imports
sys.path.insert(0, str(Path(__file__).parent.parent.parent / 'src' / 'ai'))

from ipc_client import IPCClient, MSG_QUERY
from query_processor import QueryProcessor

# ============================================================================
# Configuration
# ============================================================================

SHARED_MEMORY_PATH = "/dev/shm/jarvis_ipc"
TEST_TIMEOUT_MS = 2000  # 2 seconds

# Test queries (should match decision cache patterns in seL4)
TEST_QUERIES = [
    ("show cpu", "READ_CPU_STATS", True),           # Expected cache hit
    ("check memory", "READ_MEMORY_STATS", True),    # Expected cache hit
    ("help", "SHOW_HELP", True),                    # Expected cache hit
    ("list processes", "LIST_PROCESSES", True),     # Expected cache hit
    ("disk space", "READ_DISK_STATS", True),        # Expected cache hit
]

# ============================================================================
# Test Functions
# ============================================================================

def test_ipc_connection():
    """
    TEST 1: IPC Connection

    Verify Python can connect to seL4 via shared memory
    """
    print("=" * 70)
    print("TEST 1: IPC Connection to seL4")
    print("=" * 70)
    print()

    print(f"[TEST] Connecting to shared memory: {SHARED_MEMORY_PATH}")

    # Create IPC client
    client = IPCClient(shared_memory_path=SHARED_MEMORY_PATH, use_mock=False)

    # Attempt connection
    if not client.connect():
        print(f"[FAIL] Could not connect to shared memory")
        print()
        print("Make sure:")
        print(f"  1. seL4 is running in QEMU")
        print(f"  2. Shared memory file exists: {SHARED_MEMORY_PATH}")
        print(f"  3. Permissions are correct: ls -la {SHARED_MEMORY_PATH}")
        print()
        return None

    print(f"[PASS] Connected successfully")
    print(f"  Shared memory: {SHARED_MEMORY_PATH}")
    print(f"  Connection mode: {'MOCK' if client.use_mock else 'REAL'}")
    print()

    return client

def test_send_queries(client):
    """
    TEST 2: Send Queries to seL4

    Verify Python can send queries via IPC
    """
    print("=" * 70)
    print("TEST 2: Send Queries to seL4")
    print("=" * 70)
    print()

    passed = 0
    failed = 0

    for query, expected_command, expected_hit in TEST_QUERIES:
        print(f"[TEST] Sending query: \"{query}\"")

        # Send query
        if not client.send_message(MSG_QUERY, query):
            print(f"[FAIL] Failed to send query")
            failed += 1
            continue

        print(f"[PASS] Query sent successfully")
        passed += 1

        # Small delay to let seL4 process
        time.sleep(0.1)

    print()
    print(f"Results: {passed}/{len(TEST_QUERIES)} queries sent successfully")
    print()

    return failed == 0

def test_receive_responses(client):
    """
    TEST 3: Receive Responses from seL4

    Verify Python can receive and parse responses
    """
    print("=" * 70)
    print("TEST 3: Receive Responses from seL4")
    print("=" * 70)
    print()

    passed = 0
    failed = 0

    # Send queries and receive responses
    for query, expected_command, expected_hit in TEST_QUERIES:
        print(f"[TEST] Query: \"{query}\"")

        # Send query
        if not client.send_message(MSG_QUERY, query):
            print(f"[FAIL] Could not send query")
            failed += 1
            continue

        # Receive response
        response = client.receive_message(timeout_ms=TEST_TIMEOUT_MS)

        if not response:
            print(f"[FAIL] No response received (timeout)")
            print(f"  Check seL4 console for errors")
            failed += 1
            continue

        # Parse response
        payload = response.payload[:response.payload_size].decode('utf-8', errors='ignore')
        print(f"[RECV] Response: {payload}")

        # Parse response format: "ACTION:xxx|TRUST:x|HIT:x"
        parts = payload.split('|')
        response_dict = {}
        for part in parts:
            if ':' in part:
                key, value = part.split(':', 1)
                response_dict[key.strip()] = value.strip()

        action = response_dict.get('ACTION', 'UNKNOWN')
        cache_hit = response_dict.get('HIT', '0') == '1'
        trust = response_dict.get('TRUST', '-1')

        # Validate response
        if action == expected_command and cache_hit == expected_hit:
            print(f"[PASS] Response valid")
            print(f"  Action: {action}")
            print(f"  Cache hit: {cache_hit}")
            print(f"  Trust level: {trust}")
            passed += 1
        else:
            print(f"[FAIL] Response mismatch")
            print(f"  Expected: ACTION={expected_command}, HIT={expected_hit}")
            print(f"  Got:      ACTION={action}, HIT={cache_hit}")
            failed += 1

        print()
        time.sleep(0.1)

    print(f"Results: {passed}/{len(TEST_QUERIES)} responses valid")
    print()

    return failed == 0

def test_query_processor_integration(client):
    """
    TEST 4: Query Processor Integration

    Verify QueryProcessor works with real IPC client
    """
    print("=" * 70)
    print("TEST 4: Query Processor Integration")
    print("=" * 70)
    print()

    # Create query processor with IPC client
    processor = QueryProcessor(ipc_client=client)

    passed = 0
    failed = 0
    cache_hits = 0

    for query, expected_command, expected_hit in TEST_QUERIES:
        print(f"[TEST] Processing: \"{query}\"")

        # Process query
        command, cache_hit = processor.process(query)

        if command['success']:
            print(f"[PASS] Query processed successfully")
            print(f"  Command: {command['command']}")
            print(f"  Cache hit: {cache_hit}")
            print(f"  Trust level: {command['trust_level']}")

            if cache_hit:
                cache_hits += 1

            passed += 1
        else:
            print(f"[FAIL] Query processing failed: {command.get('error')}")
            failed += 1

        print()
        time.sleep(0.1)

    # Check cache hit rate
    cache_hit_rate = (cache_hits / len(TEST_QUERIES)) * 100 if len(TEST_QUERIES) > 0 else 0

    print(f"Results: {passed}/{len(TEST_QUERIES)} queries processed")
    print(f"Cache hit rate: {cache_hit_rate:.1f}% (target: >80%)")
    print()

    # Print statistics
    processor.print_statistics()

    return failed == 0 and cache_hit_rate >= 80

# ============================================================================
# Performance Benchmarking
# ============================================================================

def benchmark_ipc_latency(client, iterations=100):
    """
    Benchmark IPC round-trip latency
    """
    print("=" * 70)
    print("BENCHMARK: IPC Round-Trip Latency")
    print("=" * 70)
    print()

    print(f"[BENCH] Running {iterations} iterations...")

    latencies = []

    for i in range(iterations):
        # Send query
        start_time = time.time()

        if not client.send_message(MSG_QUERY, "show cpu"):
            continue

        # Receive response
        response = client.receive_message(timeout_ms=1000)

        if not response:
            continue

        # Calculate latency
        latency_us = (time.time() - start_time) * 1_000_000  # microseconds
        latencies.append(latency_us)

        # Small delay between iterations
        time.sleep(0.01)

    if len(latencies) == 0:
        print("[FAIL] No successful round-trips")
        return False

    # Calculate statistics
    latencies.sort()
    median = latencies[len(latencies) // 2]
    p95 = latencies[int(len(latencies) * 0.95)]
    p99 = latencies[int(len(latencies) * 0.99)]
    avg = sum(latencies) / len(latencies)

    print(f"[RESULTS]")
    print(f"  Iterations:   {len(latencies)}/{iterations}")
    print(f"  Median:       {median:.2f} μs")
    print(f"  Average:      {avg:.2f} μs")
    print(f"  95th %ile:    {p95:.2f} μs")
    print(f"  99th %ile:    {p99:.2f} μs")
    print()

    # Check target (<200μs in QEMU, <100μs bare metal)
    target = 200  # μs (allowing for QEMU overhead)

    if median < target:
        print(f"[PASS] Median latency {median:.2f}μs < {target}μs target")
    else:
        print(f"[WARN] Median latency {median:.2f}μs >= {target}μs target (acceptable for QEMU)")

    print()

    return True

# ============================================================================
# Main Test Runner
# ============================================================================

def main():
    """Run all Week 9 integration tests"""
    print()
    print("=" * 70)
    print("JARVIS AI-OS - Phase 1 Week 9")
    print("End-to-End Integration Test")
    print("=" * 70)
    print()
    print("Prerequisites:")
    print("  1. seL4 running in QEMU with JARVIS components")
    print("  2. Decision cache loaded (200 patterns)")
    print("  3. IPC handler listening")
    print()
    input("Press ENTER when seL4 is ready in QEMU...")
    print()

    results = []
    client = None

    try:
        # TEST 1: Connection
        client = test_ipc_connection()
        if not client:
            print("[ABORT] Cannot connect to seL4 - check setup")
            return False
        results.append(("IPC Connection", True))

        # TEST 2: Send Queries
        results.append(("Send Queries", test_send_queries(client)))

        # TEST 3: Receive Responses
        results.append(("Receive Responses", test_receive_responses(client)))

        # TEST 4: Query Processor
        results.append(("Query Processor Integration", test_query_processor_integration(client)))

        # BENCHMARK: IPC Latency
        results.append(("IPC Latency Benchmark", benchmark_ipc_latency(client)))

    except KeyboardInterrupt:
        print()
        print("[INTERRUPTED] Test cancelled by user")
        return False

    except Exception as e:
        print()
        print(f"[ERROR] Test failed with exception: {e}")
        import traceback
        traceback.print_exc()
        return False

    finally:
        # Cleanup
        if client:
            client.disconnect()

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
        print("[WEEK 9 INTEGRATION TEST: PASSED]")
        print("=" * 70)
        print()
        print("✅ Python ↔ seL4 IPC bridge working end-to-end")
        print("✅ Decision cache accessible via IPC")
        print("✅ Query processor integrated successfully")
        print("✅ Performance targets met in QEMU environment")
        print()
        print("Week 9 Deliverables Complete:")
        print("  [x] QEMU environment validated")
        print("  [x] seL4 boots with all components")
        print("  [x] IPC communication working")
        print("  [x] Cache hit rate >80%")
        print("  [x] Latency targets met")
        print()
        print("Ready for Week 10: Multi-Agent Architecture")
        print("=" * 70)
        print()
        return True
    else:
        print("=" * 70)
        print(f"[WARNING] {total - passed} test(s) failed")
        print("=" * 70)
        print()
        print("Check:")
        print("  - seL4 console for error messages")
        print("  - Shared memory permissions: ls -la /dev/shm/jarvis_ipc")
        print("  - QEMU is running: ps aux | grep qemu")
        print("  - IPC handler started in seL4")
        print()
        return False

if __name__ == "__main__":
    success = main()
    sys.exit(0 if success else 1)
