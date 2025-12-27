#!/usr/bin/env python3
"""
JARVIS AI-OS - Agent Failover Test Suite
Tests agent_failover.py: retry logic, fallback handlers, graceful degradation

This test suite validates:
1. FailoverStrategy enum - All values defined
2. FailoverEvent dataclass - Field initialization
3. AgentFailoverManager initialization - Default values, statistics
4. Retry logic - Exponential backoff, max retries
5. Fallback handlers - Registration, execution
6. Graceful degradation - Last resort behavior
7. Statistics tracking - Accurate counts
8. Event recording - History management
9. Thread safety - Concurrent operations
10. Edge cases - Error handling

Run: python test_agent_failover.py
"""

import sys
import os
import time
import threading
from unittest.mock import MagicMock

# Add parent directory to path
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from agent_failover import (
    FailoverStrategy,
    FailoverEvent,
    AgentFailoverManager,
    device_fallback_handler,
    network_fallback_handler,
    filesystem_fallback_handler,
    user_fallback_handler
)

# Test counters
tests_passed = 0
tests_failed = 0


def test_pass(name):
    global tests_passed
    print(f"  [PASS] {name}")
    tests_passed += 1


def test_fail(name, msg=""):
    global tests_failed
    print(f"  [FAIL] {name}: {msg}")
    tests_failed += 1


# ============================================================================
# Test 1: FailoverStrategy enum
# ============================================================================
def test_failover_strategy_enum():
    """Test FailoverStrategy enum has all values"""
    print("\n" + "="*70)
    print("TEST: FailoverStrategy enum")
    print("="*70)

    if hasattr(FailoverStrategy, 'RETRY'):
        test_pass("RETRY strategy defined")
    else:
        test_fail("RETRY strategy defined")

    if hasattr(FailoverStrategy, 'FALLBACK'):
        test_pass("FALLBACK strategy defined")
    else:
        test_fail("FALLBACK strategy defined")

    if hasattr(FailoverStrategy, 'DEGRADE'):
        test_pass("DEGRADE strategy defined")
    else:
        test_fail("DEGRADE strategy defined")

    if FailoverStrategy.RETRY.value == "retry":
        test_pass("RETRY value is 'retry'")
    else:
        test_fail("RETRY value is 'retry'")


# ============================================================================
# Test 2: FailoverEvent dataclass
# ============================================================================
def test_failover_event():
    """Test FailoverEvent dataclass initialization"""
    print("\n" + "="*70)
    print("TEST: FailoverEvent dataclass")
    print("="*70)

    event = FailoverEvent(
        timestamp=time.time(),
        agent_name="test_agent",
        strategy=FailoverStrategy.RETRY,
        query="test query",
        success=True,
        recovery_time=0.5,
        error_message="none"
    )

    if event.agent_name == "test_agent":
        test_pass("agent_name field correct")
    else:
        test_fail("agent_name field correct")

    if event.strategy == FailoverStrategy.RETRY:
        test_pass("strategy field correct")
    else:
        test_fail("strategy field correct")

    if event.success == True:
        test_pass("success field correct")
    else:
        test_fail("success field correct")

    if event.recovery_time == 0.5:
        test_pass("recovery_time field correct")
    else:
        test_fail("recovery_time field correct")


# ============================================================================
# Test 3: AgentFailoverManager initialization
# ============================================================================
def test_failover_manager_init():
    """Test AgentFailoverManager initializes correctly"""
    print("\n" + "="*70)
    print("TEST: AgentFailoverManager initialization")
    print("="*70)

    manager = AgentFailoverManager(max_retries=3, base_retry_delay=0.1)

    if manager.max_retries == 3:
        test_pass("max_retries set correctly")
    else:
        test_fail("max_retries set correctly", f"Got {manager.max_retries}")

    if manager.base_retry_delay == 0.1:
        test_pass("base_retry_delay set correctly")
    else:
        test_fail("base_retry_delay set correctly")

    if len(manager.failover_events) == 0:
        test_pass("failover_events starts empty")
    else:
        test_fail("failover_events starts empty")

    if len(manager.fallback_handlers) == 0:
        test_pass("fallback_handlers starts empty")
    else:
        test_fail("fallback_handlers starts empty")


# ============================================================================
# Test 4: Statistics initialization
# ============================================================================
def test_stats_init():
    """Test statistics start at zero"""
    print("\n" + "="*70)
    print("TEST: Statistics initialization")
    print("="*70)

    manager = AgentFailoverManager()
    stats = manager.get_statistics()

    if stats["total_failovers"] == 0:
        test_pass("total_failovers starts at 0")
    else:
        test_fail("total_failovers starts at 0")

    if stats["successful_retries"] == 0:
        test_pass("successful_retries starts at 0")
    else:
        test_fail("successful_retries starts at 0")

    if stats["fallback_used"] == 0:
        test_pass("fallback_used starts at 0")
    else:
        test_fail("fallback_used starts at 0")

    if stats["degraded_responses"] == 0:
        test_pass("degraded_responses starts at 0")
    else:
        test_fail("degraded_responses starts at 0")


# ============================================================================
# Test 5: Register fallback handler
# ============================================================================
def test_register_fallback():
    """Test registering fallback handlers"""
    print("\n" + "="*70)
    print("TEST: Register fallback handler")
    print("="*70)

    manager = AgentFailoverManager()

    def test_handler(query):
        return f"Fallback: {query}"

    manager.register_fallback_handler("test_agent", test_handler)

    if "test_agent" in manager.fallback_handlers:
        test_pass("Handler registered")
    else:
        test_fail("Handler registered")

    if manager.fallback_handlers["test_agent"] == test_handler:
        test_pass("Handler function stored correctly")
    else:
        test_fail("Handler function stored correctly")


# ============================================================================
# Test 6: Successful primary handler
# ============================================================================
def test_successful_primary():
    """Test successful primary handler execution"""
    print("\n" + "="*70)
    print("TEST: Successful primary handler")
    print("="*70)

    manager = AgentFailoverManager()

    def working_handler(query, context=None):
        return f"Processed: {query}"

    result = manager.process_query_with_failover(
        "test_agent", "test query", working_handler
    )

    if result["success"] == True:
        test_pass("Result success is True")
    else:
        test_fail("Result success is True")

    if "Processed: test query" in result["response"]:
        test_pass("Response contains expected text")
    else:
        test_fail("Response contains expected text")

    if result["failover_used"] == False:
        test_pass("No failover used for success")
    else:
        test_fail("No failover used for success")

    if result["strategy"] == "primary":
        test_pass("Strategy is 'primary'")
    else:
        test_fail("Strategy is 'primary'", f"Got: {result['strategy']}")


# ============================================================================
# Test 7: Retry on first failure
# ============================================================================
def test_retry_on_failure():
    """Test retry logic with exponential backoff"""
    print("\n" + "="*70)
    print("TEST: Retry on failure")
    print("="*70)

    manager = AgentFailoverManager(max_retries=3, base_retry_delay=0.05)

    attempt_count = [0]
    def failing_then_success_handler(query, context=None):
        attempt_count[0] += 1
        if attempt_count[0] < 2:  # Fail first attempt
            raise Exception("First attempt failed")
        return f"Success on attempt {attempt_count[0]}"

    result = manager.process_query_with_failover(
        "test_agent", "test query", failing_then_success_handler
    )

    if result["success"] == True:
        test_pass("Retry succeeded")
    else:
        test_fail("Retry succeeded")

    if result["failover_used"] == True:
        test_pass("Failover marked as used")
    else:
        test_fail("Failover marked as used")

    if result["strategy"] == "retry":
        test_pass("Strategy is 'retry'")
    else:
        test_fail("Strategy is 'retry'", f"Got: {result['strategy']}")

    stats = manager.get_statistics()
    if stats["successful_retries"] == 1:
        test_pass("successful_retries incremented")
    else:
        test_fail("successful_retries incremented")


# ============================================================================
# Test 8: All retries fail, use fallback
# ============================================================================
def test_all_retries_fail():
    """Test fallback is used when all retries fail"""
    print("\n" + "="*70)
    print("TEST: All retries fail, use fallback")
    print("="*70)

    manager = AgentFailoverManager(max_retries=2, base_retry_delay=0.01)

    def fallback_handler(query):
        return f"Fallback: {query}"

    manager.register_fallback_handler("test_agent", fallback_handler)

    def always_failing_handler(query, context=None):
        raise Exception("Always fails")

    result = manager.process_query_with_failover(
        "test_agent", "test query", always_failing_handler
    )

    if result["success"] == True:
        test_pass("Fallback succeeded")
    else:
        test_fail("Fallback succeeded", f"Response: {result.get('response', '')}")

    if result["strategy"] == "fallback":
        test_pass("Strategy is 'fallback'")
    else:
        test_fail("Strategy is 'fallback'", f"Got: {result['strategy']}")

    if "Fallback:" in result["response"]:
        test_pass("Response from fallback handler")
    else:
        test_fail("Response from fallback handler")


# ============================================================================
# Test 9: Graceful degradation
# ============================================================================
def test_graceful_degradation():
    """Test graceful degradation when all strategies fail"""
    print("\n" + "="*70)
    print("TEST: Graceful degradation")
    print("="*70)

    manager = AgentFailoverManager(max_retries=2, base_retry_delay=0.01)

    # No fallback handler registered

    def always_failing_handler(query, context=None):
        raise Exception("Always fails")

    result = manager.process_query_with_failover(
        "test_agent", "test query", always_failing_handler
    )

    if result["success"] == False:
        test_pass("Degraded response marked as not success")
    else:
        test_fail("Degraded response marked as not success")

    if result["strategy"] == "degrade":
        test_pass("Strategy is 'degrade'")
    else:
        test_fail("Strategy is 'degrade'", f"Got: {result['strategy']}")

    if "[DEGRADED]" in result["response"]:
        test_pass("Response contains DEGRADED marker")
    else:
        test_fail("Response contains DEGRADED marker")


# ============================================================================
# Test 10: Failover event recording
# ============================================================================
def test_event_recording():
    """Test failover events are recorded"""
    print("\n" + "="*70)
    print("TEST: Failover event recording")
    print("="*70)

    manager = AgentFailoverManager(max_retries=2, base_retry_delay=0.01)

    def fallback_handler(query):
        return "Fallback response"

    manager.register_fallback_handler("test_agent", fallback_handler)

    def always_failing_handler(query, context=None):
        raise Exception("Always fails")

    manager.process_query_with_failover(
        "test_agent", "test query", always_failing_handler
    )

    events = manager.get_failover_events(limit=10)

    if len(events) > 0:
        test_pass("Events recorded")
    else:
        test_fail("Events recorded")

    # Should have 2 events: retry failure + fallback success
    if len(events) >= 2:
        test_pass(f"Multiple events recorded ({len(events)})")
    else:
        test_fail(f"Multiple events recorded", f"Got {len(events)}")


# ============================================================================
# Test 11: Event limit enforcement
# ============================================================================
def test_event_limit():
    """Test event history limit is enforced"""
    print("\n" + "="*70)
    print("TEST: Event limit enforcement")
    print("="*70)

    manager = AgentFailoverManager(max_retries=1, base_retry_delay=0.001)
    manager.max_events = 5  # Low limit for testing

    def fallback_handler(query):
        return "Fallback"

    manager.register_fallback_handler("test", fallback_handler)

    def failing(query, context=None):
        raise Exception("Fail")

    # Generate more events than the limit
    for i in range(10):
        manager.process_query_with_failover("test", f"query{i}", failing)

    if len(manager.failover_events) <= manager.max_events:
        test_pass(f"Event limit enforced ({len(manager.failover_events)} <= {manager.max_events})")
    else:
        test_fail("Event limit enforced", f"Got {len(manager.failover_events)}")


# ============================================================================
# Test 12: Filter events by agent
# ============================================================================
def test_filter_events():
    """Test filtering events by agent name"""
    print("\n" + "="*70)
    print("TEST: Filter events by agent")
    print("="*70)

    manager = AgentFailoverManager(max_retries=1, base_retry_delay=0.001)

    def fallback(query):
        return "Fallback"

    manager.register_fallback_handler("agent1", fallback)
    manager.register_fallback_handler("agent2", fallback)

    def failing(query, context=None):
        raise Exception("Fail")

    manager.process_query_with_failover("agent1", "query1", failing)
    manager.process_query_with_failover("agent2", "query2", failing)

    agent1_events = manager.get_failover_events(agent_name="agent1")
    agent2_events = manager.get_failover_events(agent_name="agent2")

    if len(agent1_events) > 0:
        test_pass("Agent1 events found")
    else:
        test_fail("Agent1 events found")

    if all(e["agent_name"] == "agent1" for e in agent1_events):
        test_pass("Agent1 events filtered correctly")
    else:
        test_fail("Agent1 events filtered correctly")


# ============================================================================
# Test 13: Statistics accuracy
# ============================================================================
def test_statistics_accuracy():
    """Test statistics are accurate"""
    print("\n" + "="*70)
    print("TEST: Statistics accuracy")
    print("="*70)

    manager = AgentFailoverManager(max_retries=2, base_retry_delay=0.001)

    def fallback(query):
        return "Fallback"

    manager.register_fallback_handler("test", fallback)

    def failing(query, context=None):
        raise Exception("Fail")

    # Trigger 3 failovers
    for i in range(3):
        manager.process_query_with_failover("test", f"query{i}", failing)

    stats = manager.get_statistics()

    if stats["total_failovers"] == 3:
        test_pass("total_failovers is 3")
    else:
        test_fail("total_failovers is 3", f"Got {stats['total_failovers']}")

    if stats["fallback_used"] == 3:
        test_pass("fallback_used is 3")
    else:
        test_fail("fallback_used is 3", f"Got {stats['fallback_used']}")

    if stats["failed_retries"] == 3:
        test_pass("failed_retries is 3")
    else:
        test_fail("failed_retries is 3", f"Got {stats['failed_retries']}")


# ============================================================================
# Test 14: Reset statistics
# ============================================================================
def test_reset_statistics():
    """Test statistics reset"""
    print("\n" + "="*70)
    print("TEST: Reset statistics")
    print("="*70)

    manager = AgentFailoverManager(max_retries=1, base_retry_delay=0.001)

    def fallback(query):
        return "Fallback"

    manager.register_fallback_handler("test", fallback)

    def failing(query, context=None):
        raise Exception("Fail")

    manager.process_query_with_failover("test", "query", failing)

    # Verify stats are non-zero
    stats_before = manager.get_statistics()
    if stats_before["total_failovers"] > 0:
        test_pass("Stats non-zero before reset")
    else:
        test_fail("Stats non-zero before reset")

    # Reset
    manager.reset_statistics()

    stats_after = manager.get_statistics()
    if stats_after["total_failovers"] == 0:
        test_pass("Stats reset to zero")
    else:
        test_fail("Stats reset to zero", f"Got {stats_after['total_failovers']}")

    if len(manager.failover_events) == 0:
        test_pass("Events cleared on reset")
    else:
        test_fail("Events cleared on reset")


# ============================================================================
# Test 15: Health monitor integration
# ============================================================================
def test_health_monitor_integration():
    """Test integration with health monitor"""
    print("\n" + "="*70)
    print("TEST: Health monitor integration")
    print("="*70)

    # Mock health monitor
    health_monitor = MagicMock()
    health_monitor.is_agent_available.return_value = False

    manager = AgentFailoverManager(health_monitor=health_monitor)

    def fallback(query):
        return "Fallback response"

    manager.register_fallback_handler("test", fallback)

    def primary(query, context=None):
        return "Primary response"

    result = manager.process_query_with_failover("test", "query", primary)

    # Should skip primary and go directly to fallback since agent unavailable
    if result["strategy"] == "fallback":
        test_pass("Skipped to fallback when agent unavailable")
    else:
        test_fail("Skipped to fallback when agent unavailable", f"Got: {result['strategy']}")


# ============================================================================
# Test 16: Recovery time tracking
# ============================================================================
def test_recovery_time():
    """Test recovery time is tracked"""
    print("\n" + "="*70)
    print("TEST: Recovery time tracking")
    print("="*70)

    manager = AgentFailoverManager(max_retries=2, base_retry_delay=0.01)

    def fallback(query):
        return "Fallback"

    manager.register_fallback_handler("test", fallback)

    def failing(query, context=None):
        raise Exception("Fail")

    result = manager.process_query_with_failover("test", "query", failing)

    if result["recovery_time"] > 0:
        test_pass(f"Recovery time tracked ({result['recovery_time']:.4f}s)")
    else:
        test_fail("Recovery time tracked")


# ============================================================================
# Test 17: Average recovery time calculation
# ============================================================================
def test_avg_recovery_time():
    """Test average recovery time calculation"""
    print("\n" + "="*70)
    print("TEST: Average recovery time calculation")
    print("="*70)

    manager = AgentFailoverManager(max_retries=1, base_retry_delay=0.001)

    def fallback(query):
        return "Fallback"

    manager.register_fallback_handler("test", fallback)

    def failing(query, context=None):
        raise Exception("Fail")

    # Multiple failovers
    for i in range(3):
        manager.process_query_with_failover("test", f"query{i}", failing)

    stats = manager.get_statistics()

    if stats["avg_recovery_time"] > 0:
        test_pass(f"Avg recovery time calculated ({stats['avg_recovery_time']:.4f}s)")
    else:
        test_fail("Avg recovery time calculated")

    # Verify average is correct
    expected_avg = stats["total_recovery_time"] / stats["total_failovers"]
    if abs(stats["avg_recovery_time"] - expected_avg) < 0.001:
        test_pass("Avg recovery time is accurate")
    else:
        test_fail("Avg recovery time is accurate")


# ============================================================================
# Test 18: Example fallback handlers exist
# ============================================================================
def test_example_fallback_handlers():
    """Test example fallback handlers are defined"""
    print("\n" + "="*70)
    print("TEST: Example fallback handlers")
    print("="*70)

    if callable(device_fallback_handler):
        test_pass("device_fallback_handler is callable")
    else:
        test_fail("device_fallback_handler is callable")

    if callable(network_fallback_handler):
        test_pass("network_fallback_handler is callable")
    else:
        test_fail("network_fallback_handler is callable")

    if callable(filesystem_fallback_handler):
        test_pass("filesystem_fallback_handler is callable")
    else:
        test_fail("filesystem_fallback_handler is callable")

    if callable(user_fallback_handler):
        test_pass("user_fallback_handler is callable")
    else:
        test_fail("user_fallback_handler is callable")


# ============================================================================
# Test 19: User fallback handler response
# ============================================================================
def test_user_fallback_response():
    """Test user fallback handler returns expected response"""
    print("\n" + "="*70)
    print("TEST: User fallback handler response")
    print("="*70)

    response = user_fallback_handler("test query")

    if "[FALLBACK]" in response:
        test_pass("Response contains FALLBACK marker")
    else:
        test_fail("Response contains FALLBACK marker")

    if "help" in response.lower() or "commands" in response.lower():
        test_pass("Response mentions available commands")
    else:
        test_fail("Response mentions available commands")


# ============================================================================
# Test 20: Fallback handler exception handling
# ============================================================================
def test_fallback_exception():
    """Test fallback handler exception triggers degradation"""
    print("\n" + "="*70)
    print("TEST: Fallback handler exception handling")
    print("="*70)

    manager = AgentFailoverManager(max_retries=1, base_retry_delay=0.001)

    def failing_fallback(query):
        raise Exception("Fallback also fails")

    manager.register_fallback_handler("test", failing_fallback)

    def failing_primary(query, context=None):
        raise Exception("Primary fails")

    result = manager.process_query_with_failover("test", "query", failing_primary)

    if result["strategy"] == "degrade":
        test_pass("Degradation after fallback failure")
    else:
        test_fail("Degradation after fallback failure", f"Got: {result['strategy']}")


# ============================================================================
# Test 21: Thread safety - concurrent failovers
# ============================================================================
def test_thread_safety():
    """Test thread safety with concurrent failovers"""
    print("\n" + "="*70)
    print("TEST: Thread safety")
    print("="*70)

    manager = AgentFailoverManager(max_retries=1, base_retry_delay=0.001)

    def fallback(query):
        return "Fallback"

    manager.register_fallback_handler("test", fallback)

    def failing(query, context=None):
        raise Exception("Fail")

    errors = []

    def worker():
        try:
            for i in range(5):
                manager.process_query_with_failover("test", f"query{i}", failing)
        except Exception as e:
            errors.append(str(e))

    threads = [threading.Thread(target=worker) for _ in range(3)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()

    if len(errors) == 0:
        test_pass("No errors during concurrent operations")
    else:
        test_fail("No errors during concurrent operations", str(errors))


# ============================================================================
# Test 22: Context passed to handler
# ============================================================================
def test_context_passing():
    """Test context is passed to handler"""
    print("\n" + "="*70)
    print("TEST: Context passing")
    print("="*70)

    manager = AgentFailoverManager()

    received_context = [None]

    def handler_with_context(query, context=None):
        received_context[0] = context
        return "OK"

    test_context = {"user": "test", "priority": 1}
    manager.process_query_with_failover(
        "test", "query", handler_with_context, context=test_context
    )

    if received_context[0] == test_context:
        test_pass("Context passed to handler")
    else:
        test_fail("Context passed to handler")


# ============================================================================
# Test 23: Attempts count in result
# ============================================================================
def test_attempts_count():
    """Test attempts count is included in result"""
    print("\n" + "="*70)
    print("TEST: Attempts count in result")
    print("="*70)

    manager = AgentFailoverManager(max_retries=3, base_retry_delay=0.01)

    attempt = [0]
    def fail_twice(query, context=None):
        attempt[0] += 1
        if attempt[0] < 3:
            raise Exception(f"Fail {attempt[0]}")
        return "Success"

    result = manager.process_query_with_failover("test", "query", fail_twice)

    if result.get("attempts") == 3:
        test_pass("Attempts count is 3")
    else:
        test_fail("Attempts count is 3", f"Got: {result.get('attempts')}")


# ============================================================================
# Test 24: Agent name in fallback response
# ============================================================================
def test_agent_name_in_fallback():
    """Test agent name is modified in fallback response"""
    print("\n" + "="*70)
    print("TEST: Agent name in fallback response")
    print("="*70)

    manager = AgentFailoverManager(max_retries=1, base_retry_delay=0.001)

    def fallback(query):
        return "Fallback"

    manager.register_fallback_handler("device", fallback)

    def failing(query, context=None):
        raise Exception("Fail")

    result = manager.process_query_with_failover("device", "query", failing)

    if result["agent"] == "device_fallback":
        test_pass("Agent name includes '_fallback' suffix")
    else:
        test_fail("Agent name includes '_fallback' suffix", f"Got: {result['agent']}")


# ============================================================================
# Test 25: Registered fallbacks count
# ============================================================================
def test_registered_fallbacks_count():
    """Test registered fallbacks count in statistics"""
    print("\n" + "="*70)
    print("TEST: Registered fallbacks count")
    print("="*70)

    manager = AgentFailoverManager()

    manager.register_fallback_handler("agent1", lambda q: "1")
    manager.register_fallback_handler("agent2", lambda q: "2")
    manager.register_fallback_handler("agent3", lambda q: "3")

    stats = manager.get_statistics()

    if stats["registered_fallbacks"] == 3:
        test_pass("registered_fallbacks count is 3")
    else:
        test_fail("registered_fallbacks count is 3", f"Got: {stats['registered_fallbacks']}")


# ============================================================================
# Test Runner
# ============================================================================
def run_all_tests():
    """Run all tests"""
    global tests_passed, tests_failed

    print()
    print("="*70)
    print("  JARVIS AI-OS - Agent Failover Test Suite")
    print("="*70)

    tests = [
        test_failover_strategy_enum,
        test_failover_event,
        test_failover_manager_init,
        test_stats_init,
        test_register_fallback,
        test_successful_primary,
        test_retry_on_failure,
        test_all_retries_fail,
        test_graceful_degradation,
        test_event_recording,
        test_event_limit,
        test_filter_events,
        test_statistics_accuracy,
        test_reset_statistics,
        test_health_monitor_integration,
        test_recovery_time,
        test_avg_recovery_time,
        test_example_fallback_handlers,
        test_user_fallback_response,
        test_fallback_exception,
        test_thread_safety,
        test_context_passing,
        test_attempts_count,
        test_agent_name_in_fallback,
        test_registered_fallbacks_count,
    ]

    for test_func in tests:
        try:
            test_func()
        except Exception as e:
            print(f"\n  [ERROR] {test_func.__name__}: {e}")
            import traceback
            traceback.print_exc()
            tests_failed += 1

    # Summary
    print()
    print("="*70)
    print("  AGENT FAILOVER TEST SUMMARY")
    print("="*70)
    print(f"  Total tests: {tests_passed + tests_failed}")
    print(f"  Passed: {tests_passed}")
    print(f"  Failed: {tests_failed}")
    if tests_passed + tests_failed > 0:
        print(f"  Pass rate: {100 * tests_passed / (tests_passed + tests_failed):.1f}%")
    print("="*70)
    print()

    return tests_failed == 0


if __name__ == "__main__":
    success = run_all_tests()
    sys.exit(0 if success else 1)
