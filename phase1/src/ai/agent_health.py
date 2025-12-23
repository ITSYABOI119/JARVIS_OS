#!/usr/bin/env python3
"""
JARVIS AI-OS Phase 1 - Agent Health Monitoring System
Week 12: Fault Tolerance & Failover

This module implements health monitoring for all specialist agents.
Tracks heartbeats, detects failures, and manages health state transitions.

Health States:
- Healthy: Receiving heartbeats on time (<5s intervals)
- Degraded: Late heartbeats (5-10s intervals)
- Failed: No heartbeat for 10+ seconds

Author: JARVIS AI-OS Team
Date: November 20, 2025
"""

import time
import threading
from typing import Dict, List, Optional, Any
from enum import Enum
from dataclasses import dataclass, field
from datetime import datetime

class HealthState(Enum):
    """Agent health states"""
    HEALTHY = "healthy"
    DEGRADED = "degraded"
    FAILED = "failed"
    UNKNOWN = "unknown"

@dataclass
class Heartbeat:
    """Heartbeat message from agent"""
    agent_name: str
    timestamp: float
    status: str = "healthy"
    queries_processed: int = 0
    avg_response_time: float = 0.0
    metadata: Dict[str, Any] = field(default_factory=dict)

@dataclass
class AgentHealthStatus:
    """Health status for a single agent"""
    agent_name: str
    state: HealthState = HealthState.UNKNOWN
    last_heartbeat: Optional[float] = None
    last_state_change: float = field(default_factory=time.time)
    consecutive_failures: int = 0
    total_failures: int = 0
    total_recoveries: int = 0
    uptime_seconds: float = 0.0
    queries_processed: int = 0
    avg_response_time: float = 0.0

    def time_since_heartbeat(self) -> Optional[float]:
        """Get time since last heartbeat in seconds"""
        if self.last_heartbeat is None:
            return None
        return time.time() - self.last_heartbeat

    def is_healthy(self) -> bool:
        """Check if agent is healthy"""
        return self.state == HealthState.HEALTHY

    def is_degraded(self) -> bool:
        """Check if agent is degraded"""
        return self.state == HealthState.DEGRADED

    def is_failed(self) -> bool:
        """Check if agent is failed"""
        return self.state == HealthState.FAILED

    def to_dict(self) -> Dict[str, Any]:
        """Convert to dictionary"""
        return {
            "agent_name": self.agent_name,
            "state": self.state.value,
            "last_heartbeat": self.last_heartbeat,
            "time_since_heartbeat": self.time_since_heartbeat(),
            "last_state_change": self.last_state_change,
            "consecutive_failures": self.consecutive_failures,
            "total_failures": self.total_failures,
            "total_recoveries": self.total_recoveries,
            "uptime_seconds": self.uptime_seconds,
            "queries_processed": self.queries_processed,
            "avg_response_time": self.avg_response_time
        }

class AgentHealthMonitor:
    """
    Health monitoring system for specialist agents.

    Responsibilities:
    - Track heartbeats from all agents
    - Detect timeouts and failures
    - Manage health state transitions
    - Provide health status to router
    - Track uptime and failure statistics

    Configuration:
    - Heartbeat interval: 5 seconds (agents send every 5s)
    - Degraded threshold: 7 seconds (warning)
    - Failed threshold: 10 seconds (trigger failover)
    - Check interval: 1 second (monitor checks every 1s)
    """

    def __init__(self, agent_names: List[str],
                 heartbeat_interval: float = 5.0,
                 degraded_threshold: float = 7.0,
                 failed_threshold: float = 10.0,
                 check_interval: float = 0.5):
        """
        Initialize health monitor.

        Args:
            agent_names: List of agent names to monitor
            heartbeat_interval: Expected heartbeat interval (seconds)
            degraded_threshold: Threshold for degraded state (seconds)
            failed_threshold: Threshold for failed state (seconds)
            check_interval: How often to check health (seconds)
        """
        self.agent_names = agent_names
        self.heartbeat_interval = heartbeat_interval
        self.degraded_threshold = degraded_threshold
        self.failed_threshold = failed_threshold
        self.check_interval = check_interval

        # Validate threshold configuration
        if degraded_threshold >= failed_threshold:
            raise ValueError(
                f"degraded_threshold ({degraded_threshold}s) must be less than "
                f"failed_threshold ({failed_threshold}s)"
            )

        # Health status for each agent
        self.health_status: Dict[str, AgentHealthStatus] = {
            name: AgentHealthStatus(agent_name=name)
            for name in agent_names
        }

        # Heartbeat history (last 10 heartbeats per agent)
        self.heartbeat_history: Dict[str, List[Heartbeat]] = {
            name: [] for name in agent_names
        }

        # Monitoring thread
        self.monitor_thread: Optional[threading.Thread] = None
        self.running = False
        self.lock = threading.RLock()

        # Statistics
        self.stats = {
            "total_heartbeats": 0,
            "total_state_changes": 0,
            "total_failures": 0,
            "total_recoveries": 0,
            "monitoring_uptime": 0.0,
            "start_time": time.time()
        }

        # State change callbacks
        self.state_change_callbacks: List[callable] = []

    def start(self):
        """Start health monitoring"""
        if self.running:
            print("[HealthMonitor] Already running")
            return

        self.running = True
        self.monitor_thread = threading.Thread(target=self._monitor_loop, daemon=True)
        self.monitor_thread.start()
        print(f"[HealthMonitor] Started monitoring {len(self.agent_names)} agents")

    def stop(self):
        """Stop health monitoring"""
        if not self.running:
            return

        self.running = False
        if self.monitor_thread:
            self.monitor_thread.join(timeout=2.0)
        print("[HealthMonitor] Stopped")

    def receive_heartbeat(self, heartbeat: Heartbeat):
        """
        Receive heartbeat from agent.

        Args:
            heartbeat: Heartbeat message from agent
        """
        with self.lock:
            agent_name = heartbeat.agent_name

            if agent_name not in self.health_status:
                print(f"[HealthMonitor] Unknown agent: {agent_name}")
                return

            status = self.health_status[agent_name]

            # Update heartbeat timestamp
            status.last_heartbeat = heartbeat.timestamp
            status.queries_processed = heartbeat.queries_processed
            status.avg_response_time = heartbeat.avg_response_time

            # Add to history (keep last 10)
            self.heartbeat_history[agent_name].append(heartbeat)
            if len(self.heartbeat_history[agent_name]) > 10:
                self.heartbeat_history[agent_name].pop(0)

            # Update statistics
            self.stats["total_heartbeats"] += 1

            # Update health state immediately when heartbeat received
            # This provides fast response to agent recovery
            self._update_health_state(agent_name)

    def _monitor_loop(self):
        """Background monitoring loop"""
        print("[HealthMonitor] Monitoring loop started")

        while self.running:
            try:
                # Check health of all agents
                with self.lock:
                    for agent_name in self.agent_names:
                        self._update_health_state(agent_name)

                # Update monitoring uptime
                self.stats["monitoring_uptime"] = time.time() - self.stats["start_time"]

            except Exception as e:
                print(f"[HealthMonitor] Error in monitoring loop: {e}")

            # Sleep for check interval
            time.sleep(self.check_interval)

        print("[HealthMonitor] Monitoring loop stopped")

    def _update_health_state(self, agent_name: str):
        """
        Update health state for an agent based on heartbeat timing.

        Args:
            agent_name: Name of agent to update
        """
        status = self.health_status[agent_name]
        time_since = status.time_since_heartbeat()
        old_state = status.state
        new_state = old_state

        if time_since is None:
            # No heartbeat received yet
            new_state = HealthState.UNKNOWN
        elif time_since < self.degraded_threshold:
            # Healthy: heartbeat within expected interval
            new_state = HealthState.HEALTHY
            status.consecutive_failures = 0
        elif time_since < self.failed_threshold:
            # Degraded: late heartbeat (warning)
            new_state = HealthState.DEGRADED
        else:
            # Failed: no heartbeat for too long
            new_state = HealthState.FAILED
            status.consecutive_failures += 1

        # State transition handling
        if new_state != old_state:
            self._handle_state_transition(agent_name, old_state, new_state)
            status.state = new_state
            status.last_state_change = time.time()
            self.stats["total_state_changes"] += 1

    def _handle_state_transition(self, agent_name: str, old_state: HealthState, new_state: HealthState):
        """
        Handle health state transition.

        Args:
            agent_name: Name of agent
            old_state: Previous health state
            new_state: New health state
        """
        status = self.health_status[agent_name]

        print(f"[HealthMonitor] {agent_name}: {old_state.value} -> {new_state.value}")

        # Track failures and recoveries
        if new_state == HealthState.FAILED:
            status.total_failures += 1
            self.stats["total_failures"] += 1

        if old_state == HealthState.FAILED and new_state in [HealthState.HEALTHY, HealthState.DEGRADED]:
            status.total_recoveries += 1
            self.stats["total_recoveries"] += 1

        # Notify callbacks
        for callback in self.state_change_callbacks:
            try:
                callback(agent_name, old_state, new_state)
            except Exception as e:
                print(f"[HealthMonitor] Callback error: {e}")

    def register_state_change_callback(self, callback: callable):
        """
        Register callback for state changes.

        Args:
            callback: Function(agent_name, old_state, new_state)
        """
        self.state_change_callbacks.append(callback)

    def get_health_status(self, agent_name: Optional[str] = None) -> Dict[str, Any]:
        """
        Get health status for agent(s).

        Args:
            agent_name: Specific agent name, or None for all agents

        Returns:
            Dictionary with health status
        """
        with self.lock:
            if agent_name:
                if agent_name not in self.health_status:
                    return {}
                return self.health_status[agent_name].to_dict()
            else:
                return {
                    name: status.to_dict()
                    for name, status in self.health_status.items()
                }

    def get_statistics(self) -> Dict[str, Any]:
        """Get monitoring statistics"""
        with self.lock:
            return {
                **self.stats,
                "agents_monitored": len(self.agent_names),
                "healthy_agents": sum(1 for s in self.health_status.values() if s.is_healthy()),
                "degraded_agents": sum(1 for s in self.health_status.values() if s.is_degraded()),
                "failed_agents": sum(1 for s in self.health_status.values() if s.is_failed())
            }

    def is_agent_healthy(self, agent_name: str) -> bool:
        """Check if specific agent is healthy"""
        with self.lock:
            if agent_name not in self.health_status:
                return False
            return self.health_status[agent_name].is_healthy()

    def is_agent_available(self, agent_name: str) -> bool:
        """Check if agent is available (healthy or degraded)"""
        with self.lock:
            if agent_name not in self.health_status:
                return False
            status = self.health_status[agent_name]
            return status.is_healthy() or status.is_degraded()

    def get_failed_agents(self) -> List[str]:
        """Get list of failed agent names"""
        with self.lock:
            return [
                name for name, status in self.health_status.items()
                if status.is_failed()
            ]

    def reset_agent_stats(self, agent_name: str):
        """Reset statistics for an agent (after recovery)"""
        with self.lock:
            if agent_name in self.health_status:
                status = self.health_status[agent_name]
                status.consecutive_failures = 0
                # Note: Don't reset total_failures/total_recoveries (historical data)

# Example usage
if __name__ == "__main__":
    print("JARVIS AI-OS - Agent Health Monitor Test")
    print("="*70)

    # Create monitor for 4 agents
    monitor = AgentHealthMonitor(
        agent_names=["device", "network", "filesystem", "user"],
        heartbeat_interval=5.0,
        degraded_threshold=7.0,
        failed_threshold=10.0
    )

    # Register state change callback
    def on_state_change(agent, old_state, new_state):
        print(f"[Callback] {agent}: {old_state.value} -> {new_state.value}")

    monitor.register_state_change_callback(on_state_change)

    # Start monitoring
    monitor.start()

    # Simulate heartbeats
    print("\nSimulating heartbeats...")
    for i in range(5):
        for agent_name in ["device", "network", "filesystem", "user"]:
            heartbeat = Heartbeat(
                agent_name=agent_name,
                timestamp=time.time(),
                queries_processed=i * 10,
                avg_response_time=5.0 + i
            )
            monitor.receive_heartbeat(heartbeat)

        time.sleep(1)

        # Print status
        if i == 2:
            print("\nHealth Status:")
            status = monitor.get_health_status()
            for agent, info in status.items():
                print(f"  {agent}: {info['state']} (last heartbeat: {info['time_since_heartbeat']:.1f}s ago)")

    # Simulate failure (stop sending heartbeats for network agent)
    print("\nSimulating network agent failure (no heartbeats)...")
    for i in range(15):
        for agent_name in ["device", "filesystem", "user"]:  # Not network
            heartbeat = Heartbeat(
                agent_name=agent_name,
                timestamp=time.time(),
                queries_processed=50 + i * 10
            )
            monitor.receive_heartbeat(heartbeat)

        time.sleep(1)

        if i == 12:
            print("\nHealth Status (network should be failed):")
            status = monitor.get_health_status()
            for agent, info in status.items():
                print(f"  {agent}: {info['state']} (last heartbeat: {info['time_since_heartbeat']:.1f}s ago)")

    # Get statistics
    print("\nStatistics:")
    stats = monitor.get_statistics()
    for key, value in stats.items():
        print(f"  {key}: {value}")

    # Stop monitoring
    monitor.stop()
    print("\nHealth monitor test complete!")
