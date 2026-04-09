#!/bin/bash
# JARVIS AI-OS — Model Quality Bench-Off
# Runs a fixed prompt set through llama-cli on all models
# Compares response quality across models
# Usage: bash phase3/scripts/bench_quality.sh

set -uo pipefail

MODELS_DIR="$HOME/Desktop/JARVIS_OS/models"
LLAMA_CLI="$HOME/llama.cpp/build/bin/llama-cli"
RESULTS_DIR="$MODELS_DIR/quality_results"

if [ ! -x "$LLAMA_CLI" ]; then
    echo "ERROR: llama-cli not found at $LLAMA_CLI"
    exit 1
fi

mkdir -p "$RESULTS_DIR"

# 10 test prompts covering different domains
PROMPTS=(
    "The seL4 microkernel is"
    "Explain how virtual memory works in three sentences."
    "What is the difference between a process and a thread?"
    "Write a C function that reverses a string in place."
    "The capital of Australia is"
    "What are the key benefits of formally verified software?"
    "Describe the TCP three-way handshake step by step."
    "What happens when you type a URL into a browser?"
    "Compare RISC and CISC architectures in two sentences."
    "Write a bash one-liner to find all .c files larger than 1MB."
)

echo "=== JARVIS AI-OS Quality Bench-Off ==="
echo "Date: $(date)"
echo "Hardware: $(lscpu | grep 'Model name' | sed 's/.*: *//')"
echo "Prompts: ${#PROMPTS[@]}"
echo "Max tokens: 100 per response"
echo "Temperature: 0 (greedy — deterministic)"
echo "Results dir: $RESULTS_DIR"
echo ""

MODELS=$(find "$MODELS_DIR" -maxdepth 1 -name "*.gguf" -type f | sort)
COUNT=$(echo "$MODELS" | wc -l)

echo "Found $COUNT models"
echo ""

# Run each model through all prompts
N=0
echo "$MODELS" | while read model; do
    N=$((N + 1))
    NAME=$(basename "$model" .gguf)
    OUTFILE="$RESULTS_DIR/$NAME.txt"

    echo "[$N/$COUNT] $NAME"

    {
        echo "=== $NAME ==="
        echo "Date: $(date)"
        echo ""

        for i in "${!PROMPTS[@]}"; do
            P="${PROMPTS[$i]}"
            PN=$((i + 1))
            echo "--- Prompt $PN: \"$P\" ---"

            # Run llama-cli: greedy, 100 tokens, no interactive, suppress loading noise
            RESPONSE=$("$LLAMA_CLI" \
                -m "$model" \
                -p "$P" \
                -n 100 \
                --temp 0 \
                -ngl 0 \
                -t 8 \
                --no-display-prompt \
                --log-disable \
                2>/dev/null) || RESPONSE="[FAILED TO GENERATE]"

            echo "$RESPONSE"
            echo ""
        done

        echo "=== End $NAME ==="
    } > "$OUTFILE" 2>&1

    echo "  Saved to $OUTFILE"
    echo ""
done

# Generate comparison summary
SUMMARY="$RESULTS_DIR/SUMMARY.txt"
{
    echo "=== Quality Bench-Off Summary ==="
    echo "Date: $(date)"
    echo ""
    echo "Prompt | Model | First 80 chars of response"
    echo "-------|-------|---------------------------"

    for i in "${!PROMPTS[@]}"; do
        P="${PROMPTS[$i]}"
        PN=$((i + 1))
        echo ""
        echo "=== Prompt $PN: \"$P\" ==="

        for f in "$RESULTS_DIR"/*.txt; do
            [ "$f" = "$SUMMARY" ] && continue
            NAME=$(basename "$f" .txt)
            # Extract response for this prompt
            RESP=$(sed -n "/--- Prompt $PN:/,/--- Prompt $((PN+1)):\|=== End/p" "$f" | \
                   grep -v "^---" | grep -v "^=== End" | head -3 | tr '\n' ' ' | cut -c1-80)
            echo "  $NAME: $RESP"
        done
    done
} > "$SUMMARY"

echo "=== Quality Bench-Off Complete ==="
echo "Per-model results: $RESULTS_DIR/<model>.txt"
echo "Summary: $SUMMARY"
