#!/usr/bin/env python3
"""
System Monitor - Monitor CPU, memory, and detect issues

Monitors system resources during integration tests:
- Memory usage (detect leaks)
- CPU usage (detect runaway processes)
- Process health (detect crashes)
- Deadlock detection (timeout on operations)

Usage:
    monitor = SystemMonitor()
    mem_mb = monitor.get_memory_usage()
    cpu_pct = monitor.get_cpu_usage()

    # Check for issues
    if monitor.detect_memory_leak(baseline_mb, threshold_mb=100):
        print("Memory leak detected!")
"""

import psutil
import os
import time
import threading
from typing import Optional, Dict, List, Tuple
from dataclasses import dataclass
from datetime import datetime


@dataclass
class SystemSnapshot:
    """System resource snapshot"""
    timestamp: datetime
    memory_mb: float
    cpu_percent: float
    disk_usage_percent: float
    process_count: int
    thread_count: int


class SystemMonitor:
    """Monitor system resources during tests"""

    def __init__(self, process_pid: Optional[int] = None):
        """
        Initialize system monitor

        Args:
            process_pid: PID to monitor (default: current process)
        """
        if process_pid is None:
            process_pid = os.getpid()

        try:
            self.process = psutil.Process(process_pid)
        except psutil.NoSuchProcess:
            raise ValueError(f"Process {process_pid} not found")

        self.pid = process_pid
        self.snapshots: List[SystemSnapshot] = []
        self.baseline_memory_mb: Optional[float] = None

        # Deadlock detection
        self._watchdog_thread: Optional[threading.Thread] = None
        self._watchdog_stop = threading.Event()
        self._last_activity = time.time()
        self._activity_lock = threading.Lock()

    def get_memory_usage(self) -> float:
        """
        Get current memory usage in MB

        Returns:
            Memory usage in megabytes
        """
        try:
            memory_info = self.process.memory_info()
            return memory_info.rss / 1024 / 1024  # Convert bytes to MB
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            return 0.0

    def get_cpu_usage(self, interval: float = 0.1) -> float:
        """
        Get current CPU usage percentage

        Args:
            interval: Measurement interval in seconds

        Returns:
            CPU usage percentage (0-100)
        """
        try:
            return self.process.cpu_percent(interval=interval)
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            return 0.0

    def get_disk_usage(self, path: str = '/') -> float:
        """
        Get disk usage percentage

        Args:
            path: Path to check (default: root)

        Returns:
            Disk usage percentage (0-100)
        """
        try:
            # On Windows, use C: drive
            if os.name == 'nt':
                path = 'C:\\'
            disk = psutil.disk_usage(path)
            return disk.percent
        except Exception:
            return 0.0

    def get_process_count(self) -> int:
        """Get number of child processes"""
        try:
            return len(self.process.children(recursive=True))
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            return 0

    def get_thread_count(self) -> int:
        """Get number of threads"""
        try:
            return self.process.num_threads()
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            return 0

    def take_snapshot(self) -> SystemSnapshot:
        """
        Take system resource snapshot

        Returns:
            SystemSnapshot with current metrics
        """
        snapshot = SystemSnapshot(
            timestamp=datetime.now(),
            memory_mb=self.get_memory_usage(),
            cpu_percent=self.get_cpu_usage(),
            disk_usage_percent=self.get_disk_usage(),
            process_count=self.get_process_count(),
            thread_count=self.get_thread_count()
        )

        self.snapshots.append(snapshot)
        return snapshot

    def set_baseline(self):
        """Set baseline memory usage for leak detection"""
        self.baseline_memory_mb = self.get_memory_usage()

    def detect_memory_leak(self, threshold_mb: float = 100) -> Tuple[bool, float]:
        """
        Detect memory leak compared to baseline

        Args:
            threshold_mb: Memory growth threshold in MB

        Returns:
            Tuple of (leak_detected, growth_mb)
        """
        if self.baseline_memory_mb is None:
            self.set_baseline()
            return False, 0.0

        current_mb = self.get_memory_usage()
        growth_mb = current_mb - self.baseline_memory_mb

        return growth_mb > threshold_mb, growth_mb

    def get_memory_growth_rate(self) -> Optional[float]:
        """
        Calculate memory growth rate (MB/hour)

        Returns:
            Growth rate in MB/hour, or None if insufficient data
        """
        if len(self.snapshots) < 2:
            return None

        first = self.snapshots[0]
        last = self.snapshots[-1]

        duration_hours = (last.timestamp - first.timestamp).total_seconds() / 3600
        if duration_hours == 0:
            return None

        memory_growth_mb = last.memory_mb - first.memory_mb
        return memory_growth_mb / duration_hours

    def get_average_cpu(self) -> Optional[float]:
        """
        Get average CPU usage from snapshots

        Returns:
            Average CPU percentage, or None if no snapshots
        """
        if not self.snapshots:
            return None

        total_cpu = sum(s.cpu_percent for s in self.snapshots)
        return total_cpu / len(self.snapshots)

    def detect_high_cpu(self, threshold_percent: float = 80.0) -> bool:
        """
        Detect sustained high CPU usage

        Args:
            threshold_percent: CPU threshold percentage

        Returns:
            True if CPU usage exceeds threshold
        """
        avg_cpu = self.get_average_cpu()
        if avg_cpu is None:
            return False

        return avg_cpu > threshold_percent

    def is_process_alive(self) -> bool:
        """Check if monitored process is alive"""
        try:
            return self.process.is_running()
        except psutil.NoSuchProcess:
            return False

    def detect_crash(self) -> Tuple[bool, Optional[int]]:
        """
        Detect if process has crashed

        Returns:
            Tuple of (crashed, exit_code)
        """
        if not self.is_process_alive():
            try:
                # Try to get exit code (may not be available)
                status = self.process.status()
                if status == psutil.STATUS_ZOMBIE:
                    return True, None
            except:
                pass
            return True, None

        return False, None

    def start_watchdog(self, timeout_seconds: float = 60.0):
        """
        Start watchdog thread for deadlock detection

        Args:
            timeout_seconds: Timeout before considering deadlock
        """
        def watchdog_loop():
            while not self._watchdog_stop.is_set():
                with self._activity_lock:
                    elapsed = time.time() - self._last_activity

                if elapsed > timeout_seconds:
                    print(f"[WATCHDOG] Potential deadlock detected! No activity for {elapsed:.1f}s")

                time.sleep(5)  # Check every 5 seconds

        self._watchdog_thread = threading.Thread(target=watchdog_loop, daemon=True)
        self._watchdog_thread.start()

    def report_activity(self):
        """Report activity to watchdog (prevents false deadlock detection)"""
        with self._activity_lock:
            self._last_activity = time.time()

    def stop_watchdog(self):
        """Stop watchdog thread"""
        if self._watchdog_thread:
            self._watchdog_stop.set()
            self._watchdog_thread.join(timeout=10)

    def get_summary_stats(self) -> Dict:
        """
        Get summary statistics from all snapshots

        Returns:
            Dictionary with summary statistics
        """
        if not self.snapshots:
            return {
                'error': 'No snapshots available'
            }

        first = self.snapshots[0]
        last = self.snapshots[-1]

        memory_values = [s.memory_mb for s in self.snapshots]
        cpu_values = [s.cpu_percent for s in self.snapshots]

        duration_seconds = (last.timestamp - first.timestamp).total_seconds()

        return {
            'snapshot_count': len(self.snapshots),
            'duration_seconds': duration_seconds,
            'memory': {
                'initial_mb': first.memory_mb,
                'final_mb': last.memory_mb,
                'growth_mb': last.memory_mb - first.memory_mb,
                'min_mb': min(memory_values),
                'max_mb': max(memory_values),
                'avg_mb': sum(memory_values) / len(memory_values),
                'growth_rate_mb_per_hour': self.get_memory_growth_rate()
            },
            'cpu': {
                'avg_percent': self.get_average_cpu(),
                'min_percent': min(cpu_values),
                'max_percent': max(cpu_values)
            },
            'process': {
                'alive': self.is_process_alive(),
                'final_process_count': last.process_count,
                'final_thread_count': last.thread_count
            }
        }

    def print_summary(self):
        """Print summary statistics"""
        stats = self.get_summary_stats()

        if 'error' in stats:
            print(f"Error: {stats['error']}")
            return

        print("\n" + "=" * 70)
        print("SYSTEM MONITOR SUMMARY")
        print("=" * 70)

        print(f"\nDuration: {stats['duration_seconds']:.1f} seconds")
        print(f"Snapshots: {stats['snapshot_count']}")

        print("\nMemory:")
        mem = stats['memory']
        print(f"  Initial: {mem['initial_mb']:.1f} MB")
        print(f"  Final: {mem['final_mb']:.1f} MB")
        print(f"  Growth: {mem['growth_mb']:.1f} MB")
        print(f"  Range: {mem['min_mb']:.1f} - {mem['max_mb']:.1f} MB")
        print(f"  Average: {mem['avg_mb']:.1f} MB")
        if mem['growth_rate_mb_per_hour']:
            print(f"  Growth rate: {mem['growth_rate_mb_per_hour']:.2f} MB/hour")

        print("\nCPU:")
        cpu = stats['cpu']
        print(f"  Average: {cpu['avg_percent']:.1f}%")
        print(f"  Range: {cpu['min_percent']:.1f} - {cpu['max_percent']:.1f}%")

        print("\nProcess:")
        proc = stats['process']
        print(f"  Alive: {proc['alive']}")
        print(f"  Child processes: {proc['final_process_count']}")
        print(f"  Threads: {proc['final_thread_count']}")

        print("=" * 70 + "\n")


# Example usage
if __name__ == "__main__":
    print("System Monitor Example")
    print("=" * 70)

    # Create monitor
    monitor = SystemMonitor()

    # Set baseline
    monitor.set_baseline()
    print(f"Baseline memory: {monitor.baseline_memory_mb:.1f} MB")

    # Simulate work with periodic snapshots
    print("\nSimulating 10 seconds of work with snapshots...")
    for i in range(10):
        # Take snapshot
        snapshot = monitor.take_snapshot()
        print(f"Snapshot {i+1}: {snapshot.memory_mb:.1f} MB, {snapshot.cpu_percent:.1f}% CPU")

        # Report activity (for watchdog)
        monitor.report_activity()

        # Simulate work
        time.sleep(1)

    # Check for memory leak
    leak_detected, growth = monitor.detect_memory_leak(threshold_mb=10)
    if leak_detected:
        print(f"\n[WARNING] Memory leak detected! Growth: {growth:.1f} MB")
    else:
        print(f"\n[OK] No memory leak detected (growth: {growth:.1f} MB)")

    # Print summary
    monitor.print_summary()
