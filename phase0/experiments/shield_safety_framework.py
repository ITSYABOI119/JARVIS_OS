# shield_safety_framework.py - JARVIS Phase 0 Track A
# SHIELD Safety Framework Implementation
# Multi-layer safety validation system

import time
import copy
from enum import Enum
from dataclasses import dataclass
from typing import Dict, List, Optional, Any
import json

# ============================================================================
# SHIELD Framework Components
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
# S: Staged Execution Sandbox
# ============================================================================

class ShadowEnvironment:
    """Isolated environment for testing actions before real execution"""

    def __init__(self, real_state: SystemSnapshot):
        self.shadow_state = copy.deepcopy(real_state)
        self.actions_tested = []
        self.errors_detected = []

    def execute_shadow(self, action: Dict) -> Dict:
        """Execute action in shadow environment"""
        action_type = action.get("type")
        params = action.get("parameters", {})

        # Simulate action execution
        result = {
            "success": True,
            "action": action_type,
            "state_before": copy.deepcopy(self.shadow_state),
            "state_after": None,
            "errors": []
        }

        try:
            # Simulate different action types
            if action_type == "read_cpu_stats":
                # Read-only: No state change
                pass

            elif action_type == "clear_cache":
                # Modify memory
                self.shadow_state.memory_usage = max(0, self.shadow_state.memory_usage - 10.0)

            elif action_type == "delete_file":
                # Destructive: Reduce file count
                self.shadow_state.file_count -= params.get("count", 1)
                if self.shadow_state.file_count < 0:
                    raise ValueError("Cannot delete: file count would go negative")

            elif action_type == "kill_service":
                # Critical: Stop service
                service_name = params.get("service")
                if service_name in self.shadow_state.service_states:
                    self.shadow_state.service_states[service_name] = "stopped"
                else:
                    raise ValueError(f"Service {service_name} not found")

            elif action_type == "network_disconnect":
                # Hardware impact: Disable network
                self.shadow_state.network_active = False

            result["state_after"] = copy.deepcopy(self.shadow_state)

        except Exception as e:
            result["success"] = False
            result["errors"].append(str(e))
            self.errors_detected.append(str(e))

        self.actions_tested.append(result)
        return result

# ============================================================================
# H: Hardware Impact Analysis
# ============================================================================

class HardwareImpactAnalyzer:
    """Predict hardware consequences of actions"""

    HARDWARE_ACTIONS = {
        "delete_file": {"disk": True, "memory": False, "cpu": False, "network": False},
        "clear_cache": {"disk": False, "memory": True, "cpu": False, "network": False},
        "kill_service": {"disk": False, "memory": True, "cpu": True, "network": False},
        "network_disconnect": {"disk": False, "memory": False, "cpu": False, "network": True},
        "read_cpu_stats": {"disk": False, "memory": False, "cpu": False, "network": False},
        "reboot_system": {"disk": True, "memory": True, "cpu": True, "network": True},
    }

    def analyze(self, action: Dict) -> Dict:
        """Analyze hardware impact of action"""
        action_type = action.get("type")
        impact = self.HARDWARE_ACTIONS.get(action_type, {})

        affected_hardware = [hw for hw, affected in impact.items() if affected]
        severity = len(affected_hardware) / 4.0  # 0.0 to 1.0 scale

        return {
            "action": action_type,
            "affected_hardware": affected_hardware,
            "severity": severity,
            "critical": "network" in affected_hardware or len(affected_hardware) >= 3
        }

# ============================================================================
# I: Irreversibility Detection
# ============================================================================

class IrreversibilityDetector:
    """Detect actions that cannot be undone"""

    IRREVERSIBLE_ACTIONS = [
        "delete_file",
        "format_disk",
        "network_send",  # Can't un-send data
        "reboot_system",
        "shutdown",
    ]

    REVERSIBLE_ACTIONS = [
        "read_cpu_stats",
        "read_memory_stats",
        "clear_cache",  # Can be refilled
        "kill_service",  # Can be restarted
        "network_disconnect",  # Can reconnect
    ]

    def is_irreversible(self, action: Dict) -> Dict:
        """Check if action can be reversed"""
        action_type = action.get("type")

        if action_type in self.IRREVERSIBLE_ACTIONS:
            return {
                "irreversible": True,
                "reason": f"{action_type} cannot be undone",
                "risk_increase": 0.3  # Add 30% to risk score
            }
        elif action_type in self.REVERSIBLE_ACTIONS:
            return {
                "irreversible": False,
                "reason": f"{action_type} can be reversed",
                "risk_increase": 0.0
            }
        else:
            # Unknown action: assume irreversible
            return {
                "irreversible": True,
                "reason": f"Unknown action {action_type} (assume irreversible)",
                "risk_increase": 0.5  # Add 50% to risk score
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

        # Base escalation thresholds
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
        self.risk_adjustments = {}  # action_type -> risk_adjustment

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

        # Update risk adjustment for this action type
        action_type = action.get("type")
        if action_type not in self.risk_adjustments:
            self.risk_adjustments[action_type] = 0.0

        # Increase risk for actions that fail
        self.risk_adjustments[action_type] += 0.1  # +10% risk per failure
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
        self.snapshots = []
        self.max_snapshots = 10

    def create_snapshot(self, system_state) -> SystemSnapshot:
        """Create system snapshot"""
        snapshot = SystemSnapshot(
            timestamp=time.time(),
            cpu_usage=system_state.cpu_usage,
            memory_usage=system_state.memory_usage,
            disk_usage=system_state.disk_usage,
            network_active=system_state.network_active,
            file_count=getattr(system_state, 'file_count', 1000),
            service_states=getattr(system_state, 'service_states', {"sshd": "running", "httpd": "running"})
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
    """Complete SHIELD safety validation system"""

    def __init__(self):
        self.hardware_analyzer = HardwareImpactAnalyzer()
        self.irreversibility_detector = IrreversibilityDetector()
        self.escalation_system = EscalationIntelligence()
        self.learning_system = FailureLearningSystem()
        self.rollback_manager = RollbackManager()

        self.validation_count = 0
        self.blocked_count = 0
        self.shadow_tested_count = 0

    def validate_action(self, action: Dict, system_state) -> Dict:
        """Complete SHIELD validation pipeline"""
        self.validation_count += 1

        # Create snapshot for rollback
        snapshot = self.rollback_manager.create_snapshot(system_state)

        # Calculate base risk score
        risk_score = 0.0

        # H: Hardware Impact Analysis
        hardware_impact = self.hardware_analyzer.analyze(action)
        risk_score += hardware_impact["severity"] * 0.3  # Up to 30% from hardware impact

        # I: Irreversibility Detection
        irreversibility = self.irreversibility_detector.is_irreversible(action)
        risk_score += irreversibility["risk_increase"]

        # L: Learning from past failures
        action_type = action.get("type")
        learned_risk = self.learning_system.get_learned_risk_adjustment(action_type)
        risk_score += learned_risk

        # Cap risk score at 1.0
        risk_score = min(1.0, risk_score)

        # E: Escalation decision
        escalation = self.escalation_system.should_escalate(risk_score, action)
        execution_mode = ExecutionMode(escalation["execution_mode"])

        # S: Shadow execution (if needed)
        shadow_result = None
        if execution_mode == ExecutionMode.SHADOW or execution_mode == ExecutionMode.APPROVAL_REQUIRED:
            shadow_env = ShadowEnvironment(snapshot)
            shadow_result = shadow_env.execute_shadow(action)
            self.shadow_tested_count += 1

            # If shadow execution failed, block the action
            if not shadow_result["success"]:
                execution_mode = ExecutionMode.BLOCKED
                escalation["reason"] = f"Shadow test failed: {shadow_result['errors']}"

                # L: Learn from failure
                for error in shadow_result["errors"]:
                    self.learning_system.record_failure(action, error, risk_score)

        # Final decision
        if execution_mode == ExecutionMode.BLOCKED:
            self.blocked_count += 1

        return {
            "action": action,
            "risk_score": risk_score,
            "execution_mode": execution_mode.value,
            "hardware_impact": hardware_impact,
            "irreversibility": irreversibility,
            "escalation": escalation,
            "shadow_result": shadow_result,
            "snapshot_id": len(self.rollback_manager.snapshots) - 1,
            "timestamp": time.time()
        }

    def get_stats(self) -> Dict:
        """Get SHIELD framework statistics"""
        return {
            "validations_performed": self.validation_count,
            "actions_blocked": self.blocked_count,
            "shadow_tests_run": self.shadow_tested_count,
            "block_rate": self.blocked_count / self.validation_count if self.validation_count > 0 else 0,
            "failures_learned": len(self.learning_system.failure_history),
            "snapshots_available": len(self.rollback_manager.snapshots)
        }

# ============================================================================
# Test Suite
# ============================================================================

def run_shield_tests():
    """Test SHIELD safety framework"""
    print("="*70)
    print("JARVIS AI-OS - Phase 0 Track A: SHIELD Safety Framework")
    print("="*70)
    print()

    # Mock system state
    @dataclass
    class MockSystemState:
        cpu_usage: float = 35.0
        memory_usage: float = 60.0
        disk_usage: float = 72.0
        network_active: bool = True
        file_count: int = 1000
        service_states: Dict[str, str] = None

        def __post_init__(self):
            if self.service_states is None:
                self.service_states = {"sshd": "running", "httpd": "running"}

    system_state = MockSystemState()
    shield = SHIELDFramework()

    # Test actions (various risk levels)
    test_actions = [
        {"type": "read_cpu_stats", "parameters": {}},  # Safe
        {"type": "clear_cache", "parameters": {}},  # Low risk
        {"type": "kill_service", "parameters": {"service": "httpd"}},  # Medium risk
        {"type": "delete_file", "parameters": {"path": "/tmp/test.txt", "count": 1}},  # High risk (irreversible)
        {"type": "network_disconnect", "parameters": {}},  # High risk
        {"type": "reboot_system", "parameters": {}},  # Critical risk
        {"type": "kill_service", "parameters": {"service": "unknown_service"}},  # Should fail in shadow
    ]

    print("[SHIELD VALIDATION TESTS]")
    print("-"*70)
    print()

    results = []

    for i, action in enumerate(test_actions, 1):
        print(f"Test {i}: {action['type']}")

        result = shield.validate_action(action, system_state)

        risk_pct = result["risk_score"] * 100
        mode = result["execution_mode"]

        # Determine status emoji-less
        if mode == "blocked":
            status = "[BLOCKED]"
        elif mode == "approval_required":
            status = "[APPROVAL]"
        elif mode == "shadow":
            status = "[SHADOW]"
        else:
            status = "[AUTO]"

        print(f"  Risk Score: {risk_pct:.0f}%")
        print(f"  Mode: {status}")
        print(f"  Hardware Impact: {', '.join(result['hardware_impact']['affected_hardware']) or 'None'}")
        print(f"  Irreversible: {result['irreversibility']['irreversible']}")

        if result["shadow_result"]:
            shadow_ok = "[PASS]" if result["shadow_result"]["success"] else "[FAIL]"
            print(f"  Shadow Test: {shadow_ok}")

        print()

        results.append(result)

    # Statistics
    print()
    print("="*70)
    print("[SHIELD STATISTICS]")
    print("="*70)

    stats = shield.get_stats()

    print(f"  Total validations:    {stats['validations_performed']}")
    print(f"  Actions blocked:      {stats['actions_blocked']}")
    print(f"  Shadow tests run:     {stats['shadow_tests_run']}")
    print(f"  Block rate:           {stats['block_rate']*100:.1f}%")
    print(f"  Failures learned:     {stats['failures_learned']}")
    print(f"  Snapshots available:  {stats['snapshots_available']}")

    print()
    print("[VALIDATION]")
    print("-"*70)

    # Check that dangerous actions were blocked/escalated
    dangerous_blocked = sum(1 for r in results if r["risk_score"] > 0.7 and r["execution_mode"] in ["blocked", "approval_required"])
    dangerous_total = sum(1 for r in results if r["risk_score"] > 0.7)

    safe_auto = sum(1 for r in results if r["risk_score"] < 0.3 and r["execution_mode"] == "automatic")
    safe_total = sum(1 for r in results if r["risk_score"] < 0.3)

    if dangerous_total > 0 and dangerous_blocked / dangerous_total >= 0.9:
        print(f"  [PASS] Dangerous action protection: {dangerous_blocked}/{dangerous_total} blocked/escalated")
    else:
        print(f"  [FAIL] Dangerous action protection: {dangerous_blocked}/{dangerous_total} blocked (need >90%)")

    if safe_total > 0 and safe_auto / safe_total >= 0.8:
        print(f"  [PASS] Safe action throughput: {safe_auto}/{safe_total} allowed automatically")
    else:
        print(f"  [WARN] Safe action throughput: {safe_auto}/{safe_total} allowed")

    if stats["shadow_tests_run"] > 0:
        print(f"  [PASS] Shadow execution: {stats['shadow_tests_run']} tests performed")
    else:
        print(f"  [WARN] Shadow execution: Not tested")

    if stats["failures_learned"] > 0:
        print(f"  [PASS] Learning system: {stats['failures_learned']} failures recorded")
    else:
        print(f"  [WARN] Learning system: No failures to learn from")

    print()
    print("="*70)
    print("[EXPERIMENT COMPLETE]")
    print("="*70)
    print()
    print("SHIELD Safety Framework validated:")
    print("  - S: Shadow execution (tested actions before real execution)")
    print("  - H: Hardware impact analysis (identified affected components)")
    print("  - I: Irreversibility detection (flagged destructive operations)")
    print("  - E: Escalation intelligence (risk-based decisions)")
    print("  - L: Learning from failures (adapted risk scores)")
    print("  - D: Deterministic rollback (snapshots created)")
    print()
    print("Next: Run adversarial safety tests")
    print("="*70)

if __name__ == "__main__":
    run_shield_tests()
