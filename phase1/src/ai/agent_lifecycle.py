#!/usr/bin/env python3
"""
JARVIS AI-OS Phase 1 - Agent Lifecycle Manager
Week 12: Fault Tolerance & Failover (Task 3)

This module implements automatic agent restart and lifecycle management.
Detects failed agents and restarts them automatically with model reloading.

Responsibilities:
- Detect failed agents (via health monitor)
- Restart agent processes
- Reload AI models if needed
- Resume agent operation
- Track restart history and statistics

Author: JARVIS AI-OS Team
Date: November 20, 2025
"""

import os
import sys
import time
import subprocess
import threading
import signal
from typing import Dict, List, Optional, Any, Callable
from dataclasses import dataclass, field
from enum import Enum
from datetime import datetime

class RestartStrategy(Enum):
    """Agent restart strategies"""
    IMMEDIATE = "immediate"       # Restart immediately
    BACKOFF = "backoff"           # Exponential backoff
    MANUAL = "manual"             # Manual restart only

@dataclass
class AgentProcess:
    """Information about an agent process"""
    agent_name: str
    process: Optional[subprocess.Popen] = None
    pid: Optional[int] = None
    start_time: float = field(default_factory=time.time)
    restart_count: int = 0
    last_restart: Optional[float] = None
    model_path: Optional[str] = None
    command: List[str] = field(default_factory=list)

    def is_alive(self) -> bool:
        """Check if process is alive"""
        if self.process is None:
            return False
        return self.process.poll() is None

    def kill(self):
        """Kill the process"""
        if self.process and self.is_alive():
            try:
                self.process.terminate()
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()

@dataclass
class RestartEvent:
    """Record of an agent restart event"""
    timestamp: float
    agent_name: str
    reason: str
    success: bool
    restart_time: float = 0.0
    error_message: str = ""
    metadata: Dict[str, Any] = field(default_factory=dict)

class AgentLifecycleManager:
    """
    Lifecycle manager for specialist agents.

    Responsibilities:
    - Monitor agent health (via AgentHealthMonitor)
    - Detect failed agents
    - Restart agent processes automatically
    - Reload AI models if needed
    - Track restart history and statistics

    Configuration:
    - Max restart attempts: 3 (before giving up)
    - Restart backoff: 1s, 2s, 4s (exponential)
    - Restart timeout: 30 seconds
    """

    def __init__(self, health_monitor=None,
                 max_restart_attempts: int = 3,
                 base_restart_delay: float = 1.0,
                 restart_timeout: float = 30.0):
        """
        Initialize lifecycle manager.

        Args:
            health_monitor: AgentHealthMonitor instance (optional)
            max_restart_attempts: Maximum restart attempts before giving up
            base_restart_delay: Base delay for exponential backoff (seconds)
            restart_timeout: Maximum time to wait for restart (seconds)
        """
        self.health_monitor = health_monitor
        self.max_restart_attempts = max_restart_attempts
        self.base_restart_delay = base_restart_delay
        self.restart_timeout = restart_timeout

        # Agent processes
        self.agents: Dict[str, AgentProcess] = {}

        # Restart history (last 100 events)
        self.restart_events: List[RestartEvent] = []
        self.max_events = 100

        # Statistics
        self.stats = {
            "total_restarts": 0,
            "successful_restarts": 0,
            "failed_restarts": 0,
            "total_restart_time": 0.0,
            "avg_restart_time": 0.0,
            "agents_running": 0,
            "agents_stopped": 0
        }

        # Monitoring thread
        self.monitor_thread: Optional[threading.Thread] = None
        self.running = False
        self.lock = threading.RLock()

        # Restart callbacks
        self.restart_callbacks: List[Callable] = []

    def register_agent(self, agent_name: str, command: List[str],
                      model_path: Optional[str] = None):
        """
        Register an agent for lifecycle management.

        Args:
            agent_name: Name of agent
            command: Command to start agent (e.g., ["python", "agent.py"])
            model_path: Path to AI model (optional, for reloading)
        """
        with self.lock:
            if agent_name in self.agents:
                print(f"[Lifecycle] Agent {agent_name} already registered")
                return

            self.agents[agent_name] = AgentProcess(
                agent_name=agent_name,
                command=command,
                model_path=model_path
            )
            print(f"[Lifecycle] Registered agent: {agent_name}")

    def start_agent(self, agent_name: str) -> bool:
        """
        Start an agent process.

        Args:
            agent_name: Name of agent to start

        Returns:
            True if started successfully, False otherwise
        """
        with self.lock:
            if agent_name not in self.agents:
                print(f"[Lifecycle] Unknown agent: {agent_name}")
                return False

            agent = self.agents[agent_name]

            # Check if already running
            if agent.is_alive():
                print(f"[Lifecycle] Agent {agent_name} already running (PID: {agent.pid})")
                return True

            try:
                # Start process
                agent.process = subprocess.Popen(
                    agent.command,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    text=True
                )
                agent.pid = agent.process.pid
                agent.start_time = time.time()

                self.stats["agents_running"] += 1

                print(f"[Lifecycle] Started agent {agent_name} (PID: {agent.pid})")
                return True

            except Exception as e:
                print(f"[Lifecycle] Failed to start agent {agent_name}: {e}")
                return False

    def stop_agent(self, agent_name: str) -> bool:
        """
        Stop an agent process.

        Args:
            agent_name: Name of agent to stop

        Returns:
            True if stopped successfully, False otherwise
        """
        with self.lock:
            if agent_name not in self.agents:
                print(f"[Lifecycle] Unknown agent: {agent_name}")
                return False

            agent = self.agents[agent_name]

            if not agent.is_alive():
                print(f"[Lifecycle] Agent {agent_name} not running")
                return True

            try:
                agent.kill()
                self.stats["agents_running"] -= 1
                self.stats["agents_stopped"] += 1
                print(f"[Lifecycle] Stopped agent {agent_name}")
                return True

            except Exception as e:
                print(f"[Lifecycle] Failed to stop agent {agent_name}: {e}")
                return False

    def restart_agent(self, agent_name: str, reason: str = "manual") -> bool:
        """
        Restart an agent process.

        Args:
            agent_name: Name of agent to restart
            reason: Reason for restart (for logging)

        Returns:
            True if restarted successfully, False otherwise
        """
        start_time = time.time()

        with self.lock:
            if agent_name not in self.agents:
                print(f"[Lifecycle] Unknown agent: {agent_name}")
                return False

            agent = self.agents[agent_name]

            print(f"[Lifecycle] Restarting agent {agent_name} (reason: {reason})")

            # Stop current process
            if agent.is_alive():
                self.stop_agent(agent_name)

            # Wait a moment for cleanup
            time.sleep(0.5)

            # Attempt restart with exponential backoff
            for attempt in range(1, self.max_restart_attempts + 1):
                try:
                    # Start agent
                    if self.start_agent(agent_name):
                        # Wait for agent to initialize
                        time.sleep(1.0)

                        # Verify it's still alive
                        if agent.is_alive():
                            # Success!
                            restart_time = time.time() - start_time
                            agent.restart_count += 1
                            agent.last_restart = time.time()

                            self._record_restart(RestartEvent(
                                timestamp=start_time,
                                agent_name=agent_name,
                                reason=reason,
                                success=True,
                                restart_time=restart_time
                            ))

                            with self.lock:
                                self.stats["total_restarts"] += 1
                                self.stats["successful_restarts"] += 1
                                self.stats["total_restart_time"] += restart_time
                                self.stats["avg_restart_time"] = (
                                    self.stats["total_restart_time"] / self.stats["successful_restarts"]
                                )

                            # Notify callbacks
                            for callback in self.restart_callbacks:
                                try:
                                    callback(agent_name, True, restart_time)
                                except Exception as e:
                                    print(f"[Lifecycle] Callback error: {e}")

                            print(f"[Lifecycle] Agent {agent_name} restarted successfully "
                                  f"(attempt {attempt}, time: {restart_time:.2f}s)")
                            return True
                        else:
                            print(f"[Lifecycle] Agent {agent_name} started but died immediately")

                except Exception as e:
                    print(f"[Lifecycle] Restart attempt {attempt} failed: {e}")

                # Backoff before retry
                if attempt < self.max_restart_attempts:
                    delay = self.base_restart_delay * (2 ** (attempt - 1))
                    print(f"[Lifecycle] Retrying in {delay}s...")
                    time.sleep(delay)

            # All attempts failed
            restart_time = time.time() - start_time

            self._record_restart(RestartEvent(
                timestamp=start_time,
                agent_name=agent_name,
                reason=reason,
                success=False,
                restart_time=restart_time,
                error_message=f"Failed after {self.max_restart_attempts} attempts"
            ))

            with self.lock:
                self.stats["total_restarts"] += 1
                self.stats["failed_restarts"] += 1

            # Notify callbacks
            for callback in self.restart_callbacks:
                try:
                    callback(agent_name, False, restart_time)
                except Exception as e:
                    print(f"[Lifecycle] Callback error: {e}")

            print(f"[Lifecycle] Failed to restart agent {agent_name} after "
                  f"{self.max_restart_attempts} attempts")
            return False

    def _record_restart(self, event: RestartEvent):
        """Record restart event"""
        with self.lock:
            self.restart_events.append(event)
            if len(self.restart_events) > self.max_events:
                self.restart_events.pop(0)

    def start(self):
        """Start lifecycle monitoring"""
        if self.running:
            print("[Lifecycle] Already running")
            return

        if self.health_monitor is None:
            print("[Lifecycle] No health monitor provided, automatic restart disabled")
            return

        self.running = True
        self.monitor_thread = threading.Thread(target=self._monitor_loop, daemon=True)
        self.monitor_thread.start()
        print(f"[Lifecycle] Started monitoring {len(self.agents)} agents")

    def stop(self):
        """Stop lifecycle monitoring"""
        if not self.running:
            return

        self.running = False
        if self.monitor_thread:
            self.monitor_thread.join(timeout=2.0)
        print("[Lifecycle] Stopped")

    def _monitor_loop(self):
        """Background monitoring loop"""
        print("[Lifecycle] Monitoring loop started")

        while self.running:
            try:
                # Check for failed agents
                if self.health_monitor:
                    failed_agents = self.health_monitor.get_failed_agents()

                    for agent_name in failed_agents:
                        if agent_name in self.agents:
                            agent = self.agents[agent_name]

                            # Check if process is actually dead
                            if not agent.is_alive():
                                print(f"[Lifecycle] Detected failed agent: {agent_name}")
                                self.restart_agent(agent_name, reason="health_check_failed")

                # Also check for process crashes (not detected by health monitor)
                with self.lock:
                    for agent_name, agent in self.agents.items():
                        if agent.process and not agent.is_alive():
                            print(f"[Lifecycle] Detected crashed agent: {agent_name}")
                            self.restart_agent(agent_name, reason="process_crashed")

            except Exception as e:
                print(f"[Lifecycle] Error in monitoring loop: {e}")

            # Sleep before next check
            time.sleep(5.0)  # Check every 5 seconds

        print("[Lifecycle] Monitoring loop stopped")

    def register_restart_callback(self, callback: Callable):
        """
        Register callback for restart events.

        Args:
            callback: Function(agent_name, success, restart_time)
        """
        self.restart_callbacks.append(callback)

    def get_agent_status(self, agent_name: Optional[str] = None) -> Dict[str, Any]:
        """
        Get agent process status.

        Args:
            agent_name: Specific agent name, or None for all agents

        Returns:
            Dictionary with agent status
        """
        with self.lock:
            if agent_name:
                if agent_name not in self.agents:
                    return {}
                agent = self.agents[agent_name]
                return {
                    "agent_name": agent.agent_name,
                    "is_alive": agent.is_alive(),
                    "pid": agent.pid,
                    "start_time": agent.start_time,
                    "restart_count": agent.restart_count,
                    "last_restart": agent.last_restart,
                    "uptime": time.time() - agent.start_time if agent.is_alive() else 0
                }
            else:
                return {
                    name: {
                        "agent_name": agent.agent_name,
                        "is_alive": agent.is_alive(),
                        "pid": agent.pid,
                        "restart_count": agent.restart_count
                    }
                    for name, agent in self.agents.items()
                }

    def get_restart_events(self, agent_name: Optional[str] = None,
                          limit: int = 10) -> List[Dict[str, Any]]:
        """
        Get recent restart events.

        Args:
            agent_name: Filter by agent name (optional)
            limit: Maximum number of events to return

        Returns:
            List of event dictionaries
        """
        with self.lock:
            events = self.restart_events[-limit:]

            if agent_name:
                events = [e for e in events if e.agent_name == agent_name]

            return [
                {
                    "timestamp": e.timestamp,
                    "agent_name": e.agent_name,
                    "reason": e.reason,
                    "success": e.success,
                    "restart_time": e.restart_time,
                    "error_message": e.error_message
                }
                for e in events
            ]

    def get_statistics(self) -> Dict[str, Any]:
        """Get lifecycle statistics"""
        with self.lock:
            return {
                **self.stats,
                "total_events": len(self.restart_events),
                "registered_agents": len(self.agents)
            }

# Example usage
if __name__ == "__main__":
    print("JARVIS AI-OS - Agent Lifecycle Manager Test")
    print("="*70)

    # Create lifecycle manager (without health monitor for this test)
    lifecycle = AgentLifecycleManager(max_restart_attempts=3, base_restart_delay=0.5)

    # Register a dummy agent
    lifecycle.register_agent("test_agent", ["python", "-c", "import time; time.sleep(10)"])

    print("\n" + "="*70)
    print("Test 1: Start agent")
    print("="*70)

    if lifecycle.start_agent("test_agent"):
        print("[PASS] Agent started successfully")
        status = lifecycle.get_agent_status("test_agent")
        print(f"  PID: {status['pid']}, Alive: {status['is_alive']}")
    else:
        print("[FAIL] Failed to start agent")

    time.sleep(2)

    print("\n" + "="*70)
    print("Test 2: Restart agent")
    print("="*70)

    if lifecycle.restart_agent("test_agent", reason="test"):
        print("[PASS] Agent restarted successfully")
        status = lifecycle.get_agent_status("test_agent")
        print(f"  Restart count: {status['restart_count']}")
    else:
        print("[FAIL] Failed to restart agent")

    time.sleep(2)

    print("\n" + "="*70)
    print("Test 3: Stop agent")
    print("="*70)

    if lifecycle.stop_agent("test_agent"):
        print("[PASS] Agent stopped successfully")
        status = lifecycle.get_agent_status("test_agent")
        print(f"  Alive: {status['is_alive']}")
    else:
        print("[FAIL] Failed to stop agent")

    print("\n" + "="*70)
    print("Test 4: Statistics")
    print("="*70)

    stats = lifecycle.get_statistics()
    for key, value in stats.items():
        print(f"  {key}: {value}")

    print("\nLifecycle manager test complete!")
