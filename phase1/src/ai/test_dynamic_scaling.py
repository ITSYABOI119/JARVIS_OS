#!/usr/bin/env python3
"""
JARVIS AI-OS Phase 1 - Dynamic Model Scaling Test Suite
Week 13: System State Manager + Model Loader Tests

Tests the complete dynamic model scaling system with 15 comprehensive test cases.

Author: JARVIS AI-OS Team
Date: November 20, 2025
"""

import time
import sys
from system_state_manager import SystemStateManager, SystemState
from model_loader import ModelLoader

def print_test_header(test_num, description):
    """Print test header"""
    print(f"\n{'='*70}")
    print(f"TEST {test_num}: {description}")
    print(f"{'='*70}")

def test_1_state_manager_initialization():
    """Test 1: State manager initialization"""
    print_test_header(1, "State Manager Initialization")

    manager = SystemStateManager()

    assert manager.get_current_state() == SystemState.IDLE
    assert manager.previous_state is None

    print("[PASS] State manager initialized correctly in IDLE state")
    return True

def test_2_model_loader_initialization():
    """Test 2: Model loader initialization"""
    print_test_header(2, "Model Loader Initialization")

    loader = ModelLoader()

    assert loader.current_model is None
    assert loader.current_state is None

    print("[PASS] Model loader initialized correctly")
    return True

def test_3_idle_to_active_transition():
    """Test 3: IDLE -> ACTIVE state transition"""
    print_test_header(3, "IDLE -> ACTIVE Transition")

    loader = ModelLoader()
    manager = SystemStateManager(model_loader=loader)

    # Transition IDLE -> ACTIVE
    success = manager.transition_to(SystemState.ACTIVE, trigger="user_query")

    assert success == True
    assert manager.get_current_state() == SystemState.ACTIVE
    assert loader.current_model is not None  # Model should be loaded

    print(f"[PASS] IDLE -> ACTIVE transition successful")
    return True

def test_4_active_to_critical_transition():
    """Test 4: ACTIVE -> CRITICAL state transition"""
    print_test_header(4, "ACTIVE -> CRITICAL Transition")

    loader = ModelLoader()
    manager = SystemStateManager(model_loader=loader)

    # Start in ACTIVE
    manager.transition_to(SystemState.ACTIVE, trigger="init")

    # Transition ACTIVE -> CRITICAL
    success = manager.transition_to(SystemState.CRITICAL, trigger="high_risk_op")

    assert success == True
    assert manager.get_current_state() == SystemState.CRITICAL
    assert loader.validator_model is not None  # Validator should be loaded

    print(f"[PASS] ACTIVE -> CRITICAL transition successful")
    return True

def test_5_critical_to_active_transition():
    """Test 5: CRITICAL -> ACTIVE state transition"""
    print_test_header(5, "CRITICAL -> ACTIVE Transition")

    loader = ModelLoader()
    manager = SystemStateManager(model_loader=loader)

    # Start in CRITICAL
    manager.transition_to(SystemState.ACTIVE, trigger="init")
    manager.transition_to(SystemState.CRITICAL, trigger="init")

    # Transition CRITICAL -> ACTIVE
    success = manager.transition_to(SystemState.ACTIVE, trigger="risk_resolved")

    assert success == True
    assert manager.get_current_state() == SystemState.ACTIVE
    # Validator should be unloaded (handled by ModelLoader)

    print(f"[PASS] CRITICAL -> ACTIVE transition successful")
    return True

def test_6_active_to_idle_transition():
    """Test 6: ACTIVE -> IDLE state transition"""
    print_test_header(6, "ACTIVE -> IDLE Transition")

    loader = ModelLoader()
    manager = SystemStateManager(model_loader=loader)

    # Start in ACTIVE
    manager.transition_to(SystemState.ACTIVE, trigger="init")

    # Transition ACTIVE -> IDLE
    success = manager.transition_to(SystemState.IDLE, trigger="idle_timeout")

    assert success == True
    assert manager.get_current_state() == SystemState.IDLE

    print(f"[PASS] ACTIVE -> IDLE transition successful")
    return True

def test_7_emergency_transition():
    """Test 7: Any state -> EMERGENCY transition"""
    print_test_header(7, "Emergency Transition (from ACTIVE)")

    loader = ModelLoader()
    manager = SystemStateManager(model_loader=loader)

    # Start in ACTIVE
    manager.transition_to(SystemState.ACTIVE, trigger="init")

    # Transition to EMERGENCY
    success = manager.transition_to(SystemState.EMERGENCY, trigger="ai_failure")

    assert success == True
    assert manager.get_current_state() == SystemState.EMERGENCY
    assert loader.current_model is None  # No model in emergency

    print(f"[PASS] ACTIVE -> EMERGENCY transition successful")
    return True

def test_8_emergency_recovery():
    """Test 8: EMERGENCY -> IDLE recovery"""
    print_test_header(8, "Emergency Recovery (EMERGENCY -> IDLE)")

    loader = ModelLoader()
    manager = SystemStateManager(model_loader=loader)

    # Start in EMERGENCY
    manager.transition_to(SystemState.ACTIVE, trigger="init")
    manager.transition_to(SystemState.EMERGENCY, trigger="failure")

    # Recover to IDLE
    success = manager.transition_to(SystemState.IDLE, trigger="recovery")

    assert success == True
    assert manager.get_current_state() == SystemState.IDLE
    assert loader.current_model is not None  # Model should be loaded

    print(f"[PASS] EMERGENCY -> IDLE recovery successful")
    return True

def test_9_invalid_transition():
    """Test 9: Invalid state transition rejection"""
    print_test_header(9, "Invalid Transition Rejection (IDLE -> CRITICAL)")

    manager = SystemStateManager()

    # Try invalid transition IDLE -> CRITICAL
    success = manager.transition_to(SystemState.CRITICAL, trigger="invalid")

    assert success == False  # Should be rejected
    assert manager.get_current_state() == SystemState.IDLE  # Should stay in IDLE

    print(f"[PASS] Invalid transition correctly rejected")
    return True

def test_10_state_history():
    """Test 10: State transition history tracking"""
    print_test_header(10, "State Transition History")

    manager = SystemStateManager()

    # Perform multiple transitions
    manager.transition_to(SystemState.ACTIVE, trigger="query_1")
    manager.transition_to(SystemState.IDLE, trigger="timeout_1")
    manager.transition_to(SystemState.ACTIVE, trigger="query_2")

    # Check history
    history = manager.get_state_history(limit=5)

    assert len(history) == 3  # 3 transitions
    assert history[0]["from_state"] == "idle"
    assert history[0]["to_state"] == "active"
    assert history[2]["trigger"] == "query_2"

    print(f"[PASS] State history tracked correctly ({len(history)} transitions)")
    return True

def test_11_statistics_tracking():
    """Test 11: Statistics tracking"""
    print_test_header(11, "Statistics Tracking")

    loader = ModelLoader()
    manager = SystemStateManager(model_loader=loader)

    # Perform transitions
    manager.transition_to(SystemState.ACTIVE, trigger="test")
    manager.transition_to(SystemState.CRITICAL, trigger="test")
    manager.transition_to(SystemState.ACTIVE, trigger="test")

    # Check statistics
    stats = manager.get_statistics()

    assert stats["total_transitions"] == 3
    assert stats["successful_transitions"] == 3
    assert stats["failed_transitions"] == 0
    assert stats["avg_transition_time"] > 0

    # Check model loader stats
    loader_stats = loader.get_statistics()
    assert loader_stats["models_loaded"] == 3  # ACTIVE, CRITICAL, ACTIVE

    print(f"[PASS] Statistics tracked correctly")
    print(f"  Transitions: {stats['total_transitions']}")
    print(f"  Avg time: {stats['avg_transition_time']:.3f}s")
    print(f"  Models loaded: {loader_stats['models_loaded']}")
    return True

def test_12_query_recording():
    """Test 12: Query recording triggers IDLE -> ACTIVE"""
    print_test_header(12, "Query Recording (Auto-Transition)")

    manager = SystemStateManager()

    assert manager.get_current_state() == SystemState.IDLE

    # Record query (should trigger transition)
    manager.record_query()

    assert manager.get_current_state() == SystemState.ACTIVE

    print(f"[PASS] Query recording triggered IDLE -> ACTIVE transition")
    return True

def test_13_model_loading_performance():
    """Test 13: Model loading performance"""
    print_test_header(13, "Model Loading Performance")

    loader = ModelLoader()

    # Measure load times
    load_times = []

    for state in [SystemState.IDLE, SystemState.ACTIVE, SystemState.CRITICAL]:
        start = time.time()
        loader.load_model(state)
        load_time = time.time() - start
        load_times.append((state.value, load_time))
        loader.unload_model(state)

    # Check all loaded in reasonable time (< 5s each in mock mode)
    for state_name, load_time in load_times:
        print(f"  {state_name}: {load_time:.3f}s")
        assert load_time < 5.0, f"{state_name} load time too slow: {load_time:.3f}s"

    print(f"[PASS] All models loaded within performance targets")
    return True

def test_14_state_callbacks():
    """Test 14: State change callbacks"""
    print_test_header(14, "State Change Callbacks")

    manager = SystemStateManager()

    # Register callback
    callback_data = []
    def on_state_change(from_state, to_state, duration):
        callback_data.append((from_state.value, to_state.value, duration))

    manager.register_callback(on_state_change)

    # Perform transitions
    manager.transition_to(SystemState.ACTIVE, trigger="test")
    manager.transition_to(SystemState.IDLE, trigger="test")

    # Check callbacks were called
    assert len(callback_data) == 2
    assert callback_data[0][0] == "idle"
    assert callback_data[0][1] == "active"
    assert callback_data[1][0] == "active"
    assert callback_data[1][1] == "idle"

    print(f"[PASS] State change callbacks working")
    print(f"  Callbacks triggered: {len(callback_data)}")
    return True

def test_15_complete_state_cycle():
    """Test 15: Complete state cycle (IDLE -> ACTIVE -> CRITICAL -> ACTIVE -> IDLE)"""
    print_test_header(15, "Complete State Cycle")

    loader = ModelLoader()
    manager = SystemStateManager(model_loader=loader)

    states_visited = [manager.get_current_state().value]

    # Complete cycle
    manager.transition_to(SystemState.ACTIVE, trigger="query")
    states_visited.append(manager.get_current_state().value)

    manager.transition_to(SystemState.CRITICAL, trigger="high_risk")
    states_visited.append(manager.get_current_state().value)

    manager.transition_to(SystemState.ACTIVE, trigger="risk_resolved")
    states_visited.append(manager.get_current_state().value)

    manager.transition_to(SystemState.IDLE, trigger="timeout")
    states_visited.append(manager.get_current_state().value)

    # Verify complete cycle
    expected = ["idle", "active", "critical", "active", "idle"]
    assert states_visited == expected

    print(f"[PASS] Complete state cycle successful")
    print(f"  Path: {' -> '.join(states_visited)}")
    return True

def main():
    """Run all dynamic scaling tests"""
    print("="*70)
    print("JARVIS AI-OS - Dynamic Model Scaling Test Suite (Week 13)")
    print("="*70)
    print("\n15 test cases covering:")
    print("  - State manager initialization")
    print("  - Model loader initialization")
    print("  - All state transitions")
    print("  - Invalid transition rejection")
    print("  - State history tracking")
    print("  - Statistics tracking")
    print("  - Auto-transitions (query recording)")
    print("  - Performance validation")
    print("  - Callbacks")
    print("  - Complete state cycles")
    print()

    tests = [
        test_1_state_manager_initialization,
        test_2_model_loader_initialization,
        test_3_idle_to_active_transition,
        test_4_active_to_critical_transition,
        test_5_critical_to_active_transition,
        test_6_active_to_idle_transition,
        test_7_emergency_transition,
        test_8_emergency_recovery,
        test_9_invalid_transition,
        test_10_state_history,
        test_11_statistics_tracking,
        test_12_query_recording,
        test_13_model_loading_performance,
        test_14_state_callbacks,
        test_15_complete_state_cycle
    ]

    passed = 0
    failed = 0

    for test_func in tests:
        try:
            if test_func():
                passed += 1
            else:
                failed += 1
                print(f"[FAIL] {test_func.__name__}")
        except Exception as e:
            failed += 1
            print(f"[FAIL] {test_func.__name__}: {e}")
            import traceback
            traceback.print_exc()

    # Summary
    print("\n" + "="*70)
    print("TEST SUMMARY")
    print("="*70)
    total = passed + failed
    pass_rate = (passed / total * 100) if total > 0 else 0

    print(f"\nTotal Tests: {total}")
    print(f"Passed: {passed} ({pass_rate:.1f}%)")
    print(f"Failed: {failed}")

    if failed == 0:
        print("\n[SUCCESS] All dynamic scaling tests passed!")
    else:
        print(f"\n[WARNING] {failed} test(s) failed")

    print("="*70)

    return failed == 0

if __name__ == "__main__":
    success = main()
    sys.exit(0 if success else 1)
