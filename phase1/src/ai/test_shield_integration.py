#!/usr/bin/env python3
"""
JARVIS AI-OS - SHIELD + SystemStateManager Integration Test
Week 16: SHIELD Expansion

This script demonstrates how SHIELD integrates with SystemStateManager
to trigger CRITICAL state transitions for high-risk operations.

Integration flow:
1. User query arrives → SHIELD validation
2. If risk_score >= 0.6 → Trigger CRITICAL state
3. CRITICAL state loads Phi-3 + Validator
4. Dual validation occurs
5. After operation → Return to ACTIVE state

Author: JARVIS AI-OS Team
Date: November 24, 2025
"""

import sys
import time
from shield_framework import SHIELDFramework, ExecutionMode
from system_state_manager import SystemStateManager, SystemState
from model_loader import ModelLoader


# ============================================================================
# Mock System State for Testing
# ============================================================================

class MockSystemState:
    """Mock system state for SHIELD"""
    cpu_usage = 45.0
    memory_usage = 60.0
    disk_usage = 70.0
    network_active = True
    file_count = 1000
    service_states = {"sshd": "running", "httpd": "running"}


# ============================================================================
# Integration Test
# ============================================================================

def test_shield_integration():
    """Test SHIELD integration with SystemStateManager"""
    print("=" * 70)
    print("JARVIS AI-OS - SHIELD + SystemStateManager Integration Test")
    print("=" * 70)
    print()

    # Initialize components
    shield = SHIELDFramework()
    model_loader = ModelLoader()
    state_manager = SystemStateManager(model_loader=model_loader)
    system_state = MockSystemState()

    # Start in IDLE state
    print(f"Initial state: {state_manager.current_state.value.upper()}")
    print()

    # Test scenarios
    test_scenarios = [
        {
            "name": "Safe Operation (file read)",
            "action": {"type": "file_read", "parameters": {"path": "/home/user/doc.txt"}},
            "expected_mode": ExecutionMode.AUTOMATIC.value,
            "expected_state": SystemState.ACTIVE,  # Transition to ACTIVE for query
            "description": "Safe operation should not trigger CRITICAL state"
        },
        {
            "name": "High-Risk Operation (delete /etc file)",
            "action": {"type": "delete_file", "parameters": {"path": "/etc/important.conf"}},
            "expected_mode": ExecutionMode.BLOCKED.value,
            "expected_state": SystemState.ACTIVE,  # Blocked, no state change
            "description": "Blocked operation should not change state"
        },
        {
            "name": "Medium-Risk Operation (service restart)",
            "action": {"type": "service_restart", "parameters": {"service": "nginx"}},
            "expected_mode": ExecutionMode.APPROVAL_REQUIRED.value,
            "expected_state": SystemState.CRITICAL,  # Should trigger CRITICAL
            "description": "Approval-required operation should trigger CRITICAL state"
        },
        {
            "name": "Critical Operation (service stop sshd)",
            "action": {"type": "service_stop", "parameters": {"service": "sshd"}},
            "expected_mode": ExecutionMode.BLOCKED.value,
            "expected_state": SystemState.ACTIVE,  # Blocked, no state change
            "description": "Critical SSH service stop should be blocked"
        },
    ]

    print("Running integration test scenarios:")
    print("-" * 70)
    print()

    for i, scenario in enumerate(test_scenarios, 1):
        print(f"Scenario {i}: {scenario['name']}")
        print(f"  Description: {scenario['description']}")
        print(f"  Action: {scenario['action']['type']}")

        # SHIELD validation
        result = shield.validate_action(scenario['action'], system_state)
        mode = result['execution_mode']
        risk = result.get('adjusted_risk', result.get('risk_score', 0.0))

        print(f"  SHIELD Result:")
        print(f"    Risk score: {risk:.2f}")
        print(f"    Execution mode: {mode}")
        print(f"    Expected mode: {scenario['expected_mode']}")
        print(f"    Match: {'PASS' if mode == scenario['expected_mode'] else 'FAIL'}")

        # State transition logic
        current_before = state_manager.current_state
        should_transition = False

        # Determine if state transition is needed
        if mode == ExecutionMode.APPROVAL_REQUIRED.value and risk >= 0.5:
            # High-risk operations trigger CRITICAL state
            if current_before != SystemState.CRITICAL:
                print(f"  Triggering CRITICAL state (risk={risk:.2f})...")
                success = state_manager.transition_to(SystemState.CRITICAL, trigger="shield_high_risk")
                should_transition = True
        elif mode == ExecutionMode.AUTOMATIC.value or mode == ExecutionMode.SHADOW.value:
            # Low/medium risk - ensure ACTIVE state
            if current_before == SystemState.IDLE:
                print(f"  Transitioning to ACTIVE state...")
                success = state_manager.transition_to(SystemState.ACTIVE, trigger="user_query")

        current_after = state_manager.current_state
        print(f"  State: {current_before.value.upper()} -> {current_after.value.upper()}")

        # Simulate operation completion for CRITICAL state
        if current_after == SystemState.CRITICAL:
            print(f"  [Simulating dual validation in CRITICAL state...]")
            time.sleep(0.1)  # Simulate validation time
            print(f"  [Returning to ACTIVE state...]")
            state_manager.transition_to(SystemState.ACTIVE, trigger="risk_resolved")

        print()

    print("-" * 70)
    print("Integration Test Summary:")
    print("-" * 70)

    stats = shield.get_stats()
    state_stats = state_manager.stats  # Access stats dict directly

    print(f"\nSHIELD Statistics:")
    print(f"  Total validations: {stats['total_validations']}")
    print(f"  Automatic: {stats['automatic']} ({stats['automatic_rate']*100:.1f}%)")
    print(f"  Approval required: {stats['approval_required']} ({stats['approval_rate']*100:.1f}%)")
    print(f"  Blocked: {stats['blocked']} ({stats['blocked_rate']*100:.1f}%)")

    print(f"\nSystemStateManager Statistics:")
    print(f"  Total transitions: {state_stats['total_transitions']}")
    print(f"  Successful: {state_stats['successful_transitions']}")
    print(f"  Average transition time: {state_stats['avg_transition_time']:.3f}s")

    print()
    print("=" * 70)
    print("INTEGRATION TEST COMPLETE")
    print("=" * 70)
    print()
    print("Key Points:")
    print("  1. SHIELD validates all actions before execution")
    print("  2. High-risk operations (approval_required) trigger CRITICAL state")
    print("  3. CRITICAL state loads Phi-3 + Validator for dual validation")
    print("  4. After operation, system returns to ACTIVE state")
    print("  5. Blocked operations do not trigger state changes")


if __name__ == "__main__":
    try:
        test_shield_integration()
    except Exception as e:
        print(f"ERROR: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)
