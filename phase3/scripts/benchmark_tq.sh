#!/bin/bash
# TurboQuant Benchmark: 8B + 13B + 70B
set -e

TQ_BITS="${TQ_BITS:-3.5}"
CLI="/home/jarvis/llama.cpp/build-tq/bin/llama-cli"
MODELS="/home/jarvis/Desktop/JARVIS_OS/phase3/models"
SUFFIX=$(echo "$TQ_BITS" | tr -d '.')
LOG="$MODELS/benchmark_tq${SUFFIX}_results.log"
MONITOR="$MODELS/benchmark_tq${SUFFIX}_monitor.log"

export LLAMA_TQ_KEY_BITS="$TQ_BITS"
export LLAMA_TQ_VAL_BITS="$TQ_BITS"

: > "$LOG"

echo "========================================" | tee -a "$LOG"
echo " TURBOQUANT ${TQ_BITS}-bit | $(date)" | tee -a "$LOG"
echo " Ryzen 2700X (8C/16T) | 48GB RAM | No GPU" | tee -a "$LOG"
echo "========================================" | tee -a "$LOG"

echo "=== PRE-RUN ===" | tee -a "$LOG"
free -m | awk '/Mem:/{printf "RAM: %sMB used, %sMB avail\n", $3, $7}' | tee -a "$LOG"

echo "ts,ram_used,ram_avail" > "$MONITOR"
( while true; do
  R=$(free -m | awk '/Mem:/{printf "%s,%s",$3,$7}')
  echo "$(date +%H:%M:%S),$R" >> "$MONITOR"
  sleep 1
done ) &
MON=$!
trap "kill $MON 2>/dev/null" EXIT

run_prompt() {
  local LABEL="$1" TOKENS="$2" PROMPT="$3" MODEL="$4" CTX="$5"
  echo "" | tee -a "$LOG"
  echo "=== $LABEL ($TOKENS tokens) ===" | tee -a "$LOG"
  $CLI -m "$MODEL" -ngl 0 -c "$CTX" -t 8 \
    --temp 0.7 -n "$TOKENS" \
    --no-display-prompt --simple-io \
    -cnv --single-turn \
    -p "$PROMPT" \
    2>&1 | tee -a "$LOG"
  echo "" | tee -a "$LOG"
  echo "--- RAM ---" | tee -a "$LOG"
  free -m | awk '/Mem:/{printf "RAM: %sMB used, %sMB avail\n", $3, $7}' | tee -a "$LOG"
}

run_model() {
  local NAME="$1" MODEL="$2" CTX="$3"
  echo "" | tee -a "$LOG"
  echo "########################################" | tee -a "$LOG"
  echo "# MODEL: $NAME (TQ ${TQ_BITS}-bit)" | tee -a "$LOG"
  echo "########################################" | tee -a "$LOG"
  run_prompt "$NAME P1" 64 "What is the seL4 microkernel and why is it important for security?" "$MODEL" "$CTX"
  run_prompt "$NAME P2" 128 "Write a lock-free SPSC ring buffer in C with push and pop functions." "$MODEL" "$CTX"
  run_prompt "$NAME P3" 256 "Compare KV cache compression: quantization vs eviction vs sparse attention. Which is best for edge devices with 1B-3B models?" "$MODEL" "$CTX"
}

run_model "Llama-3.1-8B" "$MODELS/Meta-Llama-3.1-8B-Instruct-Q4_K_M.gguf" 4096
run_model "Llama-2-13B" "$MODELS/llama-2-13b.Q4_K_M.gguf" 4096
run_model "Llama-3.1-70B" "$MODELS/Meta-Llama-3.1-70B-Instruct-Q4_K_M.gguf" 2048

echo "" | tee -a "$LOG"
echo "========================================" | tee -a "$LOG"
echo "=== ALL DONE at $(date) ===" | tee -a "$LOG"
echo "========================================" | tee -a "$LOG"
echo "Results: $LOG"
echo "Monitor: $MONITOR"
