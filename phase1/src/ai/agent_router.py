#!/usr/bin/env python3
"""
JARVIS AI-OS Phase 1 - Agent Router

Routes queries to appropriate specialist agents based on keywords.
Manages agent coordination and shared context.
Implements conflict resolution for multi-agent coordination (Week 11).
"""

import time
import threading
from typing import Dict, Any, Optional, List, Set
from shared_context import SharedContext
from device_agent import DeviceAgent
from network_agent import NetworkAgent
from filesystem_agent import FileSystemAgent
from user_agent import UserAgent


class ConflictResolutionError(Exception):
    """Raised when conflict resolution fails"""
    pass


class DeadlockDetectedError(ConflictResolutionError):
    """Raised when deadlock is detected"""
    pass


class ResourceTimeoutError(ConflictResolutionError):
    """Raised when resource request times out"""
    pass


class ConflictResolver:
    """
    Conflict Resolution System (Week 11)

    Handles resource allocation, priority-based arbitration, and deadlock detection.

    Features:
    - Resource allocation tracking (which agent holds which resources)
    - Priority-based conflict resolution (device > network > filesystem > user)
    - Deadlock detection via wait-for graph
    - Timeout mechanism (5 second default)
    """

    def __init__(self, agent_priority: List[str]):
        """
        Initialize Conflict Resolver

        Args:
            agent_priority: List of agent names in priority order (highest first)
        """
        self.agent_priority = agent_priority

        # Resource allocation tracking
        # resource_name -> (agent_name, timestamp)
        self.resource_locks: Dict[str, tuple] = {}

        # Wait-for graph for deadlock detection
        # agent_name -> set of agents it's waiting for
        self.wait_for_graph: Dict[str, Set[str]] = {
            agent: set() for agent in agent_priority
        }

        # Pending resource requests
        # (agent_name, resource_name) -> timestamp
        self.pending_requests: Dict[tuple, float] = {}

        # Thread lock for safe concurrent access
        self.lock = threading.RLock()

        # Statistics
        self.stats = {
            "conflicts_resolved": 0,
            "deadlocks_detected": 0,
            "timeouts": 0,
            "priority_resolutions": 0
        }

    def request_resource(self, agent_name: str, resource_name: str, timeout: float = 5.0) -> bool:
        """
        Request exclusive access to a resource

        Args:
            agent_name: Name of requesting agent
            resource_name: Name of resource to acquire
            timeout: Maximum wait time in seconds (default: 5.0)

        Returns:
            bool: True if resource acquired, False on timeout

        Raises:
            DeadlockDetectedError: If deadlock is detected
        """
        with self.lock:
            start_time = time.time()
            request_time = start_time

            # Record pending request
            self.pending_requests[(agent_name, resource_name)] = request_time

            # Wait for resource to become available
            while (time.time() - start_time) < timeout:
                # Check if resource is available
                if resource_name not in self.resource_locks:
                    # Resource available - acquire it
                    self.resource_locks[resource_name] = (agent_name, time.time())

                    # Remove from pending requests
                    if (agent_name, resource_name) in self.pending_requests:
                        del self.pending_requests[(agent_name, resource_name)]

                    # Clear wait-for relationship
                    self.wait_for_graph[agent_name].clear()

                    return True

                # Resource is locked - check who holds it
                holder_agent, lock_time = self.resource_locks[resource_name]

                # Resolve conflict based on priority
                if self._should_preempt(agent_name, holder_agent):
                    # Higher priority agent can preempt lower priority
                    print(f"[CONFLICT] {agent_name} preempts {holder_agent} for resource '{resource_name}'")
                    self.stats["priority_resolutions"] += 1
                    self.stats["conflicts_resolved"] += 1

                    # Release resource from holder
                    self._force_release(resource_name, holder_agent)

                    # Acquire for requester
                    self.resource_locks[resource_name] = (agent_name, time.time())

                    # Remove from pending
                    if (agent_name, resource_name) in self.pending_requests:
                        del self.pending_requests[(agent_name, resource_name)]

                    # Clear wait-for relationship
                    self.wait_for_graph[agent_name].clear()

                    return True

                # Lower or equal priority - must wait
                # Update wait-for graph
                self.wait_for_graph[agent_name].add(holder_agent)

                # Check for deadlock
                if self._detect_deadlock():
                    self.stats["deadlocks_detected"] += 1
                    # Clear wait-for
                    self.wait_for_graph[agent_name].clear()
                    if (agent_name, resource_name) in self.pending_requests:
                        del self.pending_requests[(agent_name, resource_name)]
                    raise DeadlockDetectedError(
                        f"Deadlock detected: {agent_name} waiting for {holder_agent} on resource '{resource_name}'"
                    )

                # Wait a bit before retrying
                time.sleep(0.01)

            # Timeout reached
            self.stats["timeouts"] += 1

            # Clean up
            self.wait_for_graph[agent_name].clear()
            if (agent_name, resource_name) in self.pending_requests:
                del self.pending_requests[(agent_name, resource_name)]

            raise ResourceTimeoutError(
                f"Timeout: {agent_name} could not acquire resource '{resource_name}' within {timeout}s"
            )

    def release_resource(self, resource_name: str, agent_name: Optional[str] = None) -> bool:
        """
        Release a resource

        Args:
            resource_name: Name of resource to release
            agent_name: Name of agent releasing (optional, for validation)

        Returns:
            bool: True if released, False if not held
        """
        with self.lock:
            if resource_name not in self.resource_locks:
                return False

            holder_agent, _ = self.resource_locks[resource_name]

            # Validate holder if agent_name provided
            if agent_name and holder_agent != agent_name:
                print(f"[WARNING] {agent_name} trying to release resource held by {holder_agent}")
                return False

            # Release resource
            del self.resource_locks[resource_name]

            # Clear any wait-for relationships involving this resource
            # (agents waiting for the holder can now proceed)
            for agent in self.wait_for_graph:
                if holder_agent in self.wait_for_graph[agent]:
                    self.wait_for_graph[agent].remove(holder_agent)

            return True

    def _force_release(self, resource_name: str, agent_name: str):
        """Force release of a resource (for preemption)"""
        if resource_name in self.resource_locks:
            holder, _ = self.resource_locks[resource_name]
            if holder == agent_name:
                del self.resource_locks[resource_name]

    def _should_preempt(self, requester: str, holder: str) -> bool:
        """
        Determine if requester should preempt holder based on priority

        Priority order: device > network > filesystem > user

        Args:
            requester: Agent requesting resource
            holder: Agent currently holding resource

        Returns:
            bool: True if requester has higher priority
        """
        try:
            requester_priority = self.agent_priority.index(requester)
            holder_priority = self.agent_priority.index(holder)
            return requester_priority < holder_priority
        except ValueError:
            # Unknown agent - don't preempt
            return False

    def _detect_deadlock(self) -> bool:
        """
        Detect deadlock using cycle detection in wait-for graph

        Uses depth-first search to find cycles.

        Returns:
            bool: True if deadlock detected
        """
        # DFS-based cycle detection
        visited = set()
        rec_stack = set()

        def has_cycle(node):
            visited.add(node)
            rec_stack.add(node)

            # Check all neighbors
            for neighbor in self.wait_for_graph[node]:
                if neighbor not in visited:
                    if has_cycle(neighbor):
                        return True
                elif neighbor in rec_stack:
                    # Back edge found - cycle exists
                    return True

            rec_stack.remove(node)
            return False

        # Check all nodes
        for node in self.wait_for_graph:
            if node not in visited:
                if has_cycle(node):
                    return True

        return False

    def get_resource_status(self) -> Dict[str, Any]:
        """Get current resource allocation status"""
        with self.lock:
            return {
                "locked_resources": {
                    resource: {"holder": agent, "lock_time": timestamp}
                    for resource, (agent, timestamp) in self.resource_locks.items()
                },
                "pending_requests": {
                    f"{agent}->{resource}": timestamp
                    for (agent, resource), timestamp in self.pending_requests.items()
                },
                "wait_for_graph": {
                    agent: list(waiting_for)
                    for agent, waiting_for in self.wait_for_graph.items()
                    if waiting_for  # Only show non-empty
                },
                "statistics": dict(self.stats)
            }

    def get_statistics(self) -> Dict[str, int]:
        """Get conflict resolution statistics"""
        return dict(self.stats)


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

        # Initialize conflict resolver (Week 11)
        self.conflict_resolver = ConflictResolver(self.routing_priority)

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
            "routing_stats": self.get_routing_stats(),
            "conflict_resolution": self.conflict_resolver.get_resource_status()
        }

    def request_resource(self, agent_name: str, resource_name: str, timeout: float = 5.0) -> bool:
        """
        Request resource for an agent (Week 11)

        Args:
            agent_name: Agent requesting resource
            resource_name: Resource to acquire
            timeout: Maximum wait time in seconds

        Returns:
            bool: True if acquired

        Raises:
            DeadlockDetectedError: If deadlock detected
            ResourceTimeoutError: If timeout reached
        """
        return self.conflict_resolver.request_resource(agent_name, resource_name, timeout)

    def release_resource(self, resource_name: str, agent_name: Optional[str] = None) -> bool:
        """
        Release resource (Week 11)

        Args:
            resource_name: Resource to release
            agent_name: Agent releasing (optional)

        Returns:
            bool: True if released
        """
        return self.conflict_resolver.release_resource(resource_name, agent_name)

    def get_conflict_stats(self) -> Dict[str, int]:
        """
        Get conflict resolution statistics (Week 11)

        Returns:
            dict: Conflict resolution stats
        """
        return self.conflict_resolver.get_statistics()


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
