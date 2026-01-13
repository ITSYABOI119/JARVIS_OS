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
import math
import csv
from pathlib import Path
from enum import IntEnum
from dataclasses import dataclass
from typing import Optional, Tuple, Dict, Any, List

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
INTER_BYTE_TIMEOUT = 0.01  # seconds (lower for faster RX responsiveness)
MAX_RETRIES = 3
PENDING_PER_SEQ_LIMIT = 8
PENDING_TOTAL_LIMIT = 128

# Default benchmark queries (should be in cache patterns)
DEFAULT_BENCH_QUERIES = [
    "help",
    "status",
    "version",
    "info",
    "about",
    "hello",
    "time",
    "date",
    "uptime",
    "ls",
    "pwd",
    "ls -l",
    "ls -a",
    "cat readme",
    "ps",
    "top",
    "free",
    "df",
    "cpu",
    "list files",
    "show files"
]


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


def fmt_bytes(data: bytes, width: int = 16) -> str:
    """
    Format bytes as hex + ASCII dump lines.
    """
    if not data:
        return ""

    lines = []
    for offset in range(0, len(data), width):
        chunk = data[offset:offset + width]
        hex_bytes = ' '.join(f"{b:02X}" for b in chunk)
        ascii_preview = ''.join(chr(b) if 32 <= b <= 126 else '.' for b in chunk)
        lines.append(f"{offset:04X}  {hex_bytes:<{width * 3 - 1}}  |{ascii_preview}|")
    return "\n".join(lines)


class UARTIPCClient:
    """
    UART IPC Client for seL4 communication on Raspberry Pi 4.

    Handles serial communication, message framing, CRC validation,
    and protocol state management.
    """

    # Default serial port settings
    DEFAULT_PORT = '/dev/ttyUSB0'  # Linux/WSL
    DEFAULT_BAUD = 115200

    def __init__(self, port: str = None, baudrate: int = DEFAULT_BAUD,
                 mock_latency_ms: float = 0.0, mock_mode: bool = False,
                 raw_log_path: Optional[str] = None,
                 raw_bin_path: Optional[str] = None):
        """
        Initialize UART IPC client.

        Args:
            port: Serial port path (e.g., '/dev/ttyUSB0', 'COM3')
            baudrate: Baud rate (default: 115200)
            mock_latency_ms: Simulated latency in mock mode (0=disabled, 10-20=realistic)
            mock_mode: Force mock mode even if pyserial is available (for testing)
        """
        self.port = port or self.DEFAULT_PORT
        self.baudrate = baudrate
        self.serial: Optional['serial.Serial'] = None
        self.raw_log_path = raw_log_path
        self.raw_log_file: Optional[object] = None
        self.raw_bin_path = raw_bin_path
        self.raw_bin_file: Optional[object] = None
        self._raw_log_lock = threading.Lock()
        self._raw_log_warned = False
        self._rx_reset = threading.Event()
        self._first_query = True
        self._pending_msgs: Dict[int, List[Message]] = {}
        self._pending_total = 0
        self.last_tx_seq: Optional[int] = None
        self.last_rx_seq: Optional[int] = None
        self.last_seq_mismatch = False
        self.last_retries_used = 0

        # Mock mode settings
        self._force_mock = mock_mode
        self.mock_latency_ms = mock_latency_ms
        self.mock_latency_jitter = 0.2  # +/- 20% jitter

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
            'retries': 0,
            'mock_latency_total_ms': 0.0
        }

    def _simulate_latency(self):
        """
        Simulate UART latency in mock mode.

        In real UART at 115200 baud, a typical 50-byte message takes ~4ms,
        plus round-trip and processing time = 10-20ms total.
        """
        if self.mock_latency_ms > 0:
            import random
            # Add jitter for realism
            jitter = 1.0 + (random.random() - 0.5) * 2 * self.mock_latency_jitter
            latency = self.mock_latency_ms * jitter
            time.sleep(latency / 1000.0)
            self.stats['mock_latency_total_ms'] += latency

    @property
    def in_mock_mode(self) -> bool:
        """
        Check if running in mock mode.

        Mock mode is active if:
        - mock_mode=True was passed to __init__, OR
        - pyserial is not available
        """
        return self._force_mock or not SERIAL_AVAILABLE

    def _drain_input(self, settle_ms: int = 250) -> int:
        if not self.serial:
            return 0

        end = time.time() + (settle_ms / 1000.0)
        drained = 0
        old_timeout = self.serial.timeout
        self.serial.timeout = 0
        try:
            while time.time() < end:
                waiting = self.serial.in_waiting if hasattr(self.serial, 'in_waiting') else 0
                if waiting:
                    data = self.serial.read(waiting)
                    drained += len(data)
                else:
                    time.sleep(0.01)
        finally:
            self.serial.timeout = old_timeout

        return drained

    def _clear_rx_state(self) -> None:
        while not self.rx_queue.empty():
            try:
                self.rx_queue.get_nowait()
            except queue.Empty:
                break
        self._pending_msgs.clear()
        self._pending_total = 0

        self._rx_reset.set()

    def connect(self) -> bool:
        """
        Open serial connection to Pi 4.

        Returns:
            True if connection successful, False otherwise.
        """
        if self.in_mock_mode:
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

            if self.raw_log_path:
                try:
                    self.raw_log_file = open(
                        self.raw_log_path,
                        'a',
                        encoding='utf-8',
                        newline='\n'
                    )
                    ts = time.strftime('%Y-%m-%d %H:%M:%S', time.localtime())
                    self.raw_log_file.write(f"=== UART RAW LOG START {ts} ===\n")
                    self.raw_log_file.flush()
                except OSError as e:
                    print(f"[UART] Raw log open failed: {e}")
                    self.raw_log_file = None
            if self.raw_bin_path:
                try:
                    self.raw_bin_file = open(self.raw_bin_path, 'ab', buffering=0)
                except OSError as e:
                    print(f"[UART] Raw bin open failed: {e}")
                    self.raw_bin_file = None

            # Ensure TX idles high (clear break, assert control lines)
            self.serial.break_condition = False
            self.serial.dtr = True
            self.serial.rts = True

            # Clear any pending data
            self.serial.reset_input_buffer()
            self.serial.reset_output_buffer()
            self._drain_input(250)
            self._clear_rx_state()

            # Start receiver thread
            self.running = True
            self.rx_thread = threading.Thread(target=self._rx_loop, daemon=True)
            self.rx_thread.start()

            self.connected = True
            self.last_heartbeat = time.time()
            self._first_query = True

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
        if self.raw_log_file:
            self.raw_log_file.close()
            self.raw_log_file = None
        if self.raw_bin_file:
            self.raw_bin_file.close()
            self.raw_bin_file = None

    def _next_seq(self) -> int:
        """Get next sequence number."""
        self.seq_tx = (self.seq_tx + 1) & 0xFFFF
        return self.seq_tx

    def _build_frame_with_seq(self, msg_type: MsgType, payload: bytes,
                              flags: int = 0) -> Tuple[bytes, int]:
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
        return frame, seq

    def _build_frame(self, msg_type: MsgType, payload: bytes, flags: int = 0) -> bytes:
        frame, _ = self._build_frame_with_seq(msg_type, payload, flags)
        return frame

    def _stash_pending(self, msg: Message) -> None:
        self._pending_msgs.setdefault(msg.seq, []).append(msg)
        msgs = self._pending_msgs[msg.seq]
        if len(msgs) > PENDING_PER_SEQ_LIMIT:
            msgs.pop(0)
            self._pending_total = max(0, self._pending_total - 1)
        self._pending_total += 1
        self._evict_pending()

    def _evict_pending(self) -> None:
        while self._pending_total > PENDING_TOTAL_LIMIT and self._pending_msgs:
            seq = next(iter(self._pending_msgs))
            msgs = self._pending_msgs[seq]
            msgs.pop(0)
            self._pending_total = max(0, self._pending_total - 1)
            if not msgs:
                del self._pending_msgs[seq]

    def _pop_pending(self, seq: int, expected_type: MsgType) -> Optional[Message]:
        msgs = self._pending_msgs.get(seq)
        if not msgs:
            return None

        for idx, msg in enumerate(msgs):
            if msg.msg_type == expected_type or msg.msg_type == MsgType.ERROR:
                msgs.pop(idx)
                self._pending_total = max(0, self._pending_total - 1)
                if not msgs:
                    del self._pending_msgs[seq]
                return msg

        return None

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
                if self._rx_reset.is_set():
                    buffer.clear()
                    self._rx_reset.clear()

                # Read available bytes
                waiting = self.serial.in_waiting if hasattr(self.serial, 'in_waiting') else 0
                read_size = waiting if waiting > 0 else 1
                data = self.serial.read(read_size)
                if not data:
                    continue

                if self.raw_log_file:
                    self._log_raw("RX", data)
                if self.raw_bin_file:
                    try:
                        self.raw_bin_file.write(data)
                    except OSError as e:
                        print(f"[UART] Raw bin write failed: {e}")
                        self.raw_bin_file = None

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
                    # Guard against corrupted headers that would stall the parser
                    if length > MAX_PAYLOAD_SIZE:
                        # Drop one byte and resync on next AA55
                        buffer = buffer[1:]
                        continue
                    frame_len = 2 + HEADER_SIZE + length + CRC_SIZE

                    if frame_len > MAX_FRAME_SIZE:
                        buffer = buffer[1:]
                        continue

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

    def _log_raw(self, direction: str, data: bytes, seq: Optional[int] = None) -> None:
        if not self.raw_log_file or not data:
            return

        data_bytes = bytes(data)
        if not data_bytes:
            return

        ts = time.time()
        ts_str = time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(ts))
        ts_ms = int((ts - int(ts)) * 1000)
        header = f"[{direction}] len={len(data)} ts={ts_str}.{ts_ms:03d}"
        if seq is not None:
            header += f" seq={seq}"

        dump = fmt_bytes(data_bytes)
        hex_line = data_bytes.hex().upper()

        if not self._raw_log_warned:
            if len(data_bytes) != len(data) or len(hex_line) != len(data_bytes) * 2:
                print("[UART] Raw log warning: inconsistent data length")
                self._raw_log_warned = True
            elif data_bytes and not dump:
                print("[UART] Raw log warning: empty dump for non-empty data")
                self._raw_log_warned = True

        with self._raw_log_lock:
            try:
                self.raw_log_file.write(header + "\n")
                if dump:
                    self.raw_log_file.write(dump + "\n")
                self.raw_log_file.write(f"hex: {hex_line}\n\n")
                self.raw_log_file.flush()
            except OSError as e:
                print(f"[UART] Raw log write failed: {e}")
                self.raw_log_file = None

    def _send_raw_with_seq(self, msg_type: MsgType, payload: bytes,
                           flags: int = 0) -> Tuple[bool, Optional[int]]:
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
            return False, None

        frame, seq = self._build_frame_with_seq(msg_type, payload, flags)

        if self.in_mock_mode:
            print(f"[UART] Mock TX: {msg_type.name}, {len(payload)} bytes")
            self.stats['tx_messages'] += 1
            self.stats['tx_bytes'] += len(frame)
            return True, seq

        try:
            self.serial.write(frame)
            if self.raw_log_file:
                self._log_raw("TX", frame, seq=seq)
            self.stats['tx_messages'] += 1
            self.stats['tx_bytes'] += len(frame)
            return True, seq
        except serial.SerialException as e:
            print(f"[UART] TX error: {e}")
            return False, None

    def _send_raw(self, msg_type: MsgType, payload: bytes, flags: int = 0) -> bool:
        ok, _ = self._send_raw_with_seq(msg_type, payload, flags)
        return ok

    def _send_and_wait(self, msg_type: MsgType, payload: bytes,
                       expected_type: MsgType, timeout: float = RESPONSE_TIMEOUT,
                       retries: int = MAX_RETRIES,
                       allow_seq_mismatch: bool = False) -> Optional[Message]:
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
        effective_timeout = timeout
        if msg_type == MsgType.QUERY and self._first_query and timeout == RESPONSE_TIMEOUT:
            effective_timeout = max(timeout, 2.5)
            self._first_query = False

        self.last_seq_mismatch = False
        self.last_retries_used = 0

        for attempt in range(retries + 1):
            ok, expected_seq = self._send_raw_with_seq(msg_type, payload, MsgFlags.ACK_REQ)
            if not ok or expected_seq is None:
                self.last_tx_seq = None
                self.last_rx_seq = None
                self.last_retries_used = attempt
                return None
            self.last_tx_seq = expected_seq
            self.last_rx_seq = None

            pending = self._pop_pending(expected_seq, expected_type)
            if pending:
                if pending.msg_type == MsgType.ERROR:
                    self._handle_error(pending)
                    self.last_retries_used = attempt
                    return None
                self.last_rx_seq = pending.seq
                self.last_retries_used = attempt
                return pending

            deadline = time.time() + effective_timeout
            try:
                while True:
                    remaining = deadline - time.time()
                    if remaining <= 0:
                        raise queue.Empty
                    msg = self.rx_queue.get(timeout=remaining)
                    if msg.seq == expected_seq and msg.msg_type == expected_type:
                        self.last_rx_seq = msg.seq
                        self.last_retries_used = attempt
                        return msg
                    if msg.seq == expected_seq and msg.msg_type == MsgType.ERROR:
                        self._handle_error(msg)
                        self.last_rx_seq = msg.seq
                        self.last_retries_used = attempt
                        return None
                    if allow_seq_mismatch and msg.msg_type == expected_type:
                        self.last_seq_mismatch = True
                        self.last_rx_seq = msg.seq
                        self.last_retries_used = attempt
                        print(f"[UART] SEQ_MISMATCH expected {expected_seq} got {msg.seq}")
                        return msg
                    self._stash_pending(msg)

            except queue.Empty:
                self.stats['timeouts'] += 1
                if attempt < retries:
                    self.stats['retries'] += 1
                    print(f"[UART] Timeout, retry {attempt + 1}/{retries}")

        self.last_retries_used = retries
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

    def _payload_to_text(self, payload: bytes) -> str:
        return payload.decode('utf-8', errors='replace').rstrip('\x00')

    def _parse_kv_text(self, text: str) -> Dict[str, str]:
        kv: Dict[str, str] = {}
        for part in text.split('|'):
            if ':' in part:
                key, val = part.split(':', 1)
                kv[key.strip().upper()] = val.strip()
        return kv

    # ========== Public API ==========

    def cache_lookup(self, query: str,
                     allow_seq_mismatch: bool = False) -> Optional[Dict[str, Any]]:
        """
        Perform cache lookup query.

        Args:
            query: Query string (e.g., "list files")

        Returns:
            Dict with 'status', 'hit', 'action', 'trust' or None on failure.
        """
        payload = query.encode('utf-8') + b'\x00'

        if self.in_mock_mode:
            # Mock response for testing with optional latency simulation
            self._simulate_latency()
            return {
                'status': 0,
                'hit': True,
                'action': 'exec_ls',
                'trust': 0,
                'raw': 'ACTION:exec_ls|TRUST:0'
            }

        msg = self._send_and_wait(MsgType.QUERY, payload, MsgType.RESPONSE,
                                  allow_seq_mismatch=allow_seq_mismatch)
        if not msg or not msg.payload:
            return None

        text = self._payload_to_text(msg.payload)
        kv = self._parse_kv_text(text)
        if 'ACTION' in kv or 'HIT' in kv or 'TRUST' in kv or 'STATUS' in kv:
            try:
                hit_val = int(kv.get('HIT', '0'))
            except ValueError:
                hit_val = 0
            hit = hit_val == 1
            if 'STATUS' in kv:
                try:
                    status = int(kv['STATUS'], 0)
                except ValueError:
                    status = 0 if hit else 1
            else:
                status = 0 if hit else 1

            result = {
                'status': status,
                'hit': hit,
                'action': kv.get('ACTION'),
                'trust': None,
                'raw': text
            }
            if 'TRUST' in kv:
                try:
                    result['trust'] = int(kv['TRUST'], 0)
                except ValueError:
                    pass
            return result

        if len(msg.payload) < 2:
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
        if self.in_mock_mode:
            self._simulate_latency()
            return {'hits': 100, 'misses': 15, 'entries': 258, 'patterns': 258, 'uptime': 3600}

        msg = self._send_and_wait(MsgType.STATS_REQUEST, b'', MsgType.STATS_RESPONSE)
        if not msg or not msg.payload:
            return None

        text = self._payload_to_text(msg.payload)
        kv = self._parse_kv_text(text)
        if 'ENTRIES' in kv or 'HITS' in kv or 'MISSES' in kv or 'RATE' in kv:
            try:
                hits = int(kv.get('HITS', '0'))
            except ValueError:
                hits = 0
            try:
                misses = int(kv.get('MISSES', '0'))
            except ValueError:
                misses = 0
            try:
                entries = int(kv.get('ENTRIES', '0'))
            except ValueError:
                entries = 0
            try:
                patterns = int(kv.get('PATTERNS', str(entries)))
            except ValueError:
                patterns = entries
            try:
                uptime = int(kv.get('UPTIME', '0'))
            except ValueError:
                uptime = 0
            return {
                'hits': hits,
                'misses': misses,
                'entries': entries,
                'patterns': patterns,
                'uptime': uptime
            }

        if len(msg.payload) < 20:
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

        if self.in_mock_mode:
            self._simulate_latency()
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

        if self.in_mock_mode:
            self._simulate_latency()
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
        self.last_tx_seq = None
        self.last_rx_seq = None
        self.last_seq_mismatch = False
        self.last_retries_used = 0
        self._clear_rx_state()

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


def _load_query_set(path: str) -> List[str]:
    queries: List[str] = []
    path_obj = Path(path)
    if not path_obj.is_absolute():
        path_obj = Path(__file__).resolve().parent / path_obj
    with open(path_obj, 'r', encoding='utf-8') as handle:
        for line in handle:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            queries.append(line)
    return queries


def _percentile(values: List[float], pct: float) -> Optional[float]:
    if not values:
        return None
    ordered = sorted(values)
    idx = int(math.ceil((pct / 100.0) * len(ordered))) - 1
    idx = max(0, min(idx, len(ordered) - 1))
    return ordered[idx]


def _default_bench_log_path() -> str:
    ts = time.strftime('%Y%m%d_%H%M%S', time.localtime())
    base = Path(__file__).resolve().parents[2] / "logs"
    if base.exists():
        return str(base / f"uart_bench_{ts}.csv")
    return f"uart_bench_{ts}.csv"


def run_benchmark(client: UARTIPCClient, count: int,
                  queries: List[str], log_path: Optional[str],
                  pace_ms: int = 15) -> None:
    if count <= 0:
        print("[UART] Benchmark count must be > 0")
        return
    if not queries:
        print("[UART] Benchmark query list is empty")
        return

    log_path = log_path or _default_bench_log_path()

    hits = 0
    misses = 0
    timeouts = 0
    latencies: List[float] = []
    consecutive_timeouts = 0

    print(f"[UART] Benchmark: {count} queries")
    print(f"[UART] Benchmark log: {log_path}")

    with open(log_path, 'w', encoding='utf-8', newline='\n') as handle:
        writer = csv.writer(handle)
        writer.writerow(["index", "query", "ok", "hit", "trust", "action",
                         "tx_seq", "rx_seq", "rtt_ms", "retries_used",
                         "seq_mismatch"])

        for idx in range(count):
            query = queries[idx % len(queries)]
            start = time.time()
            result = None
            error = None
            try:
                result = client.cache_lookup(query, allow_seq_mismatch=True)
            except Exception as exc:
                error = exc
            rtt_ms = (time.time() - start) * 1000.0
            tx_seq = client.last_tx_seq if client.last_tx_seq is not None else ""
            rx_seq = client.last_rx_seq if client.last_rx_seq is not None else ""
            retries_used = client.last_retries_used
            seq_mismatch = client.last_seq_mismatch

            if not result:
                timeouts += 1
                consecutive_timeouts += 1
                if error:
                    print(f"[UART] Benchmark error at {idx + 1}: {error}")
                writer.writerow([idx + 1, query, 0, "", "", "",
                                 tx_seq, rx_seq, f"{rtt_ms:.2f}",
                                 retries_used, int(seq_mismatch)])

                if consecutive_timeouts == 3:
                    print("[UART] Benchmark: 3 timeouts, soft RX reset + heartbeat")
                    client._clear_rx_state()
                    if client.send_heartbeat():
                        print("[UART] Benchmark: heartbeat OK after reset")
                        consecutive_timeouts = 0

                if consecutive_timeouts >= 10:
                    print("[UART] Benchmark: 10 timeouts, reconnecting")
                    client.disconnect()
                    time.sleep(0.2)
                    if client.connect():
                        consecutive_timeouts = 0
                    else:
                        print("[UART] Benchmark: reconnect failed, continuing")

                backoff_ms = max(pace_ms, 25) * (2 ** min(consecutive_timeouts, 3))
                backoff_ms = min(backoff_ms, 500)
                if backoff_ms > 0:
                    time.sleep(backoff_ms / 1000.0)
                continue

            hit = bool(result.get('hit'))
            trust = result.get('trust')
            action = result.get('action') or ""
            writer.writerow([idx + 1, query, 1, int(hit),
                             trust if trust is not None else "", action,
                             tx_seq, rx_seq, f"{rtt_ms:.2f}",
                             retries_used, int(seq_mismatch)])

            if hit:
                hits += 1
            else:
                misses += 1
            latencies.append(rtt_ms)
            consecutive_timeouts = 0

            if pace_ms > 0:
                time.sleep(pace_ms / 1000.0)

    total = hits + misses + timeouts
    success = hits + misses
    hit_rate = (hits / success * 100.0) if success else 0.0
    success_rate = (success / total * 100.0) if total else 0.0
    min_ms = min(latencies) if latencies else None
    avg_ms = (sum(latencies) / len(latencies)) if latencies else None
    median_ms = _percentile(latencies, 50.0)
    p95_ms = _percentile(latencies, 95.0)

    print("\n========== UART IPC BENCH RESULTS ==========")
    print(f"Total:       {total}")
    print(f"Hits:        {hits}")
    print(f"Misses:      {misses}")
    print(f"Timeouts:    {timeouts}")
    print(f"Success:     {success_rate:.1f}%")
    print(f"Hit rate:    {hit_rate:.1f}%")
    if min_ms is not None:
        print(f"RTT (ms):    min={min_ms:.2f} avg={avg_ms:.2f} "
              f"med={median_ms:.2f} p95={p95_ms:.2f}")
    else:
        print("RTT (ms):    n/a")
    print("============================================\n")


def main():
    """Main entry point for testing."""
    import argparse

    parser = argparse.ArgumentParser(description='UART IPC Client for Pi 4')
    parser.add_argument('--port', default='/dev/ttyUSB0', help='Serial port')
    parser.add_argument('--baud', type=int, default=115200, help='Baud rate')
    parser.add_argument('--test-crc', action='store_true', help='Run CRC test')
    parser.add_argument('--query', type=str, help='Send cache lookup query')
    parser.add_argument('--bench', type=int, help='Run benchmark with N queries')
    parser.add_argument('--query-set', type=str, help='Query list for --bench (one per line)')
    parser.add_argument('--bench-log', type=str, help='CSV output path for --bench')
    parser.add_argument('--pace-ms', type=int, default=15,
                        help='Inter-query delay for --bench (ms)')
    parser.add_argument('--raw-log', type=str, help='Write readable UART log to file (UTF-8 text)')
    parser.add_argument('--raw-bin', type=str, help='Write raw UART RX bytes to binary file')
    args = parser.parse_args()

    if args.test_crc:
        test_crc()
        return

    print("UART IPC Client - Test Mode")

    client = UARTIPCClient(
        port=args.port,
        baudrate=args.baud,
        raw_log_path=args.raw_log,
        raw_bin_path=args.raw_bin
    )

    if not client.connect():
        print("Failed to connect")
        return

    try:
        if args.bench:
            if args.query_set:
                queries = _load_query_set(args.query_set)
            else:
                queries = DEFAULT_BENCH_QUERIES
            run_benchmark(client, args.bench, queries, args.bench_log,
                          pace_ms=args.pace_ms)
        elif args.query:
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
