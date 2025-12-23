#!/usr/bin/env python3
"""
JARVIS AI-OS Phase 1 - Agent Failover Manager
Week 12: Fault Tolerance & Failover

This module implements automatic failover and recovery for specialist agents.
Provides zero-downtime service when agents fail.

Failover Strategies:
1. Retry with exponential backoff (up to 3 attempts)
2. Route to rule-based fallback handler (temporary)
3. Degrade gracefully (inform user, continue with remaining agents)

Author: JARVIS AI-OS Team
Date: November 20, 2025
"""

import time
import threading
from typing import Dict, List, Optional, Any, Callable
from dataclasses import dataclass, field
from enum import Enum

class FailoverStrategy(Enum):
    """Failover strategy types"""
    RETRY = "retry"
    FALLBACK = "fallback"
    DEGRADE = "degrade"

@dataclass
class FailoverEvent:
    """Record of a failover event"""
    timestamp: float
    agent_name: str
    strategy: FailoverStrategy
    query: str
    success: bool
    recovery_time: float = 0.0
    error_message: str = ""
    metadata: Dict[str, Any] = field(default_factory=dict)

class AgentFailoverManager:
    """
    Failover manager for specialist agents.

    Responsibilities:
    - Detect failed agents (via health monitor)
    - Retry queries with exponential backoff
    - Route to rule-based fallback handlers
    - Track failover events and statistics
    - Ensure zero service interruption

    Configuration:
    - Max retries: 3 attempts
    - Retry delays: 0.1s, 0.2s, 0.4s (exponential backoff)
    - Fallback timeout: 5 seconds
    """

    def __init__(self, health_monitor=None, max_retries: int = 3, base_retry_delay: float = 0.1):
        """
        Initialize failover manager.

        Args:
            health_monitor: AgentHealthMonitor instance (optional)
            max_retries: Maximum retry attempts
            base_retry_delay: Base delay for exponential backoff (seconds)
        """
        self.health_monitor = health_monitor
        self.max_retries = max_retries
        self.base_retry_delay = base_retry_delay

        # Failover event history (last 100 events)
        self.failover_events: List[FailoverEvent] = []
        self.max_events = 100

        # Rule-based fallback handlers (agent_name -> handler function)
        self.fallback_handlers: Dict[str, Callable] = {}

        # Statistics
        self.stats = {
            "total_failovers": 0,
            "successful_retries": 0,
            "failed_retries": 0,
            "fallback_used": 0,
            "degraded_responses": 0,
            "total_recovery_time": 0.0,
            "avg_recovery_time": 0.0
        }

        self.lock = threading.RLock()

    def register_fallback_handler(self, agent_name: str, handler: Callable):
        """
        Register rule-based fallback handler for an agent.

        Args:
            agent_name: Name of agent
            handler: Function(query) -> response
        """
        self.fallback_handlers[agent_name] = handler
        print(f"[Failover] Registered fallback handler for {agent_name}")

    def process_query_with_failover(self, agent_name: str, query: str,
                                    primary_handler: Callable,
                                    context: Optional[Dict] = None) -> Dict[str, Any]:
        """
        Process query with automatic failover.

        Args:
            agent_name: Name of agent to process query
            query: Query string
            primary_handler: Primary agent handler function(query, context) -> response
            context: Optional context dictionary

        Returns:
            Response dictionary with keys:
                - success: bool
                - response: str
                - agent: str (which agent/handler was used)
                - failover_used: bool
                - strategy: str (retry/fallback/degrade)
                - recovery_time: float
        """
        start_time = time.time()

        # Check if agent is healthy
        if self.health_monitor and not self.health_monitor.is_agent_available(agent_name):
            print(f"[Failover] {agent_name} is unavailable, using fallback")
            return self._use_fallback(agent_name, query, start_time)

        # Try primary handler with retries
        for attempt in range(1, self.max_retries + 1):
            try:
                response = primary_handler(query, context)

                # Success!
                recovery_time = time.time() - start_time

                if attempt > 1:
                    # Retry succeeded
                    self._record_event(FailoverEvent(
                        timestamp=start_time,
                        agent_name=agent_name,
                        strategy=FailoverStrategy.RETRY,
                        query=query,
                        success=True,
                        recovery_time=recovery_time
                    ))
                    with self.lock:
                        self.stats["successful_retries"] += 1

                return {
                    "success": True,
                    "response": response,
                    "agent": agent_name,
                    "failover_used": attempt > 1,
                    "strategy": "retry" if attempt > 1 else "primary",
                    "recovery_time": recovery_time,
                    "attempts": attempt
                }

            except Exception as e:
                print(f"[Failover] {agent_name} attempt {attempt}/{self.max_retries} failed: {e}")

                if attempt < self.max_retries:
                    # Retry with exponential backoff
                    delay = self.base_retry_delay * (2 ** (attempt - 1))
                    time.sleep(delay)
                else:
                    # All retries failed, use fallback
                    with self.lock:
                        self.stats["failed_retries"] += 1

                    self._record_event(FailoverEvent(
                        timestamp=start_time,
                        agent_name=agent_name,
                        strategy=FailoverStrategy.RETRY,
                        query=query,
                        success=False,
                        error_message=str(e)
                    ))

                    return self._use_fallback(agent_name, query, start_time, error=str(e))

    def _use_fallback(self, agent_name: str, query: str, start_time: float,
                     error: str = "") -> Dict[str, Any]:
        """
        Use fallback handler when primary fails.

        Args:
            agent_name: Name of agent
            query: Query string
            start_time: Start timestamp
            error: Error message from primary failure

        Returns:
            Response dictionary
        """
        with self.lock:
            self.stats["total_failovers"] += 1
            self.stats["fallback_used"] += 1

        # Try rule-based fallback
        if agent_name in self.fallback_handlers:
            try:
                fallback_response = self.fallback_handlers[agent_name](query)
                recovery_time = time.time() - start_time

                self._record_event(FailoverEvent(
                    timestamp=start_time,
                    agent_name=agent_name,
                    strategy=FailoverStrategy.FALLBACK,
                    query=query,
                    success=True,
                    recovery_time=recovery_time
                ))

                with self.lock:
                    self.stats["total_recovery_time"] += recovery_time
                    self.stats["avg_recovery_time"] = self.stats["total_recovery_time"] / self.stats["total_failovers"]

                return {
                    "success": True,
                    "response": fallback_response,
                    "agent": f"{agent_name}_fallback",
                    "failover_used": True,
                    "strategy": "fallback",
                    "recovery_time": recovery_time
                }

            except Exception as e:
                print(f"[Failover] Fallback handler failed for {agent_name}: {e}")
                error = f"{error}; Fallback failed: {e}"

        # Degrade gracefully (last resort)
        return self._degrade_gracefully(agent_name, query, start_time, error)

    def _degrade_gracefully(self, agent_name: str, query: str, start_time: float,
                           error: str = "") -> Dict[str, Any]:
        """
        Degrade gracefully when all strategies fail.

        Args:
            agent_name: Name of agent
            query: Query string
            start_time: Start timestamp
            error: Accumulated error messages

        Returns:
            Response dictionary with degraded message
        """
        with self.lock:
            self.stats["degraded_responses"] += 1

        recovery_time = time.time() - start_time

        self._record_event(FailoverEvent(
            timestamp=start_time,
            agent_name=agent_name,
            strategy=FailoverStrategy.DEGRADE,
            query=query,
            success=False,
            recovery_time=recovery_time,
            error_message=error
        ))

        degraded_message = (
            f"[DEGRADED] {agent_name} agent is temporarily unavailable.\n"
            f"Query: {query}\n"
            f"The system is operating with reduced functionality.\n"
            f"Please try again later or use alternative commands."
        )

        return {
            "success": False,
            "response": degraded_message,
            "agent": agent_name,
            "failover_used": True,
            "strategy": "degrade",
            "recovery_time": recovery_time,
            "error": error
        }

    def _record_event(self, event: FailoverEvent):
        """Record failover event"""
        with self.lock:
            self.failover_events.append(event)
            if len(self.failover_events) > self.max_events:
                self.failover_events.pop(0)

    def get_failover_events(self, agent_name: Optional[str] = None,
                           limit: int = 10) -> List[Dict[str, Any]]:
        """
        Get recent failover events.

        Args:
            agent_name: Filter by agent name (optional)
            limit: Maximum number of events to return

        Returns:
            List of event dictionaries
        """
        with self.lock:
            events = self.failover_events[-limit:]

            if agent_name:
                events = [e for e in events if e.agent_name == agent_name]

            return [
                {
                    "timestamp": e.timestamp,
                    "agent_name": e.agent_name,
                    "strategy": e.strategy.value,
                    "query": e.query,
                    "success": e.success,
                    "recovery_time": e.recovery_time,
                    "error_message": e.error_message
                }
                for e in events
            ]

    def get_statistics(self) -> Dict[str, Any]:
        """Get failover statistics"""
        with self.lock:
            return {
                **self.stats,
                "total_events": len(self.failover_events),
                "registered_fallbacks": len(self.fallback_handlers)
            }

    def reset_statistics(self):
        """Reset statistics (for testing)"""
        with self.lock:
            self.stats = {
                "total_failovers": 0,
                "successful_retries": 0,
                "failed_retries": 0,
                "fallback_used": 0,
                "degraded_responses": 0,
                "total_recovery_time": 0.0,
                "avg_recovery_time": 0.0
            }
            self.failover_events.clear()

# Example fallback handlers
def device_fallback_handler(query: str) -> str:
    """Simple rule-based fallback for device agent"""
    query_lower = query.lower()

    if "disk" in query_lower or "space" in query_lower:
        import subprocess
        try:
            result = subprocess.run(["df", "-h"], capture_output=True, text=True, timeout=2)
            return f"[FALLBACK] Disk space:\n{result.stdout}"
        except:
            return "[FALLBACK] Unable to check disk space"

    elif "memory" in query_lower or "ram" in query_lower:
        import subprocess
        try:
            result = subprocess.run(["free", "-h"], capture_output=True, text=True, timeout=2)
            return f"[FALLBACK] Memory:\n{result.stdout}"
        except:
            return "[FALLBACK] Unable to check memory"

    else:
        return "[FALLBACK] Device agent unavailable. Limited functionality."

def network_fallback_handler(query: str) -> str:
    """Simple rule-based fallback for network agent"""
    query_lower = query.lower()

    if "ping" in query_lower:
        return "[FALLBACK] Network agent unavailable. Try: ping <host> in terminal"

    elif "status" in query_lower or "connection" in query_lower:
        import socket
        try:
            socket.create_connection(("8.8.8.8", 53), timeout=2)
            return "[FALLBACK] Internet connection appears to be working"
        except:
            return "[FALLBACK] Internet connection may be down"

    else:
        return "[FALLBACK] Network agent unavailable. Limited functionality."

def filesystem_fallback_handler(query: str) -> str:
    """Simple rule-based fallback for filesystem agent"""
    query_lower = query.lower()

    if "list" in query_lower or "ls" in query_lower:
        return "[FALLBACK] FileSystem agent unavailable. Try: ls in terminal"

    else:
        return "[FALLBACK] FileSystem agent unavailable. Limited functionality."

def user_fallback_handler(query: str) -> str:
    """Simple rule-based fallback for user agent"""
    return (
        "[FALLBACK] User agent unavailable.\n"
        "Available commands: help, status, cache, agent, clear, exit"
    )

# Example usage
if __name__ == "__main__":
    print("JARVIS AI-OS - Agent Failover Manager Test")
    print("="*70)

    # Create failover manager
    failover = AgentFailoverManager(max_retries=3, base_retry_delay=0.1)

    # Register fallback handlers
    failover.register_fallback_handler("device", device_fallback_handler)
    failover.register_fallback_handler("network", network_fallback_handler)
    failover.register_fallback_handler("filesystem", filesystem_fallback_handler)
    failover.register_fallback_handler("user", user_fallback_handler)

    print("\n" + "="*70)
    print("Test 1: Successful primary handler")
    print("="*70)

    def working_handler(query, context=None):
        return f"Processed: {query}"

    result = failover.process_query_with_failover("device", "test query", working_handler)
    print(f"Success: {result['success']}")
    print(f"Response: {result['response']}")
    print(f"Strategy: {result['strategy']}")

    print("\n" + "="*70)
    print("Test 2: Failing handler (triggers retry then fallback)")
    print("="*70)

    attempt_count = [0]
    def failing_handler(query, context=None):
        attempt_count[0] += 1
        raise Exception(f"Handler failed (attempt {attempt_count[0]})")

    result = failover.process_query_with_failover("device", "show disk space", failing_handler)
    print(f"Success: {result['success']}")
    print(f"Response: {result['response'][:100]}...")
    print(f"Strategy: {result['strategy']}")
    print(f"Recovery time: {result['recovery_time']:.3f}s")

    print("\n" + "="*70)
    print("Test 3: Statistics")
    print("="*70)

    stats = failover.get_statistics()
    for key, value in stats.items():
        print(f"  {key}: {value}")

    print("\n" + "="*70)
    print("Test 4: Failover events")
    print("="*70)

    events = failover.get_failover_events(limit=5)
    for i, event in enumerate(events, 1):
        print(f"\nEvent {i}:")
        print(f"  Agent: {event['agent_name']}")
        print(f"  Strategy: {event['strategy']}")
        print(f"  Success: {event['success']}")
        print(f"  Recovery time: {event['recovery_time']:.3f}s")

    print("\nFailover manager test complete!")
