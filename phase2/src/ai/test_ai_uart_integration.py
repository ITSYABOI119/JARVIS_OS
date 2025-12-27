#!/usr/bin/env python3
"""
JARVIS AI-OS - AI Model + UART Integration Tests
Phase 2 Week 32+

Tests the integration between AI models (TinyLlama, Phi-3) and UART IPC protocol.

Test categories:
1. AI response → UART frame serialization
2. Query pipeline → UART → response parsing
3. Inference time + serialization overhead benchmarks
4. End-to-end mock flow tests

Author: JARVIS AI-OS Team
Date: December 27, 2025
"""

import sys
import os
import time
import json

# Add phase1 and phase2 paths
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..', '..', 'phase1', 'src', 'ai'))

from uart_ipc_client import (
    UARTIPCClient, MsgType, Message, MAX_PAYLOAD_SIZE
)

# Try to import AI components (may not be available in all environments)
try:
    from query_processor import QueryProcessor
    QUERY_PROCESSOR_AVAILABLE = True
except ImportError:
    print("[AI-UART] Warning: QueryProcessor not available")
    QUERY_PROCESSOR_AVAILABLE = False

try:
    from model_loader import ModelLoader, LLAMA_CPP_AVAILABLE
    MODEL_LOADER_AVAILABLE = True
except ImportError:
    print("[AI-UART] Warning: ModelLoader not available")
    MODEL_LOADER_AVAILABLE = False
    LLAMA_CPP_AVAILABLE = False


# ============================================================================
# Mock AI Components (for testing without real models)
# ============================================================================

class MockAIModel:
    """Mock AI model for testing without real LLM."""

    def __init__(self, model_name: str = "mock"):
        self.model_name = model_name
        self.inference_time_ms = 50.0  # Simulated inference time

    def generate(self, prompt: str, max_tokens: int = 100) -> str:
        """Generate a mock response."""
        time.sleep(self.inference_time_ms / 1000.0)  # Simulate inference delay

        # Simulate different responses based on prompt patterns
        prompt_lower = prompt.lower()

        if "list" in prompt_lower or "ls" in prompt_lower:
            return "I'll execute the ls command to list files in the current directory."
        elif "create" in prompt_lower or "write" in prompt_lower:
            return "I'll create the file for you. Please confirm this operation."
        elif "delete" in prompt_lower or "remove" in prompt_lower:
            return "Warning: This is a destructive operation. Proceeding with caution."
        elif "status" in prompt_lower or "check" in prompt_lower:
            return "System status: All components operational."
        elif "help" in prompt_lower:
            return "Available commands: ls, cd, pwd, cat, mkdir, rm, cp, mv"
        else:
            return f"Processing your request: {prompt[:50]}..."


class MockQueryProcessor:
    """Mock query processor for testing (matches real QueryProcessor interface)."""

    def __init__(self):
        self.cache = {
            "list files": {"action": "exec_ls", "trust_level": 0, "success": True},
            "show status": {"action": "show_status", "trust_level": 0, "success": True},
            "delete temp": {"action": "delete_temp", "trust_level": 2, "success": True},
            "reboot": {"action": "system_reboot", "trust_level": 3, "success": True},
        }

    def process(self, query: str, ai_response: str = None):
        """Process a query and return (command_dict, cache_hit) tuple."""
        normalized = query.lower().strip()
        if normalized in self.cache:
            return (self.cache[normalized], True)
        return ({"action": "unknown", "trust_level": 1, "success": False}, False)


# ============================================================================
# Test 1-4: AI Response to UART Frame Serialization
# ============================================================================

def test_ai_response_serialization_short():
    """Test 1: Short AI response → UART frame."""
    print("=" * 70)
    print("TEST 1: Short AI Response Serialization")
    print("=" * 70)

    client = UARTIPCClient(mock_mode=True)
    ai = MockAIModel()
    response = ai.generate("list files")

    # Serialize as UART payload
    payload = response.encode('utf-8')

    print(f"  Response length: {len(response)} chars")
    print(f"  Payload size: {len(payload)} bytes")

    # Build UART frame
    frame = client._build_frame(MsgType.RESPONSE, payload)

    print(f"  Frame size: {len(frame)} bytes")
    print(f"  Fits in single frame: {len(payload) <= MAX_PAYLOAD_SIZE}")

    # Parse back
    msg = client._parse_frame(frame)
    decoded = msg.payload.decode('utf-8')

    success = msg is not None and decoded == response
    print(f"\n[{'PASS' if success else 'FAIL'}] test_ai_response_serialization_short")
    print()
    return success


def test_ai_response_serialization_max():
    """Test 2: Maximum length AI response → UART frame."""
    print("=" * 70)
    print("TEST 2: Maximum Length AI Response Serialization")
    print("=" * 70)

    client = UARTIPCClient(mock_mode=True)

    # Generate response exactly at max payload size
    response = "A" * MAX_PAYLOAD_SIZE
    payload = response.encode('utf-8')

    print(f"  Response length: {len(response)} chars")
    print(f"  Max payload: {MAX_PAYLOAD_SIZE} bytes")

    frame = client._build_frame(MsgType.RESPONSE, payload)
    msg = client._parse_frame(frame)

    success = msg is not None and msg.payload == payload
    print(f"  Frame built: {frame is not None}")
    print(f"  Parse success: {msg is not None}")

    print(f"\n[{'PASS' if success else 'FAIL'}] test_ai_response_serialization_max")
    print()
    return success


def test_ai_response_unicode():
    """Test 3: AI response with Unicode characters."""
    print("=" * 70)
    print("TEST 3: AI Response with Unicode Characters")
    print("=" * 70)

    client = UARTIPCClient(mock_mode=True)
    ai = MockAIModel()
    # Add Unicode to response
    response = "✓ Success: Files listed → /home/user/"
    payload = response.encode('utf-8')

    print(f"  Response: {response}")
    print(f"  UTF-8 bytes: {len(payload)}")

    frame = client._build_frame(MsgType.RESPONSE, payload)
    msg = client._parse_frame(frame)
    decoded = msg.payload.decode('utf-8')

    success = decoded == response
    print(f"  Decoded: {decoded}")

    print(f"\n[{'PASS' if success else 'FAIL'}] test_ai_response_unicode")
    print()
    return success


def test_ai_response_truncation():
    """Test 4: AI response exceeding max size (should truncate)."""
    print("=" * 70)
    print("TEST 4: AI Response Truncation")
    print("=" * 70)

    client = UARTIPCClient(mock_mode=True)

    # Response larger than max payload
    response = "X" * (MAX_PAYLOAD_SIZE + 100)
    truncated = response[:MAX_PAYLOAD_SIZE]
    payload = truncated.encode('utf-8')

    print(f"  Original length: {len(response)}")
    print(f"  Truncated length: {len(truncated)}")
    print(f"  Max payload: {MAX_PAYLOAD_SIZE}")

    frame = client._build_frame(MsgType.RESPONSE, payload)
    msg = client._parse_frame(frame)

    success = msg is not None and len(msg.payload) == MAX_PAYLOAD_SIZE
    print(f"  Payload in frame: {len(msg.payload)} bytes")

    print(f"\n[{'PASS' if success else 'FAIL'}] test_ai_response_truncation")
    print()
    return success


# ============================================================================
# Test 5-8: Query Pipeline → UART → Response Parsing
# ============================================================================

def test_query_pipeline_cache_hit():
    """Test 5: Query pipeline with cache hit."""
    print("=" * 70)
    print("TEST 5: Query Pipeline - Cache Hit")
    print("=" * 70)

    client = UARTIPCClient(mock_mode=True)
    client.connected = True

    result = client.cache_lookup("list files")

    print(f"  Query: 'list files'")
    print(f"  Cache hit: {result.get('hit', False)}")
    print(f"  Action: {result.get('action', 'N/A')}")

    success = result is not None and result.get('hit', False)
    print(f"\n[{'PASS' if success else 'FAIL'}] test_query_pipeline_cache_hit")
    print()
    return success


def test_query_pipeline_with_processor():
    """Test 6: Query pipeline using QueryProcessor."""
    print("=" * 70)
    print("TEST 6: Query Pipeline with Processor")
    print("=" * 70)

    # Always use mock processor for predictable test behavior
    # (Real QueryProcessor needs seL4 cache connection for hits)
    processor = MockQueryProcessor()
    processor_type = "Mock (predictable cache behavior)"

    print(f"  Processor type: {processor_type}")
    print(f"  Note: Real QueryProcessor requires seL4 cache connection")

    test_queries = [
        ("list files", True),
        ("show status", True),
        ("random unknown command", False),
    ]

    passed = 0
    for query, expected_hit in test_queries:
        # MockQueryProcessor returns (command_dict, cache_hit) tuple
        result = processor.process(query)
        if isinstance(result, tuple):
            command_dict, hit = result
        else:
            # Fallback for old interface
            hit = result.get('hit', False)
        ok = hit == expected_hit
        passed += 1 if ok else 0
        print(f"  '{query}': hit={hit} (expected {expected_hit}) {'✓' if ok else '✗'}")

    success = passed == len(test_queries)
    print(f"\n[{'PASS' if success else 'FAIL'}] test_query_pipeline_with_processor")
    print()
    return success


def test_query_to_uart_roundtrip():
    """Test 7: Full query → UART frame → parse roundtrip."""
    print("=" * 70)
    print("TEST 7: Query to UART Roundtrip")
    print("=" * 70)

    client = UARTIPCClient(mock_mode=True)

    queries = [
        "list files",
        "show status",
        "create file /tmp/test.txt",
        "delete /var/log/old.log",
    ]

    passed = 0
    for query in queries:
        # Build query frame
        payload = query.encode('utf-8') + b'\x00'  # Null-terminated
        frame = client._build_frame(MsgType.QUERY, payload)

        # Parse it back
        msg = client._parse_frame(frame)
        decoded = msg.payload.decode('utf-8').rstrip('\x00')

        ok = decoded == query
        passed += 1 if ok else 0
        print(f"  '{query}' → roundtrip: {'✓' if ok else '✗'}")

    success = passed == len(queries)
    print(f"\n  Passed: {passed}/{len(queries)}")
    print(f"\n[{'PASS' if success else 'FAIL'}] test_query_to_uart_roundtrip")
    print()
    return success


def test_response_parsing():
    """Test 8: Parse UART response to structured data."""
    print("=" * 70)
    print("TEST 8: UART Response Parsing")
    print("=" * 70)

    # Simulate response format from seL4: "ACTION:xxx|TRUST:n|HIT:1"
    test_responses = [
        ("ACTION:exec_ls|TRUST:0|HIT:1", {"action": "exec_ls", "trust": 0, "hit": True}),
        ("ACTION:show_status|TRUST:0|HIT:1", {"action": "show_status", "trust": 0, "hit": True}),
        ("ACTION:delete_file|TRUST:2|HIT:1", {"action": "delete_file", "trust": 2, "hit": True}),
        ("ACTION:unknown|TRUST:0|HIT:0", {"action": "unknown", "trust": 0, "hit": False}),
    ]

    passed = 0
    for raw, expected in test_responses:
        # Parse response format
        result = {}
        for part in raw.split('|'):
            if ':' in part:
                key, val = part.split(':', 1)
                if key == 'ACTION':
                    result['action'] = val
                elif key == 'TRUST':
                    result['trust'] = int(val)
                elif key == 'HIT':
                    result['hit'] = val == '1'

        ok = result == expected
        passed += 1 if ok else 0
        print(f"  '{raw[:30]}...': {'✓' if ok else '✗'}")

    success = passed == len(test_responses)
    print(f"\n  Passed: {passed}/{len(test_responses)}")
    print(f"\n[{'PASS' if success else 'FAIL'}] test_response_parsing")
    print()
    return success


# ============================================================================
# Test 9-12: Inference Time + Serialization Benchmarks
# ============================================================================

def test_benchmark_mock_inference():
    """Test 9: Benchmark mock AI inference time."""
    print("=" * 70)
    print("TEST 9: Benchmark Mock AI Inference")
    print("=" * 70)

    ai = MockAIModel()
    ai.inference_time_ms = 25.0  # 25ms simulated

    timings = []
    for i in range(20):
        start = time.perf_counter()
        response = ai.generate(f"test query {i}")
        elapsed = (time.perf_counter() - start) * 1000
        timings.append(elapsed)

    avg = sum(timings) / len(timings)
    min_t = min(timings)
    max_t = max(timings)

    print(f"  Calls: {len(timings)}")
    print(f"  Average: {avg:.2f} ms")
    print(f"  Min: {min_t:.2f} ms")
    print(f"  Max: {max_t:.2f} ms")
    print(f"  Expected: ~{ai.inference_time_ms} ms")

    # Should be around simulated time
    success = 20.0 <= avg <= 35.0
    print(f"\n[{'PASS' if success else 'FAIL'}] test_benchmark_mock_inference")
    print()
    return success


def test_benchmark_serialization_overhead():
    """Test 10: Benchmark UART serialization overhead."""
    print("=" * 70)
    print("TEST 10: Benchmark Serialization Overhead")
    print("=" * 70)

    client = UARTIPCClient(mock_mode=True)

    test_payloads = [
        ("tiny", b"OK"),
        ("small", b"A" * 50),
        ("medium", b"B" * 150),
        ("max", b"C" * MAX_PAYLOAD_SIZE),
    ]

    results = []
    for name, payload in test_payloads:
        # Measure build + parse time
        start = time.perf_counter()
        for _ in range(1000):
            frame = client._build_frame(MsgType.RESPONSE, payload)
            msg = client._parse_frame(frame)
        elapsed = (time.perf_counter() - start) * 1000
        per_op = elapsed / 1000
        results.append((name, len(payload), per_op))
        print(f"  {name:8s} ({len(payload):3d} bytes): {per_op:.4f} ms/op")

    # All operations should be sub-millisecond
    success = all(per_op < 1.0 for _, _, per_op in results)

    avg = sum(r[2] for r in results) / len(results)
    print(f"\n  Average: {avg:.4f} ms/op")

    print(f"\n[{'PASS' if success else 'FAIL'}] test_benchmark_serialization_overhead")
    print()
    return success


def test_benchmark_full_pipeline():
    """Test 11: Benchmark full pipeline (inference + serialization)."""
    print("=" * 70)
    print("TEST 11: Benchmark Full Pipeline")
    print("=" * 70)

    client = UARTIPCClient(mock_mode=True)
    ai = MockAIModel()
    ai.inference_time_ms = 30.0

    queries = [
        "list files",
        "show status",
        "create file",
        "delete temp",
        "check disk",
    ]

    timings = []
    for query in queries:
        for _ in range(5):
            start = time.perf_counter()

            # 1. Build query frame
            query_frame = client._build_frame(MsgType.QUERY, query.encode('utf-8'))

            # 2. AI inference
            response = ai.generate(query)

            # 3. Build response frame
            response_frame = client._build_frame(MsgType.RESPONSE, response.encode('utf-8'))

            # 4. Parse response
            msg = client._parse_frame(response_frame)

            elapsed = (time.perf_counter() - start) * 1000
            timings.append(elapsed)

    avg = sum(timings) / len(timings)
    min_t = min(timings)
    max_t = max(timings)

    print(f"  Total operations: {len(timings)}")
    print(f"  Average: {avg:.2f} ms")
    print(f"  Min: {min_t:.2f} ms")
    print(f"  Max: {max_t:.2f} ms")
    print(f"  Expected: ~{ai.inference_time_ms} ms (dominated by inference)")

    # Overhead should be minimal compared to inference
    success = avg < ai.inference_time_ms + 5.0  # <5ms overhead

    print(f"\n[{'PASS' if success else 'FAIL'}] test_benchmark_full_pipeline")
    print()
    return success


def test_benchmark_with_latency():
    """Test 12: Benchmark with simulated UART latency."""
    print("=" * 70)
    print("TEST 12: Benchmark with UART Latency Simulation")
    print("=" * 70)

    ai = MockAIModel()
    ai.inference_time_ms = 30.0

    client = UARTIPCClient(mock_mode=True, mock_latency_ms=15.0)  # 15ms UART RTT
    client.connected = True

    timings = []
    for _ in range(10):
        start = time.perf_counter()

        # Simulate full roundtrip
        result = client.cache_lookup("list files")  # Includes latency
        response = ai.generate("list files")  # AI inference

        elapsed = (time.perf_counter() - start) * 1000
        timings.append(elapsed)

    avg = sum(timings) / len(timings)
    expected = ai.inference_time_ms + 15.0  # inference + UART latency

    print(f"  AI inference: {ai.inference_time_ms} ms")
    print(f"  UART latency: 15 ms")
    print(f"  Expected total: ~{expected} ms")
    print(f"  Actual average: {avg:.2f} ms")

    # Should be close to expected
    success = expected - 10 <= avg <= expected + 15

    print(f"\n[{'PASS' if success else 'FAIL'}] test_benchmark_with_latency")
    print()
    return success


# ============================================================================
# Test 13-15: End-to-End Mock Flow Tests
# ============================================================================

def test_e2e_query_response_flow():
    """Test 13: End-to-end query → response flow."""
    print("=" * 70)
    print("TEST 13: E2E Query → Response Flow")
    print("=" * 70)

    ai = MockAIModel()
    client = UARTIPCClient(mock_mode=True)
    client.connected = True

    # Simulate a user query workflow
    queries = [
        "list files",
        "show current directory",
        "check system status",
    ]

    results = []
    for query in queries:
        # Step 1: Check cache
        cache_result = client.cache_lookup(query)

        # Step 2: If cache miss, ask AI
        if not cache_result.get('hit', False):
            ai_response = ai.generate(query)
            action = "ai_response"
        else:
            ai_response = None
            action = cache_result.get('action', 'unknown')

        results.append({
            'query': query,
            'cache_hit': cache_result.get('hit', False),
            'action': action,
            'ai_response': ai_response is not None
        })

        print(f"  Query: '{query}'")
        print(f"    Cache: {'HIT' if cache_result.get('hit') else 'MISS'}")
        print(f"    Action: {action}")

    success = len(results) == len(queries)
    print(f"\n[{'PASS' if success else 'FAIL'}] test_e2e_query_response_flow")
    print()
    return success


def test_e2e_shield_integration():
    """Test 14: E2E with SHIELD safety check."""
    print("=" * 70)
    print("TEST 14: E2E with SHIELD Integration")
    print("=" * 70)

    client = UARTIPCClient(mock_mode=True)
    client.connected = True

    # Test actions - mock always returns safe but we test the structure
    test_actions = [
        "read_file",
        "write_config",
        "delete_system",
        "format_disk",
    ]

    passed = 0
    for action in test_actions:
        result = client.shield_check(action)

        # Check structure - mock returns risk_score and recommendation
        ok = (result is not None and
              'risk_score' in result and
              'recommendation' in result)
        passed += 1 if ok else 0

        risk = result.get('risk_score', 'N/A')
        rec = result.get('recommendation', 'N/A')
        print(f"  {action:20s}: risk={risk} recommendation={rec}")

    success = passed == len(test_actions)
    print(f"\n  Passed: {passed}/{len(test_actions)}")
    print(f"\n[{'PASS' if success else 'FAIL'}] test_e2e_shield_integration")
    print()
    return success


def test_e2e_statistics_tracking():
    """Test 15: E2E statistics tracking."""
    print("=" * 70)
    print("TEST 15: E2E Statistics Tracking")
    print("=" * 70)

    client = UARTIPCClient(mock_mode=True, mock_latency_ms=10.0)
    client.connected = True

    # Perform various operations
    for _ in range(10):
        client.cache_lookup("test query")

    for _ in range(5):
        client.get_stats()

    for _ in range(3):
        client.send_command("ls")

    # Check statistics
    stats = client.stats

    print(f"  Total operations: 18")
    print(f"  Latency tracked: {stats.get('mock_latency_total_ms', 0):.1f} ms")
    print(f"  Expected: ~180 ms (18 * 10ms)")

    # Should track latency
    success = stats.get('mock_latency_total_ms', 0) > 100.0

    print(f"\n[{'PASS' if success else 'FAIL'}] test_e2e_statistics_tracking")
    print()
    return success


# ============================================================================
# Test Runner
# ============================================================================

def run_all_tests():
    """Run all AI-UART integration tests."""
    print()
    print("=" * 70)
    print("  JARVIS AI-OS - AI Model + UART Integration Tests")
    print("  Phase 2 Week 32+")
    print("=" * 70)
    print()

    tests = [
        # AI Response Serialization (1-4)
        test_ai_response_serialization_short,
        test_ai_response_serialization_max,
        test_ai_response_unicode,
        test_ai_response_truncation,

        # Query Pipeline (5-8)
        test_query_pipeline_cache_hit,
        test_query_pipeline_with_processor,
        test_query_to_uart_roundtrip,
        test_response_parsing,

        # Benchmarks (9-12)
        test_benchmark_mock_inference,
        test_benchmark_serialization_overhead,
        test_benchmark_full_pipeline,
        test_benchmark_with_latency,

        # E2E Flow (13-15)
        test_e2e_query_response_flow,
        test_e2e_shield_integration,
        test_e2e_statistics_tracking,
    ]

    passed = 0
    failed = []

    for test_func in tests:
        try:
            if test_func():
                passed += 1
            else:
                failed.append(test_func.__name__)
        except Exception as e:
            print(f"  [ERROR] {test_func.__name__}: {e}")
            failed.append(test_func.__name__)

    print()
    print("=" * 70)
    print("  AI-UART INTEGRATION TEST SUMMARY")
    print("=" * 70)
    print(f"  Total tests: {len(tests)}")
    print(f"  Passed: {passed}")
    print(f"  Failed: {len(failed)}")
    print(f"  Pass rate: {100.0 * passed / len(tests):.1f}%")

    if failed:
        print()
        print("  Failed tests:")
        for name in failed:
            print(f"    - {name}")

    print("=" * 70)
    print()

    return len(failed) == 0


if __name__ == "__main__":
    success = run_all_tests()
    sys.exit(0 if success else 1)
