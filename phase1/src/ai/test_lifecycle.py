#!/usr/bin/env python3
"""
JARVIS AI-OS Phase 1 - Lifecycle Management Test Suite
Week 12: Agent Lifecycle Management Tests

Tests the AgentLifecycleManager class with 10 comprehensive test cases.

Author: JARVIS AI-OS Team
Date: November 20, 2025
"""

import time
import sys
import subprocess
from agent_lifecycle import AgentLifecycleManager, AgentProcess, RestartEvent

def print_test_header(test_num, description):
    """Print test header"""
    print(f"\n{'='*70}")
    print(f"TEST {test_num}: {description}")
    print(f"{'='*70}")

def test_1_agent_registration():
    """Test 1: Agent registration"""
    print_test_header(1, "Agent Registration")

    lifecycle = AgentLifecycleManager()

    # Register agent
    lifecycle.register_agent("test_agent", ["python3", "-c", "import time; time.sleep(10)"])

    # Check registration
    assert "test_agent" in lifecycle.agents
    assert lifecycle.agents["test_agent"].agent_name == "test_agent"
    assert lifecycle.agents["test_agent"].command[0] == "python3"

    print("[PASS] Agent registration working")
    return True

def test_2_start_agent():
    """Test 2: Start agent process"""
    print_test_header(2, "Start Agent Process")

    lifecycle = AgentLifecycleManager()
    lifecycle.register_agent("test_agent", ["python3", "-c", "import time; time.sleep(5)"])

    # Start agent
    success = lifecycle.start_agent("test_agent")
    assert success == True

    # Check process is alive
    agent = lifecycle.agents["test_agent"]
    assert agent.is_alive() == True
    assert agent.pid is not None

    # Cleanup
    lifecycle.stop_agent("test_agent")

    print(f"[PASS] Agent started successfully (PID: {agent.pid})")
    return True

def test_3_stop_agent():
    """Test 3: Stop agent process"""
    print_test_header(3, "Stop Agent Process")

    lifecycle = AgentLifecycleManager()
    lifecycle.register_agent("test_agent", ["python3", "-c", "import time; time.sleep(10)"])

    # Start agent
    lifecycle.start_agent("test_agent")
    time.sleep(0.5)

    # Stop agent
    success = lifecycle.stop_agent("test_agent")
    assert success == True

    # Check process is dead
    agent = lifecycle.agents["test_agent"]
    assert agent.is_alive() == False

    print("[PASS] Agent stopped successfully")
    return True

def test_4_restart_agent():
    """Test 4: Restart agent process"""
    print_test_header(4, "Restart Agent Process")

    lifecycle = AgentLifecycleManager(max_restart_attempts=3, base_restart_delay=0.5)
    lifecycle.register_agent("test_agent", ["python3", "-c", "import time; time.sleep(10)"])

    # Start agent
    lifecycle.start_agent("test_agent")
    time.sleep(0.5)

    old_pid = lifecycle.agents["test_agent"].pid

    # Restart agent
    success = lifecycle.restart_agent("test_agent", reason="test")
    assert success == True

    # Check new process is alive
    agent = lifecycle.agents["test_agent"]
    assert agent.is_alive() == True
    assert agent.restart_count == 1
    assert agent.pid != old_pid  # New PID

    # Cleanup
    lifecycle.stop_agent("test_agent")

    print(f"[PASS] Agent restarted successfully (old PID: {old_pid}, new PID: {agent.pid})")
    return True

def test_5_restart_statistics():
    """Test 5: Restart statistics tracking"""
    print_test_header(5, "Restart Statistics Tracking")

    lifecycle = AgentLifecycleManager(max_restart_attempts=3, base_restart_delay=0.2)
    lifecycle.register_agent("test_agent", ["python3", "-c", "import time; time.sleep(5)"])

    # Start and restart agent multiple times
    lifecycle.start_agent("test_agent")
    time.sleep(0.5)

    for i in range(3):
        lifecycle.restart_agent("test_agent", reason=f"test_{i+1}")
        time.sleep(0.5)

    # Check statistics
    stats = lifecycle.get_statistics()
    assert stats["total_restarts"] == 3
    assert stats["successful_restarts"] == 3
    assert stats["failed_restarts"] == 0
    assert stats["avg_restart_time"] > 0

    # Cleanup
    lifecycle.stop_agent("test_agent")

    print(f"[PASS] Statistics: {stats['total_restarts']} restarts, "
          f"{stats['avg_restart_time']:.2f}s avg time")
    return True

def test_6_restart_events():
    """Test 6: Restart event recording"""
    print_test_header(6, "Restart Event Recording")

    lifecycle = AgentLifecycleManager(max_restart_attempts=3, base_restart_delay=0.2)
    lifecycle.register_agent("test_agent", ["python3", "-c", "import time; time.sleep(5)"])

    # Start and restart
    lifecycle.start_agent("test_agent")
    time.sleep(0.5)
    lifecycle.restart_agent("test_agent", reason="test_event")

    # Get events
    events = lifecycle.get_restart_events(limit=5)
    assert len(events) > 0
    assert events[-1]["agent_name"] == "test_agent"
    assert events[-1]["reason"] == "test_event"
    assert events[-1]["success"] == True

    # Cleanup
    lifecycle.stop_agent("test_agent")

    print(f"[PASS] Restart events recorded: {len(events)} events")
    return True

def test_7_agent_status():
    """Test 7: Agent status tracking"""
    print_test_header(7, "Agent Status Tracking")

    lifecycle = AgentLifecycleManager()
    lifecycle.register_agent("test_agent", ["python3", "-c", "import time; time.sleep(5)"])

    # Start agent
    lifecycle.start_agent("test_agent")
    time.sleep(0.5)

    # Get status
    status = lifecycle.get_agent_status("test_agent")
    assert status["agent_name"] == "test_agent"
    assert status["is_alive"] == True
    assert status["pid"] is not None
    assert status["restart_count"] == 0
    assert status["uptime"] > 0

    # Cleanup
    lifecycle.stop_agent("test_agent")

    print(f"[PASS] Agent status: PID={status['pid']}, uptime={status['uptime']:.1f}s")
    return True

def test_8_multiple_agents():
    """Test 8: Multiple agent management"""
    print_test_header(8, "Multiple Agent Management")

    lifecycle = AgentLifecycleManager()

    # Register multiple agents
    for i in range(3):
        lifecycle.register_agent(f"agent_{i}", ["python3", "-c", "import time; time.sleep(5)"])

    # Start all agents
    for i in range(3):
        lifecycle.start_agent(f"agent_{i}")

    time.sleep(0.5)

    # Check all alive
    status = lifecycle.get_agent_status()
    assert len(status) == 3
    for i in range(3):
        assert status[f"agent_{i}"]["is_alive"] == True

    # Stop all agents
    for i in range(3):
        lifecycle.stop_agent(f"agent_{i}")

    print(f"[PASS] Multiple agents managed: {len(status)} agents")
    return True

def test_9_restart_callback():
    """Test 9: Restart callback notification"""
    print_test_header(9, "Restart Callback Notification")

    lifecycle = AgentLifecycleManager(max_restart_attempts=3, base_restart_delay=0.2)
    lifecycle.register_agent("test_agent", ["python3", "-c", "import time; time.sleep(5)"])

    # Register callback
    callback_data = []
    def on_restart(agent_name, success, restart_time):
        callback_data.append((agent_name, success, restart_time))

    lifecycle.register_restart_callback(on_restart)

    # Start and restart
    lifecycle.start_agent("test_agent")
    time.sleep(0.5)
    lifecycle.restart_agent("test_agent", reason="test_callback")

    # Check callback was called
    assert len(callback_data) == 1
    assert callback_data[0][0] == "test_agent"
    assert callback_data[0][1] == True  # success
    assert callback_data[0][2] > 0  # restart_time

    # Cleanup
    lifecycle.stop_agent("test_agent")

    print(f"[PASS] Callback notified: {callback_data[0]}")
    return True

def test_10_failed_restart():
    """Test 10: Failed restart handling"""
    print_test_header(10, "Failed Restart Handling")

    print("\n[TEST SCENARIO] Testing error handling with INTENTIONALLY failing agent")
    print("  - Registering agent with invalid command 'nonexistent_command'")
    print("  - Expecting start to fail (this is the CORRECT behavior)")
    print("  - Expecting restart to fail (this is the CORRECT behavior)")
    print("  - Verifying failed_restarts counter increments\n")

    lifecycle = AgentLifecycleManager(max_restart_attempts=2, base_restart_delay=0.1)

    # Register agent with invalid command (will fail to start)
    lifecycle.register_agent("failing_agent", ["nonexistent_command"])

    # Try to start (will fail) - THIS IS EXPECTED
    print("[EXPECTED FAILURE] Attempting to start agent with invalid command...")
    success = lifecycle.start_agent("failing_agent")
    assert success == False
    print("[OK] Start failed as expected (testing error handling)")

    # Try to restart (should fail) - THIS IS EXPECTED
    print("\n[EXPECTED FAILURE] Attempting to restart failed agent...")
    success = lifecycle.restart_agent("failing_agent", reason="test_failure")
    assert success == False
    print("[OK] Restart failed as expected (testing restart limit)")

    # Check statistics
    stats = lifecycle.get_statistics()
    assert stats["failed_restarts"] > 0

    print(f"\n[PASS] Failed restart handling works correctly!")
    print(f"  - Failed restarts tracked: {stats['failed_restarts']}")
    print(f"  - System handled errors gracefully without crashing")
    return True

def main():
    """Run all lifecycle tests"""
    print("="*70)
    print("JARVIS AI-OS - Agent Lifecycle Management Test Suite (Week 12)")
    print("="*70)
    print("\n10 test cases covering:")
    print("  - Agent registration")
    print("  - Process start/stop")
    print("  - Agent restart")
    print("  - Statistics tracking")
    print("  - Event recording")
    print("  - Multiple agents")
    print("  - Callbacks")
    print("  - Error handling")
    print()

    tests = [
        test_1_agent_registration,
        test_2_start_agent,
        test_3_stop_agent,
        test_4_restart_agent,
        test_5_restart_statistics,
        test_6_restart_events,
        test_7_agent_status,
        test_8_multiple_agents,
        test_9_restart_callback,
        test_10_failed_restart
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
        print("\n[SUCCESS] All lifecycle tests passed!")
    else:
        print(f"\n[WARNING] {failed} test(s) failed")

    print("="*70)

    return failed == 0

if __name__ == "__main__":
    success = main()
    sys.exit(0 if success else 1)
