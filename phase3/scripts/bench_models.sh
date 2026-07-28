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

# Find all GGUF files. QUALITY_MODELS is an optional ERE filter on the filename
# — benching 11 models at the deployed 250-token cap takes hours, and a targeted
# comparison (incumbent + 2-3 candidates) is the usual need.
#   QUALITY_MODELS='gemma-4-E2B|Meta-Llama-3.1-8B' bash bench_models.sh quality
MODELS=$(find "$MODELS_DIR" -maxdepth 1 -name "*.gguf" -type f | sort)
if [ -n "${QUALITY_MODELS:-}" ]; then
    MODELS=$(echo "$MODELS" | grep -E "$QUALITY_MODELS" || true)
fi
COUNT=$(echo "$MODELS" | grep -c . || true)
if [ "$COUNT" -eq 0 ]; then
    echo "ERROR: no models matched (MODELS_DIR=$MODELS_DIR QUALITY_MODELS=${QUALITY_MODELS:-<none>})"
    exit 1
fi

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
    echo "Config: -ngl 0 (CPU only), -t 8, --chunks 100 (~51K tokens, ±0.1 PPL accuracy)"
    echo "Estimated time: ~10-20 min per model, ~2-3 hours total"
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
            "$LLAMA_PERPLEXITY" -m "$model" -f "$WIKITEXT" -ngl 0 -t 8 --chunks 100 2>&1 | tail -5 || echo "  FAILED: $NAME"
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

    # ---- THE REAL WORKLOAD (2026-07-27) -------------------------------------
    # The original 10 were generic. We now know the actual traffic: 30 unique
    # questions in the control-IN store @21140000. Most are unusable as quality
    # probes (8 generated filler, 4 test markers, 3 unanswerable-by-design, 4 that
    # route to SYSFACTS/DECLINE and never reach the model), so this is the GENUINE
    # subset, plus 4 of the original 10 for continuity with the old scores.
    PROMPTS=(
        # -- real control-IN traffic (short-form definitional dominates) --
        "explain paging in one line"
        "in one line, what is a mutex"
        "in one line, what is a translation lookaside buffer"
        "in one line, what is a memory management unit"
        "in one line, what does the seL4 capability system provide"
        "what is a page fault?"
        "why doesn't adding more CPU cores speed up a single-threaded program?"
        "In one sentence, why doesn't adding more CPU cores speed up a single-threaded program?"
        # -- continuity with the 2026-04 bench-off --
        "Explain how virtual memory works in three sentences."
        "What is the difference between a process and a thread?"
        "Describe the TCP three-way handshake step by step."
        "Write a C function that reverses a string in place."
    )

    # ---- SAMPLER (B3): the box generates GREEDY; the Gemma 4 card recommends
    # temp 1.0 / top_p 0.95 / top_k 64. Reported SEPARATELY from the model
    # comparison because it applies whatever model wins.
    case "${QUALITY_SAMPLER:-greedy}" in
        greedy)      SAMPLER_ARGS=(--temp 0) ;;
        recommended) SAMPLER_ARGS=(--temp 1.0 --top-p 0.95 --top-k 64) ;;
        *) echo "ERROR: QUALITY_SAMPLER must be 'greedy' or 'recommended'"; return 1 ;;
    esac

    # ---- GENERATION CAP: the DEPLOYED control-IN cap, not the original -n 100.
    # PB_GEN_MAX_CONTROL_IN in inference_server.c. A model that writes better long
    # answers is invisible at 100 tokens.
    GEN_MAX="${QUALITY_GEN_MAX:-250}"

    # ---- QUALITY_PREFIX: text prepended INSIDE the user turn, before the question.
    # DEPLOYABLE BY CONSTRUCTION: that is the exact slot inference_server.c already
    # injects the G3 retrieval preamble into (after the user-turn \n, before the
    # question) — same mechanism, different text. A harness-only device would
    # measure something the box could never ship.
    PREFIX="${QUALITY_PREFIX:-}"

    # A prefix CHANGES the system being measured, so two different prefixes must never
    # share an output filename. Until 2026-07-28 the path used ${PREFIX:+.frontload},
    # which collapsed EVERY non-empty prefix to one name: the front-load-only run
    # overwrote the two-clause run in place and those per-model artifacts were
    # unrecoverable. Guessing a name is what consumed them, so this fails CLOSED.
    if [ -n "$PREFIX" ] && [ -z "${QUALITY_PREFIX_TAG:-}" ]; then
        echo "ERROR: QUALITY_PREFIX is set but QUALITY_PREFIX_TAG is empty." >&2
        echo "       A prefixed run must name itself or it overwrites the previous one." >&2
        echo "       e.g. QUALITY_PREFIX_TAG=frontload-only" >&2
        exit 2
    fi

    echo "Prompts: ${#PROMPTS[@]} (real control-IN workload + 4 continuity)"
    echo "Max tokens: $GEN_MAX (deployed PB_GEN_MAX_CONTROL_IN)"
    echo "Sampler: ${QUALITY_SAMPLER:-greedy} (${SAMPLER_ARGS[*]})"
    echo "Template: --jinja (each model's OWN chat template); thinking: -rea off"
    echo "Prefix: ${PREFIX:-<none>}"
    echo ""

    N=0
    echo "$MODELS" | while read model; do
        N=$((N + 1))
        NAME=$(basename "$model" .gguf)
        # sampler in the filename: a greedy and a recommended-sampler run of the
        # same model are different evidence and must not overwrite each other
        OUTFILE="$QUALITY_DIR/$NAME.${QUALITY_SAMPLER:-greedy}${QUALITY_PREFIX_TAG:+.$QUALITY_PREFIX_TAG}.txt"

        echo "[$N/$COUNT] $NAME"

        # ---- VERIFY THE PROMPT THIS MODEL ACTUALLY GETS -------------------
        # The 2026-04 bench-off was misread for three months because everyone
        # trusted the flags instead of looking at the built prompt. Do not
        # trust -rea/--jinja either: assert. One extra model load per model.
        VP=$("$LLAMA_CLI" -m "$model" -p "${PREFIX:+$PREFIX }ping" -n 1 -ngl 0 -t 8 -st \
                 --jinja -rea off --verbose-prompt 2>&1 </dev/null | tr -d '\010\r' | head -80)
        TMPL_OK=0; THINK_LEAK=0
        echo "$VP" | grep -qE '<\|turn>|<\|im_start\|>|\[INST\]|<\|start_header_id\|>|<start_of_turn>' && TMPL_OK=1
        echo "$VP" | grep -qE '<\|think\|>|\[Start thinking\]|<\|channel>' && THINK_LEAK=1
        if [ "$TMPL_OK" -eq 1 ]; then
            echo "  template: APPLIED"
        else
            echo "  template: *** NOT DETECTED — scores for this model are NOT comparable ***"
        fi
        if [ "$THINK_LEAK" -eq 1 ]; then
            echo "  thinking: *** LEAKED despite -rea off — investigate before trusting scores ***"
        else
            echo "  thinking: off (no think token in the built prompt)"
        fi

        {
            echo "=== $NAME ==="
            echo "Sampler: ${QUALITY_SAMPLER:-greedy} | GenMax: $GEN_MAX | template_applied=$TMPL_OK think_leak=$THINK_LEAK"
            echo "Prefix: ${PREFIX:-<none>}"
            echo "--- BUILT PROMPT (verbatim from --verbose-prompt; assert, do not trust the flag) ---"
            printf '%s\n' "$VP" | sed -n '/<|turn>user/,/<|turn>model/p;/<|im_start|>/,/assistant/p' | head -8
            echo "--- end built prompt ---"
            echo "Date: $(date)"
            echo ""

            for i in "${!PROMPTS[@]}"; do
                P="${PROMPTS[$i]}"
                PN=$((i + 1))
                echo "--- Prompt $PN: \"$P\" ---"
                # CORRECTED INVOCATION (2026-07-27). The original was
                #   -p "$P" -n 100 --temp 0 -ngl 0 -t 8 --no-display-prompt --log-disable
                # and was believed to be raw completion because it lacked --jinja.
                # It is NOT: llama-cli auto-enables conversation mode whenever the
                # GGUF carries a chat template (PR #11214, 2025-01-13), so this
                # script silently applied each model's template AND, for Gemma 4,
                # emitted <|think|> -- i.e. it measured THINKING-ON, the config
                # closed in THINKING_MODE_RESEARCH.md. --log-disable hid the
                # "enabling conversation mode" line, which is why nobody saw it.
                #
                #   -st          one turn then exit (conversation mode is scriptable)
                #   --jinja      each model's OWN template (explicit, not inherited)
                #   -rea off     thinking OFF -- deploy-faithful (JARVIS_THINKING=0)
                #
                # NOTE -no-cnv is NOT usable: the current binary rejects it
                # ("--no-conversation is not supported by llama-cli") and proceeds
                # WITH conversation mode. Do not "fix" this by adding it back.
                # THE ONE VARIABLE UNDER TEST: the prefix goes inside the user turn,
                # ahead of the question — the g3-preamble slot, so it is deployable.
                PSEND="${PREFIX:+$PREFIX }$P"
                RAW=$("$LLAMA_CLI" \
                    -m "$model" \
                    -p "$PSEND" \
                    -n "$GEN_MAX" \
                    "${SAMPLER_ARGS[@]}" \
                    -ngl 0 \
                    -t 8 \
                    -st \
                    --jinja \
                    -rea off \
                    --no-display-prompt \
                    --log-disable \
                    2>/dev/null) || RAW="[FAILED TO GENERATE]"

                # Conversation mode prints an interactive banner, a "> prompt"
                # echo, a spinner and a timing line on STDOUT. Strip them, or the
                # blind judges score llama-cli's chrome alongside the answer.
                # The spinner is BACKSPACE-driven (0x08), not plain characters —
                # verified with od -c. Delete \b and \r FIRST, or the leading-run
                # strip below silently does nothing.
                # STRIP THE SPINNER FROM THE FIRST RESPONSE LINE ONLY.
                #
                # The previous version ran the strip on EVERY line, which silently
                # ATE CONTENT: "/**" became "**", "// x" became "x", markdown "- item"
                # lost its bullet, and ALL CODE INDENTATION was removed. A blind judge
                # spotted the mangled C comments and correctly called it a pipeline
                # artifact rather than a model failure. Its character class was also
                # malformed (awk: "regexp escape sequence `\ ' is not a known regexp
                # operator"), so it never stripped the backslash it was written for.
                #
                # The spinner only ever appears at the head of the FIRST line, so that
                # is the only place the strip belongs.
                # index() against a plain set, NOT a regex class: getting a literal
                # backslash into an awk bracket expression failed three different
                # ways (`[|\/\\-]`, `[\\|\/-]` and a "|/-\\ \t" string all left the
                # backslash behind, with awk warning about the escape). sprintf("%c",92)
                # is unambiguous.
                RESPONSE=$(printf '%s\n' "$RAW" | tr -d '\010\r' | awk '
                    BEGIN         { BS = sprintf("%c", 92) }
                    /^> /         { inresp = 1; first = 1; next }
                    /^\[ Prompt:/ { inresp = 0 }
                    inresp {
                        # SKIP BLANKS BEFORE consuming "first": llama-cli emits a
                        # blank line between the "> prompt" echo and the response, so
                        # a naive first-line strip lands on the BLANK and the real
                        # content line keeps its spinner. Cost one run to find.
                        if (length($0) == 0) next
                        if (first) {
                            while (length($0) > 0 && \
                                   (index("|/- \t", substr($0,1,1)) > 0 || substr($0,1,1) == BS))
                                $0 = substr($0,2)
                            first = 0
                        }
                        print
                    }
                ')
                [ -z "$RESPONSE" ] && RESPONSE="[NO RESPONSE EXTRACTED]"
                # tok/s comes free from llama-cli's own timing line (B-req: report speed)
                TPS=$(printf '%s\n' "$RAW" | grep -oE 'Generation: *[0-9.]+ t/s' | tail -1)
                echo "$RESPONSE"
                echo "[speed] ${TPS:-n/a}"
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
