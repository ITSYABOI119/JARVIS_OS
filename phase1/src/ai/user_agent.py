#!/usr/bin/env python3
"""
JARVIS AI-OS Phase 1 - User Interaction Agent

Handles general queries, help, status, and cache operations.
Default fallback for queries that don't match other agents.
"""

import time
from typing import Dict, Any, Optional
from agent_base import AgentBase


class UserAgent(AgentBase):
    """
    User Interaction Agent

    Specializes in:
    - Help and documentation
    - System status
    - Cache statistics
    - Agent status
    - General user queries (fallback)
    """

    # Keywords this agent handles (including fallback)
    KEYWORDS = [
        "help", "status", "cache", "agent", "agents",
        "show", "display", "tell", "what", "how",
        "info", "information", "stats", "statistics",
        "version", "about", "jarvis"
    ]

    # Action mapping (keyword → action)
    ACTIONS = {
        "help": "show_help",
        "status": "show_status",
        "cache": "show_cache_stats",
        "agent": "show_agent_status",
        "agents": "show_agent_status",
        "stats": "show_statistics",
        "statistics": "show_statistics",
        "version": "show_version",
        "about": "show_about",
    }

    def __init__(self, model=None, shared_context=None):
        """Initialize User Interaction Agent"""
        super().__init__(
            name="user",
            keywords=self.KEYWORDS,
            model=model,
            shared_context=shared_context
        )
        self.status = "ready"
        self.version = "0.1.0"
        self.build_date = "November 20, 2025"

    def process_query(self, query: str, context: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
        """
        Process user interaction query

        Args:
            query: User query
            context: Optional shared context

        Returns:
            dict: Response with action and result
        """
        start_time = time.time()
        query_lower = query.lower()

        # Determine action from query keywords
        action = self._determine_action(query_lower)

        # Execute action
        if action == "show_help":
            result = self._show_help()
        elif action == "show_status":
            result = self._show_status(context)
        elif action == "show_cache_stats":
            result = self._show_cache_stats(context)
        elif action == "show_agent_status":
            result = self._show_agent_status(context)
        elif action == "show_statistics":
            result = self._show_statistics(context)
        elif action == "show_version":
            result = self._show_version()
        elif action == "show_about":
            result = self._show_about()
        elif action == "general_query":
            result = self._general_query(query, context)
        else:
            result = self._general_query(query, context)

        # Record statistics
        response_time_ms = (time.time() - start_time) * 1000
        success = "error" not in result
        self._record_query(success, response_time_ms, cache_hit=False)

        # Update shared context
        self.update_shared_context()

        return self._create_response(
            action=action,
            result=result,
            trust_level=0,  # User queries are automatic (informational)
            inference_time_ms=response_time_ms,
            cache_hit=False,
            error=result.get("error")
        )

    def _determine_action(self, query: str) -> str:
        """Determine action from query keywords"""
        for keyword, action in self.ACTIONS.items():
            if keyword in query:
                return action
        return "general_query"  # Default fallback

    def _show_help(self) -> Dict[str, Any]:
        """Show help information"""
        help_text = """
JARVIS AI-OS - Multi-Agent System Help

Available Commands:
  help              - Show this help message
  status            - Show system status
  cache stats       - Show decision cache statistics
  agent status      - Show all agent status
  version           - Show JARVIS version
  about             - About JARVIS AI-OS

Specialist Agents:
  Device Manager    - Hardware queries (disk, memory, CPU, USB, thermal)
  Network Agent     - Network operations (ping, connectivity, ports, DNS)
  FileSystem Agent  - File operations (list, find, permissions, read)
  User Agent        - General queries and help (fallback)

Examples:
  "show disk space"        -> Device Manager
  "ping google.com"        -> Network Agent
  "list files in /tmp"     -> FileSystem Agent
  "help"                   -> User Agent
  "what is JARVIS?"        -> User Agent (general query)

For more information, visit: https://github.com/jarvis-ai-os
"""
        return {
            "help_text": help_text.strip(),
            "summary": "Help information displayed"
        }

    def _show_status(self, context: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
        """Show system status"""
        status = {
            "jarvis_version": self.version,
            "build_date": self.build_date,
            "agent_count": 4,
            "agents": ["device", "network", "filesystem", "user"],
        }

        # Add shared context info if available
        if context:
            status["system_state"] = {
                "memory_used_mb": context.get("memory_used", "N/A"),
                "memory_total_mb": context.get("memory_total", "N/A"),
                "cpu_percent": context.get("cpu_percent", "N/A"),
                "disk_used_gb": context.get("disk_used", "N/A"),
                "disk_total_gb": context.get("disk_total", "N/A"),
            }
            status["cache"] = {
                "hit_rate": context.get("cache_hit_rate", "N/A"),
                "lookups": context.get("cache_lookups", "N/A"),
            }
            status["ipc"] = {
                "sent": context.get("ipc_sent", "N/A"),
                "received": context.get("ipc_received", "N/A"),
                "drops": context.get("ipc_drops", "N/A"),
            }

        status["summary"] = "JARVIS AI-OS running"
        return status

    def _show_cache_stats(self, context: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
        """Show cache statistics"""
        if context:
            return {
                "hit_rate": context.get("cache_hit_rate", "N/A"),
                "lookups": context.get("cache_lookups", "N/A"),
                "hits": context.get("cache_hits", "N/A"),
                "misses": context.get("cache_misses", "N/A"),
                "entries": context.get("cache_entries", "N/A"),
                "capacity": context.get("cache_capacity", "N/A"),
                "summary": f"Cache hit rate: {context.get('cache_hit_rate', 'N/A')}"
            }
        else:
            return {
                "error": "Cache statistics not available (shared context not initialized)",
                "summary": "Cache statistics unavailable"
            }

    def _show_agent_status(self, context: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
        """Show all agent status"""
        if context and "agent_status" in context:
            return {
                "agents": context["agent_status"],
                "summary": f"{len(context['agent_status'])} agents active"
            }
        else:
            # Return basic info if context not available
            return {
                "agents": {
                    "device": {"status": "ready"},
                    "network": {"status": "ready"},
                    "filesystem": {"status": "ready"},
                    "user": {"status": "ready"}
                },
                "summary": "4 agents ready (context not available)"
            }

    def _show_statistics(self, context: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
        """Show comprehensive statistics"""
        stats = {
            "jarvis": {
                "version": self.version,
                "build_date": self.build_date,
                "uptime_seconds": self.get_status()["uptime_seconds"]
            }
        }

        if context:
            stats["cache"] = {
                "hit_rate": context.get("cache_hit_rate", "N/A"),
                "lookups": context.get("cache_lookups", "N/A"),
            }
            stats["ipc"] = {
                "sent": context.get("ipc_sent", "N/A"),
                "received": context.get("ipc_received", "N/A"),
                "drops": context.get("ipc_drops", "N/A"),
            }
            stats["agents"] = context.get("agent_status", {})

        stats["summary"] = "Comprehensive statistics"
        return stats

    def _show_version(self) -> Dict[str, Any]:
        """Show JARVIS version"""
        return {
            "version": self.version,
            "build_date": self.build_date,
            "phase": "Phase 1 - Proof of Concept",
            "week": "Week 11 - Multi-Agent Architecture",
            "summary": f"JARVIS AI-OS v{self.version}"
        }

    def _show_about(self) -> Dict[str, Any]:
        """Show about information"""
        about_text = """
JARVIS AI-OS - Artificial Intelligence Operating System

An AI-controlled operating system using a microkernel architecture
with autonomous decision-making capabilities.

Architecture:
  - seL4 Microkernel (formally verified)
  - Multi-Agent AI System (4 specialist agents)
  - Decision Cache (85.7% hit rate, <1ms lookup)
  - Lock-Free IPC (<100μs latency)

Current Phase: Phase 1 - Proof of Concept (Month 8, Week 11)

Project: https://github.com/jarvis-ai-os
License: Open Source (seL4 License)
"""
        return {
            "about_text": about_text.strip(),
            "summary": "About JARVIS AI-OS"
        }

    def _general_query(self, query: str, context: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
        """
        Handle general query (fallback)

        In full implementation, this would use the AI model.
        For now, return helpful message.
        """
        if self.model:
            # Use AI model for general query
            # This will be implemented when model is loaded
            return {
                "query": query,
                "response": "AI model response would go here",
                "summary": "General query processed"
            }
        else:
            # No model available, return helpful message
            return {
                "query": query,
                "response": (
                    f"I don't have a specific handler for: '{query}'. "
                    "Try 'help' for available commands, or use specialist queries like "
                    "'show disk space', 'ping google.com', or 'list files'."
                ),
                "summary": "General query (no AI model loaded)"
            }


if __name__ == "__main__":
    # Test User Interaction Agent
    print("="*70)
    print("Testing User Interaction Agent")
    print("="*70)

    agent = UserAgent()
    print(f"\nAgent: {agent}")
    print(f"Status: {agent.get_status()}")

    # Mock shared context for testing
    mock_context = {
        "memory_used": 8192,
        "memory_total": 16384,
        "cpu_percent": 45.5,
        "disk_used": 450,
        "disk_total": 1000,
        "cache_hit_rate": 0.857,
        "cache_lookups": 1000,
        "cache_hits": 857,
        "cache_misses": 143,
        "ipc_sent": 100,
        "ipc_received": 95,
        "ipc_drops": 5,
        "agent_status": {
            "device": {"status": "ready", "queries": 50},
            "network": {"status": "ready", "queries": 30},
            "filesystem": {"status": "ready", "queries": 20},
            "user": {"status": "ready", "queries": 100}
        }
    }

    # Test queries
    test_queries = [
        "help",
        "show status",
        "cache stats",
        "agent status",
        "version",
        "what is JARVIS?",
    ]

    for query in test_queries:
        print(f"\n{'='*70}")
        print(f"Query: {query}")
        print("="*70)

        response = agent.process_query(query, context=mock_context)

        print(f"Agent: {response['agent']}")
        print(f"Action: {response['action']}")
        print(f"Trust Level: {response['trust_level']}")
        print(f"Response Time: {response['inference_time_ms']:.2f}ms")

        if "error" in response:
            print(f"Error: {response['error']}")
        else:
            result = response['result']
            if 'summary' in result:
                print(f"Summary: {result['summary']}")

    # Final status
    print(f"\n{'='*70}")
    print("Final Agent Status")
    print("="*70)
    status = agent.get_status()
    print(f"Queries Processed: {status['statistics']['total_queries']}")
    print(f"Success Rate: {status['statistics']['success_rate']*100:.1f}%")
    print(f"Avg Response Time: {status['statistics']['avg_response_time_ms']:.2f}ms")
    print("\nUser Interaction Agent test complete!")
