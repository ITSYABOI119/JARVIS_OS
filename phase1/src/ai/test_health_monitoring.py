#!/usr/bin/env python3
"""
JARVIS AI-OS Phase 1 - Health Monitoring Test Suite
Week 12: Agent Health Monitoring Tests

Tests the AgentHealthMonitor class with 10 comprehensive test cases.

Author: JARVIS AI-OS Team
Date: November 20, 2025
"""

import time
import sys
from agent_health import AgentHealthMonitor, Heartbeat, HealthState

def print_test_header(test_num, description):
    """Print test header"""
    print(f"\n{'='*70}")
    print(f"TEST {test_num}: {description}")
    print(f"{'='*70}")

def test_1_heartbeat_tracking():
    """Test 1: Heartbeat tracking"""
    print_test_header(1, "Heartbeat Tracking")

    monitor = AgentHealthMonitor(["device", "network"])
    monitor.start()

    # Send heartbeat
    hb = Heartbeat(agent_name="device", timestamp=time.time(), queries_processed=10)
    monitor.receive_heartbeat(hb)

    time.sleep(0.5)

    # Check status
    status = monitor.get_health_status("device")
    assert status["agent_name"] == "device"
    assert status["queries_processed"] == 10
    assert status["last_heartbeat"] is not None

    monitor.stop()
    print("[PASS] Heartbeat tracking working")
    return True

def test_2_timeout_detection():
    """Test 2: Timeout detection"""
    print_test_header(2, "Timeout Detection")

    monitor = AgentHealthMonitor(["device"], degraded_threshold=1.0, failed_threshold=2.0, check_interval=0.2)
    monitor.start()

    # Send initial heartbeat
    hb = Heartbeat(agent_name="device", timestamp=time.time())
    monitor.receive_heartbeat(hb)

    # Wait for timeout (2s threshold + extra time for monitoring loop to detect)
    time.sleep(3.0)

    # Give monitoring thread one more cycle to update state
    time.sleep(0.5)

    # Check failed state
    status = monitor.get_health_status("device")
    assert status["state"] == "failed", f"Expected failed, got {status['state']}"

    monitor.stop()
    print("[PASS] Timeout detection working")
    return True

def test_3_state_transitions():
    """Test 3: Health state transitions"""
    print_test_header(3, "Health State Transitions")

    monitor = AgentHealthMonitor(
        ["device"],
        degraded_threshold=2.0,
        failed_threshold=4.0,
        check_interval=0.5
    )

    states_seen = []

    def callback(agent, old_state, new_state):
        states_seen.append((old_state.value, new_state.value))

    monitor.register_state_change_callback(callback)
    monitor.start()

    # Initial heartbeat (unknown -> healthy)
    hb = Heartbeat(agent_name="device", timestamp=time.time())
    monitor.receive_heartbeat(hb)
    time.sleep(1.0)

    # Wait for degraded (healthy -> degraded)
    time.sleep(2.5)

    # Wait for failed (degraded -> failed)
    time.sleep(2.5)

    monitor.stop()

    # Check transitions
    assert ("unknown", "healthy") in states_seen
    assert ("healthy", "degraded") in states_seen
    assert ("degraded", "failed") in states_seen

    print(f"[PASS] State transitions: {states_seen}")
    return True

def test_4_multiple_agents():
    """Test 4: Multiple agent health tracking"""
    print_test_header(4, "Multiple Agent Tracking")

    monitor = AgentHealthMonitor(["device", "network", "filesystem", "user"])
    monitor.start()

    # Send heartbeats for all agents
    for agent in ["device", "network", "filesystem", "user"]:
        hb = Heartbeat(agent_name=agent, timestamp=time.time())
        monitor.receive_heartbeat(hb)

    time.sleep(1.0)

    # Check all healthy
    status = monitor.get_health_status()
    assert len(status) == 4
    for agent, info in status.items():
        assert info["state"] == "healthy", f"{agent} not healthy"

    monitor.stop()
    print("[PASS] Multiple agents tracked correctly")
    return True

def test_5_statistics():
    """Test 5: Health statistics"""
    print_test_header(5, "Health Statistics")

    monitor = AgentHealthMonitor(["device", "network"])
    monitor.start()

    # Send multiple heartbeats
    for i in range(5):
        for agent in ["device", "network"]:
            hb = Heartbeat(agent_name=agent, timestamp=time.time())
            monitor.receive_heartbeat(hb)
        time.sleep(0.2)

    # Get statistics
    stats = monitor.get_statistics()
    assert stats["total_heartbeats"] == 10
    assert stats["agents_monitored"] == 2
    assert stats["healthy_agents"] == 2

    monitor.stop()
    print(f"[PASS] Statistics: {stats['total_heartbeats']} heartbeats, {stats['healthy_agents']} healthy")
    return True

def test_6_late_heartbeat():
    """Test 6: Late heartbeat (degraded state)"""
    print_test_header(6, "Late Heartbeat (Degraded State)")

    monitor = AgentHealthMonitor(
        ["device"],
        degraded_threshold=1.0,
        failed_threshold=3.0,
        check_interval=0.3
    )
    monitor.start()

    # Initial heartbeat
    hb = Heartbeat(agent_name="device", timestamp=time.time())
    monitor.receive_heartbeat(hb)

    # Wait to trigger degraded (but not failed)
    time.sleep(2.0)

    status = monitor.get_health_status("device")
    assert status["state"] == "degraded", f"Expected degraded, got {status['state']}"

    monitor.stop()
    print("[PASS] Degraded state triggered correctly")
    return True

def test_7_recovery():
    """Test 7: Agent recovery from failure"""
    print_test_header(7, "Agent Recovery from Failure")

    monitor = AgentHealthMonitor(
        ["device"],
        degraded_threshold=1.0,
        failed_threshold=2.0,
        check_interval=0.2
    )
    monitor.start()

    # Initial heartbeat
    hb = Heartbeat(agent_name="device", timestamp=time.time())
    monitor.receive_heartbeat(hb)

    # Wait for failure (2s threshold + 1.5s for monitoring loop)
    time.sleep(4.0)

    # Give monitoring thread one more cycle
    time.sleep(0.5)

    status = monitor.get_health_status("device")
    assert status["state"] == "failed", f"Expected failed, got {status['state']}"

    # Send heartbeat (recovery)
    hb = Heartbeat(agent_name="device", timestamp=time.time())
    monitor.receive_heartbeat(hb)
    time.sleep(1.0)

    # Check recovered
    status = monitor.get_health_status("device")
    assert status["state"] == "healthy", f"Expected healthy, got {status['state']}"
    assert status["total_recoveries"] == 1

    monitor.stop()
    print("[PASS] Agent recovered from failure")
    return True

def test_8_consecutive_failures():
    """Test 8: Consecutive failures tracking"""
    print_test_header(8, "Consecutive Failures Tracking")

    monitor = AgentHealthMonitor(
        ["device"],
        degraded_threshold=0.5,
        failed_threshold=1.0,
        check_interval=0.2
    )
    monitor.start()

    # Initial heartbeat
    hb = Heartbeat(agent_name="device", timestamp=time.time())
    monitor.receive_heartbeat(hb)

    # Wait for failures to accumulate (1s threshold + 1.5s for monitoring loop)
    time.sleep(3.0)

    # Give monitoring thread one more cycle
    time.sleep(0.5)

    status = monitor.get_health_status("device")
    assert status["consecutive_failures"] > 0, f"Expected consecutive_failures > 0, got {status['consecutive_failures']}"
    assert status["total_failures"] > 0, f"Expected total_failures > 0, got {status['total_failures']}"

    monitor.stop()
    print(f"[PASS] Consecutive failures: {status['consecutive_failures']}, Total: {status['total_failures']}")
    return True

def test_9_is_agent_available():
    """Test 9: Agent availability check"""
    print_test_header(9, "Agent Availability Check")

    monitor = AgentHealthMonitor(["device"])
    monitor.start()

    # Send heartbeat
    hb = Heartbeat(agent_name="device", timestamp=time.time())
    monitor.receive_heartbeat(hb)
    time.sleep(0.5)

    # Check availability
    assert monitor.is_agent_healthy("device") == True
    assert monitor.is_agent_available("device") == True

    monitor.stop()
    print("[PASS] Agent availability checks working")
    return True

def test_10_get_failed_agents():
    """Test 10: Get failed agents list"""
    print_test_header(10, "Get Failed Agents List")

    monitor = AgentHealthMonitor(
        ["device", "network", "filesystem"],
        degraded_threshold=1.0,
        failed_threshold=1.5,
        check_interval=0.2
    )
    monitor.start()

    # Send initial heartbeat to all agents to establish baseline
    for agent in ["device", "network", "filesystem"]:
        hb = Heartbeat(agent_name=agent, timestamp=time.time())
        monitor.receive_heartbeat(hb)

    time.sleep(0.5)

    # Send heartbeats for device and filesystem only (keep them alive)
    # Send heartbeats every 0.5s for 4 seconds to keep device and filesystem healthy
    for i in range(8):
        for agent in ["device", "filesystem"]:
            hb = Heartbeat(agent_name=agent, timestamp=time.time())
            monitor.receive_heartbeat(hb)
        time.sleep(0.5)

    # Give monitoring thread one more cycle to detect network failure
    time.sleep(0.5)

    # Check failed agents
    failed = monitor.get_failed_agents()
    assert "network" in failed, f"Expected network in failed list, got {failed}"
    assert "device" not in failed, f"Device should not be in failed list, got {failed}"
    assert "filesystem" not in failed, f"Filesystem should not be in failed list, got {failed}"

    monitor.stop()
    print(f"[PASS] Failed agents: {failed}")
    return True

def main():
    """Run all health monitoring tests"""
    print("="*70)
    print("JARVIS AI-OS - Health Monitoring Test Suite (Week 12)")
    print("="*70)
    print("\n10 test cases covering:")
    print("  - Heartbeat tracking")
    print("  - Timeout detection")
    print("  - State transitions")
    print("  - Multiple agents")
    print("  - Statistics")
    print("  - Edge cases")
    print()

    tests = [
        test_1_heartbeat_tracking,
        test_2_timeout_detection,
        test_3_state_transitions,
        test_4_multiple_agents,
        test_5_statistics,
        test_6_late_heartbeat,
        test_7_recovery,
        test_8_consecutive_failures,
        test_9_is_agent_available,
        test_10_get_failed_agents
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
        print("\n[SUCCESS] All health monitoring tests passed!")
    else:
        print(f"\n[WARNING] {failed} test(s) failed")

    print("="*70)

    return failed == 0

if __name__ == "__main__":
    success = main()
    sys.exit(0 if success else 1)
