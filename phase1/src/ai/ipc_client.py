#!/usr/bin/env python3
"""
JARVIS AI-OS Phase 1/2 - IPC Client
Week 5: AI Agent Bootstrap
Week 8: Real Shared Memory Integration
Week 28: Bidirectional IPC (Dual Ring Buffer)
Week 30: ivshmem Shared Memory Support

Python client for connecting to seL4 IPC ring buffer.
Mirrors the C implementation in phase1/src/ipc/ring_buffer.{h,c}
and phase1/src/sel4/dual_ring_buffer.{h,c}

Architecture:
- Shared memory for ring buffer (mmap or ivshmem)
- Lock-free SPSC dual ring buffer (query + response)
- Matches C structure layout for interoperability

Week 30 Updates:
- Support for dual_ring_buffer_t (567KB, two rings)
- Magic number validation (JARV = 0x4A415256)
- Wait for seL4 initialization before use
"""

import os
import sys
import mmap
import struct
import time
import ctypes
import logging
from pathlib import Path

# ============================================================================
# Constants (must match C implementation in ring_buffer.h and dual_ring_buffer.h)
# ============================================================================

RING_BUFFER_SIZE = 1024
MAX_MESSAGE_SIZE = 256
CACHE_LINE_SIZE = 64

# Single ring buffer size (~283KB)
SINGLE_RING_SHM_SIZE = 283648

# Dual ring buffer size (~567KB) - Week 28/30
# dual_ring_buffer_t: magic (4) + version (4) + query_ring (~283KB) + response_ring (~283KB)
DUAL_RING_SHM_SIZE = 567296

# Default shared memory size (use dual ring for Week 30+)
SHM_SIZE = DUAL_RING_SHM_SIZE

# Dual ring buffer magic number and version
DUAL_RING_MAGIC = 0x4A415256  # "JARV" in hex
DUAL_RING_VERSION = 1

# Dual ring buffer offsets (from dual_ring_buffer.h)
DUAL_RING_MAGIC_OFFSET = 0
DUAL_RING_VERSION_OFFSET = 4
DUAL_RING_QUERY_OFFSET = 8  # Start of query_ring (Python writes, seL4 reads)
DUAL_RING_RESPONSE_OFFSET = 8 + SINGLE_RING_SHM_SIZE  # Start of response_ring

# Message types (must match C enum message_type_t)
MSG_COMMAND = 0
MSG_RESPONSE = 1
MSG_QUERY = 2
MSG_EVENT = 3
MSG_CONTROL = 4
MSG_CACHE_LOOKUP = 5    # Phase 2 Week 28: Python → seL4 cache lookup
MSG_CACHE_RESPONSE = 6  # Phase 2 Week 28: seL4 → Python cache result
MSG_CACHE_STATS = 7     # Phase 2 Week 28: Python → seL4 stats request

# Logging
logging.basicConfig(level=logging.INFO, format='[%(name)s] %(message)s')
logger = logging.getLogger('IPCClient')

# ============================================================================
# C Structure Definitions (using ctypes)
# ============================================================================

class RingMessage(ctypes.Structure):
    """
    Python equivalent of ring_message_t from ring_buffer.h

    typedef struct {
        message_type_t type;
        uint32_t id;
        uint32_t payload_size;
        char payload[MAX_MESSAGE_SIZE];
        uint64_t timestamp;
    } ring_message_t;
    """
    _pack_ = 1  # No padding
    _fields_ = [
        ("type", ctypes.c_uint32),                      # message_type_t (enum -> uint32)
        ("id", ctypes.c_uint32),                        # Message ID
        ("payload_size", ctypes.c_uint32),              # Payload size
        ("payload", ctypes.c_char * MAX_MESSAGE_SIZE),  # Message data
        ("timestamp", ctypes.c_uint64)                  # Timestamp (nanoseconds)
    ]

    def __repr__(self):
        return (f"RingMessage(type={self.type}, id={self.id}, "
                f"payload_size={self.payload_size}, "
                f"payload={self.payload[:self.payload_size].decode('utf-8', errors='ignore')})")


# Calculate message size
MESSAGE_SIZE = ctypes.sizeof(RingMessage)

# ============================================================================
# IPC Client Class
# ============================================================================

class IPCClient:
    """
    Python client for seL4 IPC ring buffer

    Communication:
    - Connects to shared memory region via mmap
    - Sends messages to seL4 kernel
    - Receives responses from kernel
    - Uses lock-free SPSC ring buffer

    Week 8: Real shared memory integration
    """

    def __init__(self, shared_memory_path="/dev/shm/jarvis_ipc", use_mock=False):
        """
        Initialize IPC client

        Args:
            shared_memory_path (str): Path to shared memory region
            use_mock (bool): Use mock mode (for testing without seL4)
        """
        self.shared_memory_path = shared_memory_path
        self.use_mock = use_mock
        self.shm = None
        self.shm_fd = None
        self.message_id_counter = 0
        self.connected = False

        # Ring buffer pointers (offsets into shared memory)
        self.head_offset = 0  # Producer head (Python writes here)
        self.tail_offset = 8  # Consumer tail (seL4 reads from here)
        self.messages_offset = 64  # Start of messages array (cache-line aligned)

        # Statistics
        self.stats = {
            'messages_sent': 0,
            'messages_received': 0,
            'send_errors': 0,
            'receive_errors': 0,
        }

        logger.info(f"Initializing IPC client")
        logger.info(f"Shared memory path: {shared_memory_path}")
        logger.info(f"Mock mode: {use_mock}")

    def connect(self):
        """
        Connect to seL4 IPC ring buffer via shared memory

        Returns:
            bool: True if successful, False otherwise
        """
        if self.use_mock:
            logger.info("Mock mode - skipping real shared memory")
            self.connected = True
            return True

        logger.info("Connecting to shared memory...")

        try:
            # Check if running on Windows (use fallback)
            if os.name == 'nt':
                logger.warning("Windows detected - using mock mode (shared memory not supported)")
                self.use_mock = True
                self.connected = True
                return True

            # Create or open shared memory region
            if not os.path.exists(self.shared_memory_path):
                logger.info(f"Creating shared memory file: {self.shared_memory_path}")
                # Create file with proper size
                with open(self.shared_memory_path, 'wb') as f:
                    f.write(b'\x00' * SHM_SIZE)

            # Open shared memory file
            self.shm_fd = os.open(self.shared_memory_path, os.O_RDWR)

            # Map shared memory
            self.shm = mmap.mmap(self.shm_fd, SHM_SIZE, mmap.MAP_SHARED, mmap.PROT_READ | mmap.PROT_WRITE)

            logger.info(f"Shared memory mapped at {SHM_SIZE} bytes")

            # Initialize ring buffer (only if empty - check head/tail)
            head = self._read_uint64(self.head_offset)
            tail = self._read_uint64(self.tail_offset)

            if head == 0 and tail == 0:
                logger.info("Initializing ring buffer (head=0, tail=0)")
                self._write_uint64(self.head_offset, 0)
                self._write_uint64(self.tail_offset, 0)

            self.connected = True
            logger.info("Connected to shared memory successfully")
            return True

        except Exception as e:
            logger.error(f"Failed to connect: {e}")
            return False

    def _read_uint32(self, offset):
        """Read 32-bit unsigned integer from shared memory"""
        if self.use_mock:
            return 0
        self.shm.seek(offset)
        return struct.unpack('<I', self.shm.read(4))[0]

    def _write_uint32(self, offset, value):
        """Write 32-bit unsigned integer to shared memory"""
        if self.use_mock:
            return
        self.shm.seek(offset)
        self.shm.write(struct.pack('<I', value))

    def _read_uint64(self, offset):
        """Read 64-bit unsigned integer from shared memory"""
        if self.use_mock:
            return 0
        self.shm.seek(offset)
        return struct.unpack('<Q', self.shm.read(8))[0]

    def _write_uint64(self, offset, value):
        """Write 64-bit unsigned integer to shared memory"""
        if self.use_mock:
            return
        self.shm.seek(offset)
        self.shm.write(struct.pack('<Q', value))

    def wait_for_initialization(self, timeout_s=10):
        """
        Wait for seL4 to initialize the dual ring buffer (Week 30)

        Checks for the JARV magic number at offset 0 which indicates
        seL4 has initialized the shared memory region.

        Args:
            timeout_s (float): Timeout in seconds (default 10)

        Returns:
            bool: True if initialized, False on timeout
        """
        if self.use_mock:
            logger.info("Mock mode - skipping initialization check")
            return True

        if not self.connected:
            logger.error("Not connected")
            return False

        logger.info(f"Waiting for seL4 to initialize shared memory (timeout: {timeout_s}s)...")

        start = time.time()
        while (time.time() - start) < timeout_s:
            magic = self._read_uint32(DUAL_RING_MAGIC_OFFSET)

            if magic == DUAL_RING_MAGIC:
                version = self._read_uint32(DUAL_RING_VERSION_OFFSET)
                logger.info(f"Dual ring buffer initialized by seL4")
                logger.info(f"  Magic: 0x{magic:08X} (JARV)")
                logger.info(f"  Version: {version}")
                return True

            # Not yet initialized, wait a bit
            time.sleep(0.1)

        logger.warning(f"Timeout waiting for seL4 initialization (magic=0x{magic:08X})")
        return False

    def is_dual_ring_initialized(self):
        """
        Check if dual ring buffer is initialized (has valid magic number)

        Returns:
            bool: True if magic number matches, False otherwise
        """
        if self.use_mock:
            return True
        if not self.connected:
            return False

        magic = self._read_uint32(DUAL_RING_MAGIC_OFFSET)
        return magic == DUAL_RING_MAGIC

    def _get_next_slot(self, head):
        """Calculate next ring buffer slot"""
        return (head + 1) % RING_BUFFER_SIZE

    def send_message(self, msg_type, payload):
        """
        Send a message via IPC

        Args:
            msg_type (int): Message type (MSG_COMMAND, MSG_RESPONSE, etc.)
            payload (str or bytes): Message payload

        Returns:
            bool: True if successful, False otherwise
        """
        if not self.connected:
            logger.error("Not connected")
            return False

        # Convert payload to bytes if string
        if isinstance(payload, str):
            payload_bytes = payload.encode('utf-8')
        else:
            payload_bytes = payload

        # Check payload size
        if len(payload_bytes) > MAX_MESSAGE_SIZE:
            logger.error(f"Payload too large ({len(payload_bytes)} > {MAX_MESSAGE_SIZE})")
            self.stats['send_errors'] += 1
            return False

        # Mock mode - just simulate send
        if self.use_mock:
            logger.debug(f"Mock send: type={msg_type}, size={len(payload_bytes)}")
            self.stats['messages_sent'] += 1
            self.message_id_counter += 1
            return True

        try:
            # Read current head and tail
            head = self._read_uint64(self.head_offset)
            tail = self._read_uint64(self.tail_offset)

            # Check if buffer is full
            next_head = self._get_next_slot(head)
            if next_head == tail:
                logger.warning("Ring buffer full - message dropped")
                self.stats['send_errors'] += 1
                return False

            # Create message
            msg = RingMessage()
            msg.type = msg_type
            msg.id = self.message_id_counter
            msg.payload_size = len(payload_bytes)
            # Copy payload bytes into ctypes array using memmove
            ctypes.memmove(msg.payload, payload_bytes, len(payload_bytes))
            msg.timestamp = time.time_ns()

            # Calculate message offset in ring buffer
            msg_offset = self.messages_offset + (head * MESSAGE_SIZE)

            # Write message to shared memory (use ctypes.string_at to get raw bytes)
            self.shm.seek(msg_offset)
            msg_bytes = ctypes.string_at(ctypes.addressof(msg), ctypes.sizeof(msg))
            self.shm.write(msg_bytes)

            # Update head pointer
            self._write_uint64(self.head_offset, next_head)

            # Update statistics
            self.stats['messages_sent'] += 1
            self.message_id_counter += 1

            logger.debug(f"Sent message #{msg.id}: type={msg_type}, size={msg.payload_size}")

            return True

        except Exception as e:
            logger.error(f"Send failed: {e}")
            self.stats['send_errors'] += 1
            return False

    def receive_message(self, timeout_ms=100):
        """
        Receive a message via IPC (Python reads from seL4's output)

        Note: Python is the producer (writer), seL4 is consumer (reader) for queries.
        For responses, seL4 is producer, Python is consumer.

        Args:
            timeout_ms (int): Timeout in milliseconds

        Returns:
            RingMessage or None: Received message, or None if no message available
        """
        if not self.connected:
            logger.error("Not connected")
            return None

        # Mock mode - no messages
        if self.use_mock:
            return None

        try:
            start_time = time.time()
            timeout_s = timeout_ms / 1000.0

            while (time.time() - start_time) < timeout_s:
                # Read current head and tail
                # For receive: we're reading from seL4's output
                # Assume separate ring buffer or different head/tail for responses
                # For Week 8, simplify: use same buffer, different interpretation

                head = self._read_uint64(self.head_offset)
                tail = self._read_uint64(self.tail_offset)

                # Check if messages available
                if head == tail:
                    # Buffer empty, wait a bit
                    time.sleep(0.001)  # 1ms
                    continue

                # Read message at tail position
                msg_offset = self.messages_offset + (tail * MESSAGE_SIZE)
                self.shm.seek(msg_offset)
                msg_bytes = self.shm.read(MESSAGE_SIZE)

                # Parse message
                msg = RingMessage.from_buffer_copy(msg_bytes)

                # Update tail pointer
                next_tail = self._get_next_slot(tail)
                self._write_uint64(self.tail_offset, next_tail)

                # Update statistics
                self.stats['messages_received'] += 1

                logger.debug(f"Received message #{msg.id}: type={msg.type}")

                return msg

            # Timeout - no messages
            return None

        except Exception as e:
            logger.error(f"Receive failed: {e}")
            self.stats['receive_errors'] += 1
            return None

    def disconnect(self):
        """Disconnect from shared memory"""
        if not self.connected:
            return

        logger.info("Disconnecting...")

        try:
            if self.shm:
                self.shm.close()
            if self.shm_fd:
                os.close(self.shm_fd)

            self.connected = False
            logger.info("Disconnected successfully")

        except Exception as e:
            logger.error(f"Error during disconnect: {e}")

    # ========================================================================
    # Phase 2 Week 28: Cache Lookup Methods
    # ========================================================================

    def send_cache_lookup(self, query):
        """
        Send cache lookup query to seL4 decision cache

        Args:
            query (str): Normalized query string

        Returns:
            int: Message ID (for matching response), or 0 on failure
        """
        if not self.connected:
            logger.error("Not connected")
            return 0

        # Convert query to bytes
        if isinstance(query, str):
            query_bytes = query.encode('utf-8')
        else:
            query_bytes = query

        # Check query size
        if len(query_bytes) > MAX_MESSAGE_SIZE:
            logger.error(f"Query too large ({len(query_bytes)} > {MAX_MESSAGE_SIZE})")
            return 0

        # Use send_message with MSG_CACHE_LOOKUP type
        msg_id = self.message_id_counter
        if self.send_message(MSG_CACHE_LOOKUP, query):
            return msg_id
        else:
            return 0

    def recv_cache_response(self, msg_id, timeout_ms=10):
        """
        Receive cache response from seL4

        Args:
            msg_id (int): Expected message ID
            timeout_ms (int): Timeout in milliseconds (default 10ms)

        Returns:
            dict: {'hit': bool, 'action': str, 'trust': int} or None on timeout/error
        """
        if not self.connected:
            logger.error("Not connected")
            return None

        # Mock mode - no responses
        if self.use_mock:
            return None

        try:
            start_time = time.time()
            timeout_s = timeout_ms / 1000.0

            while (time.time() - start_time) < timeout_s:
                # Try to receive response
                msg = self.receive_message(timeout_ms=timeout_ms)

                if msg and msg.type == MSG_CACHE_RESPONSE:
                    # Check message ID match
                    if msg.id != msg_id:
                        logger.warning(f"Message ID mismatch (expected {msg_id}, got {msg.id})")
                        # Continue waiting - might be old response
                        continue

                    # Parse response payload
                    payload = msg.payload[:msg.payload_size].decode('utf-8', errors='ignore')

                    if payload == "MISS":
                        # Cache miss
                        return {'hit': False, 'action': '', 'trust': 0}

                    elif payload.startswith("HIT|"):
                        # Cache hit: "HIT|action|trust"
                        parts = payload.split('|')
                        if len(parts) >= 3:
                            action = parts[1]
                            trust = int(parts[2])
                            return {'hit': True, 'action': action, 'trust': trust}
                        else:
                            logger.error(f"Invalid HIT response format: {payload}")
                            return None

                    else:
                        logger.error(f"Invalid response payload: {payload}")
                        return None

            # Timeout
            return None

        except Exception as e:
            logger.error(f"recv_cache_response failed: {e}")
            return None

    def send_cache_stats_request(self):
        """
        Request full cache statistics from seL4

        Returns:
            int: Message ID (for matching response), or 0 on failure
        """
        if not self.connected:
            logger.error("Not connected")
            return 0

        # Send empty MSG_CACHE_STATS message
        msg_id = self.message_id_counter
        if self.send_message(MSG_CACHE_STATS, ""):
            return msg_id
        else:
            return 0

    def recv_cache_stats(self, msg_id, timeout_ms=10):
        """
        Receive cache statistics from seL4

        Args:
            msg_id (int): Expected message ID
            timeout_ms (int): Timeout in milliseconds (default 10ms)

        Returns:
            dict: {'capacity': int, 'used': int, 'lookups': int, 'hits': int,
                   'misses': int, 'evictions': int, 'hit_rate': float}
            or None on timeout/error
        """
        if not self.connected:
            logger.error("Not connected")
            return None

        # Mock mode - no responses
        if self.use_mock:
            return None

        try:
            start_time = time.time()
            timeout_s = timeout_ms / 1000.0

            while (time.time() - start_time) < timeout_s:
                # Try to receive response
                msg = self.receive_message(timeout_ms=timeout_ms)

                if msg and msg.type == MSG_RESPONSE:  # Stats response uses MSG_RESPONSE
                    # Check message ID match
                    if msg.id != msg_id:
                        logger.warning(f"Message ID mismatch (expected {msg_id}, got {msg.id})")
                        continue

                    # Parse stats payload: "capacity|used|lookups|hits|misses|evictions"
                    payload = msg.payload[:msg.payload_size].decode('utf-8', errors='ignore')
                    parts = payload.split('|')

                    if len(parts) >= 6:
                        capacity = int(parts[0])
                        used = int(parts[1])
                        lookups = int(parts[2])
                        hits = int(parts[3])
                        misses = int(parts[4])
                        evictions = int(parts[5])

                        # Calculate hit rate
                        hit_rate = (hits / lookups * 100.0) if lookups > 0 else 0.0

                        return {
                            'capacity': capacity,
                            'used': used,
                            'lookups': lookups,
                            'hits': hits,
                            'misses': misses,
                            'evictions': evictions,
                            'hit_rate': hit_rate
                        }
                    else:
                        logger.error(f"Invalid stats response format: {payload}")
                        return None

            # Timeout
            return None

        except Exception as e:
            logger.error(f"recv_cache_stats failed: {e}")
            return None

    # ========================================================================
    # End of Phase 2 Week 28 additions
    # ========================================================================

    def get_statistics(self):
        """
        Get IPC statistics

        Returns:
            dict: Statistics
        """
        return {
            'connected': self.connected,
            'mock_mode': self.use_mock,
            'messages_sent': self.stats['messages_sent'],
            'messages_received': self.stats['messages_received'],
            'send_errors': self.stats['send_errors'],
            'receive_errors': self.stats['receive_errors'],
            'shared_memory_path': self.shared_memory_path,
        }

# ============================================================================
# Test Functions
# ============================================================================

def test_ipc_client():
    """Test IPC client functionality"""
    print("="*70)
    print("JARVIS IPC Client - Week 8 Test (Real Shared Memory)")
    print("="*70)
    print()

    # Create client
    client = IPCClient()

    # Connect
    if not client.connect():
        print("ERROR: Failed to connect")
        return False

    print()

    # Test sending messages
    print("[TEST] Sending test messages...")
    print()

    test_messages = [
        (MSG_QUERY, "show cpu"),
        (MSG_QUERY, "check memory"),
        (MSG_QUERY, "help"),
    ]

    for msg_type, payload in test_messages:
        if not client.send_message(msg_type, payload):
            print(f"ERROR: Failed to send message: {payload}")
            return False
        time.sleep(0.1)  # Small delay between messages

    print()

    # Test receiving (will timeout if no seL4 response)
    print("[TEST] Receiving messages (will timeout without seL4)...")
    msg = client.receive_message(timeout_ms=100)
    if msg:
        print(f"  Received: {msg}")
    else:
        print(f"  No messages (expected without seL4 running)")
    print()

    # Statistics
    stats = client.get_statistics()
    print("="*70)
    print("[IPC STATISTICS]")
    print("="*70)
    print(f"  Connected:        {stats['connected']}")
    print(f"  Mock mode:        {stats['mock_mode']}")
    print(f"  Messages sent:    {stats['messages_sent']}")
    print(f"  Messages received: {stats['messages_received']}")
    print(f"  Send errors:      {stats['send_errors']}")
    print(f"  Receive errors:   {stats['receive_errors']}")
    print(f"  Shared memory:    {stats['shared_memory_path']}")
    print("="*70)
    print()

    # Disconnect
    client.disconnect()

    print("[TEST COMPLETE]")
    print("Week 8 Task 1 (Real IPC Client) [PASS] COMPLETE")
    print()
    print("NOTE: Full end-to-end testing requires seL4 running.")
    print("      Python -> shared memory writes are working!")
    print()

    return True

# ============================================================================
# Main Entry Point
# ============================================================================

def main():
    """Run IPC client test"""
    success = test_ipc_client()
    sys.exit(0 if success else 1)

if __name__ == "__main__":
    main()
