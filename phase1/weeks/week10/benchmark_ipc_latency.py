#!/usr/bin/env python3
"""
JARVIS AI-OS Phase 1 - Week 10 IPC Latency Benchmark

Measures IPC round-trip latency between Python and seL4 in QEMU.

Metrics:
- Minimum latency
- Maximum latency
- Median latency
- 95th percentile latency
- 99th percentile latency
- Average latency
- Standard deviation
- Throughput (messages/sec)

Prerequisites:
- seL4 running in QEMU with IPC handler
- IPC client working (from Week 8)

Usage:
    # Terminal 1 (WSL): cd ~/jarvis-phase1/hello-world_build && ./simulate
    # Terminal 2: python3 benchmark_ipc_latency.py

    # Custom iterations:
    python3 benchmark_ipc_latency.py --iterations 1000
"""

import sys
import os
import time
import statistics
from pathlib import Path
import argparse

# Add parent directory for imports
sys.path.insert(0, str(Path(__file__).parent.parent.parent / 'src' / 'ai'))

try:
    from ipc_client import IPCClient, MessageType
except ImportError:
    print("ERROR: Cannot import IPCClient. Make sure phase1/src/ai/ipc_client.py exists.")
    sys.exit(1)


class Colors:
    """ANSI color codes"""
    HEADER = '\033[95m'
    OKBLUE = '\033[94m'
    OKCYAN = '\033[96m'
    OKGREEN = '\033[92m'
    WARNING = '\033[93m'
    FAIL = '\033[91m'
    ENDC = '\033[0m'
    BOLD = '\033[1m'


def print_header(msg):
    print(f"\n{Colors.HEADER}{Colors.BOLD}{'='*70}{Colors.ENDC}")
    print(f"{Colors.HEADER}{Colors.BOLD}{msg:^70}{Colors.ENDC}")
    print(f"{Colors.HEADER}{Colors.BOLD}{'='*70}{Colors.ENDC}\n")


def benchmark_latency(iterations=100, warmup=10):
    """
    Benchmark IPC round-trip latency

    Args:
        iterations (int): Number of test iterations
        warmup (int): Number of warmup iterations (not measured)

    Returns:
        dict: Latency statistics
    """
    print(f"{Colors.OKBLUE}Initializing IPC client...{Colors.ENDC}")

    try:
        client = IPCClient()
        print(f"{Colors.OKGREEN}✓ Connected to seL4 IPC{Colors.ENDC}")
        print(f"{Colors.OKCYAN}  Shared memory: {client.shm_name}{Colors.ENDC}")
        print(f"{Colors.OKCYAN}  Ring buffer: {client.ring_size} slots{Colors.ENDC}\n")
    except Exception as e:
        print(f"{Colors.FAIL}✗ Failed to connect: {e}{Colors.ENDC}")
        return None

    # Warmup phase
    print(f"{Colors.OKBLUE}Warming up ({warmup} iterations)...{Colors.ENDC}")
    for i in range(warmup):
        try:
            client.send(MessageType.QUERY, "help")
            time.sleep(0.01)  # 10ms between warmup queries
            client.receive()  # Clear any responses
        except:
            pass

    print(f"{Colors.OKGREEN}✓ Warmup complete{Colors.ENDC}\n")

    # Benchmark phase
    print(f"{Colors.OKBLUE}Running benchmark ({iterations} iterations)...{Colors.ENDC}")

    latencies = []
    errors = 0
    timeouts = 0

    for i in range(iterations):
        try:
            # Send query and measure time
            start = time.perf_counter()
            client.send(MessageType.QUERY, "status")

            # Wait for response (with timeout)
            timeout_start = time.time()
            response = None

            while time.time() - timeout_start < 1.0:  # 1 second timeout
                response = client.receive()
                if response:
                    break
                time.sleep(0.001)  # 1ms poll interval

            end = time.perf_counter()

            if response:
                latency = (end - start) * 1000  # Convert to milliseconds
                latencies.append(latency)
            else:
                timeouts += 1

            # Progress indicator
            if (i + 1) % 10 == 0:
                print(f"{Colors.OKCYAN}  Progress: {i+1}/{iterations}{Colors.ENDC}", end='\r')

        except Exception as e:
            errors += 1

    print()  # New line after progress

    if not latencies:
        print(f"{Colors.FAIL}✗ No successful measurements!{Colors.ENDC}")
        print(f"{Colors.FAIL}  Errors: {errors}, Timeouts: {timeouts}{Colors.ENDC}")
        return None

    # Calculate statistics
    latencies_sorted = sorted(latencies)
    stats = {
        'iterations': iterations,
        'successful': len(latencies),
        'errors': errors,
        'timeouts': timeouts,
        'min': min(latencies),
        'max': max(latencies),
        'mean': statistics.mean(latencies),
        'median': statistics.median(latencies),
        'stdev': statistics.stdev(latencies) if len(latencies) > 1 else 0,
        'p95': latencies_sorted[int(len(latencies) * 0.95)] if len(latencies) > 20 else max(latencies),
        'p99': latencies_sorted[int(len(latencies) * 0.99)] if len(latencies) > 100 else max(latencies),
    }

    # Calculate throughput (messages per second)
    total_time = sum(latencies) / 1000  # Convert to seconds
    stats['throughput'] = len(latencies) / total_time if total_time > 0 else 0

    return stats


def print_results(stats):
    """Print benchmark results in a formatted table"""
    print_header("Benchmark Results")

    if not stats:
        print(f"{Colors.FAIL}No results to display{Colors.ENDC}")
        return

    # Test information
    print(f"{Colors.BOLD}Test Configuration:{Colors.ENDC}")
    print(f"  Iterations:      {stats['iterations']}")
    print(f"  Successful:      {stats['successful']}")
    print(f"  Errors:          {stats['errors']}")
    print(f"  Timeouts:        {stats['timeouts']}")
    print()

    # Latency statistics
    print(f"{Colors.BOLD}Latency Statistics (milliseconds):{Colors.ENDC}")
    print(f"  Minimum:         {stats['min']:.6f} ms")
    print(f"  Maximum:         {stats['max']:.6f} ms")
    print(f"  Mean:            {stats['mean']:.6f} ms")
    print(f"  Median:          {stats['median']:.6f} ms")
    print(f"  Std Deviation:   {stats['stdev']:.6f} ms")
    print(f"  95th Percentile: {stats['p95']:.6f} ms")
    print(f"  99th Percentile: {stats['p99']:.6f} ms")
    print()

    # Throughput
    print(f"{Colors.BOLD}Throughput:{Colors.ENDC}")
    print(f"  Messages/sec:    {stats['throughput']:.2f} msg/s")
    print()

    # Comparison with targets
    print(f"{Colors.BOLD}Target Comparison:{Colors.ENDC}")

    # Phase 0 standalone result: 0.048μs = 0.000048ms
    standalone_target = 0.000048  # ms

    # Week 10 QEMU target: <200μs = 0.2ms (allowing for virtualization overhead)
    qemu_target = 0.2  # ms

    median_us = stats['median'] * 1000  # Convert to microseconds

    print(f"  Standalone (Week 9): {standalone_target*1000:.3f} μs")
    print(f"  QEMU Target:         {qemu_target*1000:.1f} μs")
    print(f"  Measured (median):   {median_us:.3f} μs")

    # Calculate overhead
    overhead = (stats['median'] - standalone_target) * 1000  # μs
    overhead_pct = (overhead / (standalone_target * 1000)) * 100 if standalone_target > 0 else 0

    print(f"  QEMU Overhead:       +{overhead:.3f} μs (+{overhead_pct:.1f}%)")
    print()

    # Pass/fail assessment
    if stats['median'] < qemu_target:
        print(f"{Colors.OKGREEN}✓ PASS: Median latency meets QEMU target (<{qemu_target*1000:.0f}μs){Colors.ENDC}")
    else:
        print(f"{Colors.WARNING}⚠ WARNING: Median latency exceeds QEMU target{Colors.ENDC}")

    if stats['p99'] < qemu_target * 2:  # Allow 2x target for 99th percentile
        print(f"{Colors.OKGREEN}✓ PASS: 99th percentile within acceptable range{Colors.ENDC}")
    else:
        print(f"{Colors.WARNING}⚠ WARNING: 99th percentile high ({stats['p99']:.3f}ms){Colors.ENDC}")

    success_rate = (stats['successful'] / stats['iterations']) * 100
    if success_rate >= 95:
        print(f"{Colors.OKGREEN}✓ PASS: High success rate ({success_rate:.1f}%){Colors.ENDC}")
    else:
        print(f"{Colors.FAIL}✗ FAIL: Low success rate ({success_rate:.1f}%){Colors.ENDC}")


def main():
    """Main benchmark execution"""
    parser = argparse.ArgumentParser(description='Benchmark JARVIS IPC latency')
    parser.add_argument('--iterations', type=int, default=100,
                        help='Number of test iterations (default: 100)')
    parser.add_argument('--warmup', type=int, default=10,
                        help='Number of warmup iterations (default: 10)')

    args = parser.parse_args()

    print_header("JARVIS AI-OS - IPC Latency Benchmark")

    print(f"{Colors.OKCYAN}Test Parameters:{Colors.ENDC}")
    print(f"  Iterations: {args.iterations}")
    print(f"  Warmup:     {args.warmup}")
    print()

    print(f"{Colors.WARNING}⚠ Make sure seL4 is running in QEMU!{Colors.ENDC}")
    print(f"{Colors.WARNING}  Terminal 1: cd ~/jarvis-phase1/hello-world_build && ./simulate{Colors.ENDC}")
    print()

    input("Press Enter to start benchmark...")

    # Run benchmark
    stats = benchmark_latency(iterations=args.iterations, warmup=args.warmup)

    # Print results
    print_results(stats)

    # Return exit code
    if stats and stats['successful'] >= args.iterations * 0.95:
        return 0
    else:
        return 1


if __name__ == "__main__":
    try:
        exit_code = main()
        sys.exit(exit_code)
    except KeyboardInterrupt:
        print(f"\n{Colors.WARNING}Benchmark interrupted by user{Colors.ENDC}")
        sys.exit(1)
    except Exception as e:
        print(f"\n{Colors.FAIL}Error: {e}{Colors.ENDC}")
        import traceback
        traceback.print_exc()
        sys.exit(1)
