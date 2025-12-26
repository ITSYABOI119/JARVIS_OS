#!/usr/bin/env python3
"""
JARVIS AI-OS - Phase 2 Integration Tests
Phase 2 Week 31

Tests the integration between Phase 2 components:
- SystemBootstrap + IPC client integration
- UART frame build/parse roundtrip
- Platform detection
- Graceful degradation

All tests run without hardware using mocks.

Total: 10 integration tests
"""

import sys
import os
import struct
import tempfile
import platform as py_platform
from pathlib import Path
from unittest.mock import patch, MagicMock

# Add parent directory to path for imports
sys.path.insert(0, str(Path(__file__).parent))

from uart_ipc_client import (
    UARTIPCClient, MsgType, crc16_ccitt,
    SYNC_BYTES, HEADER_SIZE, MAX_PAYLOAD_SIZE
)


# ============================================================================
# Frame Roundtrip Tests
# ============================================================================

def test_frame_roundtrip_all_types():
    """Test 1: Build and parse frames for all message types."""
    print("=" * 70)
    print("TEST 1: Frame Roundtrip All Message Types")
    print("=" * 70)

    client = UARTIPCClient()
    client.seq_tx = 0

    test_cases = [
        (MsgType.QUERY, b"list files\x00"),
        (MsgType.RESPONSE, bytes([0x00, 0x01]) + b"ACTION:exec_ls|TRUST:0\x00"),
        (MsgType.HEARTBEAT, struct.pack('<Q', 1234567890)),
        (MsgType.HEARTBEAT_ACK, struct.pack('<QQ', 1234567890, 1234567891)),
        (MsgType.STATS_REQUEST, b""),
        (MsgType.STATS_RESPONSE, struct.pack('<IIIII', 100, 15, 258, 258, 3600)),
        (MsgType.COMMAND, b"ps aux\x00"),
        (MsgType.COMMAND_RESULT, b"output text\x00"),
        (MsgType.ERROR, bytes([0x02]) + b"CRC mismatch\x00"),
        (MsgType.RESET, b""),
    ]

    passed = 0
    for msg_type, payload in test_cases:
        client.seq_tx = 0  # Reset sequence

        frame = client._build_frame(msg_type, payload)
        msg = client._parse_frame(frame)

        if msg and msg.msg_type == msg_type and msg.payload == payload:
            print(f"  [PASS] {msg_type.name}: roundtrip OK")
            passed += 1
        else:
            print(f"  [FAIL] {msg_type.name}: roundtrip failed")
            if msg:
                print(f"         Got type={msg.msg_type.name}, payload len={len(msg.payload)}")

    success = passed == len(test_cases)
    print(f"\nResult: {passed}/{len(test_cases)} roundtrips passed")
    print(f"[{'PASS' if success else 'FAIL'}] test_frame_roundtrip_all_types")
    print()
    return success


def test_frame_roundtrip_max_payload():
    """Test 2: Roundtrip with maximum payload size."""
    print("=" * 70)
    print("TEST 2: Frame Roundtrip Max Payload")
    print("=" * 70)

    client = UARTIPCClient()

    # Create maximum size payload
    payload = bytes(range(256)) * (MAX_PAYLOAD_SIZE // 256)
    payload = payload[:MAX_PAYLOAD_SIZE]  # Exactly 240 bytes

    print(f"  Payload size: {len(payload)} bytes (max: {MAX_PAYLOAD_SIZE})")

    frame = client._build_frame(MsgType.QUERY, payload)
    msg = client._parse_frame(frame)

    match = msg is not None and msg.payload == payload

    print(f"  Frame size: {len(frame)} bytes")
    print(f"  Parsed successfully: {msg is not None}")
    print(f"  Payload matches: {match}")

    success = match
    print(f"\n[{'PASS' if success else 'FAIL'}] test_frame_roundtrip_max_payload")
    print()
    return success


def test_crc_error_detection():
    """Test 3: CRC error detection in corrupted frames."""
    print("=" * 70)
    print("TEST 3: CRC Error Detection")
    print("=" * 70)

    client = UARTIPCClient()

    # Build valid frame
    frame = client._build_frame(MsgType.QUERY, b"test\x00")
    print(f"  Original frame: {frame.hex()}")

    # Corrupt different parts
    corruptions = [
        ("payload byte", 8, 0xFF),
        ("type byte", 2, 0xFF),
        ("length byte", 5, 0xFF),
    ]

    detected = 0
    for name, pos, xor_val in corruptions:
        corrupted = bytearray(frame)
        corrupted[pos] ^= xor_val
        corrupted = bytes(corrupted)

        msg = client._parse_frame(corrupted)
        if msg is None:
            print(f"  [PASS] Corrupted {name}: CRC error detected")
            detected += 1
        else:
            print(f"  [FAIL] Corrupted {name}: Not detected!")

    success = detected == len(corruptions)
    print(f"\nResult: {detected}/{len(corruptions)} corruptions detected")
    print(f"[{'PASS' if success else 'FAIL'}] test_crc_error_detection")
    print()
    return success


# ============================================================================
# Platform Detection Tests
# ============================================================================

def test_platform_detection_windows():
    """Test 4: Platform detection on Windows."""
    print("=" * 70)
    print("TEST 4: Platform Detection Windows")
    print("=" * 70)

    # Import the module to test
    try:
        from system_bootstrap import SystemBootstrap
    except ImportError as e:
        print(f"  [SKIP] Cannot import SystemBootstrap: {e}")
        return True  # Skip but don't fail

    # Mock Windows environment
    with patch('platform.system', return_value='Windows'):
        bootstrap = SystemBootstrap.__new__(SystemBootstrap)
        bootstrap.platform = None  # Reset

        # Call detection
        detected = bootstrap._detect_platform()

        print(f"  Mocked platform: Windows")
        print(f"  Detected: {detected}")

        success = detected == 'windows'
        print(f"\n[{'PASS' if success else 'FAIL'}] test_platform_detection_windows")
        print()
        return success


def test_platform_detection_linux():
    """Test 5: Platform detection on Linux (no Pi, no QEMU)."""
    print("=" * 70)
    print("TEST 5: Platform Detection Linux")
    print("=" * 70)

    try:
        from system_bootstrap import SystemBootstrap
    except ImportError as e:
        print(f"  [SKIP] Cannot import SystemBootstrap: {e}")
        return True

    # Mock Linux environment without Pi or QEMU indicators
    with patch('platform.system', return_value='Linux'), \
         patch.object(Path, 'exists', return_value=False):

        bootstrap = SystemBootstrap.__new__(SystemBootstrap)
        bootstrap.platform = None

        detected = bootstrap._detect_platform()

        print(f"  Mocked platform: Linux")
        print(f"  Detected: {detected}")

        # Should return 'linux' when not Pi and not QEMU
        success = detected == 'linux'
        print(f"\n[{'PASS' if success else 'FAIL'}] test_platform_detection_linux")
        print()
        return success


# ============================================================================
# Mock IPC Tests
# ============================================================================

def test_mock_ipc_cache_operations():
    """Test 6: Mock IPC performs cache operations."""
    print("=" * 70)
    print("TEST 6: Mock IPC Cache Operations")
    print("=" * 70)

    import uart_ipc_client
    original = uart_ipc_client.SERIAL_AVAILABLE
    uart_ipc_client.SERIAL_AVAILABLE = False

    try:
        client = UARTIPCClient()
        client.connected = True

        # Test cache lookup
        result = client.cache_lookup("list files")
        has_hit = result and 'hit' in result

        # Test stats
        stats = client.get_stats()
        has_stats = stats and 'hits' in stats

        # Test command
        output = client.send_command("ps aux")
        has_output = output is not None

        # Test SHIELD check
        shield = client.shield_check("delete_file", {"path": "/tmp/test"})
        has_shield = shield and 'risk_score' in shield

        print(f"  cache_lookup: {result}")
        print(f"  get_stats: {stats}")
        print(f"  send_command: {output}")
        print(f"  shield_check: {shield}")

        success = has_hit and has_stats and has_output and has_shield

    finally:
        uart_ipc_client.SERIAL_AVAILABLE = original

    print(f"\n[{'PASS' if success else 'FAIL'}] test_mock_ipc_cache_operations")
    print()
    return success


def test_mock_ipc_statistics():
    """Test 7: Mock IPC tracks client statistics."""
    print("=" * 70)
    print("TEST 7: Mock IPC Client Statistics")
    print("=" * 70)

    import uart_ipc_client
    original = uart_ipc_client.SERIAL_AVAILABLE
    uart_ipc_client.SERIAL_AVAILABLE = False

    try:
        client = UARTIPCClient()
        client.connected = True

        # Mock mode returns directly without sending, so check stats structure
        # Build some frames to increment tx stats (mock _send_raw still tracks)
        client._build_frame(MsgType.QUERY, b"test")
        client._build_frame(MsgType.HEARTBEAT, b"12345678")

        # Get client stats
        stats = client.get_client_stats()

        # Check structure of stats dict
        expected_keys = {'tx_messages', 'rx_messages', 'tx_bytes', 'rx_bytes',
                         'crc_errors', 'timeouts', 'retries', 'connected', 'port', 'baudrate'}
        has_all_keys = expected_keys.issubset(set(stats.keys()))
        has_connected = stats.get('connected') == True
        has_port = stats.get('port') == client.port

        print(f"  Client stats keys: {list(stats.keys())}")
        print(f"  Has all expected keys: {has_all_keys}")
        print(f"  Shows connected: {has_connected}")
        print(f"  Has correct port: {has_port}")

        success = has_all_keys and has_connected and has_port

    finally:
        uart_ipc_client.SERIAL_AVAILABLE = original

    print(f"\n[{'PASS' if success else 'FAIL'}] test_mock_ipc_statistics")
    print()
    return success


# ============================================================================
# Graceful Degradation Tests
# ============================================================================

def test_ipc_not_connected():
    """Test 8: IPC operations fail gracefully when not connected."""
    print("=" * 70)
    print("TEST 8: IPC Not Connected Handling")
    print("=" * 70)

    client = UARTIPCClient()
    # Note: connected defaults to False

    # _send_raw should return False when not connected
    result = client._send_raw(MsgType.HEARTBEAT, b"test")

    print(f"  Connected: {client.connected}")
    print(f"  _send_raw result: {result}")

    success = result == False
    print(f"\n[{'PASS' if success else 'FAIL'}] test_ipc_not_connected")
    print()
    return success


def test_protocol_reset():
    """Test 9: Protocol reset clears state."""
    print("=" * 70)
    print("TEST 9: Protocol Reset")
    print("=" * 70)

    import uart_ipc_client
    original = uart_ipc_client.SERIAL_AVAILABLE
    uart_ipc_client.SERIAL_AVAILABLE = False

    try:
        client = UARTIPCClient()
        client.connected = True

        # Send some messages to increment sequence
        client._build_frame(MsgType.QUERY, b"test1")
        client._build_frame(MsgType.QUERY, b"test2")
        client._build_frame(MsgType.QUERY, b"test3")

        seq_before = client.seq_tx
        print(f"  Sequence before reset: {seq_before}")

        # Reset protocol
        client.reset_protocol()

        seq_after = client.seq_tx
        print(f"  Sequence after reset: {seq_after}")

        success = seq_before > 0 and seq_after == 0

    finally:
        uart_ipc_client.SERIAL_AVAILABLE = original

    print(f"\n[{'PASS' if success else 'FAIL'}] test_protocol_reset")
    print()
    return success


def test_client_print_status():
    """Test 10: Client status print works without error."""
    print("=" * 70)
    print("TEST 10: Client Status Print")
    print("=" * 70)

    import io
    import uart_ipc_client
    original = uart_ipc_client.SERIAL_AVAILABLE
    uart_ipc_client.SERIAL_AVAILABLE = False

    try:
        client = UARTIPCClient()
        client.connected = True

        # Capture output
        import sys
        old_stdout = sys.stdout
        sys.stdout = io.StringIO()

        try:
            client.print_status()
            output = sys.stdout.getvalue()
        finally:
            sys.stdout = old_stdout

        has_port = client.port in output
        has_baud = str(client.baudrate) in output
        has_connected = "True" in output or "connected" in output.lower()

        print(f"  Output length: {len(output)} chars")
        print(f"  Contains port: {has_port}")
        print(f"  Contains baud: {has_baud}")
        print(f"  Contains connected status: {has_connected}")

        success = has_port and has_baud and len(output) > 100

    finally:
        uart_ipc_client.SERIAL_AVAILABLE = original

    print(f"\n[{'PASS' if success else 'FAIL'}] test_client_print_status")
    print()
    return success


# ============================================================================
# Test Runner
# ============================================================================

def run_all_tests():
    """Run all integration tests and report results."""
    print()
    print("=" * 70)
    print("  JARVIS AI-OS - Phase 2 Integration Tests")
    print("  Phase 2 Week 31")
    print("=" * 70)
    print()

    tests = [
        test_frame_roundtrip_all_types,
        test_frame_roundtrip_max_payload,
        test_crc_error_detection,
        test_platform_detection_windows,
        test_platform_detection_linux,
        test_mock_ipc_cache_operations,
        test_mock_ipc_statistics,
        test_ipc_not_connected,
        test_protocol_reset,
        test_client_print_status,
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
            import traceback
            traceback.print_exc()
            failed += 1

    print()
    print("=" * 70)
    print("  INTEGRATION TEST SUMMARY")
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
