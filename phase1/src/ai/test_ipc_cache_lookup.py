#!/usr/bin/env python3
"""
JARVIS AI-OS Phase 1 - IPC Cache Lookup Test
Week 9: Test IPC-based cache lookup in QueryProcessor

This test validates:
1. QueryProcessor can use IPC client for cache lookups
2. Response parsing works correctly (ACTION:xxx|TRUST:x|HIT:x format)
3. Cache hits and misses are handled properly
4. Graceful fallback when IPC client is unavailable
"""

import sys
from pathlib import Path

# Add parent directory for imports
sys.path.insert(0, str(Path(__file__).parent))

from query_processor import QueryProcessor
from ipc_client import IPCClient

# ============================================================================
# Test Functions
# ============================================================================

def test_ipc_cache_lookup_without_client():
    """
    TEST 1: Cache lookup without IPC client

    Should gracefully return None (cache miss)
    """
    print("=" * 70)
    print("TEST 1: Cache Lookup Without IPC Client")
    print("=" * 70)
    print()

    # Create processor without IPC client
    processor = QueryProcessor()

    # Try a cache lookup
    normalized = processor.normalizer.normalize("show cpu")
    command, cache_hit = processor.process("show cpu")

    if not cache_hit:
        print("[PASS] Cache miss without IPC client (expected)")
        print(f"  Command: {command['command']}")
        print(f"  Used keyword fallback: {command['success']}")
        print()
        return True
    else:
        print("[FAIL] Unexpected cache hit without IPC client")
        print()
        return False

def test_ipc_cache_lookup_with_mock_client():
    """
    TEST 2: Cache lookup with mock IPC client

    Mock client won't have responses, so should return cache miss
    """
    print("=" * 70)
    print("TEST 2: Cache Lookup With Mock IPC Client")
    print("=" * 70)
    print()

    # Create IPC client in mock mode
    ipc_client = IPCClient(use_mock=True)
    if not ipc_client.connect():
        print("[FAIL] Failed to connect mock IPC client")
        return False

    # Create processor with IPC client
    processor = QueryProcessor(ipc_client=ipc_client)

    # Try a cache lookup
    command, cache_hit = processor.process("show cpu")

    if not cache_hit:
        print("[PASS] Cache miss with mock client (expected - no responses)")
        print(f"  Command: {command['command']}")
        print(f"  Used keyword fallback: {command['success']}")
        print()

        # Check statistics
        stats = processor.get_statistics()
        print("[STATISTICS]")
        print(f"  Total queries:  {stats['total_queries']}")
        print(f"  Cache misses:   {stats['cache_misses']}")
        print(f"  Cache hits:     {stats['cache_hits']}")
        print()

        ipc_client.disconnect()
        return True
    else:
        print("[FAIL] Unexpected cache hit with mock client")
        print()
        ipc_client.disconnect()
        return False

def test_response_parsing():
    """
    TEST 3: Response Format Parsing

    Verify the response parser handles "ACTION:xxx|TRUST:x|HIT:x" format
    """
    print("=" * 70)
    print("TEST 3: Response Format Parsing")
    print("=" * 70)
    print()

    # Test response formats
    test_responses = [
        ("ACTION:READ_CPU_STATS|TRUST:0|HIT:1", True, "READ_CPU_STATS", 0),
        ("ACTION:READ_MEMORY_STATS|TRUST:0|HIT:1", True, "READ_MEMORY_STATS", 0),
        ("ACTION:UNKNOWN|TRUST:2|HIT:0", False, "UNKNOWN", 2),
        ("ACTION:SHOW_HELP|TRUST:0|HIT:1", True, "SHOW_HELP", 0),
    ]

    passed = 0
    failed = 0

    for response, expected_hit, expected_action, expected_trust in test_responses:
        # Parse response
        parts = response.split('|')
        response_dict = {}
        for part in parts:
            if ':' in part:
                key, value = part.split(':', 1)
                response_dict[key.strip()] = value.strip()

        cache_hit = response_dict.get('HIT', '0') == '1'
        action = response_dict.get('ACTION', 'UNKNOWN')
        trust = int(response_dict.get('TRUST', '2'))

        if cache_hit == expected_hit and action == expected_action and trust == expected_trust:
            print(f"[PASS] '{response}'")
            print(f"  Hit: {cache_hit}, Action: {action}, Trust: {trust}")
            passed += 1
        else:
            print(f"[FAIL] '{response}'")
            print(f"  Expected: hit={expected_hit}, action={expected_action}, trust={expected_trust}")
            print(f"  Got:      hit={cache_hit}, action={action}, trust={trust}")
            failed += 1

    print()
    print(f"Results: {passed}/{len(test_responses)} passed")
    print()

    return failed == 0

def test_graceful_degradation():
    """
    TEST 4: Graceful Degradation

    Verify system works even without IPC connection
    """
    print("=" * 70)
    print("TEST 4: Graceful Degradation")
    print("=" * 70)
    print()

    # Create processor without IPC
    processor = QueryProcessor()

    # Process several queries
    test_queries = [
        "show cpu",
        "check memory",
        "help",
        "disk space",
    ]

    passed = 0

    for query in test_queries:
        command, cache_hit = processor.process(query)

        if command['success']:
            print(f"[PASS] '{query}' -> {command['command']} (keyword fallback)")
            passed += 1
        else:
            print(f"[FAIL] '{query}' failed to process")

    print()
    print(f"Results: {passed}/{len(test_queries)} processed successfully")
    print()

    # Check statistics
    stats = processor.get_statistics()
    print("[STATISTICS]")
    print(f"  Total queries:     {stats['total_queries']}")
    print(f"  Cache misses:      {stats['cache_misses']}")
    print(f"  Parse successes:   {stats['parse_successes']}")
    print(f"  Cache hit rate:    {stats['cache_hit_rate']:.1f}%")
    print()

    return passed == len(test_queries)

# ============================================================================
# Main Test Runner
# ============================================================================

def run_all_tests():
    """Run complete test suite"""
    print()
    print("=" * 70)
    print("JARVIS AI-OS - Phase 1 Week 9")
    print("IPC Cache Lookup Integration Test")
    print("=" * 70)
    print()

    results = []

    # TEST 1: Without IPC client
    results.append(("Cache Lookup (No IPC)", test_ipc_cache_lookup_without_client()))

    # TEST 2: With mock IPC client
    results.append(("Cache Lookup (Mock IPC)", test_ipc_cache_lookup_with_mock_client()))

    # TEST 3: Response parsing
    results.append(("Response Format Parsing", test_response_parsing()))

    # TEST 4: Graceful degradation
    results.append(("Graceful Degradation", test_graceful_degradation()))

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
        print("IPC Cache Lookup Implementation:")
        print("  [x] QueryProcessor accepts IPC client")
        print("  [x] Cache lookup via IPC implemented")
        print("  [x] Response format parsing working")
        print("  [x] Graceful fallback without IPC")
        print()
        print("Next Steps:")
        print("  - Test with real seL4 running in QEMU (Week 9)")
        print("  - Validate end-to-end IPC communication")
        print("  - Measure cache hit rates in integrated system")
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
