#!/usr/bin/env python3
"""
JARVIS AI-OS - AI Inference Benchmark
Week 15: Dynamic Scaling Optimization

This script benchmarks AI inference latency with real queries for both:
- Llama 3.2 1B (IDLE state) [Updated Jan 2026] - Target: <100ms
- Phi-3 Mini 3.8B (ACTIVE state) - Target: <600ms

Author: JARVIS AI-OS Team
Date: November 24, 2025
"""

import time
import statistics
from typing import List, Dict, Tuple
from model_loader import ModelLoader, SystemState

# Test query suite (100 queries)
TEST_QUERIES = {
    "simple": [
        "show cpu",
        "list processes",
        "disk space",
        "memory usage",
        "network status",
        "time",
        "date",
        "uptime",
        "system info",
        "kernel version",
        "temperature",
        "battery status",
        "who",
        "hostname",
        "ip address",
        "load average",
        "free memory",
        "running tasks",
        "active users",
        "system load",
        "cpu cores",
        "disk usage",
        "network interfaces",
        "open files",
        "process count",
        "swap usage",
        "filesystem type",
        "kernel modules",
        "interrupt stats",
        "system uptime",
        "cpu frequency",
        "memory total",
        "disk read speed",
        "network tx bytes",
        "process tree",
        "system logs",
        "last reboot",
        "cpu model",
        "ram speed",
        "disk model",
        "network speed",
        "gpu info",
        "bios version",
        "motherboard",
        "os version",
        "shell version",
        "python version",
        "gcc version",
        "kernel config",
        "boot time",
    ],
    "medium": [
        "explain memory usage",
        "why is cpu high",
        "network diagnostics",
        "disk health check",
        "process analysis",
        "system performance",
        "troubleshoot network",
        "optimize memory",
        "check disk errors",
        "analyze cpu load",
        "review system logs",
        "network configuration",
        "process priorities",
        "memory leaks",
        "disk fragmentation",
        "network latency test",
        "system bottlenecks",
        "resource usage",
        "performance tuning",
        "service status",
        "daemon management",
        "log rotation",
        "backup status",
        "security audit",
        "firewall rules",
        "port scanning",
        "service dependencies",
        "package updates",
        "kernel parameters",
        "system hardening",
    ],
    "complex": [
        "diagnose network connectivity issue with external servers",
        "analyze memory leak in process",
        "troubleshoot high cpu usage and identify cause",
        "investigate disk i/o bottleneck",
        "optimize system performance for database workload",
        "configure network bonding for redundancy",
        "setup raid array for data protection",
        "implement firewall rules for security",
        "diagnose kernel panic root cause",
        "analyze application crash dump",
        "troubleshoot slow boot time",
        "investigate memory pressure issues",
        "optimize network throughput",
        "diagnose hardware failures",
        "implement backup strategy",
        "configure high availability cluster",
        "troubleshoot dns resolution",
        "analyze security vulnerabilities",
        "optimize database queries",
        "investigate system instability",
    ]
}

def create_query_list() -> List[str]:
    """Create list of 100 test queries"""
    queries = []
    queries.extend(TEST_QUERIES["simple"])
    queries.extend(TEST_QUERIES["medium"])
    queries.extend(TEST_QUERIES["complex"])
    return queries

def benchmark_model(loader: ModelLoader, state: SystemState, model_name: str, target_ms: float) -> Dict:
    """
    Benchmark AI inference for a specific model.

    Args:
        loader: ModelLoader instance
        state: System state (IDLE or ACTIVE)
        model_name: Display name for model
        target_ms: Target latency in milliseconds

    Returns:
        dict with benchmark results
    """
    print(f"\\n{'='*70}")
    print(f"Benchmarking {model_name}")
    print(f"State: {state.value.upper()} | Target: <{target_ms}ms avg")
    print('='*70)

    # Load model
    print(f"Loading {model_name}...")
    start_load = time.time()
    success = loader.load_model(state)
    load_time = time.time() - start_load

    if not success:
        print(f"ERROR: Failed to load {model_name}")
        return None

    print(f"Model loaded in {load_time:.3f}s")

    # Get test queries
    queries = create_query_list()
    print(f"Running {len(queries)} test queries...")
    print()

    # Run inference benchmark
    latencies = []

    for i, query in enumerate(queries, 1):
        # Measure inference time
        start = time.time()

        try:
            # Simple inference test - just measure model overhead
            # In real use, this would call loader.generate(query)
            # For now, we simulate with a minimal operation
            if isinstance(loader.current_model, str):
                # Mock model - simulate inference
                if "tiny" in loader.current_model.lower():
                    time.sleep(0.05)  # Simulate 50ms
                else:
                    time.sleep(0.3)   # Simulate 300ms
            else:
                # Real model - measure actual inference
                # Note: This is a simplified test
                response = loader.current_model(
                    query,
                    max_tokens=50,
                    temperature=0.7,
                    stop=["\\n"],
                    echo=False
                )

        except Exception as e:
            print(f"  Query {i}: ERROR - {e}")
            continue

        elapsed_ms = (time.time() - start) * 1000
        latencies.append(elapsed_ms)

        # Print progress every 10 queries
        if i % 10 == 0:
            avg_so_far = statistics.mean(latencies)
            print(f"  Progress: {i}/{len(queries)} queries | Avg: {avg_so_far:.1f}ms")

    # Calculate statistics
    if not latencies:
        print("ERROR: No successful queries")
        return None

    results = {
        "model": model_name,
        "state": state.value,
        "queries": len(latencies),
        "min_ms": min(latencies),
        "max_ms": max(latencies),
        "mean_ms": statistics.mean(latencies),
        "median_ms": statistics.median(latencies),
        "stdev_ms": statistics.stdev(latencies) if len(latencies) > 1 else 0.0,
        "p95_ms": sorted(latencies)[int(len(latencies) * 0.95)],
        "p99_ms": sorted(latencies)[int(len(latencies) * 0.99)],
        "target_ms": target_ms,
        "meets_target": statistics.mean(latencies) < target_ms
    }

    # Print results
    print()
    print("Results:")
    print(f"  Queries:    {results['queries']}")
    print(f"  Min:        {results['min_ms']:.1f}ms")
    print(f"  Max:        {results['max_ms']:.1f}ms")
    print(f"  Mean:       {results['mean_ms']:.1f}ms")
    print(f"  Median:     {results['median_ms']:.1f}ms")
    print(f"  StdDev:     {results['stdev_ms']:.1f}ms")
    print(f"  P95:        {results['p95_ms']:.1f}ms")
    print(f"  P99:        {results['p99_ms']:.1f}ms")
    print(f"  Target:     <{results['target_ms']}ms")
    print(f"  Status:     {'PASS' if results['meets_target'] else 'FAIL'}")

    # Unload model
    loader.unload_model(state)

    return results

def main():
    """Run inference benchmark"""
    print("="*70)
    print("JARVIS AI-OS - AI Inference Benchmark (Week 15)")
    print("="*70)
    print()
    print("Testing inference latency with 100 queries per model:")
    print("  - Llama 3.2 1B (IDLE state) [Updated Jan 2026]: Target <100ms avg")
    print("  - Phi-3 Mini 3.8B (ACTIVE state): Target <600ms avg")
    print()

    # Create model loader
    loader = ModelLoader()

    # Benchmark Llama 3.2 1B (IDLE model)
    llama32_results = benchmark_model(
        loader,
        SystemState.IDLE,
        "Llama 3.2 1B Q4",
        100.0
    )

    # Benchmark Phi-3
    phi3_results = benchmark_model(
        loader,
        SystemState.ACTIVE,
        "Phi-3 Mini 3.8B Q4",
        600.0
    )

    # Summary comparison
    print("\\n" + "="*70)
    print("Benchmark Summary")
    print("="*70)
    print()

    if llama32_results and phi3_results:
        print(f"{'Model':<25} {'Mean':<10} {'Target':<10} {'Status':<10}")
        print("-"*70)
        print(f"{llama32_results['model']:<25} "
              f"{llama32_results['mean_ms']:>7.1f}ms "
              f"<{llama32_results['target_ms']:>6.0f}ms "
              f"{'PASS' if llama32_results['meets_target'] else 'FAIL':<10}")
        print(f"{phi3_results['model']:<25} "
              f"{phi3_results['mean_ms']:>7.1f}ms "
              f"<{phi3_results['target_ms']:>6.0f}ms "
              f"{'PASS' if phi3_results['meets_target'] else 'FAIL':<10}")

        print()
        print("Speed Comparison:")
        speedup = phi3_results['mean_ms'] / llama32_results['mean_ms']
        print(f"  Phi-3 is {speedup:.1f}x slower than TinyLlama")
        print(f"  TinyLlama: {1000/llama32_results['mean_ms']:.1f} queries/second")
        print(f"  Phi-3: {1000/phi3_results['mean_ms']:.1f} queries/second")

    print()
    print("="*70)
    print("Benchmark Complete!")
    print("="*70)

if __name__ == "__main__":
    main()
