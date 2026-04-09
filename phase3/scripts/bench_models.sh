#!/bin/bash
# JARVIS AI-OS — Model Bench-Off Script
# Phase 1: Speed (llama-bench)
# Phase 2: Perplexity (llama-perplexity on WikiText-2)
# Phase 3: Quality (llama-cli with fixed prompts)
# Usage: bash phase3/scripts/bench_models.sh [speed|perplexity|quality|all]

set -uo pipefail  # no -e: continue past failed models

MODELS_DIR="$HOME/Desktop/JARVIS_OS/models"
LLAMA_BENCH="$HOME/llama.cpp/build/bin/llama-bench"
LLAMA_PERPLEXITY="$HOME/llama.cpp/build/bin/llama-perplexity"
LLAMA_CLI="$HOME/llama.cpp/build/bin/llama-cli"
WIKITEXT="$MODELS_DIR/wikitext-2-raw/wiki.test.raw"

SPEED_RESULTS="$MODELS_DIR/bench_results.txt"
PPL_RESULTS="$MODELS_DIR/perplexity_results.txt"
QUALITY_DIR="$MODELS_DIR/quality_results"

MODE="${1:-all}"

# Find all GGUF files
MODELS=$(find "$MODELS_DIR" -maxdepth 1 -name "*.gguf" -type f | sort)
COUNT=$(echo "$MODELS" | wc -l)

header() {
    echo "=== JARVIS AI-OS Model Bench-Off — $1 ==="
    echo "Date: $(date)"
    echo "Hardware: $(lscpu | grep 'Model name' | sed 's/.*: *//')"
    echo "RAM: $(free -h | awk '/Mem:/ {print $2}')"
    echo "Models: $COUNT"
    echo ""
}

# ============================================================
# PHASE 1: Speed (llama-bench)
# ============================================================
run_speed() {
    if [ ! -x "$LLAMA_BENCH" ]; then
        echo "ERROR: llama-bench not found at $LLAMA_BENCH"
        return 1
    fi

    header "Speed (llama-bench)"

    echo "Found $COUNT models:"
    echo "$MODELS" | while read f; do
        SIZE=$(du -h "$f" | cut -f1)
        echo "  $SIZE  $(basename "$f")"
    done
    echo ""
    echo "Config: -ngl 0 (CPU only), -r 1, -p 512, -n 128, -t 8"
    echo ""

    {
        header "Speed"

        N=0
        echo "$MODELS" | while read model; do
            N=$((N + 1))
            NAME=$(basename "$model")
            echo "[$N/$COUNT] $NAME"
            echo "---"
            "$LLAMA_BENCH" -m "$model" -ngl 0 -r 1 -p 512 -n 128 -t 8 2>&1 || echo "  FAILED: $NAME"
            echo ""
        done

        echo "=== Speed Bench Complete ==="
    } 2>&1 | tee "$SPEED_RESULTS"

    echo ""
    echo "Speed results saved to $SPEED_RESULTS"
}

# ============================================================
# PHASE 2: Perplexity (llama-perplexity on WikiText-2)
# ============================================================
run_perplexity() {
    if [ ! -x "$LLAMA_PERPLEXITY" ]; then
        echo "ERROR: llama-perplexity not found at $LLAMA_PERPLEXITY"
        return 1
    fi

    if [ ! -f "$WIKITEXT" ]; then
        echo "ERROR: WikiText-2 not found at $WIKITEXT"
        echo "Download: cd $MODELS_DIR && wget https://huggingface.co/datasets/ggml-org/ci/resolve/main/wikitext-2-raw-v1.zip && unzip wikitext-2-raw-v1.zip"
        return 1
    fi

    header "Perplexity (WikiText-2)"

    echo "Corpus: $WIKITEXT ($(du -h "$WIKITEXT" | cut -f1))"
    echo "Config: -ngl 0 (CPU only), -t 8"
    echo "Estimated time: ~5-10 min per model, ~1-2 hours total"
    echo ""

    {
        header "Perplexity (WikiText-2)"
        echo "Corpus: $WIKITEXT"
        echo ""

        N=0
        echo "$MODELS" | while read model; do
            N=$((N + 1))
            NAME=$(basename "$model")
            echo "[$N/$COUNT] $NAME"
            echo "---"
            START=$(date +%s)
            "$LLAMA_PERPLEXITY" -m "$model" -f "$WIKITEXT" -ngl 0 -t 8 2>&1 | tail -3 || echo "  FAILED: $NAME"
            END=$(date +%s)
            echo "  Time: $((END - START))s"
            echo ""
        done

        echo "=== Perplexity Bench Complete ==="
    } 2>&1 | tee "$PPL_RESULTS"

    echo ""
    echo "Perplexity results saved to $PPL_RESULTS"
}

# ============================================================
# PHASE 3: Quality (llama-cli with fixed prompts)
# ============================================================
run_quality() {
    if [ ! -x "$LLAMA_CLI" ]; then
        echo "ERROR: llama-cli not found at $LLAMA_CLI"
        return 1
    fi

    mkdir -p "$QUALITY_DIR"

    header "Quality (10 prompts, greedy)"

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

    echo "Prompts: ${#PROMPTS[@]}"
    echo "Max tokens: 100, Temperature: 0 (greedy)"
    echo ""

    N=0
    echo "$MODELS" | while read model; do
        N=$((N + 1))
        NAME=$(basename "$model" .gguf)
        OUTFILE="$QUALITY_DIR/$NAME.txt"

        echo "[$N/$COUNT] $NAME"

        {
            echo "=== $NAME ==="
            echo "Date: $(date)"
            echo ""

            for i in "${!PROMPTS[@]}"; do
                P="${PROMPTS[$i]}"
                PN=$((i + 1))
                echo "--- Prompt $PN: \"$P\" ---"
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
    done

    echo ""
    echo "Quality results saved to $QUALITY_DIR/"
}

# ============================================================
# Main
# ============================================================
echo "=== JARVIS AI-OS Model Bench-Off ==="
echo "Mode: $MODE"
echo ""

case "$MODE" in
    speed)      run_speed ;;
    perplexity) run_perplexity ;;
    quality)    run_quality ;;
    all)
        run_speed
        echo ""
        echo "=========================================="
        echo ""
        run_perplexity
        echo ""
        echo "=========================================="
        echo ""
        run_quality
        ;;
    *)
        echo "Usage: $0 [speed|perplexity|quality|all]"
        echo "  speed      — llama-bench tok/s (pp512 + tg128)"
        echo "  perplexity — WikiText-2 perplexity (lower = better)"
        echo "  quality    — 10 prompts through llama-cli (greedy)"
        echo "  all        — run all three in sequence"
        exit 1
        ;;
esac

echo ""
echo "=== All done ==="
