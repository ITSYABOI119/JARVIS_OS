#!/bin/bash
# JARVIS AI-OS — Speed Bench (Local/Main PC)
# Runs llama-bench on all models — for comparing hardware
# Usage (in WSL): bash phase3/scripts/bench_speed_local.sh

set -uo pipefail

MODELS_DIR="/mnt/c/Users/jluca/Documents/JARVIS_OS/models"
LLAMA_BENCH="$HOME/llama.cpp/build/bin/llama-bench"
RESULTS="$MODELS_DIR/bench_results_$(hostname).txt"

# Auto-detect thread count (physical cores)
THREADS=$(nproc --all)
PHYS_CORES=$((THREADS / 2))
if [ "$PHYS_CORES" -lt 1 ]; then PHYS_CORES=4; fi

if [ ! -x "$LLAMA_BENCH" ]; then
    echo "ERROR: llama-bench not found at $LLAMA_BENCH"
    echo "Build it:"
    echo "  cd ~ && git clone https://github.com/ggerganov/llama.cpp"
    echo "  cd llama.cpp && cmake -B build -DGGML_CUDA=OFF && cmake --build build -j\$(nproc)"
    exit 1
fi

echo "=== JARVIS AI-OS Speed Bench ==="
echo "Date: $(date)"
echo "Hostname: $(hostname)"
echo "CPU: $(lscpu | grep 'Model name' | sed 's/.*: *//')"
echo "RAM: $(free -h | awk '/Mem:/ {print $2}')"
echo "Threads: $PHYS_CORES (physical cores)"
echo "llama-bench: $LLAMA_BENCH"
echo "Models dir: $MODELS_DIR"
echo ""

MODELS=$(find "$MODELS_DIR" -maxdepth 1 -name "*.gguf" -type f | sort)
COUNT=$(echo "$MODELS" | wc -l)

echo "Found $COUNT models:"
echo "$MODELS" | while read f; do
    SIZE=$(du -h "$f" | cut -f1)
    echo "  $SIZE  $(basename "$f")"
done
echo ""

{
    echo "=== Speed Bench: $(hostname) ==="
    echo "Date: $(date)"
    echo "CPU: $(lscpu | grep 'Model name' | sed 's/.*: *//')"
    echo "RAM: $(free -h | awk '/Mem:/ {print $2}')"
    echo "Threads: $PHYS_CORES"
    echo ""

    N=0
    echo "$MODELS" | while read model; do
        N=$((N + 1))
        NAME=$(basename "$model")
        echo "[$N/$COUNT] $NAME"
        echo "---"
        "$LLAMA_BENCH" -m "$model" -ngl 0 -r 1 -p 512 -n 128 -t "$PHYS_CORES" 2>&1 || echo "  FAILED: $NAME"
        echo ""
    done

    echo "=== Speed Bench Complete ==="
} 2>&1 | tee "$RESULTS"

echo ""
echo "Results saved to $RESULTS"
