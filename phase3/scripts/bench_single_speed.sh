#!/bin/bash
# JARVIS AI-OS — Single Model Speed Bench (JARVIS PC)
# Usage: bash phase3/scripts/bench_single_speed.sh /path/to/model.gguf

set -uo pipefail

MODEL="${1:-}"
LLAMA_BENCH="$HOME/llama.cpp/build/bin/llama-bench"

if [ -z "$MODEL" ] || [ ! -f "$MODEL" ]; then
    echo "Usage: bash $0 /path/to/model.gguf"
    exit 1
fi

if [ ! -x "$LLAMA_BENCH" ]; then
    echo "ERROR: llama-bench not found at $LLAMA_BENCH"
    exit 1
fi

NAME=$(basename "$MODEL")
echo "=== Speed Bench: $NAME ==="
echo "Date: $(date)"
echo "CPU: $(lscpu | grep 'Model name' | sed 's/.*: *//')"
echo ""

"$LLAMA_BENCH" -m "$MODEL" -ngl 0 -r 1 -p 512 -n 128 -t 8 2>&1

echo ""
echo "=== Done ==="
