#!/bin/bash
# JARVIS AI-OS — Model Bench-Off Script
# Runs llama-bench on all models in ~/Desktop/JARVIS_OS/models/
# Usage: bash phase3/scripts/bench_models.sh

set -uo pipefail  # no -e: continue past failed models

MODELS_DIR="$HOME/Desktop/JARVIS_OS/models"
LLAMA_BENCH="$HOME/llama.cpp/build/bin/llama-bench"
RESULTS="$MODELS_DIR/bench_results.txt"

if [ ! -x "$LLAMA_BENCH" ]; then
    echo "ERROR: llama-bench not found at $LLAMA_BENCH"
    echo "Build it: cd ~/llama.cpp && cmake -B build -DGGML_CUDA=OFF && cmake --build build -j\$(nproc)"
    exit 1
fi

echo "=== JARVIS AI-OS Model Bench-Off ==="
echo "Date: $(date)"
echo "Hardware: $(lscpu | grep 'Model name' | sed 's/.*: *//')"
echo "RAM: $(free -h | awk '/Mem:/ {print $2}')"
echo "Threads: 8"
echo "llama-bench: $LLAMA_BENCH"
echo "Models dir: $MODELS_DIR"
echo ""

# Find all GGUF files
MODELS=$(find "$MODELS_DIR" -maxdepth 1 -name "*.gguf" -type f | sort)
COUNT=$(echo "$MODELS" | wc -l)

echo "Found $COUNT models:"
echo "$MODELS" | while read f; do
    SIZE=$(du -h "$f" | cut -f1)
    echo "  $SIZE  $(basename "$f")"
done
echo ""
echo "Running bench: -ngl 0 (CPU only), -r 1, -p 512, -n 128, -t 8"
echo "Results saved to: $RESULTS"
echo ""

# Run benchmarks
{
    echo "=== JARVIS AI-OS Model Bench-Off ==="
    echo "Date: $(date)"
    echo "Hardware: $(lscpu | grep 'Model name' | sed 's/.*: *//')"
    echo "RAM: $(free -h | awk '/Mem:/ {print $2}')"
    echo ""

    N=0
    echo "$MODELS" | while read model; do
        N=$((N + 1))
        NAME=$(basename "$model")
        echo "[$N/$COUNT] $NAME"
        echo "---"
        "$LLAMA_BENCH" -m "$model" -ngl 0 -r 1 -p 512 -n 128 -t 8 2>&1 || echo "  FAILED to load/run $(basename "$model")"
        echo ""
    done

    echo "=== Bench-Off Complete ==="
} 2>&1 | tee "$RESULTS"

echo ""
echo "Results saved to $RESULTS"
