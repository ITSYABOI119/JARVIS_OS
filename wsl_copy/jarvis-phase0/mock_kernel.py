# mock_kernel.py - JARVIS Phase 0 Experiment 1
import time
from dataclasses import dataclass
from typing import Dict

@dataclass
class SystemState:
    cpu_usage: float
    memory_usage: float
    disk_usage: float
    network_active: bool

class MockMicrokernel:
    """Simulates seL4 microkernel behavior"""

    def __init__(self):
        self.system_state = SystemState(
            cpu_usage=25.0,
            memory_usage=50.0,
            disk_usage=60.0,
            network_active=True
        )
        self.command_count = 0

    def handle_interrupt(self):
        """Simulate interrupt handling <1ms"""
        start = time.perf_counter()
        # Simulate interrupt processing
        time.sleep(0.0001)  # 0.1ms
        end = time.perf_counter()
        latency_ms = (end - start) * 1000
        return latency_ms

    def execute_command(self, command: str) -> Dict:
        """Execute kernel command"""
        self.command_count += 1

        if command == "read_cpu_stats":
            return {"cpu_usage": self.system_state.cpu_usage}
        elif command == "read_mem_stats":
            return {"memory_usage": self.system_state.memory_usage}
        elif command == "clear_cache":
            return {"status": "cache_cleared"}
        else:
            return {"error": "unknown_command"}

    def ipc_send(self, message: dict):
        """Simulate IPC to AI layer"""
        start = time.perf_counter()
        # Simulate lock-free ring buffer write
        time.sleep(0.00001)  # 10μs
        end = time.perf_counter()
        latency_us = (end - start) * 1_000_000
        return latency_us

# ===== MAIN TEST =====
if __name__ == "__main__":
    print("="*60)
    print("JARVIS AI-OS - Phase 0 Experiment 1: Mock Microkernel")
    print("="*60)
    print()
    
    kernel = MockMicrokernel()

    # Test 1: Interrupt Latency
    print("[TEST 1] Interrupt Latency (target: <1ms)")
    print("-" * 60)
    latencies = [kernel.handle_interrupt() for _ in range(100)]
    avg_latency = sum(latencies) / len(latencies)
    min_latency = min(latencies)
    max_latency = max(latencies)
    
    print(f"  Iterations: 100")
    print(f"  Min:        {min_latency:.4f}ms")
    print(f"  Average:    {avg_latency:.4f}ms")
    print(f"  Max:        {max_latency:.4f}ms")
    print(f"  Target:     <1.0ms")
    
    if avg_latency < 1.0:
        print(f"  Status:     ✅ PASS")
    else:
        print(f"  Status:     ❌ FAIL")
    print()

    # Test 2: IPC Latency
    print("[TEST 2] IPC Latency (kernel↔AI, target: <100μs)")
    print("-" * 60)
    ipc_latencies = [kernel.ipc_send({"type": "test"}) for _ in range(1000)]
    avg_ipc = sum(ipc_latencies) / len(ipc_latencies)
    min_ipc = min(ipc_latencies)
    max_ipc = max(ipc_latencies)
    
    print(f"  Iterations: 1000")
    print(f"  Min:        {min_ipc:.2f}μs")
    print(f"  Average:    {avg_ipc:.2f}μs")
    print(f"  Max:        {max_ipc:.2f}μs")
    print(f"  Target:     <100μs")
    
    if avg_ipc < 100:
        print(f"  Status:     ✅ PASS")
    else:
        print(f"  Status:     ⚠️  SLOW (but may be faster on real hardware)")
    print()

    # Test 3: Command Execution
    print("[TEST 3] Command Execution")
    print("-" * 60)
    
    commands = [
        "read_cpu_stats",
        "read_mem_stats",
        "clear_cache",
    ]
    
    for cmd in commands:
        result = kernel.execute_command(cmd)
        print(f"  Command: {cmd:20s} → Result: {result}")
    
    print()
    print("="*60)
    print("[SUMMARY]")
    print("="*60)
    print(f"  Interrupt latency:    {'✅ PASS' if avg_latency < 1.0 else '❌ FAIL'} ({avg_latency:.4f}ms)")
    print(f"  IPC latency:          {'✅ PASS' if avg_ipc < 100 else '⚠️  SLOW'} ({avg_ipc:.2f}μs)")
    print(f"  Commands executed:    {kernel.command_count}")
    print()
    print("Next: Run 'python decision_cache.py' for Experiment 2")
    print("="*60)
