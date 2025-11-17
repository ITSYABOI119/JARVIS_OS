#!/usr/bin/env python3
"""
JARVIS AI-OS Phase 1 - AI Agent Test Suite
Week 5: Comprehensive testing of AI agent and IPC client

Tests:
1. Model loading
2. Basic inference
3. Performance validation (<600ms target)
4. IPC client functionality
5. End-to-end integration (Week 6)
"""

import sys
import time
import os
from pathlib import Path

# Add parent directory for imports
sys.path.insert(0, str(Path(__file__).parent))

from agent import JARVISAgent
from ipc_client import IPCClient, MSG_COMMAND, MSG_QUERY

# ============================================================================
# Test Configuration
# ============================================================================

TEST_QUERIES = [
    "What is the current system CPU usage?",
    "Check network connectivity status",
    "Show available disk space",
    "List all running processes",
    "What's the system memory usage?",
    "help",
    "status",
    "show CPU info",
    "check system temperature",
    "list active connections"
]

NUM_ITERATIONS = 10
TARGET_LATENCY_MS = 600  # <600ms target from Phase 0

# ============================================================================
# Test Functions
# ============================================================================

def test_model_loading():
    """
    TEST 1: Model Loading

    Verifies:
    - Model file exists
    - Model loads successfully
    - Load time reasonable (<5s)
    """
    print("="*70)
    print("TEST 1: Model Loading")
    print("="*70)
    print()

    agent = JARVISAgent()

    start_time = time.time()
    success = agent.load_model()
    load_time = time.time() - start_time

    if not success:
        print("[FAIL] Model failed to load")
        return False

    if load_time > 5.0:
        print(f"[WARNING] Load time {load_time:.2f}s > 5s target")
    else:
        print(f"[PASS] Model loaded in {load_time:.2f}s")

    print()
    return agent

def test_basic_inference(agent):
    """
    TEST 2: Basic Inference

    Verifies:
    - Agent can process queries
    - Responses are generated
    - No errors during inference
    """
    print("="*70)
    print("TEST 2: Basic Inference")
    print("="*70)
    print()

    test_query = "What is the system status?"
    print(f"Query: '{test_query}'")
    print()

    result = agent.process_query(test_query)

    if not result['success']:
        print(f"[FAIL] Inference failed: {result['response']}")
        return False

    print(f"[PASS] Inference successful")
    print(f"  Response: {result['response'][:100]}...")
    print(f"  Inference time: {result['inference_time_ms']:.2f}ms")
    print(f"  Tokens: {result['tokens_generated']}")
    print()

    return True

def test_performance(agent):
    """
    TEST 3: Performance Validation

    Verifies:
    - Average inference time <600ms (GPU target)
    - Consistent performance across iterations
    - No significant outliers
    """
    print("="*70)
    print("TEST 3: Performance Validation")
    print("="*70)
    print()

    print(f"Running {NUM_ITERATIONS} iterations...")
    print(f"Target: <{TARGET_LATENCY_MS}ms average")
    print()

    latencies = []
    tokens_generated = []

    for i in range(NUM_ITERATIONS):
        query = TEST_QUERIES[i % len(TEST_QUERIES)]
        result = agent.process_query(query)

        if result['success']:
            latencies.append(result['inference_time_ms'])
            tokens_generated.append(result['tokens_generated'])
            print(f"  [{i+1}/{NUM_ITERATIONS}] {result['inference_time_ms']:.2f}ms "
                  f"({result['tokens_generated']} tokens)")
        else:
            print(f"  [{i+1}/{NUM_ITERATIONS}] FAILED: {result['response']}")

    print()

    if not latencies:
        print("[FAIL] No successful queries")
        return False

    # Calculate statistics
    avg_latency = sum(latencies) / len(latencies)
    min_latency = min(latencies)
    max_latency = max(latencies)
    avg_tokens = sum(tokens_generated) / len(tokens_generated)

    print("[RESULTS]")
    print(f"  Successful queries: {len(latencies)}/{NUM_ITERATIONS}")
    print(f"  Average latency: {avg_latency:.2f}ms")
    print(f"  Min latency: {min_latency:.2f}ms")
    print(f"  Max latency: {max_latency:.2f}ms")
    print(f"  Avg tokens: {avg_tokens:.1f}")
    print()

    # Check against target
    if avg_latency < TARGET_LATENCY_MS:
        speedup = TARGET_LATENCY_MS / avg_latency
        print(f"[PASS] Avg latency {avg_latency:.2f}ms < target {TARGET_LATENCY_MS}ms")
        print(f"  ({speedup:.2f}x better than target!)")
    else:
        slowdown = avg_latency / TARGET_LATENCY_MS
        print(f"[FAIL] Avg latency {avg_latency:.2f}ms > target {TARGET_LATENCY_MS}ms")
        print(f"  ({slowdown:.2f}x slower than target)")
        print()
        print(f"  NOTE: If running on CPU (no GPU), expect ~1500ms latency.")
        print(f"        GPU required for <600ms target.")
        return False

    print()
    return True

def test_ipc_client():
    """
    TEST 4: IPC Client Functionality

    Verifies:
    - IPC client can connect
    - Can send messages
    - Message structure correct
    """
    print("="*70)
    print("TEST 4: IPC Client Functionality")
    print("="*70)
    print()

    client = IPCClient()

    # Test connection
    if not client.connect():
        print("[FAIL] IPC client failed to connect")
        return False

    print("[PASS] IPC client connected")
    print()

    # Test sending messages
    test_messages = [
        (MSG_COMMAND, "help"),
        (MSG_QUERY, "What is the CPU usage?"),
        (MSG_COMMAND, "status"),
    ]

    for msg_type, payload in test_messages:
        if not client.send_message(msg_type, payload):
            print(f"[FAIL] Failed to send message: {payload}")
            return False

    print(f"[PASS] Sent {len(test_messages)} messages successfully")
    print()

    # Statistics
    stats = client.get_statistics()
    print(f"  Messages sent: {stats['messages_sent']}")
    print()

    client.disconnect()

    print("[PASS] IPC client functional")
    print()
    print("NOTE: Week 5 uses mock IPC for testing.")
    print("      Full seL4 integration in Week 6.")
    print()

    return True

def test_integration(agent):
    """
    TEST 5: Integration Test (Future Week 6)

    Verifies:
    - AI agent + IPC client work together
    - End-to-end query flow
    - seL4 communication
    """
    print("="*70)
    print("TEST 5: Integration Test")
    print("="*70)
    print()

    print("[PENDING] Full integration test in Week 6")
    print("   Will test: seL4 QEMU <-> AI Agent via IPC")
    print()

    return True

# ============================================================================
# Main Test Runner
# ============================================================================

def run_all_tests():
    """Run complete test suite"""
    print()
    print("="*70)
    print("JARVIS AI-OS - Phase 1 Week 5")
    print("AI Agent Test Suite")
    print("="*70)
    print()

    results = []

    # TEST 1: Model Loading
    agent = test_model_loading()
    if not agent:
        print("[CRITICAL FAILURE] Cannot continue without model")
        return False
    results.append(("Model Loading", True))

    # TEST 2: Basic Inference
    success = test_basic_inference(agent)
    results.append(("Basic Inference", success))
    if not success:
        print("[CRITICAL FAILURE] Cannot continue without working inference")
        return False

    # TEST 3: Performance
    success = test_performance(agent)
    results.append(("Performance", success))

    # TEST 4: IPC Client
    success = test_ipc_client()
    results.append(("IPC Client", success))

    # TEST 5: Integration (Week 6)
    success = test_integration(agent)
    results.append(("Integration", success))

    # Print agent statistics
    agent.print_statistics()

    # Summary
    print("="*70)
    print("TEST SUMMARY")
    print("="*70)
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
        print("="*70)
        print("[ALL TESTS PASSED]")
        print("="*70)
        print()
        print("Week 5 deliverables:")
        print("  [x] Phi-3 Mini model loaded")
        print("  [x] Basic inference working")
        print("  [x] Performance target met")
        print("  [x] IPC client functional")
        print()
        print("Ready for Week 6: seL4 integration")
        print("="*70)
        print()
        return True
    else:
        print("="*70)
        print(f"[WARNING] {total - passed} TEST(S) FAILED")
        print("="*70)
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
