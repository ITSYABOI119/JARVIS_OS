#!/usr/bin/env python3
"""
Quick test of shell + multi-agent integration
"""

import sys
from pathlib import Path

# Add directories for imports
sys.path.insert(0, str(Path(__file__).parent.parent.parent / 'src' / 'shell'))
sys.path.insert(0, str(Path(__file__).parent.parent.parent / 'src' / 'ai'))

from agent_router import AgentRouter

print("="*70)
print("Testing Shell + Multi-Agent Integration")
print("="*70)

# Create router
print("\n1. Creating agent router...")
router = AgentRouter()
print(f"   Agents: {list(router.agents.keys())}")

# Test queries (simulating shell input)
test_queries = [
    "show disk space",
    "ping google.com",
    "list files in .",
    "help",
    "status",
]

print("\n2. Testing queries:")
for query in test_queries:
    print(f"\n   Query: {query}")
    response = router.route_query(query)

    agent = response['routing']['selected_agent']
    action = response['action']
    routing_time = response['routing']['routing_time_ms']
    total_time = response['routing']['total_response_time_ms']

    print(f"   Agent: {agent}")
    print(f"   Action: {action}")
    print(f"   Routing: {routing_time:.3f}ms | Total: {total_time:.2f}ms")

# Get routing stats
print("\n3. Routing Statistics:")
stats = router.get_routing_stats()
print(f"   Total Queries: {stats['total_queries']}")
print(f"   Avg Routing Time: {stats['avg_routing_time_ms']:.3f}ms")
print(f"   Routing Accuracy: {stats['routing_accuracy']*100:.1f}%")

print("\n" + "="*70)
print("Shell + Multi-Agent Integration Test Complete!")
print("="*70)
print("\nNext: Run 'python shell.py' to test interactively")
