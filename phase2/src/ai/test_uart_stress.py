#!/usr/bin/env python3
"""
JARVIS AI-OS - UART IPC Client Stress Tests
Phase 2 Week 32+

Extended stress tests for UART protocol robustness.
All tests run in mock mode (no pyserial required).

Test Categories:
- Rapid message burst (4 tests)
- CRC error recovery (3 tests)
- Sequence number stress (3 tests)
- Timeout behavior (3 tests)
- Edge cases (3 tests)

Total: 16 stress tests
"""

import sys
import time
import struct
import random
from pathlib import Path

# Add parent directory to path for imports
sys.path.insert(0, str(Path(__file__).parent))

from uart_ipc_client import (
    crc16_ccitt, UARTIPCClient, MsgType, MsgFlags, ErrorCode, Message,
    SYNC_BYTES, MAX_PAYLOAD_SIZE, HEADER_SIZE, CRC_SIZE, MAX_FRAME_SIZE
)


# ============================================================================
# Rapid Message Burst Tests
# ============================================================================

def test_rapid_frame_building_100():
    """Stress Test 1: Build 100 frames rapidly."""
    print("=" * 70)
    print("STRESS TEST 1: Build 100 Frames Rapidly")
    print("=" * 70)

    client = UARTIPCClient()
    client.seq_tx = 0

    start = time.perf_counter()
    frames = []
    for i in range(100):
        payload = f"query_{i:03d}".encode('utf-8')
        frame = client._build_frame(MsgType.QUERY, payload)
        frames.append(frame)
    elapsed = time.perf_counter() - start

    all_valid = all(f[:2] == SYNC_BYTES for f in frames)
    all_unique = len(set(frames)) == 100  # Each frame should be unique (diff seq)

    print(f"  Frames built: {len(frames)}")
    print(f"  Time: {elapsed * 1000:.2f} ms")
    print(f"  Rate: {100 / elapsed:.0f} frames/sec")
    print(f"  All valid sync: {all_valid}")
    print(f"  All unique: {all_unique}")

    success = len(frames) == 100 and all_valid and elapsed < 1.0
    print(f"\n[{'PASS' if success else 'FAIL'}] test_rapid_frame_building_100")
    print()
    return success


def test_rapid_frame_building_1000():
    """Stress Test 2: Build 1000 frames rapidly."""
    print("=" * 70)
    print("STRESS TEST 2: Build 1000 Frames Rapidly")
    print("=" * 70)

    client = UARTIPCClient()
    client.seq_tx = 0

    start = time.perf_counter()
    frames = []
    for i in range(1000):
        payload = f"q{i:04d}".encode('utf-8')
        frame = client._build_frame(MsgType.QUERY, payload)
        frames.append(frame)
    elapsed = time.perf_counter() - start

    print(f"  Frames built: {len(frames)}")
    print(f"  Time: {elapsed * 1000:.2f} ms")
    print(f"  Rate: {1000 / elapsed:.0f} frames/sec")

    success = len(frames) == 1000 and elapsed < 5.0
    print(f"\n[{'PASS' if success else 'FAIL'}] test_rapid_frame_building_1000")
    print()
    return success


def test_rapid_parse_cycle():
    """Stress Test 3: Build and parse 100 frames in cycle."""
    print("=" * 70)
    print("STRESS TEST 3: Build/Parse 100 Frames Cycle")
    print("=" * 70)

    client = UARTIPCClient()
    client.seq_tx = 0

    start = time.perf_counter()
    success_count = 0
    for i in range(100):
        payload = f"test_message_{i:04d}".encode('utf-8')
        frame = client._build_frame(MsgType.QUERY, payload)
        msg = client._parse_frame(frame)
        if msg and msg.payload == payload:
            success_count += 1
    elapsed = time.perf_counter() - start

    print(f"  Roundtrips: {success_count}/100")
    print(f"  Time: {elapsed * 1000:.2f} ms")
    print(f"  Rate: {100 / elapsed:.0f} roundtrips/sec")

    success = success_count == 100
    print(f"\n[{'PASS' if success else 'FAIL'}] test_rapid_parse_cycle")
    print()
    return success


def test_mixed_message_types_burst():
    """Stress Test 4: Rapid burst of mixed message types."""
    print("=" * 70)
    print("STRESS TEST 4: Mixed Message Types Burst")
    print("=" * 70)

    client = UARTIPCClient()
    client.seq_tx = 0

    msg_types = [
        (MsgType.QUERY, b"list files\x00"),
        (MsgType.HEARTBEAT, struct.pack('<Q', 12345)),
        (MsgType.STATS_REQUEST, b""),
        (MsgType.COMMAND, b"ps aux\x00"),
        (MsgType.SHIELD_CHECK, b"rm -rf /\x00"),
        (MsgType.STATE_CHANGE, struct.pack('<B', 2)),
    ]

    start = time.perf_counter()
    frames = []
    for _ in range(50):  # 50 rounds of 6 message types = 300 frames
        for msg_type, payload in msg_types:
            frame = client._build_frame(msg_type, payload)
            frames.append((msg_type, frame))
    elapsed = time.perf_counter() - start

    # Verify all parse correctly
    parsed = 0
    for msg_type, frame in frames:
        msg = client._parse_frame(frame)
        if msg and msg.msg_type == msg_type:
            parsed += 1

    print(f"  Total frames: {len(frames)}")
    print(f"  Successfully parsed: {parsed}")
    print(f"  Time: {elapsed * 1000:.2f} ms")
    print(f"  Rate: {len(frames) / elapsed:.0f} frames/sec")

    success = parsed == len(frames)
    print(f"\n[{'PASS' if success else 'FAIL'}] test_mixed_message_types_burst")
    print()
    return success


# ============================================================================
# CRC Error Recovery Tests
# ============================================================================

def test_crc_error_multiple_corruptions():
    """Stress Test 5: Handle multiple CRC corruptions gracefully."""
    print("=" * 70)
    print("STRESS TEST 5: Multiple CRC Corruptions")
    print("=" * 70)

    client = UARTIPCClient()
    initial_errors = client.stats['crc_errors']

    # Build valid frame, then corrupt different positions
    original = client._build_frame(MsgType.QUERY, b"test payload\x00")

    corruptions = [
        (8, "payload byte 0"),
        (9, "payload byte 1"),
        (10, "payload byte 2"),
        (2, "message type"),
        (3, "sequence low byte"),
        (5, "length low byte"),
    ]

    rejected = 0
    for pos, desc in corruptions:
        corrupted = bytearray(original)
        corrupted[pos] ^= 0xFF
        msg = client._parse_frame(bytes(corrupted))
        if msg is None:
            rejected += 1
            print(f"  [OK] Corrupted {desc}: Rejected")
        else:
            print(f"  [FAIL] Corrupted {desc}: Accepted!")

    final_errors = client.stats['crc_errors']
    errors_counted = final_errors - initial_errors

    print(f"\n  Rejected: {rejected}/{len(corruptions)}")
    print(f"  CRC errors counted: {errors_counted}")

    success = rejected == len(corruptions)
    print(f"\n[{'PASS' if success else 'FAIL'}] test_crc_error_multiple_corruptions")
    print()
    return success


def test_crc_error_random_bit_flips():
    """Stress Test 6: Random bit flip detection."""
    print("=" * 70)
    print("STRESS TEST 6: Random Bit Flip Detection")
    print("=" * 70)

    client = UARTIPCClient()
    random.seed(42)  # Reproducible

    original = client._build_frame(MsgType.QUERY, b"random test message\x00")

    detected = 0
    total = 100

    for i in range(total):
        corrupted = bytearray(original)

        # Flip 1-3 random bits
        num_flips = random.randint(1, 3)
        for _ in range(num_flips):
            pos = random.randint(2, len(corrupted) - 3)  # Skip sync and last CRC byte
            bit = random.randint(0, 7)
            corrupted[pos] ^= (1 << bit)

        msg = client._parse_frame(bytes(corrupted))
        if msg is None:
            detected += 1

    detection_rate = 100 * detected / total

    print(f"  Total tests: {total}")
    print(f"  Corrupted frames detected: {detected}")
    print(f"  Detection rate: {detection_rate:.1f}%")

    # CRC-16 should detect virtually all single and multi-bit errors
    success = detection_rate >= 99.0
    print(f"\n[{'PASS' if success else 'FAIL'}] test_crc_error_random_bit_flips")
    print()
    return success


def test_crc_recovery_after_errors():
    """Stress Test 7: Recovery after CRC errors."""
    print("=" * 70)
    print("STRESS TEST 7: Recovery After CRC Errors")
    print("=" * 70)

    client = UARTIPCClient()
    client.seq_tx = 0

    sequence = []

    # Alternate between valid and corrupted frames
    for i in range(50):
        valid = client._build_frame(MsgType.QUERY, f"msg_{i:02d}".encode())

        # Create corrupted version
        corrupted = bytearray(valid)
        corrupted[8] ^= 0xFF
        corrupted = bytes(corrupted)

        # Try corrupted first
        msg1 = client._parse_frame(corrupted)
        sequence.append(('corrupted', msg1 is None))

        # Then valid
        msg2 = client._parse_frame(valid)
        sequence.append(('valid', msg2 is not None))

    corrupted_rejected = sum(1 for s, ok in sequence if s == 'corrupted' and ok)
    valid_accepted = sum(1 for s, ok in sequence if s == 'valid' and ok)

    print(f"  Corrupted frames rejected: {corrupted_rejected}/50")
    print(f"  Valid frames after corruption accepted: {valid_accepted}/50")
    print(f"  Total sequence: {len(sequence)} frames")

    success = corrupted_rejected == 50 and valid_accepted == 50
    print(f"\n[{'PASS' if success else 'FAIL'}] test_crc_recovery_after_errors")
    print()
    return success


# ============================================================================
# Sequence Number Stress Tests
# ============================================================================

def test_sequence_10000_increments():
    """Stress Test 8: 10000 sequence number increments."""
    print("=" * 70)
    print("STRESS TEST 8: 10000 Sequence Increments")
    print("=" * 70)

    client = UARTIPCClient()
    client.seq_tx = 0

    sequences = []
    for _ in range(10000):
        seq = client._next_seq()
        sequences.append(seq)

    # Check monotonic increase (with wrap handling)
    wraps = 0
    monotonic_errors = 0
    for i in range(1, len(sequences)):
        if sequences[i] == 0 and sequences[i-1] == 0xFFFF:
            wraps += 1
        elif sequences[i] != sequences[i-1] + 1 and not (sequences[i] == 0 and sequences[i-1] == 0xFFFF):
            monotonic_errors += 1

    print(f"  Sequences generated: {len(sequences)}")
    print(f"  Wraps detected: {wraps}")
    print(f"  Monotonic errors: {monotonic_errors}")
    print(f"  First 5: {sequences[:5]}")
    print(f"  Last 5: {sequences[-5:]}")

    success = monotonic_errors == 0
    print(f"\n[{'PASS' if success else 'FAIL'}] test_sequence_10000_increments")
    print()
    return success


def test_sequence_wrap_multiple():
    """Stress Test 9: Multiple sequence wrap cycles."""
    print("=" * 70)
    print("STRESS TEST 9: Multiple Sequence Wrap Cycles")
    print("=" * 70)

    client = UARTIPCClient()

    wraps = 0
    target_wraps = 3

    # Start near wrap point - _next_seq increments THEN returns
    # So if seq_tx = 0xFFFE, _next_seq returns 0xFFFF, then next returns 0
    client.seq_tx = 0xFFFD

    iterations = 0
    max_iterations = 200000
    prev_seq = client.seq_tx

    while wraps < target_wraps and iterations < max_iterations:
        seq = client._next_seq()
        # Wrap detected when we go from 0xFFFF to 0
        if prev_seq == 0xFFFF and seq == 0:
            wraps += 1
            print(f"  Wrap #{wraps} at iteration {iterations}")
        prev_seq = seq
        iterations += 1

    print(f"\n  Total iterations: {iterations}")
    print(f"  Wraps completed: {wraps}")
    print(f"  Final seq_tx: 0x{client.seq_tx:04X}")

    success = wraps == target_wraps
    print(f"\n[{'PASS' if success else 'FAIL'}] test_sequence_wrap_multiple")
    print()
    return success


def test_sequence_in_frames():
    """Stress Test 10: Verify sequence numbers in built frames."""
    print("=" * 70)
    print("STRESS TEST 10: Sequence Numbers in Frames")
    print("=" * 70)

    client = UARTIPCClient()
    client.seq_tx = 0

    frames = []
    for i in range(100):
        frame = client._build_frame(MsgType.QUERY, b"test")
        seq_in_frame = struct.unpack('<H', frame[3:5])[0]
        frames.append(seq_in_frame)

    # Verify sequences are 1, 2, 3, ..., 100
    expected = list(range(1, 101))
    match = frames == expected

    print(f"  Frames built: {len(frames)}")
    print(f"  First 10 sequences: {frames[:10]}")
    print(f"  Last 10 sequences: {frames[-10:]}")
    print(f"  Match expected (1-100): {match}")

    success = match
    print(f"\n[{'PASS' if success else 'FAIL'}] test_sequence_in_frames")
    print()
    return success


# ============================================================================
# Timeout Behavior Tests
# ============================================================================

def test_stats_tracking_stress():
    """Stress Test 11: Statistics tracking under load."""
    print("=" * 70)
    print("STRESS TEST 11: Statistics Tracking Under Load")
    print("=" * 70)

    client = UARTIPCClient()

    # Reset stats
    for key in client.stats:
        client.stats[key] = 0

    # Build and parse many frames
    for i in range(500):
        frame = client._build_frame(MsgType.QUERY, f"q{i:03d}".encode())
        msg = client._parse_frame(frame)

    # Inject some CRC errors
    valid_frame = client._build_frame(MsgType.QUERY, b"test")
    for i in range(50):
        corrupted = bytearray(valid_frame)
        corrupted[8] ^= 0xFF
        client._parse_frame(bytes(corrupted))

    print(f"  TX messages: {client.stats['tx_messages']}")
    print(f"  CRC errors: {client.stats['crc_errors']}")

    # Note: tx_messages only incremented by _send_raw, not _build_frame
    success = client.stats['crc_errors'] == 50
    print(f"\n[{'PASS' if success else 'FAIL'}] test_stats_tracking_stress")
    print()
    return success


def test_queue_stress():
    """Stress Test 12: Message queue handling."""
    print("=" * 70)
    print("STRESS TEST 12: Message Queue Handling")
    print("=" * 70)

    client = UARTIPCClient()

    # Manually push messages to queue
    for i in range(100):
        msg = Message(MsgType.RESPONSE, i, 0, f"response_{i:03d}".encode())
        client.rx_queue.put(msg)

    print(f"  Queue size: {client.rx_queue.qsize()}")

    # Drain queue
    received = 0
    while not client.rx_queue.empty():
        msg = client.rx_queue.get_nowait()
        received += 1

    print(f"  Messages received: {received}")
    print(f"  Queue empty: {client.rx_queue.empty()}")

    success = received == 100 and client.rx_queue.empty()
    print(f"\n[{'PASS' if success else 'FAIL'}] test_queue_stress")
    print()
    return success


def test_concurrent_build_parse():
    """Stress Test 13: Simulated concurrent operations."""
    print("=" * 70)
    print("STRESS TEST 13: Simulated Concurrent Operations")
    print("=" * 70)

    import threading

    client = UARTIPCClient()
    results = {'built': 0, 'parsed': 0, 'errors': 0}
    lock = threading.Lock()

    def builder():
        for i in range(100):
            try:
                frame = client._build_frame(MsgType.QUERY, f"b{i:02d}".encode())
                with lock:
                    results['built'] += 1
            except Exception as e:
                with lock:
                    results['errors'] += 1

    def parser():
        # Create valid frames to parse
        for i in range(100):
            frame = client._build_frame(MsgType.RESPONSE, f"p{i:02d}".encode())
            try:
                msg = client._parse_frame(frame)
                if msg:
                    with lock:
                        results['parsed'] += 1
            except Exception as e:
                with lock:
                    results['errors'] += 1

    threads = [
        threading.Thread(target=builder),
        threading.Thread(target=builder),
        threading.Thread(target=parser),
        threading.Thread(target=parser),
    ]

    for t in threads:
        t.start()
    for t in threads:
        t.join()

    print(f"  Frames built: {results['built']}")
    print(f"  Frames parsed: {results['parsed']}")
    print(f"  Errors: {results['errors']}")

    success = results['errors'] == 0 and results['built'] == 200 and results['parsed'] == 200
    print(f"\n[{'PASS' if success else 'FAIL'}] test_concurrent_build_parse")
    print()
    return success


# ============================================================================
# Edge Case Tests
# ============================================================================

def test_payload_boundary_sizes():
    """Stress Test 14: Payload boundary sizes."""
    print("=" * 70)
    print("STRESS TEST 14: Payload Boundary Sizes")
    print("=" * 70)

    client = UARTIPCClient()

    # Test various payload sizes including boundaries
    sizes = [0, 1, 127, 128, 239, 240, MAX_PAYLOAD_SIZE]

    results = []
    for size in sizes:
        payload = b'X' * size
        try:
            frame = client._build_frame(MsgType.QUERY, payload)
            msg = client._parse_frame(frame)
            ok = msg is not None and len(msg.payload) == size
            results.append((size, ok))
            print(f"  Size {size:3d}: {'OK' if ok else 'FAIL'}")
        except Exception as e:
            results.append((size, False))
            print(f"  Size {size:3d}: FAIL ({e})")

    success = all(ok for _, ok in results)
    print(f"\n[{'PASS' if success else 'FAIL'}] test_payload_boundary_sizes")
    print()
    return success


def test_all_message_types_roundtrip():
    """Stress Test 15: All message types roundtrip."""
    print("=" * 70)
    print("STRESS TEST 15: All Message Types Roundtrip")
    print("=" * 70)

    client = UARTIPCClient()

    all_types = [
        MsgType.QUERY,
        MsgType.RESPONSE,
        MsgType.HEARTBEAT,
        MsgType.HEARTBEAT_ACK,
        MsgType.STATS_REQUEST,
        MsgType.STATS_RESPONSE,
        MsgType.COMMAND,
        MsgType.COMMAND_RESULT,
        MsgType.SHIELD_CHECK,
        MsgType.SHIELD_RESULT,
        MsgType.STATE_CHANGE,
        MsgType.STATE_ACK,
        MsgType.ERROR,
        MsgType.RESET,
    ]

    passed = 0
    for msg_type in all_types:
        payload = f"test_{msg_type.name}".encode()
        frame = client._build_frame(msg_type, payload)
        msg = client._parse_frame(frame)

        if msg and msg.msg_type == msg_type and msg.payload == payload:
            print(f"  {msg_type.name:20s}: OK")
            passed += 1
        else:
            print(f"  {msg_type.name:20s}: FAIL")

    print(f"\n  Passed: {passed}/{len(all_types)}")

    success = passed == len(all_types)
    print(f"\n[{'PASS' if success else 'FAIL'}] test_all_message_types_roundtrip")
    print()
    return success


def test_flag_combinations():
    """Stress Test 16: All flag combinations."""
    print("=" * 70)
    print("STRESS TEST 16: All Flag Combinations")
    print("=" * 70)

    client = UARTIPCClient()

    # Test all single flags and common combinations
    flag_tests = [
        (0x00, "No flags"),
        (MsgFlags.ACK_REQ, "ACK_REQ"),
        (MsgFlags.PRIORITY, "PRIORITY"),
        (MsgFlags.FRAGMENT, "FRAGMENT"),
        (MsgFlags.LAST_FRAG, "LAST_FRAG"),
        (MsgFlags.ACK_REQ | MsgFlags.PRIORITY, "ACK_REQ + PRIORITY"),
        (MsgFlags.FRAGMENT | MsgFlags.LAST_FRAG, "FRAGMENT + LAST_FRAG"),
        (0xFF, "All flags"),
    ]

    passed = 0
    for flags, desc in flag_tests:
        frame = client._build_frame(MsgType.QUERY, b"test", flags=flags)
        msg = client._parse_frame(frame)

        if msg and msg.flags == flags:
            print(f"  {desc:25s} (0x{flags:02X}): OK")
            passed += 1
        else:
            actual = msg.flags if msg else "N/A"
            print(f"  {desc:25s} (0x{flags:02X}): FAIL (got {actual})")

    print(f"\n  Passed: {passed}/{len(flag_tests)}")

    success = passed == len(flag_tests)
    print(f"\n[{'PASS' if success else 'FAIL'}] test_flag_combinations")
    print()
    return success


# ============================================================================
# Latency Simulation Tests
# ============================================================================

def test_latency_simulation_disabled():
    """Stress Test 17: Latency simulation disabled (default)."""
    print("=" * 70)
    print("STRESS TEST 17: Latency Simulation Disabled")
    print("=" * 70)

    client = UARTIPCClient(mock_latency_ms=0.0, mock_mode=True)
    client.connected = True

    start = time.perf_counter()
    for _ in range(100):
        result = client.cache_lookup("test query")
    elapsed = time.perf_counter() - start

    elapsed_ms = elapsed * 1000
    per_call_ms = elapsed_ms / 100

    print(f"  100 cache_lookup calls")
    print(f"  Total time: {elapsed_ms:.2f} ms")
    print(f"  Per call: {per_call_ms:.3f} ms")
    print(f"  Latency stat: {client.stats['mock_latency_total_ms']:.2f} ms")

    # Should be very fast with no latency
    success = per_call_ms < 1.0 and client.stats['mock_latency_total_ms'] == 0.0
    print(f"\n[{'PASS' if success else 'FAIL'}] test_latency_simulation_disabled")
    print()
    return success


def test_latency_simulation_10ms():
    """Stress Test 18: Latency simulation at 10ms."""
    print("=" * 70)
    print("STRESS TEST 18: Latency Simulation 10ms")
    print("=" * 70)

    client = UARTIPCClient(mock_latency_ms=10.0, mock_mode=True)
    client.connected = True

    start = time.perf_counter()
    for _ in range(10):
        result = client.cache_lookup("test query")
    elapsed = time.perf_counter() - start

    elapsed_ms = elapsed * 1000
    per_call_ms = elapsed_ms / 10
    expected_min = 8.0  # 10ms - 20% jitter
    expected_max = 12.0  # 10ms + 20% jitter

    print(f"  10 cache_lookup calls with 10ms latency")
    print(f"  Total time: {elapsed_ms:.2f} ms")
    print(f"  Per call: {per_call_ms:.2f} ms")
    print(f"  Expected range: {expected_min:.1f}-{expected_max:.1f} ms")
    print(f"  Latency stat: {client.stats['mock_latency_total_ms']:.2f} ms")

    # Should be around 10ms per call (80-120ms total for 10 calls)
    success = 80.0 <= elapsed_ms <= 150.0
    print(f"\n[{'PASS' if success else 'FAIL'}] test_latency_simulation_10ms")
    print()
    return success


def test_latency_simulation_realistic():
    """Stress Test 19: Realistic UART latency (15ms avg)."""
    print("=" * 70)
    print("STRESS TEST 19: Realistic UART Latency (15ms)")
    print("=" * 70)

    client = UARTIPCClient(mock_latency_ms=15.0, mock_mode=True)
    client.connected = True

    timings = []
    for _ in range(20):
        start = time.perf_counter()
        result = client.cache_lookup("list files")
        elapsed = (time.perf_counter() - start) * 1000
        timings.append(elapsed)

    avg = sum(timings) / len(timings)
    min_t = min(timings)
    max_t = max(timings)

    print(f"  20 cache_lookup calls")
    print(f"  Average: {avg:.2f} ms")
    print(f"  Min: {min_t:.2f} ms")
    print(f"  Max: {max_t:.2f} ms")
    print(f"  Total latency: {client.stats['mock_latency_total_ms']:.2f} ms")

    # Average should be around 15ms (+/- 20% jitter = 12-18ms)
    success = 10.0 <= avg <= 20.0
    print(f"\n[{'PASS' if success else 'FAIL'}] test_latency_simulation_realistic")
    print()
    return success


def test_latency_multiple_operations():
    """Stress Test 20: Latency across multiple operation types."""
    print("=" * 70)
    print("STRESS TEST 20: Latency Multiple Operations")
    print("=" * 70)

    client = UARTIPCClient(mock_latency_ms=12.0, mock_mode=True)
    client.connected = True

    operations = [
        ('cache_lookup', lambda: client.cache_lookup("test")),
        ('get_stats', lambda: client.get_stats()),
        ('send_command', lambda: client.send_command("ls")),
        ('shield_check', lambda: client.shield_check("read_file")),
    ]

    results = []
    for name, op in operations:
        start = time.perf_counter()
        for _ in range(5):
            op()
        elapsed = (time.perf_counter() - start) * 1000
        avg = elapsed / 5
        results.append((name, avg))
        print(f"  {name:15s}: {avg:.2f} ms avg (5 calls)")

    total_latency = client.stats['mock_latency_total_ms']
    expected_total = 12.0 * 20  # 4 ops * 5 calls = 20 calls
    print(f"\n  Total latency tracked: {total_latency:.2f} ms")
    print(f"  Expected ~{expected_total:.0f} ms")

    # All operations should have latency
    all_have_latency = all(avg > 8.0 for _, avg in results)
    success = all_have_latency and total_latency > 150.0
    print(f"\n[{'PASS' if success else 'FAIL'}] test_latency_multiple_operations")
    print()
    return success


# ============================================================================
# Test Runner
# ============================================================================

def run_all_stress_tests():
    """Run all stress tests and report results."""
    print()
    print("=" * 70)
    print("  JARVIS AI-OS - UART IPC Client STRESS Tests")
    print("  Phase 2 Week 32+")
    print("=" * 70)
    print()

    tests = [
        # Rapid burst tests (4)
        test_rapid_frame_building_100,
        test_rapid_frame_building_1000,
        test_rapid_parse_cycle,
        test_mixed_message_types_burst,
        # CRC error recovery tests (3)
        test_crc_error_multiple_corruptions,
        test_crc_error_random_bit_flips,
        test_crc_recovery_after_errors,
        # Sequence number stress tests (3)
        test_sequence_10000_increments,
        test_sequence_wrap_multiple,
        test_sequence_in_frames,
        # Timeout/stats tests (3)
        test_stats_tracking_stress,
        test_queue_stress,
        test_concurrent_build_parse,
        # Edge case tests (3)
        test_payload_boundary_sizes,
        test_all_message_types_roundtrip,
        test_flag_combinations,
        # Latency simulation tests (4)
        test_latency_simulation_disabled,
        test_latency_simulation_10ms,
        test_latency_simulation_realistic,
        test_latency_multiple_operations,
    ]

    passed = 0
    failed = 0
    failed_tests = []

    for test_func in tests:
        try:
            if test_func():
                passed += 1
            else:
                failed += 1
                failed_tests.append(test_func.__name__)
        except Exception as e:
            print(f"  [ERROR] {test_func.__name__}: {e}")
            import traceback
            traceback.print_exc()
            failed += 1
            failed_tests.append(test_func.__name__)

    print()
    print("=" * 70)
    print("  STRESS TEST SUMMARY")
    print("=" * 70)
    print(f"  Total tests: {len(tests)}")
    print(f"  Passed: {passed}")
    print(f"  Failed: {failed}")
    print(f"  Pass rate: {100 * passed / len(tests):.1f}%")
    if failed_tests:
        print(f"\n  Failed tests:")
        for name in failed_tests:
            print(f"    - {name}")
    print("=" * 70)
    print()

    return failed == 0


if __name__ == '__main__':
    success = run_all_stress_tests()
    sys.exit(0 if success else 1)
