#!/usr/bin/env python3
"""
Quick Multi-Agent Test Script

Run this to test the multi-agent system without the shell.
No AI model loading required - just fast specialist agents.

Usage:
    python3 quick_multi_agent_test.py
"""

from agent_router import AgentRouter

print("=" * 70)
print("JARVIS Multi-Agent Quick Test")
print("=" * 70)
print()

# Initialize router
print("Initializing router...")
router = AgentRouter()
print("[SUCCESS] Router initialized with 4 agents!")
print()

# Test queries
queries = [
    'show disk space',
    'show memory usage',
    'show cpu',
    'ping google.com',
    'list files',
    'help',
    'status'
]

print("Testing queries:")
print("-" * 70)
print()

for query in queries:
    print(f"Query: '{query}'")

    try:
        response = router.route_query(query)

        agent = response['routing']['selected_agent']
        action = response['action']
        routing_time = response['routing']['routing_time_ms']
        response_time = response['inference_time_ms']

        print(f"  -> Routed to: {agent.upper()} Agent")
        print(f"  -> Action: {action}")
        print(f"  -> Timing: {routing_time:.3f}ms routing + {response_time:.2f}ms response")

        # Show result summary
        result = response['result']
        if 'error' in response:
            print(f"  -> Error: {response['error']}")
        elif 'summary' in result:
            print(f"  -> Result: {result['summary']}")
        elif isinstance(result, dict) and 'status' in result:
            print(f"  -> Result: {result}")
        else:
            print(f"  -> Result: OK")

    except Exception as e:
        print(f"  -> ERROR: {e}")

    print()

print("-" * 70)
print()

# Show statistics
print("Agent Statistics:")
print("-" * 70)
for agent_name, agent in router.agents.items():
    stats = agent.statistics  # Access statistics attribute directly
    query_count = stats.total_queries
    if query_count > 0:
        avg_time = stats.total_response_time_ms / query_count
        print(f"  {agent_name.capitalize():15} - {query_count} queries, {avg_time:.2f}ms avg")
    else:
        print(f"  {agent_name.capitalize():15} - 0 queries")

print()

routing_stats = router.get_routing_stats()
print(f"Overall routing time: {routing_stats['avg_routing_time_ms']:.3f}ms avg")
print()

print("=" * 70)
print("[SUCCESS] All tests completed!")
print("=" * 70)
