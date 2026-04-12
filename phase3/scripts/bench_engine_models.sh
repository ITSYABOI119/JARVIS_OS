#!/bin/bash
# JARVIS AI-OS — Native Engine Benchmark
# Compiles bench_engine.c and runs it against all GGUF models in models/
# Usage: bash phase3/scripts/bench_engine_models.sh

set -uo pipefail  # no -e: continue past failed models

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
MODELS_DIR="$REPO_ROOT/models"
RESULTS="$MODELS_DIR/jarvis_engine_bench.txt"
BENCH_BIN="/tmp/bench_engine"
BENCH_SRC="$REPO_ROOT/phase3/src/ai/bench_engine.c"

# --- Auto-compile -----------------------------------------------------------

compile_bench() {
    echo "Compiling bench_engine..."
    gcc -O2 -mavx2 -mfma -std=c11 -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE \
        -I "$REPO_ROOT/phase3/src/ai" \
        "$REPO_ROOT/phase3/src/ai/bench_engine.c" \
        "$REPO_ROOT/phase3/src/ai/llama_quant.c" \
        "$REPO_ROOT/phase3/src/ai/llama_load.c" \
        "$REPO_ROOT/phase3/src/ai/llama_forward.c" \
        "$REPO_ROOT/phase3/src/ai/gguf_parser.c" \
        "$REPO_ROOT/phase3/src/ai/gguf_vocab.c" \
        "$REPO_ROOT/phase3/src/ai/tokenizer.c" \
        "$REPO_ROOT/phase3/src/ai/tensor_ops.c" \
        "$REPO_ROOT/phase3/src/ai/dequant.c" \
        "$REPO_ROOT/phase3/src/ai/sampling.c" \
        "$REPO_ROOT/phase3/src/ai/inference.c" \
        -lm -o "$BENCH_BIN"
    if [ $? -ne 0 ]; then
        echo "ERROR: compilation failed"
        exit 1
    fi
    echo "Compiled OK: $BENCH_BIN"
}

if [ ! -f "$BENCH_SRC" ]; then
    echo "ERROR: source not found: $BENCH_SRC"
    exit 1
fi

if [ ! -x "$BENCH_BIN" ] || [ "$BENCH_SRC" -nt "$BENCH_BIN" ]; then
    compile_bench
else
    echo "bench_engine binary is up to date."
fi

# --- Discover models ---------------------------------------------------------

MODELS=$(find "$MODELS_DIR" -maxdepth 1 -name "*.gguf" -type f | sort)
if [ -z "$MODELS" ]; then
    echo "ERROR: no .gguf files found in $MODELS_DIR"
    exit 1
fi
COUNT=$(echo "$MODELS" | wc -l)

# --- Run benchmarks ----------------------------------------------------------

{
    echo "=== JARVIS Native Engine Benchmark ==="
    echo "Date: $(date)"
    echo "Host: $(hostname)"
    echo "CPU:  $(grep 'model name' /proc/cpuinfo 2>/dev/null | head -1 | sed 's/.*: //' || echo 'unknown')"
    echo "RAM:  $(grep 'MemTotal' /proc/meminfo 2>/dev/null | awk '{printf "%.1f GB", $2/1048576}' || echo 'unknown')"
    echo "Models: $COUNT"
    echo ""

    N=0
    echo "$MODELS" | while read -r model; do
        N=$((N + 1))
        NAME=$(basename "$model")
        SIZE=$(du -h "$model" | cut -f1)

        echo "[$N/$COUNT] $NAME ($SIZE)"
        echo "---"

        set +e
        "$BENCH_BIN" "$model" -v 2>&1
        RC=$?
        set -e

        if [ $RC -ne 0 ]; then
            echo "  FAILED (exit $RC)"
        fi
        echo ""
    done

    echo "=== Done: $COUNT models tested ==="
} 2>&1 | tee "$RESULTS"

echo ""
echo "Results saved to $RESULTS"
