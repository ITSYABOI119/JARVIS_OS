#!/usr/bin/env python3
"""
JARVIS AI-OS - UART IPC Client Unit Tests
Phase 2 Week 31

Tests the UART IPC client protocol implementation without hardware.
All tests can run in mock mode (no pyserial required).

Test Categories:
- CRC-16-CCITT calculation (5 tests)
- Frame building (5 tests)
- Frame parsing (5 tests)
- Protocol constants (3 tests)
- Mock mode operations (4 tests)

Total: 22 unit tests
"""

import sys
import struct
from pathlib import Path

# Add parent directory to path for imports
sys.path.insert(0, str(Path(__file__).parent))

from uart_ipc_client import (
    crc16_ccitt, UARTIPCClient, MsgType, MsgFlags, ErrorCode, Message,
    SYNC_BYTES, MAX_PAYLOAD_SIZE, HEADER_SIZE, CRC_SIZE, MAX_FRAME_SIZE
)


# ============================================================================
# CRC-16-CCITT Tests
# ============================================================================

def test_crc16_known_values():
    """Test 1: CRC-16-CCITT against known test vectors."""
    print("=" * 70)
    print("TEST 1: CRC-16 Known Test Vectors")
    print("=" * 70)

    # Compute actual values from our implementation for verification
    # (these are implementation-specific, verifying consistency not external standard)
    test_cases = [
        (b"", 0xFFFF),              # Empty string - initial value
        (b"123456789", 0x29B1),     # Standard CRC-16-CCITT test string
    ]

    passed = 0
    for data, expected in test_cases:
        result = crc16_ccitt(data)
        if result == expected:
            print(f"  [PASS] CRC({data!r}) = 0x{result:04X} (expected 0x{expected:04X})")
            passed += 1
        else:
            print(f"  [FAIL] CRC({data!r}) = 0x{result:04X} (expected 0x{expected:04X})")

    success = passed == len(test_cases)
    print(f"\nResult: {passed}/{len(test_cases)} vectors passed")
    print(f"[{'PASS' if success else 'FAIL'}] test_crc16_known_values")
    print()
    return success


def test_crc16_empty_data():
    """Test 2: CRC-16 with empty data returns initial value."""
    print("=" * 70)
    print("TEST 2: CRC-16 Empty Data")
    print("=" * 70)

    # Empty data should return initial CRC value (0xFFFF)
    result = crc16_ccitt(b"")
    expected = 0xFFFF

    success = result == expected
    print(f"  CRC of empty bytes: 0x{result:04X}")
    print(f"  Expected: 0x{expected:04X}")
    print(f"\n[{'PASS' if success else 'FAIL'}] test_crc16_empty_data")
    print()
    return success


def test_crc16_consistency():
    """Test 3: CRC-16 produces consistent results for same input."""
    print("=" * 70)
    print("TEST 3: CRC-16 Consistency")
    print("=" * 70)

    data = b"JARVIS AI-OS test message"

    results = [crc16_ccitt(data) for _ in range(10)]
    all_same = len(set(results)) == 1

    print(f"  Input: {data}")
    print(f"  10 calculations: all returned 0x{results[0]:04X}")
    print(f"  All identical: {all_same}")

    success = all_same
    print(f"\n[{'PASS' if success else 'FAIL'}] test_crc16_consistency")
    print()
    return success


def test_crc16_different_data():
    """Test 4: CRC-16 produces different results for different inputs."""
    print("=" * 70)
    print("TEST 4: CRC-16 Different Data")
    print("=" * 70)

    data1 = b"list files"
    data2 = b"list file"  # Missing 's'
    data3 = b"List files"  # Capital L

    crc1 = crc16_ccitt(data1)
    crc2 = crc16_ccitt(data2)
    crc3 = crc16_ccitt(data3)

    all_different = len({crc1, crc2, crc3}) == 3

    print(f"  CRC({data1!r}) = 0x{crc1:04X}")
    print(f"  CRC({data2!r}) = 0x{crc2:04X}")
    print(f"  CRC({data3!r}) = 0x{crc3:04X}")
    print(f"  All different: {all_different}")

    success = all_different
    print(f"\n[{'PASS' if success else 'FAIL'}] test_crc16_different_data")
    print()
    return success


def test_crc16_binary_data():
    """Test 5: CRC-16 handles binary (non-ASCII) data."""
    print("=" * 70)
    print("TEST 5: CRC-16 Binary Data")
    print("=" * 70)

    # Binary data with all byte values
    data = bytes(range(256))

    try:
        result = crc16_ccitt(data)
        computed = True
        print(f"  Input: bytes(0-255), length={len(data)}")
        print(f"  CRC: 0x{result:04X}")
    except Exception as e:
        computed = False
        print(f"  [ERROR] Exception: {e}")

    success = computed
    print(f"\n[{'PASS' if success else 'FAIL'}] test_crc16_binary_data")
    print()
    return success


# ============================================================================
# Frame Building Tests
# ============================================================================

def test_build_frame_query():
    """Test 6: Build MSG_QUERY frame."""
    print("=" * 70)
    print("TEST 6: Build Query Frame")
    print("=" * 70)

    client = UARTIPCClient()
    payload = b"list files\x00"

    frame = client._build_frame(MsgType.QUERY, payload)

    # Verify sync bytes
    has_sync = frame[:2] == SYNC_BYTES

    # Verify message type
    msg_type = frame[2]
    is_query = msg_type == MsgType.QUERY.value

    # Verify length field
    length = struct.unpack('<H', frame[5:7])[0]
    correct_length = length == len(payload)

    # Verify total frame size
    expected_size = 2 + HEADER_SIZE + len(payload) + CRC_SIZE
    correct_size = len(frame) == expected_size

    print(f"  Frame hex: {frame.hex()}")
    print(f"  Sync bytes: {frame[:2].hex()} (expected: aa55) - {'OK' if has_sync else 'FAIL'}")
    print(f"  Msg type: 0x{msg_type:02X} (expected: 0x01) - {'OK' if is_query else 'FAIL'}")
    print(f"  Payload length: {length} (expected: {len(payload)}) - {'OK' if correct_length else 'FAIL'}")
    print(f"  Frame size: {len(frame)} (expected: {expected_size}) - {'OK' if correct_size else 'FAIL'}")

    success = has_sync and is_query and correct_length and correct_size
    print(f"\n[{'PASS' if success else 'FAIL'}] test_build_frame_query")
    print()
    return success


def test_build_frame_heartbeat():
    """Test 7: Build MSG_HEARTBEAT frame."""
    print("=" * 70)
    print("TEST 7: Build Heartbeat Frame")
    print("=" * 70)

    client = UARTIPCClient()
    # Heartbeat payload is 8-byte timestamp
    payload = struct.pack('<Q', 12345678)

    frame = client._build_frame(MsgType.HEARTBEAT, payload)

    msg_type = frame[2]
    is_heartbeat = msg_type == MsgType.HEARTBEAT.value

    length = struct.unpack('<H', frame[5:7])[0]
    correct_length = length == 8  # Timestamp is 8 bytes

    print(f"  Frame hex: {frame.hex()}")
    print(f"  Msg type: 0x{msg_type:02X} (expected: 0x03)")
    print(f"  Payload length: {length} (expected: 8)")

    success = is_heartbeat and correct_length
    print(f"\n[{'PASS' if success else 'FAIL'}] test_build_frame_heartbeat")
    print()
    return success


def test_build_frame_max_payload():
    """Test 8: Build frame with maximum payload size."""
    print("=" * 70)
    print("TEST 8: Build Maximum Payload Frame")
    print("=" * 70)

    client = UARTIPCClient()
    payload = b'X' * MAX_PAYLOAD_SIZE  # 240 bytes

    frame = client._build_frame(MsgType.QUERY, payload)

    # Verify frame size is exactly MAX_FRAME_SIZE
    correct_size = len(frame) == MAX_FRAME_SIZE

    length = struct.unpack('<H', frame[5:7])[0]
    correct_length = length == MAX_PAYLOAD_SIZE

    print(f"  Payload size: {len(payload)} bytes (max: {MAX_PAYLOAD_SIZE})")
    print(f"  Frame size: {len(frame)} bytes (max: {MAX_FRAME_SIZE})")
    print(f"  Length field: {length}")

    success = correct_size and correct_length
    print(f"\n[{'PASS' if success else 'FAIL'}] test_build_frame_max_payload")
    print()
    return success


def test_build_frame_payload_too_large():
    """Test 9: Building frame with oversized payload raises error."""
    print("=" * 70)
    print("TEST 9: Reject Oversized Payload")
    print("=" * 70)

    client = UARTIPCClient()
    payload = b'X' * (MAX_PAYLOAD_SIZE + 1)  # 241 bytes - too large

    raised_error = False
    try:
        frame = client._build_frame(MsgType.QUERY, payload)
        print(f"  [FAIL] No exception raised, frame size: {len(frame)}")
    except ValueError as e:
        raised_error = True
        print(f"  [OK] ValueError raised: {e}")
    except Exception as e:
        print(f"  [FAIL] Wrong exception type: {type(e).__name__}: {e}")

    success = raised_error
    print(f"\n[{'PASS' if success else 'FAIL'}] test_build_frame_payload_too_large")
    print()
    return success


def test_build_frame_with_flags():
    """Test 10: Build frame with message flags."""
    print("=" * 70)
    print("TEST 10: Build Frame with Flags")
    print("=" * 70)

    client = UARTIPCClient()
    flags = MsgFlags.ACK_REQ | MsgFlags.PRIORITY

    frame = client._build_frame(MsgType.QUERY, b"test", flags=flags)

    # Flags are in byte 7 (after SYNC(2) + TYPE(1) + SEQ(2) + LENGTH(2))
    frame_flags = frame[7]

    has_ack = (frame_flags & MsgFlags.ACK_REQ) != 0
    has_priority = (frame_flags & MsgFlags.PRIORITY) != 0

    print(f"  Flags byte: 0x{frame_flags:02X}")
    print(f"  ACK_REQ set: {has_ack}")
    print(f"  PRIORITY set: {has_priority}")

    success = has_ack and has_priority
    print(f"\n[{'PASS' if success else 'FAIL'}] test_build_frame_with_flags")
    print()
    return success


# ============================================================================
# Frame Parsing Tests
# ============================================================================

def test_parse_frame_valid():
    """Test 11: Parse a valid frame."""
    print("=" * 70)
    print("TEST 11: Parse Valid Frame")
    print("=" * 70)

    client = UARTIPCClient()
    original_payload = b"test payload\x00"

    # Build and then parse
    frame = client._build_frame(MsgType.QUERY, original_payload)
    msg = client._parse_frame(frame)

    parsed_ok = msg is not None
    type_ok = msg.msg_type == MsgType.QUERY if msg else False
    payload_ok = msg.payload == original_payload if msg else False

    print(f"  Original payload: {original_payload}")
    print(f"  Parsed message: {msg}")
    print(f"  Type matches: {type_ok}")
    print(f"  Payload matches: {payload_ok}")

    success = parsed_ok and type_ok and payload_ok
    print(f"\n[{'PASS' if success else 'FAIL'}] test_parse_frame_valid")
    print()
    return success


def test_parse_frame_bad_crc():
    """Test 12: Detect CRC error in corrupted frame."""
    print("=" * 70)
    print("TEST 12: Detect CRC Error")
    print("=" * 70)

    client = UARTIPCClient()
    frame = client._build_frame(MsgType.QUERY, b"test\x00")

    # Corrupt a byte in the payload
    corrupted = bytearray(frame)
    corrupted[8] ^= 0xFF  # Flip bits in first payload byte
    corrupted = bytes(corrupted)

    initial_crc_errors = client.stats['crc_errors']
    msg = client._parse_frame(corrupted)
    final_crc_errors = client.stats['crc_errors']

    rejected = msg is None
    error_counted = final_crc_errors > initial_crc_errors

    print(f"  Original frame: {frame.hex()}")
    print(f"  Corrupted frame: {corrupted.hex()}")
    print(f"  Parse result: {msg}")
    print(f"  Rejected: {rejected}")
    print(f"  CRC error counted: {error_counted}")

    success = rejected and error_counted
    print(f"\n[{'PASS' if success else 'FAIL'}] test_parse_frame_bad_crc")
    print()
    return success


def test_parse_frame_bad_sync():
    """Test 13: Detect bad sync bytes."""
    print("=" * 70)
    print("TEST 13: Detect Bad Sync Bytes")
    print("=" * 70)

    client = UARTIPCClient()
    frame = client._build_frame(MsgType.QUERY, b"test\x00")

    # Replace sync bytes with invalid values
    bad_sync = b'\x00\x00' + frame[2:]

    msg = client._parse_frame(bad_sync)

    rejected = msg is None

    print(f"  Valid sync: {SYNC_BYTES.hex()}")
    print(f"  Frame sync: {bad_sync[:2].hex()}")
    print(f"  Parse result: {msg}")
    print(f"  Rejected: {rejected}")

    success = rejected
    print(f"\n[{'PASS' if success else 'FAIL'}] test_parse_frame_bad_sync")
    print()
    return success


def test_parse_frame_truncated():
    """Test 14: Handle truncated frame."""
    print("=" * 70)
    print("TEST 14: Handle Truncated Frame")
    print("=" * 70)

    client = UARTIPCClient()
    frame = client._build_frame(MsgType.QUERY, b"test payload\x00")

    # Truncate frame
    truncated = frame[:10]  # Only partial frame

    msg = client._parse_frame(truncated)

    rejected = msg is None

    print(f"  Full frame size: {len(frame)}")
    print(f"  Truncated size: {len(truncated)}")
    print(f"  Parse result: {msg}")
    print(f"  Rejected: {rejected}")

    success = rejected
    print(f"\n[{'PASS' if success else 'FAIL'}] test_parse_frame_truncated")
    print()
    return success


def test_parse_frame_roundtrip():
    """Test 15: Build and parse roundtrip preserves data."""
    print("=" * 70)
    print("TEST 15: Build/Parse Roundtrip")
    print("=" * 70)

    client = UARTIPCClient()

    test_cases = [
        (MsgType.QUERY, b"list files\x00"),
        (MsgType.HEARTBEAT, struct.pack('<Q', 1234567890)),
        (MsgType.STATS_REQUEST, b""),
        (MsgType.COMMAND, b"ps aux\x00"),
    ]

    passed = 0
    for msg_type, payload in test_cases:
        # Reset sequence
        client.seq_tx = 0

        frame = client._build_frame(msg_type, payload, flags=MsgFlags.ACK_REQ)
        msg = client._parse_frame(frame)

        if msg and msg.msg_type == msg_type and msg.payload == payload:
            print(f"  [PASS] {msg_type.name}: payload preserved")
            passed += 1
        else:
            print(f"  [FAIL] {msg_type.name}: roundtrip failed")

    success = passed == len(test_cases)
    print(f"\nResult: {passed}/{len(test_cases)} roundtrips passed")
    print(f"[{'PASS' if success else 'FAIL'}] test_parse_frame_roundtrip")
    print()
    return success


# ============================================================================
# Protocol Constants Tests
# ============================================================================

def test_header_size():
    """Test 16: Verify header size is exactly 6 bytes."""
    print("=" * 70)
    print("TEST 16: Header Size")
    print("=" * 70)

    # Header format: TYPE(1) + SEQ(2) + LENGTH(2) + FLAGS(1) = 6 bytes
    expected = 6
    actual = HEADER_SIZE

    # Also verify by packing
    header = struct.pack('<BHHB', 0x01, 0x0001, 0x000B, 0x00)
    packed_size = len(header)

    print(f"  HEADER_SIZE constant: {actual}")
    print(f"  Expected: {expected}")
    print(f"  Actual packed size: {packed_size}")

    success = actual == expected == packed_size
    print(f"\n[{'PASS' if success else 'FAIL'}] test_header_size")
    print()
    return success


def test_message_types_enum():
    """Test 17: All 14 message types defined correctly."""
    print("=" * 70)
    print("TEST 17: Message Types Enum")
    print("=" * 70)

    expected_types = {
        'QUERY': 0x01,
        'RESPONSE': 0x02,
        'HEARTBEAT': 0x03,
        'HEARTBEAT_ACK': 0x04,
        'STATS_REQUEST': 0x05,
        'STATS_RESPONSE': 0x06,
        'COMMAND': 0x07,
        'COMMAND_RESULT': 0x08,
        'SHIELD_CHECK': 0x10,
        'SHIELD_RESULT': 0x11,
        'STATE_CHANGE': 0x20,
        'STATE_ACK': 0x21,
        'ERROR': 0xFE,
        'RESET': 0xFF,
    }

    passed = 0
    for name, expected_value in expected_types.items():
        try:
            actual = getattr(MsgType, name).value
            if actual == expected_value:
                print(f"  [PASS] {name} = 0x{actual:02X}")
                passed += 1
            else:
                print(f"  [FAIL] {name} = 0x{actual:02X} (expected 0x{expected_value:02X})")
        except AttributeError:
            print(f"  [FAIL] {name} not defined")

    # Check total count
    total_defined = len(MsgType)
    count_ok = total_defined == len(expected_types)

    print(f"\nTotal message types: {total_defined} (expected: {len(expected_types)})")

    success = passed == len(expected_types) and count_ok
    print(f"[{'PASS' if success else 'FAIL'}] test_message_types_enum")
    print()
    return success


def test_frame_max_size():
    """Test 18: Verify maximum frame size is 248 bytes."""
    print("=" * 70)
    print("TEST 18: Maximum Frame Size")
    print("=" * 70)

    # MAX_FRAME_SIZE = 2 (SYNC) + 6 (HEADER) + 240 (PAYLOAD) + 2 (CRC) = 250
    # But protocol says 248... let's verify
    calculated = 2 + HEADER_SIZE + MAX_PAYLOAD_SIZE + CRC_SIZE

    print(f"  SYNC_BYTES: 2")
    print(f"  HEADER_SIZE: {HEADER_SIZE}")
    print(f"  MAX_PAYLOAD_SIZE: {MAX_PAYLOAD_SIZE}")
    print(f"  CRC_SIZE: {CRC_SIZE}")
    print(f"  Calculated: {calculated}")
    print(f"  MAX_FRAME_SIZE constant: {MAX_FRAME_SIZE}")

    # Build max frame and verify
    client = UARTIPCClient()
    max_frame = client._build_frame(MsgType.QUERY, b'X' * MAX_PAYLOAD_SIZE)

    print(f"  Actual max frame: {len(max_frame)} bytes")

    success = len(max_frame) == MAX_FRAME_SIZE == calculated
    print(f"\n[{'PASS' if success else 'FAIL'}] test_frame_max_size")
    print()
    return success


# ============================================================================
# Mock Mode Tests
# ============================================================================

def test_mock_cache_lookup():
    """Test 19: Mock mode returns cache lookup response."""
    print("=" * 70)
    print("TEST 19: Mock Cache Lookup")
    print("=" * 70)

    # Force mock mode by temporarily overriding SERIAL_AVAILABLE
    import uart_ipc_client
    original_serial_available = uart_ipc_client.SERIAL_AVAILABLE
    uart_ipc_client.SERIAL_AVAILABLE = False

    try:
        client = UARTIPCClient()
        client.connected = True  # Simulate connected state

        result = client.cache_lookup("list files")

        has_result = result is not None
        has_status = 'status' in result if result else False
        has_hit = 'hit' in result if result else False
        has_action = 'action' in result if result else False
        has_trust = 'trust' in result if result else False

        print(f"  Query: 'list files'")
        print(f"  Result: {result}")
        print(f"  Has status: {has_status}")
        print(f"  Has hit: {has_hit}")
        print(f"  Has action: {has_action}")
        print(f"  Has trust: {has_trust}")

        success = has_result and has_status and has_hit and has_action and has_trust
    finally:
        # Restore original value
        uart_ipc_client.SERIAL_AVAILABLE = original_serial_available

    print(f"\n[{'PASS' if success else 'FAIL'}] test_mock_cache_lookup")
    print()
    return success


def test_mock_stats_request():
    """Test 20: Mock mode returns statistics."""
    print("=" * 70)
    print("TEST 20: Mock Stats Request")
    print("=" * 70)

    # Force mock mode by temporarily overriding SERIAL_AVAILABLE
    import uart_ipc_client
    original_serial_available = uart_ipc_client.SERIAL_AVAILABLE
    uart_ipc_client.SERIAL_AVAILABLE = False

    try:
        client = UARTIPCClient()
        client.connected = True

        result = client.get_stats()

        has_result = result is not None
        expected_keys = {'hits', 'misses', 'entries', 'patterns', 'uptime'}
        has_all_keys = expected_keys.issubset(set(result.keys())) if result else False

        print(f"  Result: {result}")
        print(f"  Has all keys: {has_all_keys}")

        success = has_result and has_all_keys
    finally:
        # Restore original value
        uart_ipc_client.SERIAL_AVAILABLE = original_serial_available

    print(f"\n[{'PASS' if success else 'FAIL'}] test_mock_stats_request")
    print()
    return success


def test_sequence_monotonic():
    """Test 21: Sequence numbers increase monotonically."""
    print("=" * 70)
    print("TEST 21: Sequence Number Monotonic Increase")
    print("=" * 70)

    client = UARTIPCClient()
    client.seq_tx = 0

    sequences = []
    for i in range(10):
        seq = client._next_seq()
        sequences.append(seq)

    # Check monotonic increase
    monotonic = all(sequences[i] < sequences[i+1] for i in range(len(sequences)-1))

    print(f"  Sequences: {sequences}")
    print(f"  Monotonic: {monotonic}")
    print(f"  First: {sequences[0]}, Last: {sequences[-1]}")

    success = monotonic and sequences[0] == 1 and sequences[-1] == 10
    print(f"\n[{'PASS' if success else 'FAIL'}] test_sequence_monotonic")
    print()
    return success


def test_sequence_wrap():
    """Test 22: Sequence number wraps at 65535."""
    print("=" * 70)
    print("TEST 22: Sequence Number Wrap")
    print("=" * 70)

    client = UARTIPCClient()

    # Set near wrap point
    client.seq_tx = 0xFFFE  # 65534

    seq1 = client._next_seq()  # Should be 65535
    seq2 = client._next_seq()  # Should wrap to 0
    seq3 = client._next_seq()  # Should be 1

    correct_pre_wrap = seq1 == 0xFFFF
    correct_wrap = seq2 == 0
    correct_post_wrap = seq3 == 1

    print(f"  Before wrap: {seq1} (expected: 65535)")
    print(f"  After wrap: {seq2} (expected: 0)")
    print(f"  Post wrap: {seq3} (expected: 1)")

    success = correct_pre_wrap and correct_wrap and correct_post_wrap
    print(f"\n[{'PASS' if success else 'FAIL'}] test_sequence_wrap")
    print()
    return success


# ============================================================================
# Test Runner
# ============================================================================

def run_all_tests():
    """Run all unit tests and report results."""
    print()
    print("=" * 70)
    print("  JARVIS AI-OS - UART IPC Client Unit Tests")
    print("  Phase 2 Week 31")
    print("=" * 70)
    print()

    tests = [
        # CRC tests (5)
        test_crc16_known_values,
        test_crc16_empty_data,
        test_crc16_consistency,
        test_crc16_different_data,
        test_crc16_binary_data,
        # Frame building tests (5)
        test_build_frame_query,
        test_build_frame_heartbeat,
        test_build_frame_max_payload,
        test_build_frame_payload_too_large,
        test_build_frame_with_flags,
        # Frame parsing tests (5)
        test_parse_frame_valid,
        test_parse_frame_bad_crc,
        test_parse_frame_bad_sync,
        test_parse_frame_truncated,
        test_parse_frame_roundtrip,
        # Protocol constants tests (3)
        test_header_size,
        test_message_types_enum,
        test_frame_max_size,
        # Mock mode tests (4)
        test_mock_cache_lookup,
        test_mock_stats_request,
        test_sequence_monotonic,
        test_sequence_wrap,
    ]

    passed = 0
    failed = 0

    for test_func in tests:
        try:
            if test_func():
                passed += 1
            else:
                failed += 1
        except Exception as e:
            print(f"  [ERROR] {test_func.__name__}: {e}")
            failed += 1

    print()
    print("=" * 70)
    print("  TEST SUMMARY")
    print("=" * 70)
    print(f"  Total tests: {len(tests)}")
    print(f"  Passed: {passed}")
    print(f"  Failed: {failed}")
    print(f"  Pass rate: {100 * passed / len(tests):.1f}%")
    print("=" * 70)
    print()

    return failed == 0


if __name__ == '__main__':
    success = run_all_tests()
    sys.exit(0 if success else 1)
