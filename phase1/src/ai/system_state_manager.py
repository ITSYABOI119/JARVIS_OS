#!/usr/bin/env python3
"""
JARVIS AI-OS Phase 1 - System State Manager
Week 13: Dynamic Model Scaling
Week 17: Shadow Execution - Snapshot Triggers

This module implements the dynamic model scaling state machine with 4 states:
- IDLE: Llama 3.2 1B (2GB RAM) - Monitoring mode [Updated Jan 2026]
- ACTIVE: Phi-3 Mini 3.8B (8GB RAM) - General operations
- CRITICAL: Phi-3 Mini + Validator (10GB RAM) - Safety-critical ops
- EMERGENCY: Rule-based fallback (<100MB RAM) - AI failure recovery

Responsibilities:
- Track current system state
- Validate and execute state transitions
- Coordinate model loading/unloading
- Monitor system resources
- Log state changes and transitions
- Trigger snapshots before risky state transitions (Week 17)

Author: JARVIS AI-OS Team
Date: November 20, 2025 (Updated: November 25, 2025 for Week 17)
"""

import time
import psutil
import threading
from typing import Optional, Dict, Any, List, Callable
from enum import Enum
from dataclasses import dataclass, field
from datetime import datetime

class SystemState(Enum):
    """System states for dynamic model scaling"""
    IDLE = "idle"               # Llama 3.2 1B - monitoring [Updated Jan 2026]
    ACTIVE = "active"           # Phi-3 Mini 3.8B - general ops
    CRITICAL = "critical"       # Phi-3 + Validator - safety-critical
    EMERGENCY = "emergency"     # Rule-based fallback - AI failure
    SUSPENDING = "suspending"   # Week 22: Preparing for suspend
    RESUMING = "resuming"       # Week 22: Resuming from suspend

@dataclass
class StateTransition:
    """Record of a state transition"""
    timestamp: float
    from_state: SystemState
    to_state: SystemState
    trigger: str
    duration: float = 0.0
    success: bool = True
    error_message: str = ""
    metadata: Dict[str, Any] = field(default_factory=dict)

class SystemStateManager:
    """
    Manages system state transitions for dynamic model scaling.

    State Machine:
    - IDLE: Minimal resources (Llama 3.2 1B, 2GB RAM) [Updated Jan 2026]
    - ACTIVE: Normal operations (Phi-3 Mini 3.8B, 8GB RAM)
    - CRITICAL: Safety-critical (Phi-3 + Validator, 10GB RAM)
    - EMERGENCY: AI failure fallback (rule-based, <100MB RAM)

    Transition Rules:
    - IDLE → ACTIVE: User query (cache miss)
    - ACTIVE → IDLE: No queries for 30s
    - ACTIVE → CRITICAL: High-risk operation (SHIELD)
    - CRITICAL → ACTIVE: Risk resolved
    - Any → EMERGENCY: AI failure
    - EMERGENCY → IDLE: Recovery attempt

    Performance Targets:
    - IDLE → ACTIVE: <2.5s
    - ACTIVE → IDLE: <1s
    - ACTIVE → CRITICAL: <2s
    - Any → EMERGENCY: <0.1s
    """

    # Valid state transitions
    VALID_TRANSITIONS = {
        SystemState.IDLE: [SystemState.ACTIVE, SystemState.EMERGENCY, SystemState.SUSPENDING],
        SystemState.ACTIVE: [SystemState.IDLE, SystemState.CRITICAL, SystemState.EMERGENCY, SystemState.SUSPENDING],
        SystemState.CRITICAL: [SystemState.ACTIVE, SystemState.EMERGENCY, SystemState.SUSPENDING],
        SystemState.EMERGENCY: [SystemState.IDLE],
        SystemState.SUSPENDING: [SystemState.RESUMING],  # Week 22: Only resume from suspend
        SystemState.RESUMING: [SystemState.IDLE, SystemState.ACTIVE, SystemState.CRITICAL]  # Week 22: Restore pre-suspend state
    }

    # State transition latency targets (seconds)
    TRANSITION_TARGETS = {
        (SystemState.IDLE, SystemState.ACTIVE): 2.5,
        (SystemState.ACTIVE, SystemState.IDLE): 1.0,
        (SystemState.ACTIVE, SystemState.CRITICAL): 2.0,
        (SystemState.CRITICAL, SystemState.ACTIVE): 0.5,
        (SystemState.EMERGENCY, SystemState.IDLE): 2.0,
    }

    def __init__(self, model_loader=None, snapshot_manager=None, initial_state: SystemState = SystemState.IDLE):
        """
        Initialize state manager.

        Args:
            model_loader: ModelLoader instance (optional, for testing)
            snapshot_manager: EnhancedRollbackManager instance (optional, for Week 17 snapshots)
            initial_state: Starting state (default: IDLE)
        """
        self.model_loader = model_loader
        self.snapshot_manager = snapshot_manager  # Week 17: Snapshot triggers
        self.current_state = initial_state
        self.previous_state = None

        # State timing
        self.state_entered_at = time.time()
        self.last_query_time = 0.0
        self.idle_timeout = 30.0  # seconds

        # State history (last 100 transitions)
        self.state_history: List[StateTransition] = []
        self.max_history = 100

        # Statistics
        self.stats = {
            "total_transitions": 0,
            "successful_transitions": 0,
            "failed_transitions": 0,
            "total_transition_time": 0.0,
            "avg_transition_time": 0.0,
            "time_in_idle": 0.0,
            "time_in_active": 0.0,
            "time_in_critical": 0.0,
            "time_in_emergency": 0.0,
            "time_in_suspending": 0.0,  # Week 22
            "time_in_resuming": 0.0      # Week 22
        }

        # State change callbacks
        self.callbacks: List[Callable] = []

        # Thread safety
        self.lock = threading.RLock()

        # Background monitoring
        self.monitor_thread: Optional[threading.Thread] = None
        self.running = False

        print(f"[StateManager] Initialized in {initial_state.value} state")

    def register_callback(self, callback: Callable):
        """
        Register callback for state changes.

        Args:
            callback: Function(from_state, to_state, duration)
        """
        self.callbacks.append(callback)

    def transition_to(self, new_state: SystemState, trigger: str = "manual") -> bool:
        """
        Transition to a new state.

        Args:
            new_state: Target state
            trigger: Reason for transition (for logging)

        Returns:
            True if transition successful, False otherwise
        """
        start_time = time.time()

        with self.lock:
            # Check if already in target state
            if self.current_state == new_state:
                print(f"[StateManager] Already in {new_state.value} state")
                return True

            # Validate transition
            if new_state not in self.VALID_TRANSITIONS[self.current_state]:
                error = f"Invalid transition: {self.current_state.value} -> {new_state.value}"
                print(f"[StateManager] {error}")
                self._record_transition(
                    self.current_state, new_state, trigger,
                    time.time() - start_time, False, error
                )
                return False

            old_state = self.current_state

            print(f"[StateManager] Transitioning: {old_state.value} -> {new_state.value} (trigger: {trigger})")

            # Week 17: Create snapshot before risky state transitions
            snapshot_id = None
            if self.snapshot_manager and new_state == SystemState.CRITICAL:
                try:
                    print(f"[StateManager] Creating pre-CRITICAL snapshot...")
                    snapshot_id = self.snapshot_manager.create_snapshot(
                        system_state=self,
                        snapshot_type='memory',  # Fast memory snapshot
                        trigger=f'pre_critical_{trigger}'
                    )
                    print(f"[StateManager] Snapshot created: {snapshot_id}")
                except Exception as e:
                    print(f"[StateManager] Snapshot creation failed: {e}")
                    # Continue with transition even if snapshot fails

            try:
                # Update state timing statistics
                time_in_state = time.time() - self.state_entered_at
                self.stats[f"time_in_{old_state.value}"] += time_in_state

                # Perform state transition
                success = self._execute_transition(old_state, new_state)

                if not success:
                    print(f"[StateManager] Transition failed")
                    self._record_transition(
                        old_state, new_state, trigger,
                        time.time() - start_time, False, "Transition execution failed"
                    )
                    return False

                # Update state
                self.previous_state = old_state
                self.current_state = new_state
                self.state_entered_at = time.time()

                # Record successful transition
                duration = time.time() - start_time
                self._record_transition(old_state, new_state, trigger, duration, True, "")

                # Update statistics
                self.stats["total_transitions"] += 1
                self.stats["successful_transitions"] += 1
                self.stats["total_transition_time"] += duration
                self.stats["avg_transition_time"] = (
                    self.stats["total_transition_time"] / self.stats["successful_transitions"]
                )

                # Check performance target
                target = self.TRANSITION_TARGETS.get((old_state, new_state))
                if target and duration > target:
                    print(f"[StateManager] WARNING: Transition took {duration:.2f}s "
                          f"(target: {target}s)")

                # Notify callbacks
                for callback in self.callbacks:
                    try:
                        callback(old_state, new_state, duration)
                    except Exception as e:
                        print(f"[StateManager] Callback error: {e}")

                print(f"[StateManager] Transition complete: {old_state.value} -> {new_state.value} "
                      f"({duration:.2f}s)")
                return True

            except Exception as e:
                print(f"[StateManager] Transition error: {e}")
                self._record_transition(
                    old_state, new_state, trigger,
                    time.time() - start_time, False, str(e)
                )
                self.stats["failed_transitions"] += 1
                return False

    def _execute_transition(self, from_state: SystemState, to_state: SystemState) -> bool:
        """
        Execute the actual state transition (model loading/unloading).

        Args:
            from_state: Current state
            to_state: Target state

        Returns:
            True if successful, False otherwise
        """
        # If no model loader, just simulate transition
        if self.model_loader is None:
            print(f"[StateManager] Simulating transition (no model loader)")
            time.sleep(0.1)  # Simulate transition time
            return True

        try:
            # Unload current model
            if from_state != SystemState.EMERGENCY:
                print(f"[StateManager] Unloading {from_state.value} model...")
                self.model_loader.unload_model(from_state)

            # Load new model
            if to_state != SystemState.EMERGENCY:
                print(f"[StateManager] Loading {to_state.value} model...")
                self.model_loader.load_model(to_state)

            return True

        except Exception as e:
            print(f"[StateManager] Model transition error: {e}")
            return False

    def _record_transition(self, from_state: SystemState, to_state: SystemState,
                          trigger: str, duration: float, success: bool, error: str):
        """Record state transition in history"""
        transition = StateTransition(
            timestamp=time.time(),
            from_state=from_state,
            to_state=to_state,
            trigger=trigger,
            duration=duration,
            success=success,
            error_message=error
        )

        with self.lock:
            self.state_history.append(transition)
            if len(self.state_history) > self.max_history:
                self.state_history.pop(0)

    def check_idle_timeout(self) -> bool:
        """
        Check if system should transition from ACTIVE to IDLE.

        Returns:
            True if should transition, False otherwise
        """
        if self.current_state != SystemState.ACTIVE:
            return False

        time_since_last_query = time.time() - self.last_query_time

        if time_since_last_query > self.idle_timeout:
            print(f"[StateManager] Idle timeout reached ({time_since_last_query:.1f}s)")
            return True

        return False

    def record_query(self):
        """Record that a user query was received"""
        with self.lock:
            self.last_query_time = time.time()

            # If in IDLE, transition to ACTIVE
            if self.current_state == SystemState.IDLE:
                self.transition_to(SystemState.ACTIVE, trigger="user_query")

    def check_low_battery(self) -> bool:
        """
        Check if battery is low (<15%).

        Returns:
            True if battery low, False otherwise
        """
        try:
            battery = psutil.sensors_battery()
            if battery and battery.percent < 15 and not battery.power_plugged:
                print(f"[StateManager] Low battery detected: {battery.percent}%")
                return True
        except Exception:
            pass  # Battery info not available (desktop system)

        return False

    def start_monitoring(self):
        """Start background monitoring for automatic transitions"""
        if self.running:
            print("[StateManager] Already monitoring")
            return

        self.running = True
        self.monitor_thread = threading.Thread(target=self._monitor_loop, daemon=True)
        self.monitor_thread.start()
        print("[StateManager] Started monitoring")

    def stop_monitoring(self):
        """Stop background monitoring"""
        if not self.running:
            return

        self.running = False
        if self.monitor_thread:
            self.monitor_thread.join(timeout=2.0)
        print("[StateManager] Stopped monitoring")

    def _monitor_loop(self):
        """Background monitoring loop"""
        print("[StateManager] Monitoring loop started")

        while self.running:
            try:
                # Check idle timeout
                if self.check_idle_timeout():
                    self.transition_to(SystemState.IDLE, trigger="idle_timeout")

                # Check low battery
                if self.check_low_battery():
                    if self.current_state != SystemState.IDLE:
                        self.transition_to(SystemState.IDLE, trigger="low_battery")

            except Exception as e:
                print(f"[StateManager] Monitor error: {e}")

            # Check every 5 seconds
            time.sleep(5.0)

        print("[StateManager] Monitoring loop stopped")

    def get_current_state(self) -> SystemState:
        """Get current system state"""
        return self.current_state

    def get_state_info(self) -> Dict[str, Any]:
        """Get detailed state information"""
        with self.lock:
            time_in_state = time.time() - self.state_entered_at
            time_since_query = time.time() - self.last_query_time if self.last_query_time > 0 else None

            return {
                "current_state": self.current_state.value,
                "previous_state": self.previous_state.value if self.previous_state else None,
                "time_in_state": time_in_state,
                "time_since_last_query": time_since_query,
                "idle_timeout": self.idle_timeout,
                "total_transitions": self.stats["total_transitions"],
                "avg_transition_time": self.stats["avg_transition_time"]
            }

    def get_current_state_str(self) -> str:
        """
        Get current system state as string (for snapshot_manager compatibility).

        Returns:
            Current state as string ("idle", "active", "critical", "emergency")
        """
        return self.current_state.value

    def get_statistics(self) -> Dict[str, Any]:
        """Get state manager statistics"""
        with self.lock:
            # Get system resource metrics for snapshots
            try:
                cpu_percent = psutil.cpu_percent(interval=0.1)
                memory = psutil.virtual_memory()
                disk = psutil.disk_usage('/')
                net_connections = psutil.net_connections()
            except (OSError, IOError, psutil.Error):
                cpu_percent = 0.0
                memory = type('obj', (object,), {'percent': 0.0})()
                disk = type('obj', (object,), {'percent': 0.0})()
                net_connections = []

            return {
                **self.stats,
                "current_state": self.current_state.value,
                "state_history_size": len(self.state_history),
                # Week 17: Add snapshot-compatible metrics
                "cpu_usage": cpu_percent,
                "memory_usage": memory.percent if hasattr(memory, 'percent') else 0.0,
                "disk_usage": disk.percent if hasattr(disk, 'percent') else 0.0,
                "network_active": len(net_connections) > 0
            }

    def get_state_history(self, limit: int = 10) -> List[Dict[str, Any]]:
        """Get recent state transitions"""
        with self.lock:
            return [
                {
                    "timestamp": t.timestamp,
                    "from_state": t.from_state.value,
                    "to_state": t.to_state.value,
                    "trigger": t.trigger,
                    "duration": t.duration,
                    "success": t.success,
                    "error": t.error_message
                }
                for t in self.state_history[-limit:]
            ]

    # ========================================================================
    # Week 22: Suspend/Resume Support
    # ========================================================================

    def prepare_suspend(self) -> bool:
        """
        Prepare system for suspend (Week 22).

        Transitions to SUSPENDING state and stops resource monitoring.

        Returns:
            True if successful, False if transition invalid
        """
        with self.lock:
            # Save current state for restoration after resume
            self.pre_suspend_state = self.current_state

            # Transition to SUSPENDING
            success = self.transition_to(SystemState.SUSPENDING, trigger="suspend_request")

            if success:
                # Stop resource monitoring during suspend
                self.stop_monitoring()
                print(f"[SystemState] Prepared for suspend (saved state: {self.pre_suspend_state.value})")

            return success

    def complete_resume(self) -> bool:
        """
        Complete resume from suspend (Week 22).

        Transitions through RESUMING to restore pre-suspend state.

        Returns:
            True if successful, False if transition invalid
        """
        with self.lock:
            # Transition to RESUMING
            success = self.transition_to(SystemState.RESUMING, trigger="resume_request")

            if not success:
                print("[SystemState] Failed to transition to RESUMING")
                return False

            # Restart resource monitoring
            self.start_monitoring()

            # Restore pre-suspend state (or ACTIVE if unknown)
            target_state = getattr(self, 'pre_suspend_state', SystemState.ACTIVE)

            print(f"[SystemState] Resuming to {target_state.value}")

            # Transition to target state
            success = self.transition_to(target_state, trigger="resume_restore")

            if success:
                print(f"[SystemState] Resume complete (restored to: {target_state.value})")

            return success

    def serialize(self) -> Dict[str, Any]:
        """
        Serialize state manager for suspend (Week 22).

        Returns:
            dict: State manager state
        """
        with self.lock:
            return {
                'current_state': self.current_state.value,
                'pre_suspend_state': getattr(self, 'pre_suspend_state', SystemState.ACTIVE).value,
                'resource_usage': getattr(self, 'resource_usage', {}).copy(),
                'state_history': [
                    {
                        'timestamp': t.timestamp,
                        'from_state': t.from_state.value,
                        'to_state': t.to_state.value,
                        'trigger': t.trigger,
                        'duration': t.duration,
                        'success': t.success
                    }
                    for t in self.state_history[-10:]  # Last 10 transitions
                ],
                'statistics': {
                    'total_transitions': getattr(self, 'total_transitions', 0),
                    'failed_transitions': getattr(self, 'failed_transitions', 0)
                }
            }

    def deserialize(self, state: Dict[str, Any]):
        """
        Restore state manager from suspend (Week 22).

        Args:
            state: Serialized state manager state
        """
        with self.lock:
            # Restore current state
            try:
                self.current_state = SystemState(state.get('current_state', 'active'))
            except ValueError:
                print(f"[SystemState] Invalid current_state, defaulting to ACTIVE")
                self.current_state = SystemState.ACTIVE

            # Restore pre-suspend state
            try:
                pre_suspend = state.get('pre_suspend_state', 'active')
                self.pre_suspend_state = SystemState(pre_suspend)
            except ValueError:
                print(f"[SystemState] Invalid pre_suspend_state, defaulting to ACTIVE")
                self.pre_suspend_state = SystemState.ACTIVE

            # Restore resource usage
            if 'resource_usage' in state:
                self.resource_usage = state['resource_usage'].copy()

            # Restore statistics
            if 'statistics' in state:
                stats = state['statistics']
                self.total_transitions = stats.get('total_transitions', 0)
                self.failed_transitions = stats.get('failed_transitions', 0)

            print(f"[SystemState] State restored: {self.current_state.value}")

# Example usage
if __name__ == "__main__":
    print("JARVIS AI-OS - System State Manager Test")
    print("="*70)

    # Create state manager
    manager = SystemStateManager()

    print("\n" + "="*70)
    print("Test 1: IDLE -> ACTIVE transition")
    print("="*70)

    success = manager.transition_to(SystemState.ACTIVE, trigger="test")
    if success:
        print(f"[PASS] Transitioned to {manager.get_current_state().value}")
    else:
        print("[FAIL] Transition failed")

    print("\n" + "="*70)
    print("Test 2: ACTIVE -> CRITICAL transition")
    print("="*70)

    success = manager.transition_to(SystemState.CRITICAL, trigger="high_risk_op")
    if success:
        print(f"[PASS] Transitioned to {manager.get_current_state().value}")
    else:
        print("[FAIL] Transition failed")

    print("\n" + "="*70)
    print("Test 3: CRITICAL -> ACTIVE transition")
    print("="*70)

    success = manager.transition_to(SystemState.ACTIVE, trigger="risk_resolved")
    if success:
        print(f"[PASS] Transitioned to {manager.get_current_state().value}")
    else:
        print("[FAIL] Transition failed")

    print("\n" + "="*70)
    print("Test 4: Invalid transition (ACTIVE -> IDLE then IDLE -> CRITICAL)")
    print("="*70)

    manager.transition_to(SystemState.IDLE, trigger="test")
    success = manager.transition_to(SystemState.CRITICAL, trigger="invalid")
    if not success:
        print("[PASS] Invalid transition correctly rejected")
    else:
        print("[FAIL] Invalid transition was allowed")

    print("\n" + "="*70)
    print("Test 5: State statistics")
    print("="*70)

    stats = manager.get_statistics()
    print(f"Total transitions: {stats['total_transitions']}")
    print(f"Successful: {stats['successful_transitions']}")
    print(f"Failed: {stats['failed_transitions']}")
    print(f"Average transition time: {stats['avg_transition_time']:.3f}s")

    print("\nState manager test complete!")
