#!/usr/bin/env python3
"""
JARVIS AI-OS Phase 1 - Fault Tolerance Integration Test
Week 12: Complete Fault Tolerance System Test

Tests the integration of AgentHealthMonitor + AgentFailoverManager + AgentLifecycleManager.
Validates the complete fault tolerance system working together.

Author: JARVIS AI-OS Team
Date: November 20, 2025
"""

import time
import sys
from agent_health import AgentHealthMonitor, Heartbeat, HealthState
from agent_failover import AgentFailoverManager
from agent_lifecycle import AgentLifecycleManager

def print_test_header(test_num, description):
    """Print test header"""
    print(f"\n{'='*70}")
    print(f"TEST {test_num}: {description}")
    print(f"{'='*70}")

def test_1_health_failover_integration():
    """Test 1: Health monitoring + Failover integration"""
    print_test_header(1, "Health Monitoring + Failover Integration")

    # Setup
    health_monitor = AgentHealthMonitor(
        ["device"],
        degraded_threshold=1.0,
        failed_threshold=2.0,
        check_interval=0.2
    )
    failover_manager = AgentFailoverManager(health_monitor=health_monitor)

    # Register fallback
    def device_fallback(query):
        return f"[FALLBACK] {query}"

    failover_manager.register_fallback_handler("device", device_fallback)

    # Start monitoring
    health_monitor.start()

    # Send heartbeat
    hb = Heartbeat(agent_name="device", timestamp=time.time())
    health_monitor.receive_heartbeat(hb)

    time.sleep(0.5)

    # Query while healthy (should use primary)
    query_count = [0]
    def primary_handler(query, context=None):
        query_count[0] += 1
        return f"[PRIMARY] {query}"

    result = failover_manager.process_query_with_failover(
        "device", "test query", primary_handler
    )
    assert result["success"] == True
    assert "PRIMARY" in result["response"]
    assert result["failover_used"] == False

    # Wait for agent to fail (2s threshold + monitoring time)
    time.sleep(3.0)

    # Query while failed (should use fallback)
    result = failover_manager.process_query_with_failover(
        "device", "test query", primary_handler
    )
    assert result["success"] == True
    assert "FALLBACK" in result["response"]
    assert result["failover_used"] == True

    health_monitor.stop()

    print("[PASS] Health + Failover integration working")
    return True

def test_2_lifecycle_health_integration():
    """Test 2: Lifecycle + Health monitoring integration"""
    print_test_header(2, "Lifecycle + Health Monitoring Integration")

    # Setup health monitor
    health_monitor = AgentHealthMonitor(
        ["test_agent"],
        degraded_threshold=1.0,
        failed_threshold=2.0,
        check_interval=0.5
    )

    # Setup lifecycle manager
    lifecycle = AgentLifecycleManager(health_monitor=health_monitor)
    lifecycle.register_agent("test_agent", ["python", "-c", "import time; time.sleep(5)"])

    # Start monitoring
    health_monitor.start()
    lifecycle.start()

    # Start agent and send heartbeat
    lifecycle.start_agent("test_agent")
    hb = Heartbeat(agent_name="test_agent", timestamp=time.time())
    health_monitor.receive_heartbeat(hb)

    time.sleep(1.0)

    # Check agent is healthy
    status = health_monitor.get_health_status("test_agent")
    assert status["state"] == "healthy"

    # Stop monitoring
    health_monitor.stop()
    lifecycle.stop()
    lifecycle.stop_agent("test_agent")

    print("[PASS] Lifecycle + Health integration working")
    return True

def test_3_complete_fault_tolerance():
    """Test 3: Complete fault tolerance system"""
    print_test_header(3, "Complete Fault Tolerance System")

    # Setup all three components
    health_monitor = AgentHealthMonitor(
        ["test_agent"],
        degraded_threshold=1.0,
        failed_threshold=2.0,
        check_interval=0.5
    )

    failover_manager = AgentFailoverManager(health_monitor=health_monitor)

    def test_fallback(query):
        return f"[FALLBACK] {query}"

    failover_manager.register_fallback_handler("test_agent", test_fallback)

    lifecycle = AgentLifecycleManager(health_monitor=health_monitor)
    lifecycle.register_agent("test_agent", ["python", "-c", "import time; time.sleep(10)"])

    # Start all systems
    health_monitor.start()
    lifecycle.start()

    # Start agent
    lifecycle.start_agent("test_agent")
    time.sleep(0.5)

    # Send heartbeat
    hb = Heartbeat(agent_name="test_agent", timestamp=time.time())
    health_monitor.receive_heartbeat(hb)

    time.sleep(0.5)

    # Verify healthy
    status = health_monitor.get_health_status("test_agent")
    assert status["state"] == "healthy"

    # Test query routing
    def primary_handler(query, context=None):
        return f"[PRIMARY] {query}"

    result = failover_manager.process_query_with_failover(
        "test_agent", "test", primary_handler
    )
    assert "PRIMARY" in result["response"]

    # Cleanup
    health_monitor.stop()
    lifecycle.stop()
    lifecycle.stop_agent("test_agent")

    print("[PASS] Complete fault tolerance system working")
    return True

def test_4_zero_service_interruption():
    """Test 4: Zero service interruption during failover"""
    print_test_header(4, "Zero Service Interruption During Failover")

    # Setup
    health_monitor = AgentHealthMonitor(
        ["device"],
        degraded_threshold=0.5,
        failed_threshold=1.0,
        check_interval=0.2
    )

    failover_manager = AgentFailoverManager(health_monitor=health_monitor)

    def device_fallback(query):
        return "[FALLBACK] Service available (degraded mode)"

    failover_manager.register_fallback_handler("device", device_fallback)

    health_monitor.start()

    # Send initial heartbeat
    hb = Heartbeat(agent_name="device", timestamp=time.time())
    health_monitor.receive_heartbeat(hb)

    time.sleep(0.5)

    # Test queries over time (spanning healthy -> failed transition)
    results = []

    def primary_handler(query, context=None):
        return f"[PRIMARY] {query}"

    for i in range(5):
        result = failover_manager.process_query_with_failover(
            "device", f"query_{i}", primary_handler
        )
        results.append(result["success"])
        time.sleep(0.5)

    # All queries should succeed (either primary or fallback)
    assert all(results), f"Some queries failed: {results}"

    health_monitor.stop()

    print(f"[PASS] Zero service interruption: {len(results)}/{ len(results)} queries succeeded")
    return True

def test_5_recovery_after_failure():
    """Test 5: System recovery after agent failure"""
    print_test_header(5, "System Recovery After Agent Failure")

    # Setup
    health_monitor = AgentHealthMonitor(
        ["device"],
        degraded_threshold=1.0,
        failed_threshold=2.0,
        check_interval=0.3
    )

    failover_manager = AgentFailoverManager(health_monitor=health_monitor)

    def device_fallback(query):
        return "[FALLBACK] Temporary service"

    failover_manager.register_fallback_handler("device", device_fallback)

    health_monitor.start()

    # Simulate: healthy -> failed -> healthy

    # Initial heartbeat (healthy)
    hb = Heartbeat(agent_name="device", timestamp=time.time())
    health_monitor.receive_heartbeat(hb)
    time.sleep(0.5)

    status = health_monitor.get_health_status("device")
    assert status["state"] == "healthy"

    # Wait for failure (2s threshold + time for detection)
    time.sleep(3.0)

    status = health_monitor.get_health_status("device")
    assert status["state"] == "failed"

    # Send heartbeat (recovery)
    hb = Heartbeat(agent_name="device", timestamp=time.time())
    health_monitor.receive_heartbeat(hb)
    time.sleep(0.5)

    status = health_monitor.get_health_status("device")
    assert status["state"] == "healthy"
    assert status["total_recoveries"] == 1

    health_monitor.stop()

    print(f"[PASS] System recovered successfully (1 recovery)")
    return True

def main():
    """Run all integration tests"""
    print("="*70)
    print("JARVIS AI-OS - Fault Tolerance Integration Test Suite (Week 12)")
    print("="*70)
    print("\n5 integration test cases covering:")
    print("  - Health + Failover integration")
    print("  - Lifecycle + Health integration")
    print("  - Complete system integration")
    print("  - Zero service interruption")
    print("  - Recovery after failure")
    print()

    tests = [
        test_1_health_failover_integration,
        test_2_lifecycle_health_integration,
        test_3_complete_fault_tolerance,
        test_4_zero_service_interruption,
        test_5_recovery_after_failure
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
        print("\n[SUCCESS] All integration tests passed!")
    else:
        print(f"\n[WARNING] {failed} test(s) failed")

    print("="*70)

    return failed == 0

if __name__ == "__main__":
    success = main()
    sys.exit(0 if success else 1)
