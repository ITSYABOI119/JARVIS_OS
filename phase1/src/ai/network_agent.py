#!/usr/bin/env python3
"""
JARVIS AI-OS Phase 1 - Network Agent

Handles network operations, connectivity checks, and remote access.
"""

import time
import subprocess
import socket
from typing import Dict, Any, Optional
from agent_base import AgentBase


class NetworkAgent(AgentBase):
    """
    Network Agent

    Specializes in:
    - Network status queries
    - Ping/connectivity tests
    - Port listening checks
    - Routing table
    - DNS lookups
    """

    # Keywords this agent handles
    KEYWORDS = [
        "network", "ping", "ssh", "http", "connection",
        "ip", "port", "interface", "wifi", "ethernet",
        "router", "gateway", "dns", "firewall",
        "netstat", "ifconfig", "route"
    ]

    # Action mapping (keyword → action)
    ACTIONS = {
        "ping": "ping_host",
        "network": "show_network_status",
        "interface": "show_network_status",
        "ip": "show_network_status",
        "ifconfig": "show_network_status",
        "port": "show_listening_ports",
        "netstat": "show_listening_ports",
        "route": "show_routes",
        "router": "show_routes",
        "gateway": "show_routes",
        "connection": "check_internet_connection",
        "dns": "check_dns",
    }

    def __init__(self, model=None, shared_context=None):
        """Initialize Network Agent"""
        super().__init__(
            name="network",
            keywords=self.KEYWORDS,
            model=model,
            shared_context=shared_context
        )
        self.status = "ready"  # No model needed for network commands

    def process_query(self, query: str, context: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
        """
        Process network query

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

        # Extract host from query if needed (for ping)
        host = self._extract_host(query)

        # Execute action
        if action == "ping_host":
            result = self._ping_host(host if host else "8.8.8.8")
        elif action == "show_network_status":
            result = self._show_network_status()
        elif action == "show_listening_ports":
            result = self._show_listening_ports()
        elif action == "show_routes":
            result = self._show_routes()
        elif action == "check_internet_connection":
            result = self._check_internet_connection()
        elif action == "check_dns":
            result = self._check_dns(host if host else "google.com")
        else:
            result = {"error": f"Unknown action: {action}"}

        # Record statistics
        response_time_ms = (time.time() - start_time) * 1000
        success = "error" not in result
        self._record_query(success, response_time_ms, cache_hit=False)

        # Update shared context
        self.update_shared_context()

        return self._create_response(
            action=action,
            result=result,
            trust_level=0,  # Network queries are automatic (read-only)
            inference_time_ms=response_time_ms,
            cache_hit=False,
            error=result.get("error")
        )

    def _determine_action(self, query: str) -> str:
        """Determine action from query keywords"""
        for keyword, action in self.ACTIONS.items():
            if keyword in query:
                return action
        return "show_network_status"  # Default

    def _extract_host(self, query: str) -> Optional[str]:
        """Extract hostname/IP from query"""
        words = query.split()
        for word in words:
            # Check if word looks like hostname or IP
            if "." in word or ":" in word:
                # Remove trailing punctuation
                word = word.strip(".,;:!?")
                return word
        return None

    def _ping_host(self, host: str) -> Dict[str, Any]:
        """Ping a host"""
        try:
            # Use ping command (cross-platform: -n for Windows, -c for Unix)
            import platform
            param = "-n" if platform.system().lower() == "windows" else "-c"

            result = subprocess.run(
                ["ping", param, "3", host],
                capture_output=True,
                text=True,
                timeout=10.0
            )

            if result.returncode == 0:
                # Parse ping output for round-trip time
                output = result.stdout
                success = True
                summary = f"Host {host} is reachable"
            else:
                output = result.stderr
                success = False
                summary = f"Host {host} is unreachable"

            return {
                "host": host,
                "success": success,
                "output": output,
                "summary": summary
            }

        except subprocess.TimeoutExpired:
            return {"error": f"Ping to {host} timed out"}
        except FileNotFoundError:
            return {"error": "ping command not found"}
        except Exception as e:
            return {"error": f"Failed to ping {host}: {str(e)}"}

    def _show_network_status(self) -> Dict[str, Any]:
        """Show network interface status"""
        try:
            # Try ip addr (Linux) or ifconfig (Unix/macOS)
            commands_to_try = [
                ["ip", "addr"],
                ["ifconfig"],
                ["ipconfig"]  # Windows
            ]

            for cmd in commands_to_try:
                try:
                    result = subprocess.run(
                        cmd,
                        capture_output=True,
                        text=True,
                        timeout=5.0
                    )

                    if result.returncode == 0:
                        return {
                            "command": " ".join(cmd),
                            "output": result.stdout,
                            "summary": "Network interfaces listed"
                        }
                except FileNotFoundError:
                    continue

            return {"error": "No network status command available (ip/ifconfig/ipconfig)"}

        except subprocess.TimeoutExpired:
            return {"error": "Network status command timed out"}
        except Exception as e:
            return {"error": f"Failed to get network status: {str(e)}"}

    def _show_listening_ports(self) -> Dict[str, Any]:
        """Show listening ports"""
        try:
            # Try netstat or ss
            commands_to_try = [
                ["netstat", "-tuln"],  # Unix/Linux
                ["netstat", "-an"],    # Windows
                ["ss", "-tuln"]        # Modern Linux
            ]

            for cmd in commands_to_try:
                try:
                    result = subprocess.run(
                        cmd,
                        capture_output=True,
                        text=True,
                        timeout=5.0
                    )

                    if result.returncode == 0:
                        # Parse output for listening ports
                        lines = result.stdout.strip().split("\n")
                        listening = [line for line in lines if "LISTEN" in line or "0.0.0.0" in line]

                        return {
                            "command": " ".join(cmd),
                            "output": result.stdout,
                            "listening_count": len(listening),
                            "summary": f"{len(listening)} listening ports found"
                        }
                except FileNotFoundError:
                    continue

            return {"error": "No port listing command available (netstat/ss)"}

        except subprocess.TimeoutExpired:
            return {"error": "Port listing command timed out"}
        except Exception as e:
            return {"error": f"Failed to list ports: {str(e)}"}

    def _show_routes(self) -> Dict[str, Any]:
        """Show routing table"""
        try:
            # Try ip route (Linux) or route (Unix/Windows)
            commands_to_try = [
                ["ip", "route"],
                ["route", "-n"],    # Unix/Linux
                ["route", "print"]  # Windows
            ]

            for cmd in commands_to_try:
                try:
                    result = subprocess.run(
                        cmd,
                        capture_output=True,
                        text=True,
                        timeout=5.0
                    )

                    if result.returncode == 0:
                        return {
                            "command": " ".join(cmd),
                            "output": result.stdout,
                            "summary": "Routing table retrieved"
                        }
                except FileNotFoundError:
                    continue

            return {"error": "No route command available"}

        except subprocess.TimeoutExpired:
            return {"error": "Route command timed out"}
        except Exception as e:
            return {"error": f"Failed to get routes: {str(e)}"}

    def _check_internet_connection(self) -> Dict[str, Any]:
        """Check internet connectivity"""
        try:
            # Try to connect to well-known servers
            test_hosts = [
                ("8.8.8.8", 53),       # Google DNS
                ("1.1.1.1", 53),       # Cloudflare DNS
                ("208.67.222.222", 53) # OpenDNS
            ]

            for host, port in test_hosts:
                try:
                    socket.create_connection((host, port), timeout=3.0)
                    return {
                        "connected": True,
                        "test_host": host,
                        "summary": f"Internet connection OK (tested {host})"
                    }
                except (socket.timeout, socket.error):
                    continue

            return {
                "connected": False,
                "summary": "No internet connection detected"
            }

        except Exception as e:
            return {"error": f"Failed to check connection: {str(e)}"}

    def _check_dns(self, hostname: str) -> Dict[str, Any]:
        """Check DNS resolution"""
        try:
            # Resolve hostname to IP
            ip_address = socket.gethostbyname(hostname)

            return {
                "hostname": hostname,
                "ip_address": ip_address,
                "success": True,
                "summary": f"{hostname} resolves to {ip_address}"
            }

        except socket.gaierror as e:
            return {
                "hostname": hostname,
                "success": False,
                "error": f"DNS resolution failed: {str(e)}",
                "summary": f"Failed to resolve {hostname}"
            }
        except Exception as e:
            return {"error": f"DNS check failed: {str(e)}"}


if __name__ == "__main__":
    # Test Network Agent
    print("="*70)
    print("Testing Network Agent")
    print("="*70)

    agent = NetworkAgent()
    print(f"\nAgent: {agent}")
    print(f"Status: {agent.get_status()}")

    # Test queries
    test_queries = [
        "ping google.com",
        "show network status",
        "show listening ports",
        "check internet connection",
        "check dns for github.com",
    ]

    for query in test_queries:
        print(f"\n{'='*70}")
        print(f"Query: {query}")
        print("="*70)

        response = agent.process_query(query)

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
            else:
                print(f"Result: {result}")

    # Final status
    print(f"\n{'='*70}")
    print("Final Agent Status")
    print("="*70)
    status = agent.get_status()
    print(f"Queries Processed: {status['statistics']['total_queries']}")
    print(f"Success Rate: {status['statistics']['success_rate']*100:.1f}%")
    print(f"Avg Response Time: {status['statistics']['avg_response_time_ms']:.2f}ms")
    print("\nNetwork Agent test complete!")
