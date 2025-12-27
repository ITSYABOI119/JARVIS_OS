#!/usr/bin/env python3
"""
JARVIS AI-OS Phase 1 - Shared Context Pool

Manages shared system state accessible to all agents.
Provides lock-free read access with atomic updates.
"""

import time
import psutil
from typing import Dict, Any, List
from threading import RLock


class SharedContext:
    """
    Shared Context Pool for Multi-Agent System

    Provides:
    - System state (memory, CPU, disk usage)
    - Active operations tracking
    - Decision cache statistics
    - IPC statistics
    - Agent health status

    Features:
    - Lock-free reads (for most operations)
    - Atomic updates
    - Thread-safe
    """

    def __init__(self):
        """Initialize shared context"""
        self._lock = RLock()  # For thread-safe writes
        self._last_update = time.time()
        self._update_interval = 1.0  # Update system state every 1s

        # System state
        self.memory_used = 0  # MB
        self.memory_total = 0  # MB
        self.cpu_percent = 0.0  # 0-100
        self.disk_used = 0  # GB
        self.disk_total = 0  # GB

        # Active operations (list of operation descriptions)
        self.active_operations: List[str] = []

        # Cache statistics
        self.cache_hit_rate = 0.0  # 0-1.0
        self.cache_lookups = 0
        self.cache_hits = 0
        self.cache_misses = 0
        self.cache_entries = 0
        self.cache_capacity = 256

        # IPC statistics
        self.ipc_sent = 0
        self.ipc_received = 0
        self.ipc_drops = 0

        # Agent health status
        self.agent_status: Dict[str, Dict[str, Any]] = {}

        # Initialize system state
        self.update_system_state()

    def update_system_state(self, force: bool = False):
        """
        Update system state (memory, CPU, disk)

        Args:
            force: Force update even if within update_interval
        """
        now = time.time()
        if not force and (now - self._last_update) < self._update_interval:
            return  # Skip update if too soon

        try:
            # Memory
            mem = psutil.virtual_memory()
            self.memory_used = mem.used // (1024 * 1024)  # MB
            self.memory_total = mem.total // (1024 * 1024)  # MB

            # CPU
            self.cpu_percent = psutil.cpu_percent(interval=0.1)

            # Disk
            disk = psutil.disk_usage('/')
            self.disk_used = disk.used // (1024 * 1024 * 1024)  # GB
            self.disk_total = disk.total // (1024 * 1024 * 1024)  # GB

            self._last_update = now

        except Exception as e:
            # Fail silently - don't break agents if psutil fails
            pass

    def add_operation(self, operation: str):
        """
        Add active operation

        Args:
            operation: Description of operation (e.g., "copying file", "pinging host")
        """
        with self._lock:
            if operation not in self.active_operations:
                self.active_operations.append(operation)

    def remove_operation(self, operation: str):
        """
        Remove active operation

        Args:
            operation: Description of operation to remove
        """
        with self._lock:
            if operation in self.active_operations:
                self.active_operations.remove(operation)

    def update_cache_stats(self, lookups: int, hits: int, misses: int, entries: int):
        """
        Update cache statistics

        Args:
            lookups: Total cache lookups
            hits: Total cache hits
            misses: Total cache misses
            entries: Current cache entries
        """
        with self._lock:
            self.cache_lookups = lookups
            self.cache_hits = hits
            self.cache_misses = misses
            self.cache_entries = entries

            if lookups > 0:
                self.cache_hit_rate = hits / lookups
            else:
                self.cache_hit_rate = 0.0

    def update_ipc_stats(self, sent: int, received: int, drops: int):
        """
        Update IPC statistics

        Args:
            sent: Total messages sent
            received: Total messages received
            drops: Total messages dropped
        """
        with self._lock:
            self.ipc_sent = sent
            self.ipc_received = received
            self.ipc_drops = drops

    def update_agent_status(self, agent_name: str, status: Dict[str, Any]):
        """
        Update agent health status

        Args:
            agent_name: Name of agent (e.g., "device", "network")
            status: Agent status dict from agent.get_status()
        """
        with self._lock:
            self.agent_status[agent_name] = status

    def to_dict(self) -> Dict[str, Any]:
        """
        Convert context to dictionary (for passing to agents)

        Returns:
            dict: Shared context as dictionary
        """
        # Update system state before returning
        self.update_system_state()

        return {
            # System state
            "memory_used": self.memory_used,
            "memory_total": self.memory_total,
            "cpu_percent": self.cpu_percent,
            "disk_used": self.disk_used,
            "disk_total": self.disk_total,

            # Active operations (copy to avoid concurrent modification)
            "active_operations": list(self.active_operations),

            # Cache statistics
            "cache_hit_rate": self.cache_hit_rate,
            "cache_lookups": self.cache_lookups,
            "cache_hits": self.cache_hits,
            "cache_misses": self.cache_misses,
            "cache_entries": self.cache_entries,
            "cache_capacity": self.cache_capacity,

            # IPC statistics
            "ipc_sent": self.ipc_sent,
            "ipc_received": self.ipc_received,
            "ipc_drops": self.ipc_drops,

            # Agent health (copy to avoid concurrent modification)
            "agent_status": dict(self.agent_status),

            # Timestamp
            "timestamp": time.time()
        }

    def get_summary(self) -> str:
        """
        Get human-readable summary

        Returns:
            str: Summary of system state
        """
        self.update_system_state()

        return f"""
Shared Context Summary:
  Memory: {self.memory_used}/{self.memory_total} MB ({self.memory_used*100//self.memory_total if self.memory_total > 0 else 0}%)
  CPU: {self.cpu_percent:.1f}%
  Disk: {self.disk_used}/{self.disk_total} GB ({self.disk_used*100//self.disk_total if self.disk_total > 0 else 0}%)
  Cache: {self.cache_hit_rate*100:.1f}% hit rate ({self.cache_entries}/{self.cache_capacity} entries)
  IPC: {self.ipc_sent} sent, {self.ipc_received} received, {self.ipc_drops} drops
  Active Ops: {len(self.active_operations)}
  Agents: {len(self.agent_status)} registered
""".strip()


if __name__ == "__main__":
    # Test Shared Context
    print("="*70)
    print("Testing Shared Context Pool")
    print("="*70)

    context = SharedContext()

    print("\nInitial State:")
    print(context.get_summary())

    # Update cache stats
    print("\nUpdating cache statistics...")
    context.update_cache_stats(lookups=1000, hits=857, misses=143, entries=103)

    # Update IPC stats
    print("Updating IPC statistics...")
    context.update_ipc_stats(sent=100, received=95, drops=5)

    # Add operations
    print("Adding active operations...")
    context.add_operation("pinging google.com")
    context.add_operation("listing /tmp directory")

    # Update agent status
    print("Updating agent status...")
    context.update_agent_status("device", {
        "name": "device",
        "status": "ready",
        "queries": 50,
        "avg_response_ms": 6.73
    })
    context.update_agent_status("network", {
        "name": "network",
        "status": "ready",
        "queries": 30,
        "avg_response_ms": 467.0
    })

    print("\nUpdated State:")
    print(context.get_summary())

    # Convert to dict
    print("\nContext as Dictionary:")
    context_dict = context.to_dict()
    for key, value in context_dict.items():
        if key == "agent_status":
            print(f"  {key}: {len(value)} agents")
        else:
            print(f"  {key}: {value}")

    # Remove operation
    print("\nRemoving operation...")
    context.remove_operation("pinging google.com")

    print("\nFinal State:")
    print(context.get_summary())

    print("\n" + "="*70)
    print("Shared Context test complete!")
