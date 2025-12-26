#!/usr/bin/env python3
"""
JARVIS AI-OS - UART IPC Client for Raspberry Pi 4
Phase 2 Week 31-33

Communicates with seL4 kernel running on Pi 4 via USB-serial adapter.

Protocol: See phase2/docs/UART_IPC_PROTOCOL.md for full specification.

Usage:
    # Connect to Pi 4
    client = UARTIPCClient('/dev/ttyUSB0')
    client.connect()

    # Send cache lookup
    result = client.cache_lookup("list files")
    print(result)  # {'status': 0, 'hit': True, 'action': 'exec_ls', 'trust': 0}

    # Disconnect
    client.disconnect()
"""

import struct
import time
import threading
import queue
from enum import IntEnum
from dataclasses import dataclass
from typing import Optional, Tuple, Dict, Any

# Try to import serial, provide mock if not available
try:
    import serial
    SERIAL_AVAILABLE = True
except ImportError:
    SERIAL_AVAILABLE = False
    print("[UART] Warning: pyserial not installed. Running in mock mode.")
    print("[UART] Install with: pip install pyserial")

# Protocol Constants
SYNC_BYTES = bytes([0xAA, 0x55])
MAX_PAYLOAD_SIZE = 240
HEADER_SIZE = 6  # TYPE(1) + SEQ(2) + LENGTH(2) + FLAGS(1) = 6 bytes
CRC_SIZE = 2
MAX_FRAME_SIZE = 2 + HEADER_SIZE + MAX_PAYLOAD_SIZE + CRC_SIZE  # 248 bytes

# Timing
HEARTBEAT_INTERVAL = 5.0  # seconds
HEARTBEAT_TIMEOUT = 30.0  # seconds
RESPONSE_TIMEOUT = 0.5    # seconds
INTER_BYTE_TIMEOUT = 0.1  # seconds
MAX_RETRIES = 3


class MsgType(IntEnum):
    """Message types as defined in protocol spec."""
    QUERY = 0x01
    RESPONSE = 0x02
    HEARTBEAT = 0x03
    HEARTBEAT_ACK = 0x04
    STATS_REQUEST = 0x05
    STATS_RESPONSE = 0x06
    COMMAND = 0x07
    COMMAND_RESULT = 0x08
    SHIELD_CHECK = 0x10
    SHIELD_RESULT = 0x11
    STATE_CHANGE = 0x20
    STATE_ACK = 0x21
    ERROR = 0xFE
    RESET = 0xFF


class MsgFlags(IntEnum):
    """Message flag bits."""
    ACK_REQ = 0x01
    PRIORITY = 0x02
    FRAGMENT = 0x04
    LAST_FRAG = 0x08


class ErrorCode(IntEnum):
    """Error codes for ERROR messages."""
    INVALID_TYPE = 0x01
    CRC_MISMATCH = 0x02
    PAYLOAD_TOO_LARGE = 0x03
    TIMEOUT = 0x04
    SEQUENCE_ERROR = 0x05
    CACHE_ERROR = 0x10
    COMMAND_ERROR = 0x11
    UNKNOWN = 0xFF


@dataclass
class Message:
    """Parsed IPC message."""
    msg_type: MsgType
    seq: int
    flags: int
    payload: bytes

    def __repr__(self):
        return (f"Message(type={self.msg_type.name}, seq={self.seq}, "
                f"flags=0x{self.flags:02x}, payload={len(self.payload)} bytes)")


def crc16_ccitt(data: bytes) -> int:
    """
    Calculate CRC-16-CCITT checksum.
    Polynomial: 0x1021, Initial: 0xFFFF
    """
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = (crc << 1) ^ 0x1021
            else:
                crc <<= 1
            crc &= 0xFFFF
    return crc


class UARTIPCClient:
    """
    UART IPC Client for seL4 communication on Raspberry Pi 4.

    Handles serial communication, message framing, CRC validation,
    and protocol state management.
    """

    # Default serial port settings
    DEFAULT_PORT = '/dev/ttyUSB0'  # Linux/WSL
    DEFAULT_BAUD = 115200

    def __init__(self, port: str = None, baudrate: int = DEFAULT_BAUD):
        """
        Initialize UART IPC client.

        Args:
            port: Serial port path (e.g., '/dev/ttyUSB0', 'COM3')
            baudrate: Baud rate (default: 115200)
        """
        self.port = port or self.DEFAULT_PORT
        self.baudrate = baudrate
        self.serial: Optional['serial.Serial'] = None

        # Protocol state
        self.seq_tx = 0
        self.seq_rx = 0
        self.connected = False
        self.last_heartbeat = 0.0

        # Threading
        self.rx_queue: queue.Queue = queue.Queue()
        self.rx_thread: Optional[threading.Thread] = None
        self.running = False

        # Statistics
        self.stats = {
            'tx_messages': 0,
            'rx_messages': 0,
            'tx_bytes': 0,
            'rx_bytes': 0,
            'crc_errors': 0,
            'timeouts': 0,
            'retries': 0
        }

    def connect(self) -> bool:
        """
        Open serial connection to Pi 4.

        Returns:
            True if connection successful, False otherwise.
        """
        if not SERIAL_AVAILABLE:
            print(f"[UART] Mock mode: Would connect to {self.port} @ {self.baudrate}")
            self.connected = True
            return True

        try:
            self.serial = serial.Serial(
                port=self.port,
                baudrate=self.baudrate,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=INTER_BYTE_TIMEOUT,
                write_timeout=1.0
            )

            # Clear any pending data
            self.serial.reset_input_buffer()
            self.serial.reset_output_buffer()

            # Start receiver thread
            self.running = True
            self.rx_thread = threading.Thread(target=self._rx_loop, daemon=True)
            self.rx_thread.start()

            self.connected = True
            self.last_heartbeat = time.time()

            print(f"[UART] Connected to {self.port} @ {self.baudrate}")
            return True

        except serial.SerialException as e:
            print(f"[UART] Failed to connect: {e}")
            return False

    def disconnect(self):
        """Close serial connection."""
        self.running = False
        self.connected = False

        if self.rx_thread:
            self.rx_thread.join(timeout=1.0)
            self.rx_thread = None

        if self.serial:
            self.serial.close()
            self.serial = None
            print("[UART] Disconnected")

    def _next_seq(self) -> int:
        """Get next sequence number."""
        self.seq_tx = (self.seq_tx + 1) & 0xFFFF
        return self.seq_tx

    def _build_frame(self, msg_type: MsgType, payload: bytes, flags: int = 0) -> bytes:
        """
        Build a complete message frame.

        Args:
            msg_type: Message type
            payload: Payload bytes
            flags: Message flags

        Returns:
            Complete frame bytes including SYNC and CRC.
        """
        if len(payload) > MAX_PAYLOAD_SIZE:
            raise ValueError(f"Payload too large: {len(payload)} > {MAX_PAYLOAD_SIZE}")

        seq = self._next_seq()

        # Build header: TYPE(1) + SEQ(2) + LENGTH(2) + FLAGS(1) = 6 bytes
        header = struct.pack('<BHHB',
            msg_type.value,     # TYPE (1 byte)
            seq,                # SEQ (2 bytes, little-endian)
            len(payload),       # LENGTH (2 bytes, little-endian)
            flags               # FLAGS (1 byte)
        )

        data = header + payload
        crc = crc16_ccitt(data)

        frame = SYNC_BYTES + data + struct.pack('<H', crc)
        return frame

    def _parse_frame(self, frame: bytes) -> Optional[Message]:
        """
        Parse a received frame.

        Args:
            frame: Complete frame bytes including SYNC and CRC.

        Returns:
            Parsed Message or None if invalid.
        """
        if len(frame) < 2 + HEADER_SIZE + CRC_SIZE:
            return None

        # Check sync bytes
        if frame[:2] != SYNC_BYTES:
            return None

        # Parse header
        msg_type = frame[2]
        seq = struct.unpack('<H', frame[3:5])[0]
        length = struct.unpack('<H', frame[5:7])[0]
        flags = frame[7]

        # Validate length
        expected_len = 2 + HEADER_SIZE + length + CRC_SIZE
        if len(frame) < expected_len:
            return None

        # Extract payload
        payload = frame[8:8 + length]

        # Verify CRC
        data_for_crc = frame[2:8 + length]  # TYPE through PAYLOAD
        expected_crc = struct.unpack('<H', frame[8 + length:10 + length])[0]
        actual_crc = crc16_ccitt(data_for_crc)

        if expected_crc != actual_crc:
            self.stats['crc_errors'] += 1
            print(f"[UART] CRC error: expected 0x{expected_crc:04x}, got 0x{actual_crc:04x}")
            return None

        try:
            msg_type_enum = MsgType(msg_type)
        except ValueError:
            print(f"[UART] Unknown message type: 0x{msg_type:02x}")
            return None

        return Message(msg_type_enum, seq, flags, payload)

    def _rx_loop(self):
        """Background thread for receiving messages."""
        buffer = bytearray()

        while self.running and self.serial:
            try:
                # Read available bytes
                data = self.serial.read(256)
                if not data:
                    continue

                buffer.extend(data)
                self.stats['rx_bytes'] += len(data)

                # Look for sync bytes
                while len(buffer) >= 2:
                    sync_pos = buffer.find(SYNC_BYTES)
                    if sync_pos < 0:
                        # No sync found, keep last byte in case partial sync
                        buffer = buffer[-1:]
                        break
                    elif sync_pos > 0:
                        # Discard bytes before sync
                        buffer = buffer[sync_pos:]

                    # Need at least header to determine length
                    if len(buffer) < 2 + HEADER_SIZE:
                        break

                    # Get payload length
                    length = struct.unpack('<H', buffer[5:7])[0]
                    frame_len = 2 + HEADER_SIZE + length + CRC_SIZE

                    if len(buffer) < frame_len:
                        break  # Wait for more data

                    # Extract and parse frame
                    frame = bytes(buffer[:frame_len])
                    buffer = buffer[frame_len:]

                    msg = self._parse_frame(frame)
                    if msg:
                        self.stats['rx_messages'] += 1
                        self.seq_rx = msg.seq

                        # Handle heartbeat internally
                        if msg.msg_type == MsgType.HEARTBEAT:
                            self._handle_heartbeat(msg)
                        else:
                            self.rx_queue.put(msg)

            except serial.SerialException as e:
                print(f"[UART] RX error: {e}")
                break

    def _handle_heartbeat(self, msg: Message):
        """Handle received heartbeat."""
        self.last_heartbeat = time.time()

        # Send acknowledgment
        if len(msg.payload) >= 8:
            orig_ts = msg.payload[:8]
            now_ts = struct.pack('<Q', int(time.time() * 1000))
            self._send_raw(MsgType.HEARTBEAT_ACK, orig_ts + now_ts)

    def _send_raw(self, msg_type: MsgType, payload: bytes, flags: int = 0) -> bool:
        """
        Send a message without waiting for response.

        Args:
            msg_type: Message type
            payload: Payload bytes
            flags: Message flags

        Returns:
            True if sent successfully.
        """
        if not self.connected:
            return False

        frame = self._build_frame(msg_type, payload, flags)

        if not SERIAL_AVAILABLE:
            print(f"[UART] Mock TX: {msg_type.name}, {len(payload)} bytes")
            self.stats['tx_messages'] += 1
            self.stats['tx_bytes'] += len(frame)
            return True

        try:
            self.serial.write(frame)
            self.stats['tx_messages'] += 1
            self.stats['tx_bytes'] += len(frame)
            return True
        except serial.SerialException as e:
            print(f"[UART] TX error: {e}")
            return False

    def _send_and_wait(self, msg_type: MsgType, payload: bytes,
                       expected_type: MsgType, timeout: float = RESPONSE_TIMEOUT,
                       retries: int = MAX_RETRIES) -> Optional[Message]:
        """
        Send a message and wait for response.

        Args:
            msg_type: Request message type
            payload: Request payload
            expected_type: Expected response type
            timeout: Response timeout in seconds
            retries: Number of retries on timeout

        Returns:
            Response Message or None on failure.
        """
        for attempt in range(retries + 1):
            if not self._send_raw(msg_type, payload, MsgFlags.ACK_REQ):
                return None

            try:
                msg = self.rx_queue.get(timeout=timeout)
                if msg.msg_type == expected_type:
                    return msg
                elif msg.msg_type == MsgType.ERROR:
                    self._handle_error(msg)
                    return None
                else:
                    # Put back unexpected message
                    self.rx_queue.put(msg)

            except queue.Empty:
                self.stats['timeouts'] += 1
                if attempt < retries:
                    self.stats['retries'] += 1
                    print(f"[UART] Timeout, retry {attempt + 1}/{retries}")

        return None

    def _handle_error(self, msg: Message):
        """Handle ERROR message."""
        if len(msg.payload) < 1:
            return

        code = msg.payload[0]
        error_msg = msg.payload[1:].decode('utf-8', errors='replace').rstrip('\x00')

        try:
            code_name = ErrorCode(code).name
        except ValueError:
            code_name = f"UNKNOWN(0x{code:02x})"

        print(f"[UART] Error {code_name}: {error_msg}")

    # ========== Public API ==========

    def cache_lookup(self, query: str) -> Optional[Dict[str, Any]]:
        """
        Perform cache lookup query.

        Args:
            query: Query string (e.g., "list files")

        Returns:
            Dict with 'status', 'hit', 'action', 'trust' or None on failure.
        """
        payload = query.encode('utf-8') + b'\x00'

        if not SERIAL_AVAILABLE:
            # Mock response for testing
            return {
                'status': 0,
                'hit': True,
                'action': 'exec_ls',
                'trust': 0,
                'raw': 'ACTION:exec_ls|TRUST:0'
            }

        msg = self._send_and_wait(MsgType.QUERY, payload, MsgType.RESPONSE)
        if not msg or len(msg.payload) < 2:
            return None

        status = msg.payload[0]
        hit = msg.payload[1] == 0x01
        action_str = msg.payload[2:].decode('utf-8', errors='replace').rstrip('\x00')

        # Parse action string (format: "ACTION:xxx|TRUST:n")
        result = {
            'status': status,
            'hit': hit,
            'action': None,
            'trust': None,
            'raw': action_str
        }

        for part in action_str.split('|'):
            if ':' in part:
                key, val = part.split(':', 1)
                if key == 'ACTION':
                    result['action'] = val
                elif key == 'TRUST':
                    try:
                        result['trust'] = int(val)
                    except ValueError:
                        pass

        return result

    def send_heartbeat(self) -> bool:
        """
        Send heartbeat and wait for acknowledgment.

        Returns:
            True if acknowledged, False on timeout.
        """
        ts = struct.pack('<Q', int(time.time() * 1000))
        msg = self._send_and_wait(MsgType.HEARTBEAT, ts, MsgType.HEARTBEAT_ACK)

        if msg:
            self.last_heartbeat = time.time()
            return True
        return False

    def get_stats(self) -> Optional[Dict[str, int]]:
        """
        Get cache statistics from Pi 4.

        Returns:
            Dict with 'hits', 'misses', 'entries', 'patterns', 'uptime' or None.
        """
        if not SERIAL_AVAILABLE:
            return {'hits': 100, 'misses': 15, 'entries': 258, 'patterns': 258, 'uptime': 3600}

        msg = self._send_and_wait(MsgType.STATS_REQUEST, b'', MsgType.STATS_RESPONSE)
        if not msg or len(msg.payload) < 20:
            return None

        hits, misses, entries, patterns, uptime = struct.unpack('<IIIII', msg.payload[:20])
        return {
            'hits': hits,
            'misses': misses,
            'entries': entries,
            'patterns': patterns,
            'uptime': uptime
        }

    def send_command(self, command: str) -> Optional[str]:
        """
        Send shell command for execution.

        Args:
            command: Command string

        Returns:
            Command output string or None on failure.
        """
        payload = command.encode('utf-8') + b'\x00'

        if not SERIAL_AVAILABLE:
            return f"[Mock] Would execute: {command}"

        msg = self._send_and_wait(MsgType.COMMAND, payload, MsgType.COMMAND_RESULT,
                                   timeout=5.0)  # Longer timeout for commands
        if not msg:
            return None

        return msg.payload.decode('utf-8', errors='replace').rstrip('\x00')

    def shield_check(self, action: str, context: Dict[str, Any] = None) -> Optional[Dict[str, Any]]:
        """
        Request SHIELD risk assessment.

        Args:
            action: Action to check
            context: Optional context dict

        Returns:
            Dict with 'risk_score', 'recommendation', 'factors' or None.
        """
        # Encode action + context as JSON-like string
        import json
        data = {'action': action}
        if context:
            data['context'] = context
        payload = json.dumps(data).encode('utf-8') + b'\x00'

        if not SERIAL_AVAILABLE:
            return {
                'risk_score': 0.15,
                'recommendation': 'allow',
                'factors': ['safe_action', 'normal_user']
            }

        msg = self._send_and_wait(MsgType.SHIELD_CHECK, payload, MsgType.SHIELD_RESULT)
        if not msg:
            return None

        try:
            result_str = msg.payload.decode('utf-8', errors='replace').rstrip('\x00')
            return json.loads(result_str)
        except json.JSONDecodeError:
            return None

    def reset_protocol(self):
        """Send protocol reset and reinitialize state."""
        self._send_raw(MsgType.RESET, b'')
        self.seq_tx = 0
        self.seq_rx = 0

        # Clear RX queue
        while not self.rx_queue.empty():
            try:
                self.rx_queue.get_nowait()
            except queue.Empty:
                break

    def get_client_stats(self) -> Dict[str, Any]:
        """Get client-side statistics."""
        return {
            **self.stats,
            'connected': self.connected,
            'port': self.port,
            'baudrate': self.baudrate,
            'last_heartbeat_age': time.time() - self.last_heartbeat if self.last_heartbeat else None
        }

    def print_status(self):
        """Print client status for debugging."""
        print("\n========== UART IPC CLIENT STATUS ==========")
        print(f"Port:        {self.port}")
        print(f"Baud Rate:   {self.baudrate}")
        print(f"Connected:   {self.connected}")
        print(f"Serial OK:   {SERIAL_AVAILABLE}")
        print(f"\nSequence:")
        print(f"  TX Seq:    {self.seq_tx}")
        print(f"  RX Seq:    {self.seq_rx}")
        print(f"\nStatistics:")
        for key, val in self.stats.items():
            print(f"  {key}: {val}")
        if self.last_heartbeat:
            age = time.time() - self.last_heartbeat
            print(f"  Last Heartbeat: {age:.1f}s ago")
        print("=============================================\n")


# ========== Test/Demo Code ==========

def test_crc():
    """Test CRC calculation."""
    # Test vector
    data = bytes([0x01, 0x01, 0x00, 0x0B, 0x00, 0x01]) + b"list files\x00"
    crc = crc16_ccitt(data)
    print(f"CRC test: 0x{crc:04x}")

    # Verify frame building
    client = UARTIPCClient()
    frame = client._build_frame(MsgType.QUERY, b"test\x00")
    print(f"Frame: {frame.hex()}")

    # Verify frame parsing
    msg = client._parse_frame(frame)
    print(f"Parsed: {msg}")


def main():
    """Main entry point for testing."""
    import argparse

    parser = argparse.ArgumentParser(description='UART IPC Client for Pi 4')
    parser.add_argument('--port', default='/dev/ttyUSB0', help='Serial port')
    parser.add_argument('--baud', type=int, default=115200, help='Baud rate')
    parser.add_argument('--test-crc', action='store_true', help='Run CRC test')
    parser.add_argument('--query', type=str, help='Send cache lookup query')
    args = parser.parse_args()

    if args.test_crc:
        test_crc()
        return

    print("UART IPC Client - Test Mode")

    client = UARTIPCClient(port=args.port, baudrate=args.baud)

    if not client.connect():
        print("Failed to connect")
        return

    try:
        if args.query:
            result = client.cache_lookup(args.query)
            print(f"Query result: {result}")
        else:
            # Interactive test
            print("Sending heartbeat...")
            if client.send_heartbeat():
                print("Heartbeat OK")
            else:
                print("Heartbeat timeout")

            print("\nGetting stats...")
            stats = client.get_stats()
            print(f"Stats: {stats}")

            print("\nTesting cache lookup...")
            result = client.cache_lookup("list files")
            print(f"Result: {result}")

        client.print_status()

    finally:
        client.disconnect()


if __name__ == '__main__':
    main()
