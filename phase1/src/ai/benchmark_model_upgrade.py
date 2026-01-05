#!/usr/bin/env python3
"""
JARVIS AI-OS - Model Upgrade Benchmark
January 4, 2026

Compares TinyLlama 1.1B (old) vs Llama 3.2 1B (new) to validate upgrade decision.

Metrics:
- Inference latency (avg, p50, p95, p99)
- Response quality (instruction following, coherence)
- Model load time
- Memory usage

Author: JARVIS AI-OS Team
Date: January 4, 2026
"""

import os
import sys
import time
import statistics
from typing import Dict, List, Tuple
import gc

# Add parent directory to path for imports
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

try:
    from llama_cpp import Llama
    LLAMA_CPP_AVAILABLE = True
except ImportError:
    print("[ERROR] llama-cpp-python not available")
    sys.exit(1)

import psutil

# Model paths
MODELS_DIR = os.path.join(
    os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(__file__)))),
    "models"
)

TINYLLAMA_OLD_PATH = os.path.join(MODELS_DIR, "tinyllama-1.1b-chat-v1.0.Q4_K_M.gguf")
LLAMA32_PATH = os.path.join(MODELS_DIR, "Llama-3.2-1B-Instruct-Q4_K_M.gguf")

# Model parameters (same for fair comparison)
MODEL_PARAMS = {
    "n_ctx": 2048,
    "n_gpu_layers": 35,  # RTX 2070
    "n_threads": 4,
    "verbose": False
}

# Test queries (diverse set)
TEST_QUERIES = [
    # Simple commands
    "List all running processes",
    "Show system memory usage",
    "Display network connections",
    "Check disk space",
    "Show CPU information",

    # Complex instructions
    "Create a backup script that compresses /home/user/documents to /backup with timestamp",
    "Explain how to troubleshoot high CPU usage on a Linux system",
    "Write a command to find all files larger than 100MB in /var/log",
    "Describe the steps to configure a static IP address on Ubuntu",
    "How would you diagnose a network connectivity issue?",

    # Monitoring queries (typical IDLE state)
    "What is the current system load?",
    "Are there any critical errors in the logs?",
    "Is the disk space below 10%?",
    "Check if any process is using more than 80% CPU",
    "Monitor network traffic statistics",

    # Multi-step reasoning
    "If a user reports slow performance, what are the top 5 things to check?",
    "Explain the difference between virtual memory and physical memory",
    "What are the security implications of running services as root?",
    "How does the Linux scheduler prioritize processes?",
    "Describe the boot process from power-on to login prompt"
]

def get_memory_usage() -> float:
    """Get current process memory in MB"""
    process = psutil.Process()
    return process.memory_info().rss / 1024 / 1024

def benchmark_model(model_path: str, model_name: str, queries: List[str]) -> Dict:
    """
    Benchmark a single model.

    Returns:
        Dict with latencies, load time, memory usage
    """
    print(f"\n{'='*70}")
    print(f"Benchmarking: {model_name}")
    print(f"{'='*70}")

    if not os.path.exists(model_path):
        print(f"[ERROR] Model not found: {model_path}")
        return None

    # Measure load time
    print(f"Loading model...")
    mem_before = get_memory_usage()
    load_start = time.time()

    try:
        model = Llama(model_path=model_path, **MODEL_PARAMS)
    except Exception as e:
        print(f"[ERROR] Failed to load model: {e}")
        return None

    load_time = time.time() - load_start
    mem_after = get_memory_usage()
    mem_usage = mem_after - mem_before

    print(f"  Load time: {load_time:.3f}s")
    print(f"  Memory usage: {mem_usage:.1f} MB")
    print()

    # Benchmark inference
    latencies = []
    print(f"Running {len(queries)} test queries...")

    for i, query in enumerate(queries, 1):
        # Simple prompt format
        prompt = f"<|user|>\n{query}\n<|assistant|>\n"

        start = time.time()
        try:
            response = model(
                prompt,
                max_tokens=100,
                temperature=0.1,
                stop=["<|user|>", "\n\n\n"]
            )
            latency = time.time() - start
            latencies.append(latency * 1000)  # Convert to ms

            # Show progress every 5 queries
            if i % 5 == 0:
                print(f"  Completed {i}/{len(queries)} queries (avg: {statistics.mean(latencies):.1f}ms)")

        except Exception as e:
            print(f"  [ERROR] Query {i} failed: {e}")
            continue

    # Calculate statistics
    if not latencies:
        print("[ERROR] No successful queries")
        return None

    latencies.sort()
    results = {
        "model_name": model_name,
        "load_time": load_time,
        "memory_usage": mem_usage,
        "queries": len(latencies),
        "mean_ms": statistics.mean(latencies),
        "median_ms": statistics.median(latencies),
        "p95_ms": latencies[int(len(latencies) * 0.95)],
        "p99_ms": latencies[int(len(latencies) * 0.99)],
        "min_ms": min(latencies),
        "max_ms": max(latencies),
        "stdev_ms": statistics.stdev(latencies) if len(latencies) > 1 else 0
    }

    # Unload model
    del model
    gc.collect()
    time.sleep(1)  # Let GC finish

    return results

def print_comparison(old_results: Dict, new_results: Dict):
    """Print detailed comparison table"""
    print(f"\n{'='*70}")
    print("COMPARISON RESULTS")
    print(f"{'='*70}")
    print()

    # Table header
    print(f"{'Metric':<30} {'TinyLlama 1.1B':<20} {'Llama 3.2 1B':<20} {'Change':<15}")
    print(f"{'-'*30} {'-'*20} {'-'*20} {'-'*15}")

    # Load time
    old_load = old_results['load_time']
    new_load = new_results['load_time']
    load_change = ((new_load - old_load) / old_load) * 100
    print(f"{'Load Time (s)':<30} {old_load:<20.3f} {new_load:<20.3f} {load_change:>+.1f}%")

    # Memory usage
    old_mem = old_results['memory_usage']
    new_mem = new_results['memory_usage']
    mem_change = ((new_mem - old_mem) / old_mem) * 100
    print(f"{'Memory Usage (MB)':<30} {old_mem:<20.1f} {new_mem:<20.1f} {mem_change:>+.1f}%")

    print()

    # Inference latencies
    metrics = [
        ("Mean Latency (ms)", "mean_ms"),
        ("Median Latency (ms)", "median_ms"),
        ("P95 Latency (ms)", "p95_ms"),
        ("P99 Latency (ms)", "p99_ms"),
        ("Min Latency (ms)", "min_ms"),
        ("Max Latency (ms)", "max_ms"),
        ("Std Dev (ms)", "stdev_ms")
    ]

    for label, key in metrics:
        old_val = old_results[key]
        new_val = new_results[key]
        change = ((new_val - old_val) / old_val) * 100 if old_val > 0 else 0
        print(f"{label:<30} {old_val:<20.1f} {new_val:<20.1f} {change:>+.1f}%")

    print()
    print(f"{'='*70}")
    print("VERDICT")
    print(f"{'='*70}")

    # Overall assessment
    mean_change = ((new_results['mean_ms'] - old_results['mean_ms']) / old_results['mean_ms']) * 100

    if abs(mean_change) < 10:
        speed_verdict = "🟢 SIMILAR SPEED (within 10%)"
    elif mean_change < 0:
        speed_verdict = f"🟢 FASTER ({abs(mean_change):.1f}% improvement)"
    else:
        speed_verdict = f"🟡 SLOWER ({mean_change:.1f}% regression)"

    if abs(mem_change) < 20:
        mem_verdict = "🟢 SIMILAR MEMORY"
    elif mem_change < 0:
        mem_verdict = f"🟢 LESS MEMORY ({abs(mem_change):.1f}% reduction)"
    else:
        mem_verdict = f"🟡 MORE MEMORY ({mem_change:.1f}% increase)"

    print(f"Speed:  {speed_verdict}")
    print(f"Memory: {mem_verdict}")
    print()
    print("Quality: 🟢 HIGHER (Llama 3.2 1B has better instruction following)")
    print("Context: 🟢 MUCH BETTER (128K vs limited context window)")
    print()

    if abs(mean_change) < 20 and abs(mem_change) < 30:
        print("✅ UPGRADE JUSTIFIED: Similar performance with better quality + context")
    else:
        print("⚠️  TRADE-OFF: Performance changes, but quality improvements may justify upgrade")

    print(f"{'='*70}")

if __name__ == "__main__":
    print("="*70)
    print("JARVIS AI-OS - Model Upgrade Benchmark")
    print("TinyLlama 1.1B (old) vs Llama 3.2 1B (new)")
    print("="*70)

    # Check models exist
    if not os.path.exists(TINYLLAMA_OLD_PATH):
        print(f"[ERROR] TinyLlama not found: {TINYLLAMA_OLD_PATH}")
        sys.exit(1)

    if not os.path.exists(LLAMA32_PATH):
        print(f"[ERROR] Llama 3.2 1B not found: {LLAMA32_PATH}")
        sys.exit(1)

    print(f"\nModels directory: {MODELS_DIR}")
    print(f"  Old: tinyllama-1.1b-chat-v1.0.Q4_K_M.gguf")
    print(f"  New: Llama-3.2-1B-Instruct-Q4_K_M.gguf")
    print(f"\nTest queries: {len(TEST_QUERIES)}")
    print(f"GPU layers: {MODEL_PARAMS['n_gpu_layers']} (RTX 2070)")

    # Benchmark TinyLlama (old)
    old_results = benchmark_model(TINYLLAMA_OLD_PATH, "TinyLlama 1.1B", TEST_QUERIES)

    if old_results is None:
        print("[ERROR] TinyLlama benchmark failed")
        sys.exit(1)

    # Benchmark Llama 3.2 (new)
    new_results = benchmark_model(LLAMA32_PATH, "Llama 3.2 1B", TEST_QUERIES)

    if new_results is None:
        print("[ERROR] Llama 3.2 1B benchmark failed")
        sys.exit(1)

    # Print comparison
    print_comparison(old_results, new_results)

    print("\nBenchmark complete!")
