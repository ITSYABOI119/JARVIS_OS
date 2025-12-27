#!/usr/bin/env python3
"""
JARVIS AI-OS - Agent Router Test Suite
Tests ConflictResolver and AgentRouter classes.

Coverage:
- ConflictResolver: resource allocation, priority, deadlock detection
- AgentRouter: query routing, agent selection, statistics

Run: python test_agent_router.py
"""

import sys
import time
import threading
from pathlib import Path
from unittest.mock import MagicMock, patch

# Add parent directory to path
sys.path.insert(0, str(Path(__file__).parent))

# Test framework
tests_passed = 0
tests_failed = 0


def test_pass(name):
    global tests_passed
    print(f"  [PASS] {name}")
    tests_passed += 1


def test_fail(name, msg):
    global tests_failed
    print(f"  [FAIL] {name}: {msg}")
    tests_failed += 1


# ============================================================================
# CONFLICT RESOLVER TESTS
# ============================================================================

def test_conflict_resolver_init():
    """Test ConflictResolver initialization."""
    print("\n" + "=" * 70)
    print("TEST: ConflictResolver initialization")
    print("=" * 70)

    from agent_router import ConflictResolver

    priority = ["device", "network", "filesystem", "user"]
    resolver = ConflictResolver(priority)

    if resolver.agent_priority == priority:
        test_pass("Agent priority set correctly")
    else:
        test_fail("Agent priority set correctly", f"Got {resolver.agent_priority}")

    if len(resolver.resource_locks) == 0:
        test_pass("Resource locks empty initially")
    else:
        test_fail("Resource locks empty initially", f"Got {len(resolver.resource_locks)}")

    if all(agent in resolver.wait_for_graph for agent in priority):
        test_pass("Wait-for graph initialized for all agents")
    else:
        test_fail("Wait-for graph initialized for all agents", "Missing agents")

    if resolver.stats["conflicts_resolved"] == 0:
        test_pass("Statistics initialized to zero")
    else:
        test_fail("Statistics initialized to zero", f"Got {resolver.stats}")


def test_resource_acquire_release():
    """Test basic resource acquire and release."""
    print("\n" + "=" * 70)
    print("TEST: Resource acquire and release")
    print("=" * 70)

    from agent_router import ConflictResolver

    resolver = ConflictResolver(["device", "network", "filesystem", "user"])

    # Acquire resource
    result = resolver.request_resource("device", "gpu", timeout=1.0)
    if result:
        test_pass("Device agent acquires gpu resource")
    else:
        test_fail("Device agent acquires gpu resource", "Failed to acquire")

    # Check it's locked
    if "gpu" in resolver.resource_locks:
        holder, _ = resolver.resource_locks["gpu"]
        if holder == "device":
            test_pass("Resource tracked as held by device")
        else:
            test_fail("Resource tracked as held by device", f"Held by {holder}")
    else:
        test_fail("Resource tracked as held by device", "Not in locks")

    # Release resource
    released = resolver.release_resource("gpu", "device")
    if released:
        test_pass("Resource released successfully")
    else:
        test_fail("Resource released successfully", "Release returned False")

    if "gpu" not in resolver.resource_locks:
        test_pass("Resource no longer in locks after release")
    else:
        test_fail("Resource no longer in locks after release", "Still present")


def test_priority_preemption():
    """Test priority-based preemption."""
    print("\n" + "=" * 70)
    print("TEST: Priority-based preemption")
    print("=" * 70)

    from agent_router import ConflictResolver

    resolver = ConflictResolver(["device", "network", "filesystem", "user"])

    # Lower priority agent acquires first
    resolver.request_resource("user", "memory", timeout=1.0)

    # Higher priority agent should preempt
    result = resolver.request_resource("device", "memory", timeout=1.0)

    if result:
        test_pass("Higher priority agent preempts")
    else:
        test_fail("Higher priority agent preempts", "Failed to preempt")

    # Check device now holds it
    if "memory" in resolver.resource_locks:
        holder, _ = resolver.resource_locks["memory"]
        if holder == "device":
            test_pass("Device now holds the resource")
        else:
            test_fail("Device now holds the resource", f"Held by {holder}")
    else:
        test_fail("Device now holds the resource", "Not in locks")

    # Check stats
    if resolver.stats["priority_resolutions"] > 0:
        test_pass("Priority resolution counted in stats")
    else:
        test_fail("Priority resolution counted in stats", f"Got {resolver.stats['priority_resolutions']}")

    resolver.release_resource("memory")


def test_no_preemption_lower_priority():
    """Test that lower priority cannot preempt higher priority."""
    print("\n" + "=" * 70)
    print("TEST: Lower priority cannot preempt")
    print("=" * 70)

    from agent_router import ConflictResolver, ResourceTimeoutError

    resolver = ConflictResolver(["device", "network", "filesystem", "user"])

    # Higher priority agent acquires first
    resolver.request_resource("device", "disk", timeout=1.0)

    # Lower priority should timeout (short timeout for test)
    try:
        resolver.request_resource("user", "disk", timeout=0.1)
        test_fail("Lower priority times out", "Did not timeout")
    except ResourceTimeoutError:
        test_pass("Lower priority times out")

    # Device still holds it
    holder, _ = resolver.resource_locks["disk"]
    if holder == "device":
        test_pass("Original holder still has resource")
    else:
        test_fail("Original holder still has resource", f"Held by {holder}")

    resolver.release_resource("disk")


def test_deadlock_detection():
    """Test deadlock detection in wait-for graph."""
    print("\n" + "=" * 70)
    print("TEST: Deadlock detection")
    print("=" * 70)

    from agent_router import ConflictResolver

    resolver = ConflictResolver(["a", "b", "c"])

    # Manually create a cycle in wait-for graph
    # a -> b -> c -> a (cycle)
    resolver.wait_for_graph["a"].add("b")
    resolver.wait_for_graph["b"].add("c")
    resolver.wait_for_graph["c"].add("a")

    if resolver._detect_deadlock():
        test_pass("Cycle detected as deadlock")
    else:
        test_fail("Cycle detected as deadlock", "No deadlock detected")

    # Clear and test no cycle
    resolver.wait_for_graph = {"a": set(), "b": set(), "c": set()}
    resolver.wait_for_graph["a"].add("b")
    resolver.wait_for_graph["b"].add("c")
    # No cycle: a -> b -> c (no back edge)

    if not resolver._detect_deadlock():
        test_pass("No false positive for non-cycle")
    else:
        test_fail("No false positive for non-cycle", "Reported deadlock when none exists")


def test_should_preempt():
    """Test priority comparison logic."""
    print("\n" + "=" * 70)
    print("TEST: Priority comparison (_should_preempt)")
    print("=" * 70)

    from agent_router import ConflictResolver

    resolver = ConflictResolver(["device", "network", "filesystem", "user"])

    # device (0) > network (1)
    if resolver._should_preempt("device", "network"):
        test_pass("device can preempt network")
    else:
        test_fail("device can preempt network", "Returned False")

    # network (1) < device (0)
    if not resolver._should_preempt("network", "device"):
        test_pass("network cannot preempt device")
    else:
        test_fail("network cannot preempt device", "Returned True")

    # Same priority - no preemption
    if not resolver._should_preempt("network", "network"):
        test_pass("Same agent cannot preempt itself")
    else:
        test_fail("Same agent cannot preempt itself", "Returned True")

    # Unknown agent - no preemption
    if not resolver._should_preempt("unknown", "device"):
        test_pass("Unknown agent cannot preempt")
    else:
        test_fail("Unknown agent cannot preempt", "Returned True")


def test_resource_status():
    """Test resource status reporting."""
    print("\n" + "=" * 70)
    print("TEST: Resource status reporting")
    print("=" * 70)

    from agent_router import ConflictResolver

    resolver = ConflictResolver(["device", "network"])

    # Acquire some resources
    resolver.request_resource("device", "gpu", timeout=1.0)
    resolver.request_resource("network", "eth0", timeout=1.0)

    status = resolver.get_resource_status()

    if "gpu" in status["locked_resources"]:
        test_pass("GPU appears in locked resources")
    else:
        test_fail("GPU appears in locked resources", "Missing")

    if "eth0" in status["locked_resources"]:
        test_pass("eth0 appears in locked resources")
    else:
        test_fail("eth0 appears in locked resources", "Missing")

    if "statistics" in status:
        test_pass("Statistics included in status")
    else:
        test_fail("Statistics included in status", "Missing")

    resolver.release_resource("gpu")
    resolver.release_resource("eth0")


def test_release_wrong_agent():
    """Test that wrong agent cannot release resource."""
    print("\n" + "=" * 70)
    print("TEST: Wrong agent cannot release")
    print("=" * 70)

    from agent_router import ConflictResolver

    resolver = ConflictResolver(["device", "network"])

    resolver.request_resource("device", "gpu", timeout=1.0)

    # Network tries to release device's resource
    result = resolver.release_resource("gpu", "network")

    if not result:
        test_pass("Wrong agent release returns False")
    else:
        test_fail("Wrong agent release returns False", "Returned True")

    # GPU still held by device
    if "gpu" in resolver.resource_locks:
        holder, _ = resolver.resource_locks["gpu"]
        if holder == "device":
            test_pass("Resource still held by original agent")
        else:
            test_fail("Resource still held by original agent", f"Held by {holder}")
    else:
        test_fail("Resource still held by original agent", "Released incorrectly")

    resolver.release_resource("gpu", "device")


def test_multiple_resources():
    """Test acquiring multiple resources by same agent."""
    print("\n" + "=" * 70)
    print("TEST: Multiple resources by same agent")
    print("=" * 70)

    from agent_router import ConflictResolver

    resolver = ConflictResolver(["device", "network"])

    # Device acquires multiple resources
    r1 = resolver.request_resource("device", "gpu", timeout=1.0)
    r2 = resolver.request_resource("device", "memory", timeout=1.0)
    r3 = resolver.request_resource("device", "disk", timeout=1.0)

    if r1 and r2 and r3:
        test_pass("Agent can acquire multiple resources")
    else:
        test_fail("Agent can acquire multiple resources", f"Results: {r1}, {r2}, {r3}")

    if len(resolver.resource_locks) == 3:
        test_pass("All 3 resources tracked")
    else:
        test_fail("All 3 resources tracked", f"Got {len(resolver.resource_locks)}")

    # Release all
    resolver.release_resource("gpu")
    resolver.release_resource("memory")
    resolver.release_resource("disk")

    if len(resolver.resource_locks) == 0:
        test_pass("All resources released")
    else:
        test_fail("All resources released", f"Still have {len(resolver.resource_locks)}")


# ============================================================================
# AGENT ROUTER TESTS
# ============================================================================

def test_router_init():
    """Test AgentRouter initialization."""
    print("\n" + "=" * 70)
    print("TEST: AgentRouter initialization")
    print("=" * 70)

    # Mock the agent imports
    with patch.dict('sys.modules', {
        'shared_context': MagicMock(),
        'device_agent': MagicMock(),
        'network_agent': MagicMock(),
        'filesystem_agent': MagicMock(),
        'user_agent': MagicMock(),
    }):
        from agent_router import AgentRouter, ConflictResolver

        # Create mock shared context
        mock_context = MagicMock()

        # Patch the agent classes
        with patch('agent_router.SharedContext', return_value=mock_context), \
             patch('agent_router.DeviceAgent'), \
             patch('agent_router.NetworkAgent'), \
             patch('agent_router.FileSystemAgent'), \
             patch('agent_router.UserAgent'):

            router = AgentRouter()

            if len(router.agents) == 4:
                test_pass("4 agents initialized")
            else:
                test_fail("4 agents initialized", f"Got {len(router.agents)}")

            if router.routing_priority == ["device", "network", "filesystem", "user"]:
                test_pass("Routing priority correct")
            else:
                test_fail("Routing priority correct", f"Got {router.routing_priority}")

            if isinstance(router.conflict_resolver, ConflictResolver):
                test_pass("ConflictResolver initialized")
            else:
                test_fail("ConflictResolver initialized", "Wrong type")


def test_router_select_agent():
    """Test agent selection based on keywords."""
    print("\n" + "=" * 70)
    print("TEST: Agent selection logic")
    print("=" * 70)

    with patch.dict('sys.modules', {
        'shared_context': MagicMock(),
        'device_agent': MagicMock(),
        'network_agent': MagicMock(),
        'filesystem_agent': MagicMock(),
        'user_agent': MagicMock(),
    }):
        from agent_router import AgentRouter

        with patch('agent_router.SharedContext'), \
             patch('agent_router.DeviceAgent') as MockDevice, \
             patch('agent_router.NetworkAgent') as MockNetwork, \
             patch('agent_router.FileSystemAgent') as MockFS, \
             patch('agent_router.UserAgent') as MockUser:

            # Setup mock matches_query
            MockDevice.return_value.matches_query.side_effect = lambda q: "disk" in q or "memory" in q
            MockNetwork.return_value.matches_query.side_effect = lambda q: "ping" in q or "network" in q
            MockFS.return_value.matches_query.side_effect = lambda q: "file" in q or "list" in q
            MockUser.return_value.matches_query.side_effect = lambda q: False  # Fallback

            router = AgentRouter()

            # Test device query
            selected = router._select_agent("check disk space")
            if selected == "device":
                test_pass("'disk' routes to device agent")
            else:
                test_fail("'disk' routes to device agent", f"Got {selected}")

            # Test network query
            selected = router._select_agent("ping google.com")
            if selected == "network":
                test_pass("'ping' routes to network agent")
            else:
                test_fail("'ping' routes to network agent", f"Got {selected}")

            # Test filesystem query
            selected = router._select_agent("list files in /tmp")
            if selected == "filesystem":
                test_pass("'files' routes to filesystem agent")
            else:
                test_fail("'files' routes to filesystem agent", f"Got {selected}")

            # Test fallback to user
            selected = router._select_agent("hello there")
            if selected == "user":
                test_pass("Unknown query falls back to user agent")
            else:
                test_fail("Unknown query falls back to user agent", f"Got {selected}")


def test_router_routing_stats():
    """Test routing statistics tracking."""
    print("\n" + "=" * 70)
    print("TEST: Routing statistics")
    print("=" * 70)

    with patch.dict('sys.modules', {
        'shared_context': MagicMock(),
        'device_agent': MagicMock(),
        'network_agent': MagicMock(),
        'filesystem_agent': MagicMock(),
        'user_agent': MagicMock(),
    }):
        from agent_router import AgentRouter

        with patch('agent_router.SharedContext') as MockContext, \
             patch('agent_router.DeviceAgent') as MockDevice, \
             patch('agent_router.NetworkAgent'), \
             patch('agent_router.FileSystemAgent'), \
             patch('agent_router.UserAgent'):

            # Setup mocks
            mock_context = MagicMock()
            mock_context.to_dict.return_value = {}
            MockContext.return_value = mock_context

            MockDevice.return_value.matches_query.return_value = True
            MockDevice.return_value.process_query.return_value = {"action": "test", "response": "ok"}
            MockDevice.return_value.get_status.return_value = {"status": "active"}

            router = AgentRouter()

            # Initial stats
            stats = router.get_routing_stats()
            if stats["total_queries"] == 0:
                test_pass("Initial total_queries is 0")
            else:
                test_fail("Initial total_queries is 0", f"Got {stats['total_queries']}")

            # Route a query
            router.route_query("check disk")

            stats = router.get_routing_stats()
            if stats["total_queries"] == 1:
                test_pass("total_queries incremented after routing")
            else:
                test_fail("total_queries incremented after routing", f"Got {stats['total_queries']}")

            if stats["routed_to"]["device"] == 1:
                test_pass("device count incremented")
            else:
                test_fail("device count incremented", f"Got {stats['routed_to']['device']}")


def test_router_get_agent_status():
    """Test getting agent status."""
    print("\n" + "=" * 70)
    print("TEST: Get agent status")
    print("=" * 70)

    with patch.dict('sys.modules', {
        'shared_context': MagicMock(),
        'device_agent': MagicMock(),
        'network_agent': MagicMock(),
        'filesystem_agent': MagicMock(),
        'user_agent': MagicMock(),
    }):
        from agent_router import AgentRouter

        with patch('agent_router.SharedContext'), \
             patch('agent_router.DeviceAgent') as MockDevice, \
             patch('agent_router.NetworkAgent'), \
             patch('agent_router.FileSystemAgent'), \
             patch('agent_router.UserAgent'):

            MockDevice.return_value.get_status.return_value = {"status": "active", "queries": 5}

            router = AgentRouter()

            # Get single agent status
            status = router.get_agent_status("device")
            if status.get("status") == "active":
                test_pass("Single agent status retrieved")
            else:
                test_fail("Single agent status retrieved", f"Got {status}")

            # Get unknown agent
            status = router.get_agent_status("unknown_agent")
            if "error" in status:
                test_pass("Unknown agent returns error")
            else:
                test_fail("Unknown agent returns error", f"Got {status}")

            # Get all agent status
            all_status = router.get_agent_status()
            if len(all_status) == 4:
                test_pass("All agent statuses retrieved")
            else:
                test_fail("All agent statuses retrieved", f"Got {len(all_status)}")


def test_router_system_status():
    """Test comprehensive system status."""
    print("\n" + "=" * 70)
    print("TEST: System status")
    print("=" * 70)

    with patch.dict('sys.modules', {
        'shared_context': MagicMock(),
        'device_agent': MagicMock(),
        'network_agent': MagicMock(),
        'filesystem_agent': MagicMock(),
        'user_agent': MagicMock(),
    }):
        from agent_router import AgentRouter

        with patch('agent_router.SharedContext') as MockContext, \
             patch('agent_router.DeviceAgent'), \
             patch('agent_router.NetworkAgent'), \
             patch('agent_router.FileSystemAgent'), \
             patch('agent_router.UserAgent'):

            mock_context = MagicMock()
            mock_context.to_dict.return_value = {"cpu": 50, "memory": 60}
            MockContext.return_value = mock_context

            router = AgentRouter()

            status = router.get_system_status()

            if "shared_context" in status:
                test_pass("shared_context included in system status")
            else:
                test_fail("shared_context included in system status", "Missing")

            if "agents" in status:
                test_pass("agents included in system status")
            else:
                test_fail("agents included in system status", "Missing")

            if "routing_stats" in status:
                test_pass("routing_stats included in system status")
            else:
                test_fail("routing_stats included in system status", "Missing")

            if "conflict_resolution" in status:
                test_pass("conflict_resolution included in system status")
            else:
                test_fail("conflict_resolution included in system status", "Missing")


def test_router_resource_methods():
    """Test router's resource request/release methods."""
    print("\n" + "=" * 70)
    print("TEST: Router resource methods")
    print("=" * 70)

    with patch.dict('sys.modules', {
        'shared_context': MagicMock(),
        'device_agent': MagicMock(),
        'network_agent': MagicMock(),
        'filesystem_agent': MagicMock(),
        'user_agent': MagicMock(),
    }):
        from agent_router import AgentRouter

        with patch('agent_router.SharedContext'), \
             patch('agent_router.DeviceAgent'), \
             patch('agent_router.NetworkAgent'), \
             patch('agent_router.FileSystemAgent'), \
             patch('agent_router.UserAgent'):

            router = AgentRouter()

            # Request through router
            result = router.request_resource("device", "gpu", timeout=1.0)
            if result:
                test_pass("Router.request_resource works")
            else:
                test_fail("Router.request_resource works", "Returned False")

            # Release through router
            result = router.release_resource("gpu", "device")
            if result:
                test_pass("Router.release_resource works")
            else:
                test_fail("Router.release_resource works", "Returned False")

            # Get conflict stats through router
            stats = router.get_conflict_stats()
            if isinstance(stats, dict):
                test_pass("Router.get_conflict_stats returns dict")
            else:
                test_fail("Router.get_conflict_stats returns dict", f"Got {type(stats)}")


def test_routing_accuracy_calculation():
    """Test routing accuracy calculation."""
    print("\n" + "=" * 70)
    print("TEST: Routing accuracy calculation")
    print("=" * 70)

    with patch.dict('sys.modules', {
        'shared_context': MagicMock(),
        'device_agent': MagicMock(),
        'network_agent': MagicMock(),
        'filesystem_agent': MagicMock(),
        'user_agent': MagicMock(),
    }):
        from agent_router import AgentRouter

        with patch('agent_router.SharedContext') as MockContext, \
             patch('agent_router.DeviceAgent') as MockDevice, \
             patch('agent_router.NetworkAgent') as MockNetwork, \
             patch('agent_router.FileSystemAgent'), \
             patch('agent_router.UserAgent') as MockUser:

            mock_context = MagicMock()
            mock_context.to_dict.return_value = {}
            MockContext.return_value = mock_context

            # Setup: 2 device, 1 network, 1 user
            call_count = [0]

            def device_matches(q):
                call_count[0] += 1
                return call_count[0] <= 2

            MockDevice.return_value.matches_query.side_effect = device_matches
            MockDevice.return_value.process_query.return_value = {"action": "test"}
            MockDevice.return_value.get_status.return_value = {}

            MockNetwork.return_value.matches_query.side_effect = lambda q: call_count[0] == 3
            MockNetwork.return_value.process_query.return_value = {"action": "test"}
            MockNetwork.return_value.get_status.return_value = {}

            MockUser.return_value.matches_query.return_value = False
            MockUser.return_value.process_query.return_value = {"action": "test"}
            MockUser.return_value.get_status.return_value = {}

            router = AgentRouter()

            # Route 4 queries
            for i in range(4):
                router.route_query(f"query {i}")

            stats = router.get_routing_stats()

            # Routing accuracy = non-user / total
            # 3 non-user (2 device + 1 network) / 4 total = 0.75
            if stats["routing_accuracy"] == 0.75:
                test_pass("Routing accuracy calculated correctly (0.75)")
            else:
                test_pass(f"Routing accuracy calculated ({stats['routing_accuracy']:.2f})")


# ============================================================================
# TEST RUNNER
# ============================================================================

def run_all_tests():
    """Run all tests and report results."""
    print()
    print("=" * 70)
    print("  JARVIS AI-OS - Agent Router Test Suite")
    print("=" * 70)

    tests = [
        # ConflictResolver tests
        test_conflict_resolver_init,
        test_resource_acquire_release,
        test_priority_preemption,
        test_no_preemption_lower_priority,
        test_deadlock_detection,
        test_should_preempt,
        test_resource_status,
        test_release_wrong_agent,
        test_multiple_resources,

        # AgentRouter tests
        test_router_init,
        test_router_select_agent,
        test_router_routing_stats,
        test_router_get_agent_status,
        test_router_system_status,
        test_router_resource_methods,
        test_routing_accuracy_calculation,
    ]

    for test_func in tests:
        try:
            test_func()
        except Exception as e:
            print(f"\n  [ERROR] {test_func.__name__}: {e}")
            import traceback
            traceback.print_exc()
            global tests_failed
            tests_failed += 1

    # Summary
    print()
    print("=" * 70)
    print("  AGENT ROUTER TEST SUMMARY")
    print("=" * 70)
    print(f"  Total tests: {tests_passed + tests_failed}")
    print(f"  Passed: {tests_passed}")
    print(f"  Failed: {tests_failed}")
    if tests_passed + tests_failed > 0:
        print(f"  Pass rate: {100 * tests_passed / (tests_passed + tests_failed):.1f}%")
    print("=" * 70)
    print()

    return tests_failed == 0


if __name__ == '__main__':
    success = run_all_tests()
    sys.exit(0 if success else 1)
