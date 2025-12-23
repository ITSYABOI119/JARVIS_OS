#!/usr/bin/env python3
"""
QEMU Manager - Manage QEMU lifecycle for integration tests

Handles:
- Starting QEMU with seL4 + JARVIS
- Monitoring boot process
- Interacting with QEMU console
- Graceful shutdown

Usage:
    manager = QEMUManager()
    boot_time = manager.start()
    print(f"Boot time: {boot_time:.2f}s")
    manager.stop()
"""

import subprocess
import time
import os
import signal
import threading
import queue
from typing import Optional, List, Tuple
from pathlib import Path
from dataclasses import dataclass


@dataclass
class QEMUConfig:
    """QEMU configuration"""
    qemu_binary: str = 'qemu-system-x86_64'
    memory_mb: int = 2048
    cpus: int = 2
    headless: bool = True
    timeout_seconds: int = 120


class QEMUManager:
    """Manage QEMU instance for integration tests"""

    def __init__(self, config: Optional[QEMUConfig] = None):
        """
        Initialize QEMU manager

        Args:
            config: QEMU configuration (uses defaults if None)
        """
        self.config = config or QEMUConfig()
        self.process: Optional[subprocess.Popen] = None
        self.output_queue = queue.Queue()
        self.output_thread: Optional[threading.Thread] = None
        self.boot_time: Optional[float] = None

        # Find seL4 tutorials directory
        self.sel4_dir = self._find_sel4_tutorials()

    def _find_sel4_tutorials(self) -> Optional[Path]:
        """
        Find seL4 tutorials directory

        Returns:
            Path to sel4-tutorials, or None if not found
        """
        # Check common locations
        possible_paths = [
            Path.home() / 'sel4-tutorials',
            Path('/home') / os.getenv('USER', 'user') / 'sel4-tutorials',
            Path('/mnt/c/Users') / os.getenv('USER', 'user') / 'sel4-tutorials',
        ]

        for path in possible_paths:
            if path.exists() and path.is_dir():
                return path

        return None

    def start(self, image_path: Optional[str] = None) -> float:
        """
        Start QEMU with JARVIS

        Args:
            image_path: Path to JARVIS image (auto-detect if None)

        Returns:
            Boot time in seconds

        Raises:
            RuntimeError: If QEMU fails to start or boot times out
        """
        if self.process is not None:
            raise RuntimeError("QEMU is already running")

        # Auto-detect image path
        if image_path is None:
            image_path = self._find_jarvis_image()
            if image_path is None:
                raise RuntimeError("Could not find JARVIS image. Please specify image_path.")

        # Build QEMU command
        cmd = self._build_qemu_command(image_path)

        # Start QEMU
        print(f"Starting QEMU...")
        print(f"Command: {' '.join(cmd)}")

        try:
            self.process = subprocess.Popen(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                stdin=subprocess.PIPE,
                text=True,
                bufsize=1
            )
        except FileNotFoundError:
            raise RuntimeError(f"QEMU binary not found: {self.config.qemu_binary}")

        # Start output monitoring thread
        self.output_thread = threading.Thread(
            target=self._monitor_output,
            daemon=True
        )
        self.output_thread.start()

        # Wait for boot
        boot_time = self._wait_for_boot()
        self.boot_time = boot_time

        return boot_time

    def _build_qemu_command(self, image_path: str) -> List[str]:
        """Build QEMU command line"""
        cmd = [
            self.config.qemu_binary,
            '-m', str(self.config.memory_mb),
            '-smp', str(self.config.cpus),
            '-kernel', image_path,
        ]

        if self.config.headless:
            cmd.extend(['-nographic'])

        # Add serial console
        cmd.extend(['-serial', 'mon:stdio'])

        return cmd

    def _find_jarvis_image(self) -> Optional[str]:
        """
        Find JARVIS kernel image

        Returns:
            Path to JARVIS image, or None if not found
        """
        if self.sel4_dir is None:
            return None

        # Check build directory
        possible_paths = [
            self.sel4_dir / 'hello-world' / 'build' / 'kernel' / 'kernel.elf',
            self.sel4_dir / 'hello-world' / 'build' / 'images' / 'kernel',
        ]

        for path in possible_paths:
            if path.exists():
                return str(path)

        return None

    def _monitor_output(self):
        """Monitor QEMU output (runs in background thread)"""
        if self.process is None:
            return

        # Monitor stdout
        while True:
            line = self.process.stdout.readline()
            if not line:
                break

            line = line.strip()
            if line:
                self.output_queue.put(('stdout', line))

        # Monitor stderr
        for line in self.process.stderr:
            line = line.strip()
            if line:
                self.output_queue.put(('stderr', line))

    def _wait_for_boot(self) -> float:
        """
        Wait for QEMU to boot

        Returns:
            Boot time in seconds

        Raises:
            RuntimeError: If boot times out or fails
        """
        start_time = time.time()
        boot_detected = False

        # Boot success indicators
        success_patterns = [
            'Decision cache initialized',
            'IPC ring buffer initialized',
            'JARVIS ready',
            'seL4 Bootstrapping',  # Fallback: seL4 boot message
        ]

        # Boot failure indicators
        failure_patterns = [
            'Kernel panic',
            'Fatal error',
            'Assertion failed',
        ]

        print("Waiting for boot...")

        while True:
            # Check timeout
            elapsed = time.time() - start_time
            if elapsed > self.config.timeout_seconds:
                raise RuntimeError(f"Boot timeout after {elapsed:.1f}s")

            # Check if process died
            if self.process.poll() is not None:
                raise RuntimeError(f"QEMU process died (exit code: {self.process.returncode})")

            # Check output
            try:
                stream, line = self.output_queue.get(timeout=0.1)

                # Print output
                print(f"[{stream}] {line}")

                # Check for success
                for pattern in success_patterns:
                    if pattern.lower() in line.lower():
                        boot_time = time.time() - start_time
                        print(f"\nBoot successful! ({boot_time:.2f}s)")
                        return boot_time

                # Check for failure
                for pattern in failure_patterns:
                    if pattern.lower() in line.lower():
                        raise RuntimeError(f"Boot failed: {line}")

            except queue.Empty:
                continue

    def send_command(self, command: str) -> str:
        """
        Send command to QEMU console

        Args:
            command: Command to send

        Returns:
            Command output

        Raises:
            RuntimeError: If QEMU is not running
        """
        if self.process is None:
            raise RuntimeError("QEMU is not running")

        # Send command
        self.process.stdin.write(command + '\n')
        self.process.stdin.flush()

        # Wait for output (with timeout)
        output_lines = []
        timeout = 5.0
        start = time.time()

        while time.time() - start < timeout:
            try:
                stream, line = self.output_queue.get(timeout=0.1)
                output_lines.append(line)

                # Check for command completion (heuristic)
                if '>' in line or '$' in line:  # Shell prompt
                    break

            except queue.Empty:
                continue

        return '\n'.join(output_lines)

    def stop(self, timeout: float = 10.0):
        """
        Stop QEMU gracefully

        Args:
            timeout: Timeout for graceful shutdown (seconds)
        """
        if self.process is None:
            return

        print("Stopping QEMU...")

        # Try graceful shutdown first
        try:
            self.process.terminate()
            self.process.wait(timeout=timeout)
            print("QEMU stopped gracefully")
        except subprocess.TimeoutExpired:
            print("Graceful shutdown timed out, killing QEMU...")
            self.process.kill()
            self.process.wait()
            print("QEMU killed")

        self.process = None

    def is_running(self) -> bool:
        """Check if QEMU is running"""
        if self.process is None:
            return False

        return self.process.poll() is None

    def get_exit_code(self) -> Optional[int]:
        """Get QEMU exit code (if stopped)"""
        if self.process is None:
            return None

        return self.process.poll()

    def __enter__(self):
        """Context manager entry"""
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        """Context manager exit (ensures cleanup)"""
        self.stop()


# Example usage
if __name__ == "__main__":
    print("QEMU Manager Example")
    print("=" * 70)

    # Create manager
    config = QEMUConfig(
        memory_mb=2048,
        cpus=2,
        headless=True,
        timeout_seconds=120
    )

    manager = QEMUManager(config)

    try:
        # Start QEMU
        boot_time = manager.start()
        print(f"\nQEMU booted in {boot_time:.2f}s")

        # Wait a bit
        print("\nRunning for 10 seconds...")
        time.sleep(10)

        # Note: Sending commands requires stdin support in seL4
        # For now, we just boot and monitor

    except Exception as e:
        print(f"\nError: {e}")

    finally:
        # Stop QEMU
        manager.stop()
        print("\nExample complete")
