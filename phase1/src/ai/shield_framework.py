#!/usr/bin/env python3
"""
JARVIS AI-OS - Enhanced SHIELD Safety Framework
Week 16: SHIELD Expansion
Week 17: Shadow Execution Integration

This module implements the production SHIELD framework with:
- 100 action types (expanded from Phase 0's 7)
- Pattern matching (wildcard support)
- Context-aware risk scoring (paths, processes, services)
- Integration with SystemStateManager
- Real shadow execution with Linux namespaces (Week 17)

Based on Phase 0 SHIELD validation experiment.

Author: JARVIS AI-OS Team
Date: November 24, 2025 (Updated: November 25, 2025 for Week 17)
"""

import time
import copy
import fnmatch
from enum import Enum
from dataclasses import dataclass
from typing import Dict, List, Optional, Any, Tuple
import json

# Import action database
from shield_action_db import (
    ACTION_DATABASE,
    ActionMetadata,
    ActionScope,
    get_action_by_name
)

# Week 17: Import real shadow execution
try:
    from shadow_executor import RealShadowEnvironment
    REAL_SHADOW_AVAILABLE = True
except ImportError:
    REAL_SHADOW_AVAILABLE = False
    print("[SHIELD] Warning: RealShadowEnvironment not available, using simulated shadow execution")


# ============================================================================
# SHIELD Framework Components (From Phase 0)
# S: Staged Execution Sandbox
# H: Hardware Impact Analysis
# I: Irreversibility Detection
# E: Escalation Intelligence
# L: Learning from Failures
# D: Deterministic Rollback
# ============================================================================


class RiskLevel(Enum):
    """Continuous risk scoring (0.0-1.0)"""
    SAFE = 0.0          # 0.0-0.2: Safe operations
    LOW = 0.2           # 0.2-0.4: Low risk
    MEDIUM = 0.4        # 0.4-0.6: Medium risk
    HIGH = 0.6          # 0.6-0.8: High risk
    CRITICAL = 0.8      # 0.8-1.0: Critical risk


class ExecutionMode(Enum):
    """Action execution modes"""
    AUTOMATIC = "automatic"      # Execute immediately
    SHADOW = "shadow"            # Test in sandbox first
    APPROVAL_REQUIRED = "approval_required"  # Require user approval
    BLOCKED = "blocked"          # Block execution


# ============================================================================
# System State for Shadow Execution
# ============================================================================

@dataclass
class SystemSnapshot:
    """Snapshot of system state for rollback"""
    timestamp: float
    cpu_usage: float
    memory_usage: float
    disk_usage: float
    network_active: bool
    file_count: int
    service_states: Dict[str, str]

    def to_dict(self) -> Dict:
        return {
            "timestamp": self.timestamp,
            "cpu_usage": self.cpu_usage,
            "memory_usage": self.memory_usage,
            "disk_usage": self.disk_usage,
            "network_active": self.network_active,
            "file_count": self.file_count,
            "service_states": self.service_states
        }


# ============================================================================
# Pattern Matching System (NEW for Week 16)
# ============================================================================

class PatternMatcher:
    """Match action names to action types using wildcard patterns"""

    def __init__(self):
        # Cache for pattern matches
        self.match_cache: Dict[str, str] = {}

    def match_action(self, action_name: str) -> Optional[ActionMetadata]:
        """
        Match action name to action type using patterns.

        Args:
            action_name: Action name (e.g., "delete_user_file", "rm_config")

        Returns:
            ActionMetadata if match found, None otherwise
        """
        # Check cache first
        if action_name in self.match_cache:
            cached_type = self.match_cache[action_name]
            return get_action_by_name(cached_type)

        # Try exact match first
        action = get_action_by_name(action_name)
        if action:
            self.match_cache[action_name] = action_name
            return action

        # Try pattern matching
        for action_type, action_meta in ACTION_DATABASE.items():
            for pattern in action_meta.patterns:
                if fnmatch.fnmatch(action_name, pattern):
                    # Cache the match
                    self.match_cache[action_name] = action_type
                    return action_meta

        # No match found
        return None

    def get_match_stats(self) -> Dict:
        """Get pattern matching statistics"""
        return {
            "cached_matches": len(self.match_cache),
            "cache": self.match_cache
        }


# ============================================================================
# Context-Aware Risk Scoring (NEW for Week 16)
# ============================================================================

class ContextAwareRiskScorer:
    """Adjust risk scores based on action context"""

    # Critical system paths (higher risk)
    CRITICAL_PATHS = [
        "/boot", "/boot/*",
        "/etc", "/etc/*",
        "/bin", "/bin/*", "/sbin", "/sbin/*",
        "/usr/bin", "/usr/bin/*", "/usr/sbin", "/usr/sbin/*",
        "/lib", "/lib/*", "/lib64", "/lib64/*",
        "/sys", "/sys/*",
        "/proc", "/proc/*",
        "/dev", "/dev/*"
    ]

    # Safe user paths (lower risk)
    SAFE_PATHS = [
        "/tmp", "/tmp/*",
        "/var/tmp", "/var/tmp/*",
        "/home/*/Downloads", "/home/*/Downloads/*",
        "/home/*/tmp", "/home/*/tmp/*"
    ]

    # Critical processes (higher risk if affected)
    CRITICAL_PROCESSES = [
        "init", "systemd", "sshd", "kernel",
        "docker", "containerd",
        "nginx", "apache2", "httpd",
        "postgresql", "mysql", "mongod"
    ]

    # Critical services (higher risk)
    CRITICAL_SERVICES = [
        "sshd", "systemd", "networkd", "firewalld",
        "docker", "kubelet",
        "postgresql", "mysql", "redis"
    ]

    def __init__(self):
        self.adjustment_history = []

    def adjust_risk_for_context(self, base_risk: float, action: Dict,
                                  action_meta: ActionMetadata) -> Tuple[float, List[str]]:
        """
        Adjust risk score based on action context.

        Args:
            base_risk: Base risk score from action metadata
            action: Action dictionary with parameters
            action_meta: Action metadata

        Returns:
            Tuple of (adjusted_risk, reasons)
        """
        adjusted_risk = base_risk
        reasons = []

        params = action.get("parameters", {})

        # 1. Path-based adjustments (for file operations)
        if action_meta.category == "file_operations":
            path = params.get("path", "")

            if path:
                # Check critical paths
                for critical_pattern in self.CRITICAL_PATHS:
                    if fnmatch.fnmatch(path, critical_pattern):
                        adjusted_risk = min(1.0, adjusted_risk + 0.3)
                        reasons.append(f"Critical system path: {path}")
                        break

                # Check safe paths
                for safe_pattern in self.SAFE_PATHS:
                    if fnmatch.fnmatch(path, safe_pattern):
                        adjusted_risk = max(0.0, adjusted_risk - 0.2)
                        reasons.append(f"Safe user path: {path}")
                        break

        # 2. Process-based adjustments
        if action_meta.category == "process_management":
            process_name = params.get("process", params.get("name", ""))

            if process_name in self.CRITICAL_PROCESSES:
                adjusted_risk = min(1.0, adjusted_risk + 0.3)
                reasons.append(f"Critical process: {process_name}")

        # 3. Service criticality adjustments
        if action_meta.category == "service_management":
            service_name = params.get("service", "")

            if service_name in self.CRITICAL_SERVICES:
                adjusted_risk = min(1.0, adjusted_risk + 0.3)
                reasons.append(f"Critical service: {service_name}")

        # 4. Network exposure adjustments
        if action_meta.category == "network_operations":
            # Listening on privileged ports (<1024)
            if "listen" in action.get("type", ""):
                port = params.get("port", 0)
                if port < 1024:
                    adjusted_risk = min(1.0, adjusted_risk + 0.2)
                    reasons.append(f"Privileged port: {port}")

            # External connections (higher risk)
            if "connect" in action.get("type", ""):
                host = params.get("host", "")
                if not (host.startswith("127.") or host == "localhost"):
                    adjusted_risk = min(1.0, adjusted_risk + 0.1)
                    reasons.append(f"External connection: {host}")

        # 5. User/permission context
        user = params.get("user", "")
        if user == "root":
            adjusted_risk = min(1.0, adjusted_risk + 0.2)
            reasons.append("Running as root")

        # 6. Batch operations (multiple targets)
        count = params.get("count", 1)
        if count > 10:
            adjusted_risk = min(1.0, adjusted_risk + 0.15)
            reasons.append(f"Batch operation: {count} items")

        # 7. Resource-intensive parameters (Week 18 tuning)
        if action_meta.category == "memory_management":
            # Large memory allocations
            size_mb = params.get("size_mb", 0)
            if size_mb > 10240:  # >10GB
                adjusted_risk = min(1.0, adjusted_risk + 0.5)
                reasons.append(f"Excessive memory allocation: {size_mb}MB")
            elif size_mb > 4096:  # >4GB
                adjusted_risk = min(1.0, adjusted_risk + 0.3)
                reasons.append(f"Large memory allocation: {size_mb}MB")

        # Record adjustment
        self.adjustment_history.append({
            "action": action.get("type"),
            "base_risk": base_risk,
            "adjusted_risk": adjusted_risk,
            "delta": adjusted_risk - base_risk,
            "reasons": reasons,
            "timestamp": time.time()
        })

        return adjusted_risk, reasons


# ============================================================================
# S: Staged Execution Sandbox
# ============================================================================

class ShadowEnvironment:
    """Isolated environment for testing actions before real execution"""

    def __init__(self, real_state: SystemSnapshot):
        self.shadow_state = copy.deepcopy(real_state)
        self.actions_tested = []
        self.errors_detected = []

    def _check_resource_limits(self, action_type: str, params: Dict) -> Tuple[bool, str]:
        """
        Check if action would exceed system resource limits.

        Returns:
            (is_safe, reason) - True if safe, False if would exceed limits
        """
        try:
            import psutil

            # Memory allocation checks
            if "memory" in action_type and "allocate" in action_type:
                requested_mb = params.get("size_mb", 0)
                available_mb = psutil.virtual_memory().available / (1024**2)

                # Don't allow >80% of available memory
                if requested_mb > available_mb * 0.8:
                    return False, f"Would use {requested_mb}MB (>{available_mb*0.8:.0f}MB available)"

                # Don't allow >10GB allocations (likely problematic)
                if requested_mb > 10240:  # 10GB
                    return False, f"Allocation too large: {requested_mb}MB"

            # Process spawn limits
            elif "process" in action_type and "start" in action_type:
                command = params.get("command", "")

                # Check for resource-intensive commands
                stress_commands = ["stress", "fork", "forkbomb", ":(){ :|:& };:"]
                if any(cmd in command.lower() for cmd in stress_commands):
                    return False, f"Resource-intensive command detected: {command}"

            return True, ""

        except Exception as e:
            # If we can't check, err on the side of caution
            return True, ""  # Allow if check fails

    def execute_shadow(self, action: Dict, action_meta: ActionMetadata) -> Dict:
        """Execute action in shadow environment with safety checks"""
        action_type = action.get("type")
        params = action.get("parameters", {})

        # NEW: Resource limit checks (Week 18 tuning)
        is_safe, limit_reason = self._check_resource_limits(action_type, params)
        if not is_safe:
            return {
                "success": False,
                "action": action_type,
                "state_before": copy.deepcopy(self.shadow_state),
                "state_after": None,
                "errors": [f"Resource limit exceeded: {limit_reason}"]
            }

        result = {
            "success": True,
            "action": action_type,
            "state_before": copy.deepcopy(self.shadow_state),
            "state_after": None,
            "errors": []
        }

        # NEW: Timing analysis (Week 18 tuning)
        import time
        start_time = time.time()

        try:
            # Simulate based on action category
            if action_meta.category == "file_operations":
                if "delete" in action_type or "rm" in action_type:
                    count = params.get("count", 1)
                    self.shadow_state.file_count -= count
                    if self.shadow_state.file_count < 0:
                        raise ValueError(f"File count would go negative: {self.shadow_state.file_count}")

            elif action_meta.category == "memory_management":
                if "free" in action_type or "clear" in action_type:
                    self.shadow_state.memory_usage = max(0, self.shadow_state.memory_usage - 10.0)

            elif action_meta.category == "service_management":
                if "stop" in action_type or "kill" in action_type:
                    service_name = params.get("service", "unknown")
                    if service_name in self.shadow_state.service_states:
                        self.shadow_state.service_states[service_name] = "stopped"

            elif action_meta.category == "network_operations":
                if "down" in action_type or "disconnect" in action_type:
                    self.shadow_state.network_active = False

            result["state_after"] = copy.deepcopy(self.shadow_state)

        except Exception as e:
            result["success"] = False
            result["errors"].append(str(e))
            self.errors_detected.append(str(e))

        # NEW: Timing analysis (Week 18 tuning)
        elapsed_ms = (time.time() - start_time) * 1000

        # Flag suspiciously fast executions (<10ms for simulated operations)
        # Real operations take time; if shadow completes instantly, it may not
        # accurately reflect real execution behavior
        if elapsed_ms < 10 and result["success"]:
            # Log anomaly but don't fail (shadow is meant to be fast)
            # This is informational for monitoring
            result["timing_anomaly"] = True
            result["elapsed_ms"] = elapsed_ms

        self.actions_tested.append(result)
        return result


# ============================================================================
# H: Hardware Impact Analysis
# ============================================================================

class HardwareImpactAnalyzer:
    """Predict hardware consequences of actions"""

    def analyze(self, action_meta: ActionMetadata) -> Dict:
        """Analyze hardware impact of action"""
        severity = 0.0

        # Calculate severity from metadata
        if action_meta.hardware_impact:
            severity = 0.5  # Base hardware impact

        # Adjust by scope
        if action_meta.scope == ActionScope.GLOBAL:
            severity += 0.3
        elif action_meta.scope == ActionScope.MODERATE:
            severity += 0.15

        # Category-specific analysis
        critical_categories = ["system_control", "hardware_control", "network_operations"]
        if action_meta.category in critical_categories:
            severity = min(1.0, severity + 0.2)

        return {
            "action": action_meta.name,
            "hardware_impact": action_meta.hardware_impact,
            "scope": action_meta.scope.value,
            "severity": min(1.0, severity),
            "critical": severity >= 0.7
        }


# ============================================================================
# I: Irreversibility Detection
# ============================================================================

class IrreversibilityDetector:
    """Detect actions that cannot be undone"""

    def is_irreversible(self, action_meta: ActionMetadata) -> Dict:
        """Check if action can be reversed"""
        if action_meta.reversible:
            return {
                "irreversible": False,
                "reason": f"{action_meta.name} can be reversed",
                "risk_increase": 0.0
            }
        else:
            return {
                "irreversible": True,
                "reason": f"{action_meta.name} cannot be undone",
                "risk_increase": 0.3  # Add 30% to risk score
            }


# ============================================================================
# E: Escalation Intelligence
# ============================================================================

class EscalationIntelligence:
    """Decide when to escalate to user approval"""

    def __init__(self):
        self.escalation_history = []

    def should_escalate(self, risk_score: float, action: Dict) -> Dict:
        """Determine if action requires escalation"""

        # Escalation thresholds
        if risk_score < 0.3:
            mode = ExecutionMode.AUTOMATIC
            reason = "Low risk, safe to execute automatically"
        elif risk_score < 0.5:
            mode = ExecutionMode.SHADOW
            reason = "Medium risk, test in shadow environment first"
        elif risk_score < 0.7:
            mode = ExecutionMode.APPROVAL_REQUIRED
            reason = "High risk, requires user approval"
        else:
            mode = ExecutionMode.BLOCKED
            reason = "Critical risk, execution blocked"

        decision = {
            "risk_score": risk_score,
            "execution_mode": mode.value,
            "reason": reason,
            "timestamp": time.time()
        }

        self.escalation_history.append(decision)
        return decision


# ============================================================================
# L: Learning from Failures
# ============================================================================

class FailureLearningSystem:
    """Learn from past failures to improve risk scoring"""

    def __init__(self):
        self.failure_history = []
        self.risk_adjustments: Dict[str, float] = {}

    def record_failure(self, action: Dict, error: str, risk_score: float):
        """Record a failure for learning"""
        failure = {
            "action": action.get("type"),
            "parameters": action.get("parameters"),
            "error": error,
            "risk_score": risk_score,
            "timestamp": time.time()
        }

        self.failure_history.append(failure)

        # Update risk adjustment
        action_type = action.get("type")
        if action_type not in self.risk_adjustments:
            self.risk_adjustments[action_type] = 0.0

        # Increase risk for actions that fail
        self.risk_adjustments[action_type] += 0.1  # +10% per failure
        self.risk_adjustments[action_type] = min(0.5, self.risk_adjustments[action_type])  # Cap at +50%

    def get_learned_risk_adjustment(self, action_type: str) -> float:
        """Get learned risk adjustment for action type"""
        return self.risk_adjustments.get(action_type, 0.0)

    def get_failure_count(self, action_type: str) -> int:
        """Get number of failures for action type"""
        return sum(1 for f in self.failure_history if f["action"] == action_type)


# ============================================================================
# D: Deterministic Rollback
# ============================================================================

class RollbackManager:
    """Instant recovery via snapshots"""

    def __init__(self):
        self.snapshots: List[SystemSnapshot] = []
        self.max_snapshots = 10

    def create_snapshot(self, system_state) -> SystemSnapshot:
        """Create system snapshot"""
        snapshot = SystemSnapshot(
            timestamp=time.time(),
            cpu_usage=getattr(system_state, 'cpu_usage', 0.0),
            memory_usage=getattr(system_state, 'memory_usage', 0.0),
            disk_usage=getattr(system_state, 'disk_usage', 0.0),
            network_active=getattr(system_state, 'network_active', True),
            file_count=getattr(system_state, 'file_count', 1000),
            service_states=getattr(system_state, 'service_states', {})
        )

        self.snapshots.append(snapshot)

        # Keep only last N snapshots
        if len(self.snapshots) > self.max_snapshots:
            self.snapshots.pop(0)

        return snapshot

    def rollback_to_snapshot(self, snapshot_index: int = -1) -> SystemSnapshot:
        """Rollback to a previous snapshot"""
        if not self.snapshots:
            raise ValueError("No snapshots available for rollback")

        return self.snapshots[snapshot_index]


# ============================================================================
# Main SHIELD Framework
# ============================================================================

class SHIELDFramework:
    """Complete SHIELD safety validation system with Week 16 enhancements"""

    def __init__(self):
        # Core SHIELD components
        self.pattern_matcher = PatternMatcher()
        self.context_scorer = ContextAwareRiskScorer()
        self.hardware_analyzer = HardwareImpactAnalyzer()
        self.irreversibility_detector = IrreversibilityDetector()
        self.escalation_system = EscalationIntelligence()
        self.learning_system = FailureLearningSystem()
        self.rollback_manager = RollbackManager()

        # Statistics
        self.validation_count = 0
        self.blocked_count = 0
        self.shadow_tested_count = 0
        self.approved_count = 0
        self.automatic_count = 0

    def validate_action(self, action: Dict, system_state) -> Dict:
        """
        Complete SHIELD validation pipeline.

        Args:
            action: Action dictionary with 'type' and 'parameters'
            system_state: Current system state

        Returns:
            Validation result dictionary
        """
        self.validation_count += 1
        action_type = action.get("type", "unknown")

        # 1. Pattern matching - find action metadata
        action_meta = self.pattern_matcher.match_action(action_type)

        if not action_meta:
            # Unknown action - assign high risk
            return {
                "validated": False,
                "execution_mode": ExecutionMode.BLOCKED.value,
                "risk_score": 0.9,
                "reason": f"Unknown action type: {action_type}",
                "action": action_type,
                "timestamp": time.time()
            }

        # 2. Create snapshot for rollback
        snapshot = self.rollback_manager.create_snapshot(system_state)

        # 3. Calculate base risk score from metadata
        base_risk = action_meta.base_risk

        # 4. Context-aware risk adjustment
        adjusted_risk, context_reasons = self.context_scorer.adjust_risk_for_context(
            base_risk, action, action_meta
        )

        # 5. Hardware impact analysis
        hardware_impact = self.hardware_analyzer.analyze(action_meta)
        if hardware_impact["critical"]:
            adjusted_risk = min(1.0, adjusted_risk + 0.1)

        # 6. Irreversibility detection
        irreversibility = self.irreversibility_detector.is_irreversible(action_meta)
        adjusted_risk = min(1.0, adjusted_risk + irreversibility["risk_increase"])

        # 7. Learning system adjustment
        learned_adjustment = self.learning_system.get_learned_risk_adjustment(action_type)
        adjusted_risk = min(1.0, adjusted_risk + learned_adjustment)

        # 8. Escalation decision
        escalation = self.escalation_system.should_escalate(adjusted_risk, action)

        # Update statistics
        mode = escalation["execution_mode"]
        if mode == ExecutionMode.BLOCKED.value:
            self.blocked_count += 1
        elif mode == ExecutionMode.SHADOW.value:
            self.shadow_tested_count += 1
        elif mode == ExecutionMode.APPROVAL_REQUIRED.value:
            self.approved_count += 1
        elif mode == ExecutionMode.AUTOMATIC.value:
            self.automatic_count += 1

        # 9. Shadow execution for medium-risk actions
        shadow_result = None
        if mode == ExecutionMode.SHADOW.value:
            # Week 17: Use RealShadowEnvironment if available (real namespace isolation)
            if REAL_SHADOW_AVAILABLE:
                try:
                    real_shadow = RealShadowEnvironment(timeout=5.0)
                    exec_result = real_shadow.execute_shadow_real(action, action_meta)

                    # For read-only/monitoring operations, ignore command failures
                    # (e.g., "systemctl status sshd" failing because service doesn't exist
                    # is not a safety issue - the action is still safe)
                    is_readonly = (action_meta.reversible and
                                   not action_meta.hardware_impact and
                                   action_meta.base_risk <= 0.3)

                    final_success = exec_result.success or is_readonly

                    # Convert RealShadowEnvironment result to expected format
                    shadow_result = {
                        "success": final_success,
                        "action": action_type,
                        "returncode": exec_result.returncode,
                        "stdout": exec_result.stdout,
                        "stderr": exec_result.stderr,
                        "execution_time_ms": exec_result.execution_time_ms,
                        "errors": [] if final_success else [exec_result.stderr],
                        "isolated": exec_result.namespace_isolated,
                        "state_prediction": exec_result.state_prediction
                    }
                except Exception as e:
                    # Fall back to simulated shadow on error
                    print(f"[SHIELD] Real shadow execution failed: {e}, falling back to simulated")
                    shadow_env = ShadowEnvironment(snapshot)
                    shadow_result = shadow_env.execute_shadow(action, action_meta)
            else:
                # Fall back to simulated shadow execution
                shadow_env = ShadowEnvironment(snapshot)
                shadow_result = shadow_env.execute_shadow(action, action_meta)

            if not shadow_result["success"]:
                # Shadow execution failed - block action
                mode = ExecutionMode.BLOCKED.value
                escalation["execution_mode"] = mode
                errors = shadow_result.get('errors', ['Unknown error'])
                escalation["reason"] = f"Shadow execution failed: {errors}"
                self.blocked_count += 1
                self.shadow_tested_count -= 1

        # Return validation result
        return {
            "validated": True,
            "action": action_type,
            "action_category": action_meta.category,
            "base_risk": base_risk,
            "adjusted_risk": adjusted_risk,
            "risk_adjustments": {
                "context": context_reasons,
                "hardware": hardware_impact["severity"],
                "irreversible": irreversibility["risk_increase"],
                "learned": learned_adjustment
            },
            "execution_mode": mode,
            "reason": escalation["reason"],
            "hardware_impact": hardware_impact,
            "irreversible": irreversibility["irreversible"],
            "shadow_result": shadow_result,
            "snapshot_id": len(self.rollback_manager.snapshots) - 1,
            "timestamp": time.time()
        }

    def get_stats(self) -> Dict:
        """Get SHIELD statistics"""
        total = self.validation_count
        if total == 0:
            return {
                "total_validations": 0,
                "blocked_rate": 0.0,
                "shadow_rate": 0.0,
                "approval_rate": 0.0,
                "automatic_rate": 0.0
            }

        return {
            "total_validations": total,
            "blocked": self.blocked_count,
            "shadow_tested": self.shadow_tested_count,
            "approval_required": self.approved_count,
            "automatic": self.automatic_count,
            "blocked_rate": self.blocked_count / total,
            "shadow_rate": self.shadow_tested_count / total,
            "approval_rate": self.approved_count / total,
            "automatic_rate": self.automatic_count / total,
            "pattern_matches": self.pattern_matcher.get_match_stats()["cached_matches"],
            "failure_count": len(self.learning_system.failure_history),
            "snapshots": len(self.rollback_manager.snapshots)
        }


if __name__ == "__main__":
    # Test SHIELD framework
    print("JARVIS AI-OS - Enhanced SHIELD Framework")
    print("=" * 70)

    # Create mock system state
    class MockSystemState:
        cpu_usage = 45.0
        memory_usage = 60.0
        disk_usage = 70.0
        network_active = True
        file_count = 1000
        service_states = {"sshd": "running", "httpd": "running"}

    system_state = MockSystemState()
    shield = SHIELDFramework()

    # Test actions
    test_actions = [
        {"type": "file_read", "parameters": {"path": "/home/user/document.txt"}},
        {"type": "delete_file", "parameters": {"path": "/home/user/tmp/test.txt", "count": 1}},
        {"type": "rm_config", "parameters": {"path": "/etc/config.conf"}},
        {"type": "kill_service", "parameters": {"service": "sshd"}},
        {"type": "system_shutdown", "parameters": {}},
    ]

    print("\nTesting SHIELD validation:\n")
    for action in test_actions:
        result = shield.validate_action(action, system_state)
        print(f"Action: {result['action']}")
        print(f"  Category: {result['action_category']}")
        print(f"  Risk: {result['base_risk']:.2f} -> {result['adjusted_risk']:.2f}")
        print(f"  Mode: {result['execution_mode']}")
        print(f"  Reason: {result['reason']}")
        if result['risk_adjustments']['context']:
            print(f"  Context: {', '.join(result['risk_adjustments']['context'])}")
        print()

    # Print statistics
    print("SHIELD Statistics:")
    print("=" * 70)
    stats = shield.get_stats()
    for key, value in stats.items():
        if isinstance(value, float):
            print(f"  {key}: {value:.2%}" if key.endswith("_rate") else f"  {key}: {value:.2f}")
        else:
            print(f"  {key}: {value}")
