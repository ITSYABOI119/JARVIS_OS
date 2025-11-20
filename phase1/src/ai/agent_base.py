#!/usr/bin/env python3
"""
JARVIS AI-OS Phase 1 - Base Agent Class

Provides common interface and functionality for all specialist agents.
"""

import time
from typing import Dict, Any, Optional
from dataclasses import dataclass, field


@dataclass
class AgentStatistics:
    """Statistics tracking for agent"""
    total_queries: int = 0
    successful_queries: int = 0
    failed_queries: int = 0
    total_response_time_ms: float = 0.0
    cache_hits: int = 0
    cache_misses: int = 0

    @property
    def avg_response_time_ms(self) -> float:
        """Calculate average response time"""
        if self.total_queries == 0:
            return 0.0
        return self.total_response_time_ms / self.total_queries

    @property
    def success_rate(self) -> float:
        """Calculate success rate (0-1.0)"""
        if self.total_queries == 0:
            return 0.0
        return self.successful_queries / self.total_queries


class AgentBase:
    """
    Base class for all JARVIS specialist agents.

    Provides:
    - Common interface (process_query)
    - Statistics tracking
    - Health monitoring
    - Shared context access
    - Error handling
    """

    def __init__(self, name: str, keywords: list, model=None, shared_context=None):
        """
        Initialize base agent

        Args:
            name: Agent name (e.g., "device", "network")
            keywords: List of keywords this agent handles
            model: AI model instance (optional, can be set later)
            shared_context: Reference to shared context pool (optional)
        """
        self.name = name
        self.keywords = keywords
        self.model = model
        self.shared_context = shared_context
        self.statistics = AgentStatistics()
        self.status = "initializing"
        self.created_at = time.time()

    def process_query(self, query: str, context: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
        """
        Process a query and return response.

        This method must be implemented by subclasses.

        Args:
            query: The user query to process
            context: Optional context from shared context pool

        Returns:
            dict: Response with format:
                {
                    "agent": str,
                    "action": str,
                    "result": Any,
                    "trust_level": int,
                    "inference_time_ms": float,
                    "cache_hit": bool,
                    "error": str (optional)
                }
        """
        raise NotImplementedError(f"Agent {self.name} must implement process_query()")

    def _record_query(self, success: bool, response_time_ms: float, cache_hit: bool = False):
        """
        Record query statistics

        Args:
            success: Whether query was successful
            response_time_ms: Response time in milliseconds
            cache_hit: Whether this was a cache hit
        """
        self.statistics.total_queries += 1
        if success:
            self.statistics.successful_queries += 1
        else:
            self.statistics.failed_queries += 1

        self.statistics.total_response_time_ms += response_time_ms

        if cache_hit:
            self.statistics.cache_hits += 1
        else:
            self.statistics.cache_misses += 1

    def _create_response(
        self,
        action: str,
        result: Any,
        trust_level: int = 0,
        inference_time_ms: float = 0.0,
        cache_hit: bool = False,
        error: Optional[str] = None
    ) -> Dict[str, Any]:
        """
        Create standardized response dict

        Args:
            action: Action performed (e.g., "show_disk_space")
            result: Action result (any type)
            trust_level: Trust level (0=automatic, 1=notify, 2=request, 3=require)
            inference_time_ms: AI inference time
            cache_hit: Whether this was a cache hit
            error: Error message if query failed

        Returns:
            dict: Standardized response
        """
        response = {
            "agent": self.name,
            "action": action,
            "result": result,
            "trust_level": trust_level,
            "inference_time_ms": inference_time_ms,
            "cache_hit": cache_hit,
        }

        if error:
            response["error"] = error

        return response

    def get_status(self) -> Dict[str, Any]:
        """
        Get agent health status

        Returns:
            dict: Agent status information
        """
        uptime_seconds = time.time() - self.created_at

        return {
            "name": self.name,
            "status": self.status,
            "uptime_seconds": uptime_seconds,
            "keywords": self.keywords,
            "statistics": {
                "total_queries": self.statistics.total_queries,
                "successful_queries": self.statistics.successful_queries,
                "failed_queries": self.statistics.failed_queries,
                "success_rate": self.statistics.success_rate,
                "avg_response_time_ms": self.statistics.avg_response_time_ms,
                "cache_hits": self.statistics.cache_hits,
                "cache_misses": self.statistics.cache_misses,
            },
        }

    def set_model(self, model):
        """Set AI model for this agent"""
        self.model = model
        self.status = "ready"

    def set_shared_context(self, shared_context):
        """Set reference to shared context pool"""
        self.shared_context = shared_context

    def update_shared_context(self):
        """Update shared context with agent status"""
        if self.shared_context:
            self.shared_context.update_agent_status(self.name, self.get_status())

    def matches_query(self, query: str) -> bool:
        """
        Check if this agent's keywords match the query

        Args:
            query: User query

        Returns:
            bool: True if any keyword matches
        """
        query_lower = query.lower()
        return any(keyword in query_lower for keyword in self.keywords)

    def __repr__(self) -> str:
        return f"<{self.__class__.__name__} name='{self.name}' status='{self.status}'>"


class SimpleAgent(AgentBase):
    """
    Simple agent implementation for testing.

    Responds to queries with pre-defined actions without AI model.
    """

    def __init__(self, name: str, keywords: list, actions: Dict[str, str]):
        """
        Initialize simple agent

        Args:
            name: Agent name
            keywords: List of keywords
            actions: Dict mapping keywords to actions (e.g., {"disk": "show_disk_space"})
        """
        super().__init__(name, keywords)
        self.actions = actions
        self.status = "ready"

    def process_query(self, query: str, context: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
        """
        Process query using pre-defined action mapping

        Args:
            query: User query
            context: Optional context

        Returns:
            dict: Response
        """
        start_time = time.time()
        query_lower = query.lower()

        # Find matching action
        action = None
        for keyword, act in self.actions.items():
            if keyword in query_lower:
                action = act
                break

        if not action:
            action = "unknown"
            result = f"No matching action for query: {query}"
            success = False
        else:
            result = f"Executed action: {action}"
            success = True

        # Record statistics
        response_time_ms = (time.time() - start_time) * 1000
        self._record_query(success, response_time_ms, cache_hit=False)

        return self._create_response(
            action=action,
            result=result,
            trust_level=0,
            inference_time_ms=response_time_ms,
            cache_hit=False,
            error=None if success else "No matching action"
        )


if __name__ == "__main__":
    # Test agent base class
    print("Testing AgentBase...")

    # Create simple test agent
    agent = SimpleAgent(
        name="test",
        keywords=["disk", "memory", "cpu"],
        actions={
            "disk": "show_disk_space",
            "memory": "show_memory",
            "cpu": "show_cpu"
        }
    )

    print(f"Agent: {agent}")
    print(f"Status: {agent.get_status()}")

    # Test queries
    test_queries = [
        "show disk space",
        "show memory usage",
        "show cpu info",
        "unknown command"
    ]

    for query in test_queries:
        print(f"\nQuery: {query}")
        response = agent.process_query(query)
        print(f"Response: {response}")

    # Final status
    print(f"\nFinal Status: {agent.get_status()}")
    print("\nAgentBase test complete!")
