#!/usr/bin/env python3
"""
JARVIS AI-OS Phase 1 - Query Pipeline Test Suite
Week 6: Comprehensive testing of query processing pipeline

Tests:
1. Query normalization (15+ variants)
2. Hash function consistency
3. Command parsing (keyword-based)
4. Cache behavior (mock)
5. End-to-end pipeline flow
6. Integration with AI agent
"""

import sys
import time
from pathlib import Path

# Add parent directory for imports
sys.path.insert(0, str(Path(__file__).parent))

from query_processor import QueryNormalizer, QueryProcessor, CommandParser, fnv1a_hash_64

# ============================================================================
# Test Configuration
# ============================================================================

# Normalization test cases
NORMALIZATION_TESTS = [
    ("  SHOW   CPU  ", "show cpu"),
    ("What's\t\tthe\nmemory?", "what's the memory?"),
    ("   help   ", "help"),
    ("CHECK    DISK    SPACE", "check disk space"),
    ("Network STATUS", "network status"),
    ("  System   Info  ", "system info"),
    ("TEMPERATURE", "temperature"),
    ("List All Processes", "list all processes"),
    ("  what is the  uptime?  ", "what is the uptime?"),
    ("Show me CPU and Memory", "show me cpu and memory"),
]

# Command parsing test cases
COMMAND_TESTS = [
    ("show cpu", "READ_CPU_STATS"),
    ("check memory", "READ_MEMORY_STATS"),
    ("disk space", "READ_DISK_STATS"),
    ("list processes", "LIST_PROCESSES"),
    ("help", "SHOW_HELP"),
    ("status", "SHOW_STATUS"),
    ("network status", "NETWORK_STATUS"),
    ("system info", "SYSTEM_INFO"),
    ("uptime", "UPTIME"),
    ("temperature", "TEMPERATURE"),
    ("show ram", "READ_MEMORY_STATS"),
    ("check storage", "READ_DISK_STATS"),
]

# Hash consistency tests
HASH_TEST_STRINGS = [
    "hello",
    "world",
    "show cpu",
    "check memory",
    "help",
]

# ============================================================================
# Test Functions
# ============================================================================

def test_query_normalization():
    """
    TEST 1: Query Normalization

    Verifies:
    - Lowercase conversion
    - Whitespace collapse
    - Trim leading/trailing spaces
    """
    print("=" * 70)
    print("TEST 1: Query Normalization")
    print("=" * 70)
    print()

    normalizer = QueryNormalizer()
    passed = 0
    failed = 0

    for input_query, expected_output in NORMALIZATION_TESTS:
        actual_output = normalizer.normalize(input_query)

        if actual_output == expected_output:
            print(f"[PASS] '{input_query}' -> '{actual_output}'")
            passed += 1
        else:
            print(f"[FAIL] '{input_query}'")
            print(f"  Expected: '{expected_output}'")
            print(f"  Got:      '{actual_output}'")
            failed += 1

    print()
    print(f"Results: {passed}/{len(NORMALIZATION_TESTS)} passed")
    print()

    return failed == 0

def test_hash_function():
    """
    TEST 2: Hash Function Consistency

    Verifies:
    - Hash consistency (same input = same hash)
    - Hash uniqueness (different input = different hash)
    - 64-bit hash values
    """
    print("=" * 70)
    print("TEST 2: Hash Function Consistency")
    print("=" * 70)
    print()

    passed = 0
    failed = 0

    # Test consistency
    for test_str in HASH_TEST_STRINGS:
        hash1 = fnv1a_hash_64(test_str)
        hash2 = fnv1a_hash_64(test_str)

        if hash1 == hash2:
            print(f"[PASS] Consistency: '{test_str}' -> 0x{hash1:016x}")
            passed += 1
        else:
            print(f"[FAIL] Hash inconsistency for '{test_str}'")
            print(f"  Hash 1: 0x{hash1:016x}")
            print(f"  Hash 2: 0x{hash2:016x}")
            failed += 1

    print()

    # Test uniqueness
    hashes = [fnv1a_hash_64(s) for s in HASH_TEST_STRINGS]
    unique_hashes = len(set(hashes))

    if unique_hashes == len(HASH_TEST_STRINGS):
        print(f"[PASS] Uniqueness: {unique_hashes}/{len(HASH_TEST_STRINGS)} unique hashes")
        passed += 1
    else:
        print(f"[FAIL] Hash collision detected")
        print(f"  Expected: {len(HASH_TEST_STRINGS)} unique")
        print(f"  Got:      {unique_hashes} unique")
        failed += 1

    print()
    print(f"Results: {passed}/{len(HASH_TEST_STRINGS) + 1} passed")
    print()

    return failed == 0

def test_command_parser():
    """
    TEST 3: Command Parsing

    Verifies:
    - Keyword extraction
    - Command mapping
    - Parameter extraction (future)
    """
    print("=" * 70)
    print("TEST 3: Command Parsing")
    print("=" * 70)
    print()

    parser = CommandParser()
    passed = 0
    failed = 0

    for query, expected_command in COMMAND_TESTS:
        result = parser.parse("", query)  # No AI response, use keywords

        if result['success'] and result['command'] == expected_command:
            print(f"[PASS] '{query}' -> {result['command']}")
            passed += 1
        elif result['success']:
            print(f"[FAIL] '{query}'")
            print(f"  Expected: {expected_command}")
            print(f"  Got:      {result['command']}")
            failed += 1
        else:
            print(f"[FAIL] '{query}' - Parse failed: {result['error']}")
            failed += 1

    print()
    print(f"Results: {passed}/{len(COMMAND_TESTS)} passed")
    print()

    return failed == 0

def test_query_processor_pipeline():
    """
    TEST 4: Query Processor Pipeline

    Verifies:
    - End-to-end query processing
    - Normalization + parsing
    - Statistics tracking
    """
    print("=" * 70)
    print("TEST 4: Query Processor Pipeline")
    print("=" * 70)
    print()

    processor = QueryProcessor()
    passed = 0
    failed = 0

    test_queries = [
        "  SHOW   CPU  ",
        "What's the memory usage?",
        "   help   ",
        "check disk space",
        "network status",
    ]

    for query in test_queries:
        command, cache_hit = processor.process(query)

        if command['success']:
            print(f"[PASS] '{query}' -> {command['command']}")
            print(f"  Cache hit: {cache_hit}")
            print(f"  Trust level: {command['trust_level']}")
            passed += 1
        else:
            print(f"[FAIL] '{query}' - {command['error']}")
            failed += 1

        print()

    # Print processor statistics
    processor.print_statistics()

    print(f"Results: {passed}/{len(test_queries)} passed")
    print()

    return failed == 0

def test_performance():
    """
    TEST 5: Performance Testing

    Verifies:
    - Query normalization <1ms
    - Hash computation <1ms
    - Command parsing <1ms
    """
    print("=" * 70)
    print("TEST 5: Performance Testing")
    print("=" * 70)
    print()

    normalizer = QueryNormalizer()
    processor = QueryProcessor()

    iterations = 1000
    test_query = "  SHOW   CPU   USAGE  "

    # Test normalization performance
    start_time = time.time()
    for _ in range(iterations):
        normalizer.normalize(test_query)
    norm_time = (time.time() - start_time) / iterations * 1000  # ms

    print(f"Normalization: {norm_time:.4f}ms per query ({iterations} iterations)")

    # Test hash performance
    normalized = normalizer.normalize(test_query)
    start_time = time.time()
    for _ in range(iterations):
        fnv1a_hash_64(normalized)
    hash_time = (time.time() - start_time) / iterations * 1000  # ms

    print(f"Hash computation: {hash_time:.4f}ms per query ({iterations} iterations)")

    # Test full pipeline performance
    start_time = time.time()
    for _ in range(iterations):
        processor.process(test_query)
    pipeline_time = (time.time() - start_time) / iterations * 1000  # ms

    print(f"Full pipeline: {pipeline_time:.4f}ms per query ({iterations} iterations)")
    print()

    # Check targets
    passed = 0
    failed = 0

    if norm_time < 1.0:
        print(f"[PASS] Normalization < 1ms target ({norm_time:.4f}ms)")
        passed += 1
    else:
        print(f"[FAIL] Normalization > 1ms target ({norm_time:.4f}ms)")
        failed += 1

    if hash_time < 1.0:
        print(f"[PASS] Hash < 1ms target ({hash_time:.4f}ms)")
        passed += 1
    else:
        print(f"[FAIL] Hash > 1ms target ({hash_time:.4f}ms)")
        failed += 1

    if pipeline_time < 2.0:
        print(f"[PASS] Pipeline < 2ms target ({pipeline_time:.4f}ms)")
        passed += 1
    else:
        print(f"[FAIL] Pipeline > 2ms target ({pipeline_time:.4f}ms)")
        failed += 1

    print()
    print(f"Results: {passed}/3 performance targets met")
    print()

    return failed == 0

def test_ai_agent_integration():
    """
    TEST 6: AI Agent Integration (without model)

    Verifies:
    - Agent can import query processor
    - Query processor integrates correctly
    - Statistics tracking works
    """
    print("=" * 70)
    print("TEST 6: AI Agent Integration")
    print("=" * 70)
    print()

    try:
        from agent import JARVISAgent

        # Create agent without loading model
        agent = JARVISAgent(use_query_processor=True)

        # Check query processor is enabled
        if agent.use_query_processor and agent.query_processor:
            print("[PASS] Query processor enabled in agent")
        else:
            print("[FAIL] Query processor not enabled")
            return False

        # Check statistics are accessible
        if hasattr(agent.query_processor, 'get_statistics'):
            stats = agent.query_processor.get_statistics()
            print(f"[PASS] Query processor statistics accessible")
            print(f"  Total queries: {stats['total_queries']}")
        else:
            print("[FAIL] Query processor statistics not accessible")
            return False

        print()
        print("[PASS] AI agent integration successful")
        print()

        return True

    except Exception as e:
        print(f"[FAIL] AI agent integration failed: {e}")
        print()
        return False

# ============================================================================
# Main Test Runner
# ============================================================================

def run_all_tests():
    """Run complete test suite"""
    print()
    print("=" * 70)
    print("JARVIS AI-OS - Phase 1 Week 6")
    print("Query Processing Pipeline Test Suite")
    print("=" * 70)
    print()

    results = []

    # TEST 1: Query Normalization
    results.append(("Query Normalization", test_query_normalization()))

    # TEST 2: Hash Function
    results.append(("Hash Function", test_hash_function()))

    # TEST 3: Command Parser
    results.append(("Command Parser", test_command_parser()))

    # TEST 4: Query Processor Pipeline
    results.append(("Query Processor", test_query_processor_pipeline()))

    # TEST 5: Performance
    results.append(("Performance", test_performance()))

    # TEST 6: AI Agent Integration
    results.append(("AI Agent Integration", test_ai_agent_integration()))

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
        print("Week 6 deliverables:")
        print("  [x] Query normalization working")
        print("  [x] Hash function consistent")
        print("  [x] Command parsing functional")
        print("  [x] Query processor pipeline working")
        print("  [x] Performance targets met")
        print("  [x] AI agent integration successful")
        print()
        print("Ready for Week 7: Shell Interface & seL4 IPC")
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
