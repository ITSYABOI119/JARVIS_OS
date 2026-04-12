#!/bin/bash
# JARVIS AI-OS — Native Engine Benchmark (retest previously failed models)
# Compiles bench_engine.c and runs it against specific models that failed
# Usage: bash phase3/scripts/bench_engine_failed.sh

set -uo pipefail  # no -e: continue past failed models

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
MODELS_DIR="$REPO_ROOT/models"
RESULTS="$MODELS_DIR/jarvis_engine_bench_retest.txt"
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
        "$REPO_ROOT/phase3/src/ai/ssm.c" \
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

# --- Previously failed models ------------------------------------------------

MODELS=$(cat <<'LIST'
$MODELS_DIR/Phi-3-mini-4k-instruct-q4.gguf
$MODELS_DIR/Qwen3.5-4B-Q4_K_M.gguf
$MODELS_DIR/Qwen3.5-9B-Q4_K_M.gguf
LIST
)
# Expand $MODELS_DIR in the paths
MODELS=$(echo "$MODELS" | sed "s|\\\$MODELS_DIR|$MODELS_DIR|g")
COUNT=$(echo "$MODELS" | wc -l)

# --- Run benchmarks ----------------------------------------------------------

{
    echo "=== JARVIS Engine Retest — Previously Failed Models ==="
    echo "Date: $(date)"
    echo "Host: $(hostname)"
    echo "CPU:  $(grep 'model name' /proc/cpuinfo 2>/dev/null | head -1 | sed 's/.*: //' || echo 'unknown')"
    echo "RAM:  $(grep 'MemTotal' /proc/meminfo 2>/dev/null | awk '{printf "%.1f GB", $2/1048576}' || echo 'unknown')"
    echo "Models: $COUNT"
    echo ""
    echo "| model                          |       size |     params | backend    | threads |            test |                  t/s |"
    echo "| ------------------------------ | ---------: | ---------: | ---------- | ------: | --------------: | -------------------: |"

    N=0
    echo "$MODELS" | while read -r model; do
        N=$((N + 1))
        NAME=$(basename "$model")
        SIZE=$(du -h "$model" 2>/dev/null | cut -f1 || echo "?")

        echo "" >&2
        echo "[$N/$COUNT] $NAME ($SIZE)" >&2

        set +e
        "$BENCH_BIN" "$model"
        RC=$?
        set -e

        if [ $RC -ne 0 ]; then
            printf "| %-30s | %10s |            | %-10s | %7d | %15s | %17s |\n" \
                "$NAME" "$SIZE" "JARVIS" 1 "tg128" "FAILED"
        fi
    done

    echo ""
    echo "=== Done: $COUNT models tested ==="
} 2>&1 | tee "$RESULTS"

echo ""
echo "Results saved to $RESULTS"
