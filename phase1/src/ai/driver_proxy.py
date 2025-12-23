"""
JARVIS AI-OS Phase 1 - Driver Proxy
Week 23: VirtIO Block Driver Implementation - Phase 3

This module provides a Python interface to the C driver framework.
Enables FileSystem Agent to interact with block drivers.

Features:
- ctypes-based C driver bindings
- Mock mode for development (no C drivers available)
- Block read/write operations
- Driver lifecycle management (init, start, stop)
- Error handling with fallback to os module

Author: JARVIS AI-OS Team
Date: December 3, 2025
"""

import ctypes
import os
import sys
from typing import Optional, Tuple, List
from enum import IntEnum


class DriverState(IntEnum):
    """Driver state (mirrors driver_registry.h)"""
    UNLOADED = 0
    LOADED = 1
    ACTIVE = 2
    SUSPENDED = 3
    FAILED = 4


class DriverInfo(ctypes.Structure):
    """Driver info structure (mirrors driver_registry.h)"""
    _pack_ = 1
    _fields_ = [
        ("name", ctypes.c_char * 32),
        ("state", ctypes.c_uint32),
        ("total_reads", ctypes.c_uint64),
        ("total_writes", ctypes.c_uint64),
        ("total_errors", ctypes.c_uint64),
        ("avg_latency_us", ctypes.c_uint64),
        ("created_at", ctypes.c_uint64),
        ("last_error", ctypes.c_uint64),
    ]


class DriverProxy:
    """
    Proxy to C driver framework.

    Provides Python interface to block drivers implemented in C.
    Falls back to mock implementation if C library unavailable.
    """

    def __init__(self, driver_lib_path: Optional[str] = None):
        """
        Initialize driver proxy.

        Args:
            driver_lib_path: Path to libdriver.so (optional, auto-detect if None)
        """
        self.mock_mode = True
        self.driver_lib = None
        self._mock_storage = {}  # Mock block storage (sector -> bytes)

        # Try to load C driver library
        if driver_lib_path is None:
            # Auto-detect based on platform
            if sys.platform == 'linux':
                driver_lib_path = '/mnt/c/Users/jluca/Documents/JARVIS_OS/phase1/src/drivers/libdriver.so'
            elif sys.platform == 'win32':
                driver_lib_path = 'C:\\Users\\jluca\\Documents\\JARVIS_OS\\phase1\\src\\drivers\\libdriver.dll'

        if driver_lib_path and os.path.exists(driver_lib_path):
            try:
                self.driver_lib = ctypes.CDLL(driver_lib_path)
                self._setup_function_signatures()

                # Initialize driver registry
                result = self.driver_lib.driver_registry_init()
                if result == 0:
                    self.mock_mode = False
                    print("[DriverProxy] Initialized with C driver library")
                else:
                    print(f"[DriverProxy] Failed to init driver registry: {result}, using mock mode")
            except Exception as e:
                print(f"[DriverProxy] Failed to load driver library: {e}, using mock mode")
        else:
            print(f"[DriverProxy] Driver library not found at {driver_lib_path}, using mock mode")

    def _setup_function_signatures(self):
        """Setup ctypes function signatures for C driver library."""
        if not self.driver_lib:
            return

        # driver_registry_init() -> int
        self.driver_lib.driver_registry_init.argtypes = []
        self.driver_lib.driver_registry_init.restype = ctypes.c_int

        # driver_register(name, ops, private_data) -> int
        self.driver_lib.driver_register.argtypes = [
            ctypes.c_char_p,
            ctypes.c_void_p,
            ctypes.c_void_p
        ]
        self.driver_lib.driver_register.restype = ctypes.c_int

        # driver_start(name) -> int
        self.driver_lib.driver_start.argtypes = [ctypes.c_char_p]
        self.driver_lib.driver_start.restype = ctypes.c_int

        # driver_stop(name) -> int
        self.driver_lib.driver_stop.argtypes = [ctypes.c_char_p]
        self.driver_lib.driver_stop.restype = ctypes.c_int

        # driver_read(name, lba, count, buffer) -> int
        self.driver_lib.driver_read.argtypes = [
            ctypes.c_char_p,
            ctypes.c_uint64,
            ctypes.c_uint32,
            ctypes.c_void_p
        ]
        self.driver_lib.driver_read.restype = ctypes.c_int

        # driver_write(name, lba, count, buffer) -> int
        self.driver_lib.driver_write.argtypes = [
            ctypes.c_char_p,
            ctypes.c_uint64,
            ctypes.c_uint32,
            ctypes.c_void_p
        ]
        self.driver_lib.driver_write.restype = ctypes.c_int

        # driver_get_info(name, info) -> int
        self.driver_lib.driver_get_info.argtypes = [
            ctypes.c_char_p,
            ctypes.POINTER(DriverInfo)
        ]
        self.driver_lib.driver_get_info.restype = ctypes.c_int

        # driver_list_all(names, max_count) -> int
        self.driver_lib.driver_list_all.argtypes = [
            ctypes.c_void_p,
            ctypes.c_uint32
        ]
        self.driver_lib.driver_list_all.restype = ctypes.c_int

        # driver_registry_cleanup() -> void
        self.driver_lib.driver_registry_cleanup.argtypes = []
        self.driver_lib.driver_registry_cleanup.restype = None

    def read_block(self, driver_name: str, lba: int, count: int) -> Tuple[bool, bytes]:
        """
        Read blocks from driver.

        Args:
            driver_name: Driver name (e.g., "virtio-blk")
            lba: Logical block address (sector number)
            count: Number of blocks to read

        Returns:
            Tuple of (success, data)
        """
        if self.mock_mode:
            # Mock implementation
            data = bytearray()
            for i in range(count):
                sector = lba + i
                if sector in self._mock_storage:
                    data.extend(self._mock_storage[sector])
                else:
                    data.extend(b'\x00' * 512)  # Empty sector
            return (True, bytes(data))

        # Real driver call
        buffer_size = count * 512
        buffer = ctypes.create_string_buffer(buffer_size)

        result = self.driver_lib.driver_read(
            driver_name.encode('utf-8'),
            ctypes.c_uint64(lba),
            ctypes.c_uint32(count),
            buffer
        )

        if result > 0:
            return (True, buffer.raw[:result])
        else:
            return (False, b'')

    def write_block(self, driver_name: str, lba: int, data: bytes) -> bool:
        """
        Write blocks to driver.

        Args:
            driver_name: Driver name (e.g., "virtio-blk")
            lba: Logical block address (sector number)
            data: Data to write (must be multiple of 512 bytes)

        Returns:
            True if successful, False otherwise
        """
        if len(data) % 512 != 0:
            print(f"[DriverProxy] Write data must be multiple of 512 bytes, got {len(data)}")
            return False

        count = len(data) // 512

        if self.mock_mode:
            # Mock implementation
            for i in range(count):
                sector = lba + i
                sector_data = data[i * 512:(i + 1) * 512]
                self._mock_storage[sector] = sector_data
            return True

        # Real driver call
        buffer = ctypes.create_string_buffer(data, len(data))

        result = self.driver_lib.driver_write(
            driver_name.encode('utf-8'),
            ctypes.c_uint64(lba),
            ctypes.c_uint32(count),
            buffer
        )

        return result == len(data)

    def get_driver_info(self, driver_name: str) -> Optional[dict]:
        """
        Get driver information.

        Args:
            driver_name: Driver name

        Returns:
            Dictionary with driver info, or None if not found
        """
        if self.mock_mode:
            # Mock implementation
            return {
                'name': driver_name,
                'state': 'ACTIVE' if driver_name == 'virtio-blk' else 'UNLOADED',
                'total_reads': 0,
                'total_writes': 0,
                'total_errors': 0,
                'avg_latency_us': 0,
            }

        # Real driver call
        info = DriverInfo()
        result = self.driver_lib.driver_get_info(
            driver_name.encode('utf-8'),
            ctypes.byref(info)
        )

        if result == 0:
            return {
                'name': info.name.decode('utf-8'),
                'state': DriverState(info.state).name,
                'total_reads': info.total_reads,
                'total_writes': info.total_writes,
                'total_errors': info.total_errors,
                'avg_latency_us': info.avg_latency_us,
            }
        else:
            return None

    def list_drivers(self) -> List[str]:
        """
        List all registered drivers.

        Returns:
            List of driver names
        """
        if self.mock_mode:
            # Mock implementation
            return ['virtio-blk']

        # Real driver call
        max_drivers = 32
        names_buffer = (ctypes.c_char * 32 * max_drivers)()

        count = self.driver_lib.driver_list_all(
            ctypes.cast(names_buffer, ctypes.c_void_p),
            ctypes.c_uint32(max_drivers)
        )

        if count > 0:
            return [names_buffer[i].decode('utf-8') for i in range(count)]
        else:
            return []

    def cleanup(self):
        """Cleanup driver registry."""
        if not self.mock_mode and self.driver_lib:
            self.driver_lib.driver_registry_cleanup()
            print("[DriverProxy] Driver registry cleaned up")


# Global driver proxy instance
_driver_proxy: Optional[DriverProxy] = None


def get_driver_proxy() -> DriverProxy:
    """
    Get global driver proxy instance (singleton).

    Returns:
        DriverProxy instance
    """
    global _driver_proxy
    if _driver_proxy is None:
        _driver_proxy = DriverProxy()
    return _driver_proxy


# Convenience functions for FileSystem Agent

def read_file_via_driver(driver_name: str, lba: int, size: int) -> Tuple[bool, bytes]:
    """
    Read file data via block driver.

    Args:
        driver_name: Driver name
        lba: Starting logical block address
        size: Number of bytes to read

    Returns:
        Tuple of (success, data)
    """
    proxy = get_driver_proxy()

    # Calculate block count (round up to 512-byte boundaries)
    block_count = (size + 511) // 512

    success, data = proxy.read_block(driver_name, lba, block_count)

    if success:
        # Trim to requested size
        return (True, data[:size])
    else:
        return (False, b'')


def write_file_via_driver(driver_name: str, lba: int, data: bytes) -> bool:
    """
    Write file data via block driver.

    Args:
        driver_name: Driver name
        lba: Starting logical block address
        data: Data to write

    Returns:
        True if successful
    """
    proxy = get_driver_proxy()

    # Pad data to 512-byte boundary
    if len(data) % 512 != 0:
        padding = 512 - (len(data) % 512)
        data = data + b'\x00' * padding

    return proxy.write_block(driver_name, lba, data)


if __name__ == '__main__':
    # Test driver proxy
    print("=== Driver Proxy Test ===\n")

    proxy = DriverProxy()

    print(f"Mock mode: {proxy.mock_mode}\n")

    # Test 1: List drivers
    print("Test 1: List drivers")
    drivers = proxy.list_drivers()
    print(f"  Drivers: {drivers}")
    assert len(drivers) > 0, "Should have at least one driver"
    print("  ✓ PASS\n")

    # Test 2: Get driver info
    print("Test 2: Get driver info")
    info = proxy.get_driver_info("virtio-blk")
    print(f"  Info: {info}")
    assert info is not None, "Should get driver info"
    print("  ✓ PASS\n")

    # Test 3: Write block
    print("Test 3: Write block")
    test_data = b"Hello, JARVIS!" + b'\x00' * (512 - 14)  # Pad to 512 bytes
    success = proxy.write_block("virtio-blk", 0, test_data)
    print(f"  Write result: {success}")
    assert success, "Write should succeed"
    print("  ✓ PASS\n")

    # Test 4: Read block
    print("Test 4: Read block")
    success, data = proxy.read_block("virtio-blk", 0, 1)
    print(f"  Read result: {success}, data: {data[:20]}...")
    assert success, "Read should succeed"
    assert data[:14] == b"Hello, JARVIS!", "Data should match"
    print("  ✓ PASS\n")

    # Test 5: Convenience functions
    print("Test 5: Convenience functions")
    write_success = write_file_via_driver("virtio-blk", 1, b"Test file content")
    read_success, read_data = read_file_via_driver("virtio-blk", 1, 17)
    print(f"  Write: {write_success}, Read: {read_success}, Data: {read_data}")
    assert write_success and read_success, "Should succeed"
    assert read_data == b"Test file content", "Data should match"
    print("  ✓ PASS\n")

    print("=== All Tests PASSED ===")

    proxy.cleanup()
