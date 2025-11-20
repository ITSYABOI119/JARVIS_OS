#!/usr/bin/env python3
"""
JARVIS AI-OS Phase 1 - Agent Router

Routes queries to appropriate specialist agents based on keywords.
Manages agent coordination and shared context.
"""

import time
from typing import Dict, Any, Optional
from shared_context import SharedContext
from device_agent import DeviceAgent
from network_agent import NetworkAgent
from filesystem_agent import FileSystemAgent
from user_agent import UserAgent


class AgentRouter:
    """
    Agent Router

    Responsibilities:
    - Route queries to correct agent based on keywords
    - Manage shared context pool
    - Collect and return agent responses
    - Track routing statistics
    """

    def __init__(self, shared_context: Optional[SharedContext] = None):
        """
        Initialize Agent Router

        Args:
            shared_context: Optional pre-created shared context (creates new if None)
        """
        # Create or use provided shared context
        self.shared_context = shared_context if shared_context else SharedContext()

        # Initialize all 4 specialist agents
        self.agents = {
            "device": DeviceAgent(shared_context=self.shared_context),
            "network": NetworkAgent(shared_context=self.shared_context),
            "filesystem": FileSystemAgent(shared_context=self.shared_context),
            "user": UserAgent(shared_context=self.shared_context),
        }

        # Routing priority (checked in this order)
        self.routing_priority = ["device", "network", "filesystem", "user"]

        # Routing statistics
        self.routing_stats = {
            "total_queries": 0,
            "routed_to": {
                "device": 0,
                "network": 0,
                "filesystem": 0,
                "user": 0
            },
            "routing_times_ms": [],
            "total_response_times_ms": [],
        }

    def route_query(self, query: str) -> Dict[str, Any]:
        """
        Route query to appropriate agent

        Args:
            query: User query

        Returns:
            dict: Agent response with routing metadata
        """
        start_time = time.time()

        # Select agent based on keywords
        selected_agent_name = self._select_agent(query)
        selected_agent = self.agents[selected_agent_name]

        # Calculate routing time
        routing_time_ms = (time.time() - start_time) * 1000

        # Get shared context for agent
        context = self.shared_context.to_dict()

        # Process query with selected agent
        response = selected_agent.process_query(query, context)

        # Calculate total response time
        total_response_time_ms = (time.time() - start_time) * 1000

        # Update routing statistics
        self.routing_stats["total_queries"] += 1
        self.routing_stats["routed_to"][selected_agent_name] += 1
        self.routing_stats["routing_times_ms"].append(routing_time_ms)
        self.routing_stats["total_response_times_ms"].append(total_response_time_ms)

        # Update agent status in shared context
        self.shared_context.update_agent_status(
            selected_agent_name,
            selected_agent.get_status()
        )

        # Add routing metadata to response
        response["routing"] = {
            "selected_agent": selected_agent_name,
            "routing_time_ms": routing_time_ms,
            "total_response_time_ms": total_response_time_ms
        }

        return response

    def _select_agent(self, query: str) -> str:
        """
        Select agent based on keyword matching

        Priority: device > network > filesystem > user (fallback)

        Args:
            query: User query

        Returns:
            str: Selected agent name
        """
        query_lower = query.lower()

        # Check each agent in priority order
        for agent_name in self.routing_priority:
            agent = self.agents[agent_name]
            if agent.matches_query(query_lower):
                return agent_name

        # Default fallback to user agent
        return "user"

    def get_routing_stats(self) -> Dict[str, Any]:
        """
        Get routing statistics

        Returns:
            dict: Routing statistics
        """
        stats = {
            "total_queries": self.routing_stats["total_queries"],
            "routed_to": dict(self.routing_stats["routed_to"]),
        }

        # Calculate averages
        if self.routing_stats["routing_times_ms"]:
            stats["avg_routing_time_ms"] = sum(self.routing_stats["routing_times_ms"]) / len(self.routing_stats["routing_times_ms"])
        else:
            stats["avg_routing_time_ms"] = 0.0

        if self.routing_stats["total_response_times_ms"]:
            stats["avg_total_response_time_ms"] = sum(self.routing_stats["total_response_times_ms"]) / len(self.routing_stats["total_response_times_ms"])
        else:
            stats["avg_total_response_time_ms"] = 0.0

        # Calculate routing accuracy (percentage routed to non-user agents)
        non_user_queries = sum(count for name, count in stats["routed_to"].items() if name != "user")
        if stats["total_queries"] > 0:
            stats["routing_accuracy"] = non_user_queries / stats["total_queries"]
        else:
            stats["routing_accuracy"] = 0.0

        return stats

    def get_agent_status(self, agent_name: Optional[str] = None) -> Dict[str, Any]:
        """
        Get agent status

        Args:
            agent_name: Specific agent name, or None for all agents

        Returns:
            dict: Agent status
        """
        if agent_name:
            if agent_name in self.agents:
                return self.agents[agent_name].get_status()
            else:
                return {"error": f"Unknown agent: {agent_name}"}
        else:
            # Return all agent status
            return {
                name: agent.get_status()
                for name, agent in self.agents.items()
            }

    def get_system_status(self) -> Dict[str, Any]:
        """
        Get comprehensive system status

        Returns:
            dict: System status including context and agents
        """
        return {
            "shared_context": self.shared_context.to_dict(),
            "agents": self.get_agent_status(),
            "routing_stats": self.get_routing_stats()
        }


if __name__ == "__main__":
    # Test Agent Router
    print("="*70)
    print("Testing Agent Router")
    print("="*70)

    router = AgentRouter()

    print("\nInitial Status:")
    print(f"Agents loaded: {list(router.agents.keys())}")
    print(f"Routing priority: {router.routing_priority}")

    # Test queries (one for each agent)
    test_queries = [
        ("show disk space", "device"),
        ("ping google.com", "network"),
        ("list files in /tmp", "filesystem"),
        ("help", "user"),
        ("show memory usage", "device"),
        ("check internet connection", "network"),
        ("find *.py", "filesystem"),
        ("what is JARVIS?", "user"),
    ]

    print("\n" + "="*70)
    print("Testing Query Routing")
    print("="*70)

    for query, expected_agent in test_queries:
        print(f"\nQuery: {query}")
        print(f"Expected Agent: {expected_agent}")

        response = router.route_query(query)

        actual_agent = response["routing"]["selected_agent"]
        routing_time = response["routing"]["routing_time_ms"]
        total_time = response["routing"]["total_response_time_ms"]

        print(f"Actual Agent: {actual_agent}")
        print(f"Routing Time: {routing_time:.2f}ms")
        print(f"Total Time: {total_time:.2f}ms")
        print(f"Action: {response['action']}")

        if actual_agent == expected_agent:
            print("[PASS] Routing CORRECT")
        else:
            print(f"[FAIL] Routing INCORRECT (expected {expected_agent}, got {actual_agent})")

    # Routing statistics
    print("\n" + "="*70)
    print("Routing Statistics")
    print("="*70)

    stats = router.get_routing_stats()
    print(f"Total Queries: {stats['total_queries']}")
    print(f"Avg Routing Time: {stats['avg_routing_time_ms']:.2f}ms")
    print(f"Avg Total Response Time: {stats['avg_total_response_time_ms']:.2f}ms")
    print(f"Routing Accuracy: {stats['routing_accuracy']*100:.1f}%")
    print("\nQueries Routed To:")
    for agent, count in stats['routed_to'].items():
        percentage = (count / stats['total_queries'] * 100) if stats['total_queries'] > 0 else 0
        print(f"  {agent}: {count} ({percentage:.1f}%)")

    # Agent status
    print("\n" + "="*70)
    print("Agent Status")
    print("="*70)

    agent_status = router.get_agent_status()
    for agent_name, status in agent_status.items():
        stats_data = status['statistics']
        print(f"\n{agent_name.upper()} Agent:")
        print(f"  Status: {status['status']}")
        print(f"  Queries: {stats_data['total_queries']}")
        print(f"  Success Rate: {stats_data['success_rate']*100:.1f}%")
        print(f"  Avg Response Time: {stats_data['avg_response_time_ms']:.2f}ms")

    print("\n" + "="*70)
    print("Agent Router test complete!")
