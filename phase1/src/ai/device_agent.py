#!/usr/bin/env python3
"""
JARVIS AI-OS Phase 1 - Device Manager Agent

Handles hardware operations, device management, and system resources.
"""

import os
import time
import subprocess
from typing import Dict, Any, Optional
from agent_base import AgentBase


class DeviceAgent(AgentBase):
    """
    Device Manager Agent

    Specializes in:
    - Disk space queries
    - Memory usage
    - CPU information
    - USB/PCI devices
    - Hardware status
    """

    # Keywords this agent handles
    KEYWORDS = [
        "device", "hardware", "disk", "usb", "driver",
        "memory", "ram", "cpu", "processor",
        "storage", "partition", "mount",
        "thermal", "temperature", "battery",
        "space", "df", "free"
    ]

    # Action mapping (keyword → action)
    ACTIONS = {
        "disk": "show_disk_space",
        "space": "show_disk_space",
        "df": "show_disk_space",
        "memory": "show_memory",
        "ram": "show_memory",
        "free": "show_memory",
        "cpu": "show_cpu",
        "processor": "show_cpu",
        "usb": "list_usb_devices",
        "pci": "list_pci_devices",
        "device": "list_devices",
        "thermal": "show_thermal",
        "temperature": "show_thermal",
        "battery": "show_battery",
    }

    def __init__(self, model=None, shared_context=None):
        """Initialize Device Manager Agent"""
        super().__init__(
            name="device",
            keywords=self.KEYWORDS,
            model=model,
            shared_context=shared_context
        )
        self.status = "ready"  # No model needed for system commands

    def process_query(self, query: str, context: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
        """
        Process device/hardware query

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
        if action == "show_disk_space":
            result = self._show_disk_space()
        elif action == "show_memory":
            result = self._show_memory()
        elif action == "show_cpu":
            result = self._show_cpu()
        elif action == "list_usb_devices":
            result = self._list_usb_devices()
        elif action == "list_pci_devices":
            result = self._list_pci_devices()
        elif action == "list_devices":
            result = self._list_all_devices()
        elif action == "show_thermal":
            result = self._show_thermal()
        elif action == "show_battery":
            result = self._show_battery()
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
            trust_level=0,  # Device queries are automatic (read-only)
            inference_time_ms=response_time_ms,
            cache_hit=False,
            error=result.get("error")
        )

    def _determine_action(self, query: str) -> str:
        """Determine action from query keywords"""
        for keyword, action in self.ACTIONS.items():
            if keyword in query:
                return action
        return "unknown"

    def _show_disk_space(self) -> Dict[str, Any]:
        """Show disk space using df command"""
        try:
            # Run df -h (human-readable)
            result = subprocess.run(
                ["df", "-h"],
                capture_output=True,
                text=True,
                timeout=5.0
            )

            if result.returncode == 0:
                # Parse df output
                lines = result.stdout.strip().split("\n")
                if len(lines) < 2:
                    return {"error": "No disk information available"}

                # Skip header, parse filesystems
                filesystems = []
                for line in lines[1:]:
                    parts = line.split()
                    if len(parts) >= 6:
                        filesystems.append({
                            "filesystem": parts[0],
                            "size": parts[1],
                            "used": parts[2],
                            "available": parts[3],
                            "use_percent": parts[4],
                            "mounted_on": parts[5]
                        })

                return {
                    "filesystems": filesystems,
                    "summary": f"{len(filesystems)} filesystems mounted"
                }
            else:
                return {"error": f"df command failed: {result.stderr}"}

        except subprocess.TimeoutExpired:
            return {"error": "df command timed out"}
        except FileNotFoundError:
            return {"error": "df command not found (Windows?)"}
        except Exception as e:
            return {"error": f"Failed to get disk space: {str(e)}"}

    def _show_memory(self) -> Dict[str, Any]:
        """Show memory usage using free command"""
        try:
            # Run free -h (human-readable)
            result = subprocess.run(
                ["free", "-h"],
                capture_output=True,
                text=True,
                timeout=5.0
            )

            if result.returncode == 0:
                # Parse free output
                lines = result.stdout.strip().split("\n")
                if len(lines) < 2:
                    return {"error": "No memory information available"}

                # Parse memory line (line 1)
                mem_line = lines[1].split()
                if len(mem_line) >= 7:
                    return {
                        "total": mem_line[1],
                        "used": mem_line[2],
                        "free": mem_line[3],
                        "shared": mem_line[4],
                        "buff_cache": mem_line[5],
                        "available": mem_line[6],
                        "summary": f"{mem_line[2]} used / {mem_line[1]} total"
                    }
                else:
                    return {"error": "Unexpected free output format"}
            else:
                return {"error": f"free command failed: {result.stderr}"}

        except subprocess.TimeoutExpired:
            return {"error": "free command timed out"}
        except FileNotFoundError:
            return {"error": "free command not found (Windows?)"}
        except Exception as e:
            return {"error": f"Failed to get memory info: {str(e)}"}

    def _show_cpu(self) -> Dict[str, Any]:
        """Show CPU information using lscpu command"""
        try:
            # Run lscpu
            result = subprocess.run(
                ["lscpu"],
                capture_output=True,
                text=True,
                timeout=5.0
            )

            if result.returncode == 0:
                # Parse lscpu output
                cpu_info = {}
                for line in result.stdout.strip().split("\n"):
                    if ":" in line:
                        key, value = line.split(":", 1)
                        cpu_info[key.strip()] = value.strip()

                # Extract key fields
                return {
                    "architecture": cpu_info.get("Architecture", "unknown"),
                    "cpu_count": cpu_info.get("CPU(s)", "unknown"),
                    "model_name": cpu_info.get("Model name", "unknown"),
                    "cpu_mhz": cpu_info.get("CPU MHz", "unknown"),
                    "summary": f"{cpu_info.get('Model name', 'CPU')} ({cpu_info.get('CPU(s)', 'N')} cores)"
                }
            else:
                return {"error": f"lscpu command failed: {result.stderr}"}

        except subprocess.TimeoutExpired:
            return {"error": "lscpu command timed out"}
        except FileNotFoundError:
            return {"error": "lscpu command not found (Windows?)"}
        except Exception as e:
            return {"error": f"Failed to get CPU info: {str(e)}"}

    def _list_usb_devices(self) -> Dict[str, Any]:
        """List USB devices using lsusb command"""
        try:
            # Run lsusb
            result = subprocess.run(
                ["lsusb"],
                capture_output=True,
                text=True,
                timeout=5.0
            )

            if result.returncode == 0:
                # Parse lsusb output
                devices = []
                for line in result.stdout.strip().split("\n"):
                    if line.strip():
                        devices.append(line.strip())

                return {
                    "devices": devices,
                    "count": len(devices),
                    "summary": f"{len(devices)} USB devices found"
                }
            else:
                return {"error": f"lsusb command failed: {result.stderr}"}

        except subprocess.TimeoutExpired:
            return {"error": "lsusb command timed out"}
        except FileNotFoundError:
            return {"error": "lsusb command not found (install usbutils)"}
        except Exception as e:
            return {"error": f"Failed to list USB devices: {str(e)}"}

    def _list_pci_devices(self) -> Dict[str, Any]:
        """List PCI devices using lspci command"""
        try:
            # Run lspci
            result = subprocess.run(
                ["lspci"],
                capture_output=True,
                text=True,
                timeout=5.0
            )

            if result.returncode == 0:
                # Parse lspci output
                devices = []
                for line in result.stdout.strip().split("\n"):
                    if line.strip():
                        devices.append(line.strip())

                return {
                    "devices": devices,
                    "count": len(devices),
                    "summary": f"{len(devices)} PCI devices found"
                }
            else:
                return {"error": f"lspci command failed: {result.stderr}"}

        except subprocess.TimeoutExpired:
            return {"error": "lspci command timed out"}
        except FileNotFoundError:
            return {"error": "lspci command not found (install pciutils)"}
        except Exception as e:
            return {"error": f"Failed to list PCI devices: {str(e)}"}

    def _list_all_devices(self) -> Dict[str, Any]:
        """List all devices (USB + PCI)"""
        usb = self._list_usb_devices()
        pci = self._list_pci_devices()

        total_count = usb.get("count", 0) + pci.get("count", 0)

        return {
            "usb": usb,
            "pci": pci,
            "total_count": total_count,
            "summary": f"{usb.get('count', 0)} USB + {pci.get('count', 0)} PCI devices"
        }

    def _show_thermal(self) -> Dict[str, Any]:
        """Show thermal/temperature information"""
        try:
            # Try sensors command
            result = subprocess.run(
                ["sensors"],
                capture_output=True,
                text=True,
                timeout=5.0
            )

            if result.returncode == 0:
                return {
                    "output": result.stdout,
                    "summary": "Temperature sensors (install lm-sensors for details)"
                }
            else:
                return {"error": "sensors command not available (install lm-sensors)"}

        except subprocess.TimeoutExpired:
            return {"error": "sensors command timed out"}
        except FileNotFoundError:
            return {"error": "sensors command not found (install lm-sensors)"}
        except Exception as e:
            return {"error": f"Failed to get thermal info: {str(e)}"}

    def _show_battery(self) -> Dict[str, Any]:
        """Show battery status"""
        try:
            # Try acpi command
            result = subprocess.run(
                ["acpi", "-b"],
                capture_output=True,
                text=True,
                timeout=5.0
            )

            if result.returncode == 0:
                return {
                    "output": result.stdout,
                    "summary": "Battery status"
                }
            else:
                return {"error": "No battery found or acpi command not available"}

        except subprocess.TimeoutExpired:
            return {"error": "acpi command timed out"}
        except FileNotFoundError:
            return {"error": "acpi command not found (install acpi)"}
        except Exception as e:
            return {"error": f"Failed to get battery info: {str(e)}"}

    def serialize(self) -> Dict[str, Any]:
        """
        Serialize agent state for suspend/resume (Week 22).

        Returns:
            dict: Agent state including statistics and status
        """
        return {
            'type': 'device_agent',
            'name': self.name,
            'status': getattr(self, 'status', 'ready'),
            'statistics': {
                'total_queries': getattr(self, 'total_queries', 0),
                'successful_queries': getattr(self, 'successful_queries', 0),
                'failed_queries': getattr(self, 'failed_queries', 0),
                'total_response_time_ms': getattr(self, 'total_response_time_ms', 0.0),
                'cache_hits': getattr(self, 'cache_hits', 0)
            },
            'timestamp': time.time()
        }

    def deserialize(self, state: Dict[str, Any]):
        """
        Restore agent state from suspend/resume (Week 22).

        Args:
            state: Serialized agent state
        """
        # Restore status
        self.status = state.get('status', 'ready')

        # Restore statistics
        stats = state.get('statistics', {})
        self.total_queries = stats.get('total_queries', 0)
        self.successful_queries = stats.get('successful_queries', 0)
        self.failed_queries = stats.get('failed_queries', 0)
        self.total_response_time_ms = stats.get('total_response_time_ms', 0.0)
        self.cache_hits = stats.get('cache_hits', 0)


if __name__ == "__main__":
    # Test Device Manager Agent
    print("="*70)
    print("Testing Device Manager Agent")
    print("="*70)

    agent = DeviceAgent()
    print(f"\nAgent: {agent}")
    print(f"Status: {agent.get_status()}")

    # Test queries
    test_queries = [
        "show disk space",
        "show memory usage",
        "show cpu information",
        "list usb devices",
        "show temperature",
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
            print(f"Result: {response['result']}")

    # Final status
    print(f"\n{'='*70}")
    print("Final Agent Status")
    print("="*70)
    status = agent.get_status()
    print(f"Queries Processed: {status['statistics']['total_queries']}")
    print(f"Success Rate: {status['statistics']['success_rate']*100:.1f}%")
    print(f"Avg Response Time: {status['statistics']['avg_response_time_ms']:.2f}ms")
    print("\nDevice Manager Agent test complete!")
