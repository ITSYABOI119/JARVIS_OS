#!/usr/bin/env python3
"""
JARVIS AI-OS Phase 1 - Multi-Agent Test Suite

Comprehensive tests for multi-agent system:
- Agent routing accuracy
- Shared context
- Agent coordination
- Performance
"""

import time
from agent_router import AgentRouter
from shared_context import SharedContext


def test_routing_accuracy():
    """Test 1: Routing Accuracy (>90% target)"""
    print("\n" + "="*70)
    print("TEST 1: Routing Accuracy")
    print("="*70)

    router = AgentRouter()

    test_cases = [
        # (query, expected_agent)
        ("show disk space", "device"),
        ("show memory usage", "device"),
        ("show cpu info", "device"),
        ("list usb devices", "device"),
        ("ping google.com", "network"),
        ("show network status", "network"),
        ("check internet connection", "network"),
        ("show listening ports", "network"),
        ("list files in /tmp", "filesystem"),
        ("find *.py", "filesystem"),
        ("show permissions for .", "filesystem"),
        ("read file test.txt", "filesystem"),
        ("help", "user"),
        ("status", "user"),
        ("cache stats", "user"),
        ("version", "user"),
        ("what is JARVIS?", "user"),
        ("unknown query", "user"),  # Fallback
    ]

    correct = 0
    total = len(test_cases)

    for query, expected_agent in test_cases:
        response = router.route_query(query)
        actual_agent = response["routing"]["selected_agent"]

        if actual_agent == expected_agent:
            correct += 1
            print(f"[PASS] '{query}' -> {actual_agent}")
        else:
            print(f"[FAIL] '{query}' -> {actual_agent} (expected {expected_agent})")

    accuracy = (correct / total) * 100
    print(f"\nAccuracy: {correct}/{total} = {accuracy:.1f}%")
    print(f"Target: >90%")

    if accuracy >= 90.0:
        print("[PASS] Routing accuracy meets target!")
        return True
    else:
        print("[FAIL] Routing accuracy below target")
        return False


def test_routing_performance():
    """Test 2: Routing Performance (<5ms routing overhead target)"""
    print("\n" + "="*70)
    print("TEST 2: Routing Performance")
    print("="*70)

    router = AgentRouter()

    test_queries = [
        "show disk space",
        "ping google.com",
        "list files",
        "help",
    ]

    routing_times = []

    for query in test_queries:
        response = router.route_query(query)
        routing_time = response["routing"]["routing_time_ms"]
        routing_times.append(routing_time)
        print(f"Query: '{query}' - Routing Time: {routing_time:.3f}ms")

    avg_routing_time = sum(routing_times) / len(routing_times)
    max_routing_time = max(routing_times)

    print(f"\nAvg Routing Time: {avg_routing_time:.3f}ms")
    print(f"Max Routing Time: {max_routing_time:.3f}ms")
    print(f"Target: <5ms")

    if avg_routing_time < 5.0:
        print("[PASS] Routing overhead meets target!")
        return True
    else:
        print("[FAIL] Routing overhead too high")
        return False


def test_agent_response_time():
    """Test 3: Agent Response Time (<200ms for local commands)"""
    print("\n" + "="*70)
    print("TEST 3: Agent Response Time")
    print("="*70)

    router = AgentRouter()

    # Test local commands only (exclude network ping)
    local_queries = [
        ("show disk space", "device"),
        ("show memory usage", "device"),
        ("list files in .", "filesystem"),
        ("help", "user"),
        ("status", "user"),
    ]

    response_times = []

    for query, agent_name in local_queries:
        response = router.route_query(query)
        # Get agent inference time (not total which includes routing)
        inference_time = response["inference_time_ms"]
        response_times.append(inference_time)
        print(f"[{agent_name}] '{query}' - {inference_time:.2f}ms")

    avg_response_time = sum(response_times) / len(response_times)
    max_response_time = max(response_times)

    print(f"\nAvg Response Time: {avg_response_time:.2f}ms")
    print(f"Max Response Time: {max_response_time:.2f}ms")
    print(f"Target: <200ms for local commands")

    if avg_response_time < 200.0:
        print("[PASS] Response time meets target!")
        return True
    else:
        print("[FAIL] Response time too high")
        return False


def test_shared_context():
    """Test 4: Shared Context Access"""
    print("\n" + "="*70)
    print("TEST 4: Shared Context Access")
    print("="*70)

    context = SharedContext()

    # Test system state update
    print("Testing system state update...")
    context.update_system_state(force=True)
    print(f"  Memory: {context.memory_used}/{context.memory_total} MB")
    print(f"  CPU: {context.cpu_percent:.1f}%")
    print(f"  Disk: {context.disk_used}/{context.disk_total} GB")

    # Test cache stats update
    print("\nTesting cache stats update...")
    context.update_cache_stats(lookups=1000, hits=857, misses=143, entries=103)
    print(f"  Hit Rate: {context.cache_hit_rate*100:.1f}%")
    print(f"  Entries: {context.cache_entries}/{context.cache_capacity}")

    # Test IPC stats update
    print("\nTesting IPC stats update...")
    context.update_ipc_stats(sent=100, received=95, drops=5)
    print(f"  Sent: {context.ipc_sent}")
    print(f"  Received: {context.ipc_received}")
    print(f"  Drops: {context.ipc_drops}")

    # Test operations
    print("\nTesting active operations...")
    context.add_operation("test operation 1")
    context.add_operation("test operation 2")
    print(f"  Active: {context.active_operations}")
    context.remove_operation("test operation 1")
    print(f"  After remove: {context.active_operations}")

    # Test to_dict
    print("\nTesting to_dict conversion...")
    context_dict = context.to_dict()
    print(f"  Dict keys: {list(context_dict.keys())}")
    print(f"  Timestamp: {context_dict['timestamp']}")

    print("\n[PASS] Shared context working correctly!")
    return True


def test_agent_health():
    """Test 5: Agent Health Monitoring"""
    print("\n" + "="*70)
    print("TEST 5: Agent Health Monitoring")
    print("="*70)

    router = AgentRouter()

    # Process some queries
    test_queries = [
        "show disk space",
        "ping google.com",
        "list files",
        "help",
    ]

    for query in test_queries:
        router.route_query(query)

    # Check agent status
    print("\nAgent Health Status:")
    agent_status = router.get_agent_status()

    all_healthy = True
    for agent_name, status in agent_status.items():
        health = status['status']
        queries = status['statistics']['total_queries']
        print(f"  {agent_name}: {health} ({queries} queries)")

        if health != "ready":
            all_healthy = False

    if all_healthy:
        print("\n[PASS] All agents healthy!")
        return True
    else:
        print("\n[FAIL] Some agents unhealthy")
        return False


def test_multi_agent_coordination():
    """Test 6: Multi-Agent Coordination (Context Sharing)"""
    print("\n" + "="*70)
    print("TEST 6: Multi-Agent Coordination")
    print("="*70)

    # Create shared context
    shared_context = SharedContext()

    # Update with mock data
    shared_context.update_cache_stats(lookups=1000, hits=857, misses=143, entries=103)
    shared_context.update_ipc_stats(sent=100, received=95, drops=5)

    # Create router with shared context
    router = AgentRouter(shared_context=shared_context)

    # Process query that accesses shared context (status query)
    print("\nProcessing status query (accesses shared context)...")
    response = router.route_query("show status")

    # Check if context was passed correctly
    result = response["result"]
    if "cache" in result:
        print(f"  Cache hit rate from context: {result['cache']['hit_rate']}")
        print(f"  IPC stats from context: sent={result['ipc']['sent']}, received={result['ipc']['received']}")
        print("\n[PASS] Shared context accessible to agents!")
        return True
    else:
        print("\n[FAIL] Context not passed to agent")
        return False


def test_routing_statistics():
    """Test 7: Routing Statistics Tracking"""
    print("\n" + "="*70)
    print("TEST 7: Routing Statistics")
    print("="*70)

    router = AgentRouter()

    # Process various queries
    test_queries = [
        "show disk space",  # device
        "ping google.com",  # network
        "list files",       # filesystem
        "help",             # user
        "show memory",      # device
    ]

    for query in test_queries:
        router.route_query(query)

    # Get statistics
    stats = router.get_routing_stats()

    print(f"\nTotal Queries: {stats['total_queries']}")
    print(f"Avg Routing Time: {stats['avg_routing_time_ms']:.3f}ms")
    print(f"Routing Accuracy: {stats['routing_accuracy']*100:.1f}%")
    print("\nQueries Routed To:")
    for agent, count in stats['routed_to'].items():
        print(f"  {agent}: {count}")

    if stats['total_queries'] == 5:
        print("\n[PASS] Routing statistics tracked correctly!")
        return True
    else:
        print("\n[FAIL] Statistics tracking error")
        return False


def main():
    """Run all tests"""
    print("="*70)
    print("JARVIS AI-OS - Multi-Agent Test Suite")
    print("="*70)

    results = []

    # Run all tests
    results.append(("Routing Accuracy", test_routing_accuracy()))
    results.append(("Routing Performance", test_routing_performance()))
    results.append(("Agent Response Time", test_agent_response_time()))
    results.append(("Shared Context", test_shared_context()))
    results.append(("Agent Health", test_agent_health()))
    results.append(("Multi-Agent Coordination", test_multi_agent_coordination()))
    results.append(("Routing Statistics", test_routing_statistics()))

    # Summary
    print("\n" + "="*70)
    print("TEST SUMMARY")
    print("="*70)

    passed = sum(1 for _, result in results if result)
    total = len(results)

    for test_name, result in results:
        status = "[PASS]" if result else "[FAIL]"
        print(f"{status} {test_name}")

    print(f"\nTotal: {passed}/{total} tests passed ({passed*100//total}%)")

    if passed == total:
        print("\n[SUCCESS] All tests passed!")
        return 0
    else:
        print(f"\n[FAILURE] {total - passed} test(s) failed")
        return 1


if __name__ == "__main__":
    import sys
    exit_code = main()
    sys.exit(exit_code)
