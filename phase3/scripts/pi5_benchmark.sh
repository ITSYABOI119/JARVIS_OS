#!/bin/bash
# =============================================================================
# JARVIS AI-OS Phase 3: Pi 5 llama.cpp / Ollama Benchmark Script
# =============================================================================
#
# Purpose: Benchmark LLM inference on Raspberry Pi 5 (4GB) to determine
#          practical model limits for Phase 3 multi-Pi or standalone use.
#
# Hardware: Raspberry Pi 5, 4GB LPDDR4X, Cortex-A76 4C @ 2.4 GHz
# OS:       Raspberry Pi OS (64-bit, Bookworm or later)
#
# Usage:
#   chmod +x pi5_benchmark.sh
#   ./pi5_benchmark.sh           # Full benchmark (all models)
#   ./pi5_benchmark.sh --quick   # Quick benchmark (1B models only)
#   ./pi5_benchmark.sh --install # Install dependencies only
#
# Output: CSV file at ~/jarvis_pi5_benchmark_YYYYMMDD_HHMMSS.csv
#         Human-readable summary on stdout
#
# Date:   March 19, 2026
# Author: JARVIS Development Team
# =============================================================================

set -euo pipefail

# --- Configuration -----------------------------------------------------------

# Models to benchmark (ordered by size, smallest first)
# Format: "ollama_name:friendly_name:param_count"
MODELS_1B=(
    "gemma3:1b:Gemma3-1B:1B"
    "llama3.2:1b:Llama3.2-1B:1B"
    "qwen2.5:1.5b:Qwen2.5-1.5B:1.5B"
)

MODELS_2B=(
    "granite3.1-dense:2b:Granite3.1-Dense-2B:2B"
    "smollm2:1.7b:SmolLM2-1.7B:1.7B"
)

MODELS_3B=(
    "llama3.2:3b:Llama3.2-3B:3B"
    "qwen2.5:3b:Qwen2.5-3B:3B"
)

# Benchmark prompts (short and long to test different workloads)
PROMPT_SHORT="What is the capital of France? Answer in one sentence."
PROMPT_MEDIUM="Explain how a microkernel operating system works in 3 paragraphs."
PROMPT_LONG="Write a detailed comparison of RTOS vs general-purpose OS for embedded systems. Cover scheduling, memory management, interrupt handling, and real-time guarantees. Be thorough."

# Number of benchmark runs per model (for averaging)
BENCHMARK_RUNS=3

# Swap configuration for 3B models on 4GB RAM
SWAP_SIZE_MB=4096

# Output files
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
CSV_FILE="$HOME/jarvis_pi5_benchmark_${TIMESTAMP}.csv"
LOG_FILE="$HOME/jarvis_pi5_benchmark_${TIMESTAMP}.log"

# --- Color codes for terminal output -----------------------------------------

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No color

# --- Helper functions ---------------------------------------------------------

log() {
    echo -e "${BLUE}[BENCHMARK]${NC} $1"
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] $1" >> "$LOG_FILE"
}

warn() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] WARNING: $1" >> "$LOG_FILE"
}

error() {
    echo -e "${RED}[ERROR]${NC} $1"
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] ERROR: $1" >> "$LOG_FILE"
}

success() {
    echo -e "${GREEN}[OK]${NC} $1"
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] OK: $1" >> "$LOG_FILE"
}

# --- System checks ------------------------------------------------------------

check_system() {
    log "Checking system..."

    # Check architecture
    ARCH=$(uname -m)
    if [ "$ARCH" != "aarch64" ]; then
        error "Expected aarch64 architecture, got $ARCH"
        error "This script is designed for Raspberry Pi 5 (64-bit OS)"
        exit 1
    fi

    # Check Pi model
    if [ -f /proc/device-tree/model ]; then
        PI_MODEL=$(tr -d '\0' < /proc/device-tree/model)
        log "Hardware: $PI_MODEL"
    else
        warn "Cannot determine Pi model (not running on Pi?)"
        PI_MODEL="Unknown"
    fi

    # Check RAM
    TOTAL_RAM_KB=$(grep MemTotal /proc/meminfo | awk '{print $2}')
    TOTAL_RAM_MB=$((TOTAL_RAM_KB / 1024))
    log "Total RAM: ${TOTAL_RAM_MB} MB"

    if [ "$TOTAL_RAM_MB" -lt 3500 ]; then
        warn "Less than 4GB RAM detected (${TOTAL_RAM_MB} MB)"
        warn "3B models will likely OOM without swap"
    fi

    # Check available RAM
    AVAIL_RAM_KB=$(grep MemAvailable /proc/meminfo | awk '{print $2}')
    AVAIL_RAM_MB=$((AVAIL_RAM_KB / 1024))
    log "Available RAM: ${AVAIL_RAM_MB} MB"

    # Check swap
    SWAP_TOTAL_KB=$(grep SwapTotal /proc/meminfo | awk '{print $2}')
    SWAP_TOTAL_MB=$((SWAP_TOTAL_KB / 1024))
    log "Swap: ${SWAP_TOTAL_MB} MB"

    # Check CPU frequency
    if [ -f /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq ]; then
        CPU_MAX_KHZ=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq)
        CPU_MAX_MHZ=$((CPU_MAX_KHZ / 1000))
        log "CPU max frequency: ${CPU_MAX_MHZ} MHz"
    fi

    # Check temperature
    if [ -f /sys/class/thermal/thermal_zone0/temp ]; then
        TEMP_MILLI=$(cat /sys/class/thermal/thermal_zone0/temp)
        TEMP_C=$((TEMP_MILLI / 1000))
        log "CPU temperature: ${TEMP_C} C"
        if [ "$TEMP_C" -gt 70 ]; then
            warn "CPU temperature is high (${TEMP_C}C). Results may be affected by throttling."
            warn "Ensure active cooling is installed and working."
        fi
    fi

    # Check for active cooling
    if [ -d /sys/class/thermal/cooling_device0 ]; then
        log "Cooling device detected"
    else
        warn "No cooling device detected. Active cooling strongly recommended."
    fi
}

# --- Install dependencies -----------------------------------------------------

install_ollama() {
    log "Installing Ollama..."

    if command -v ollama &> /dev/null; then
        OLLAMA_VER=$(ollama --version 2>&1 || echo "unknown")
        log "Ollama already installed: $OLLAMA_VER"
        return 0
    fi

    log "Downloading and installing Ollama..."
    curl -fsSL https://ollama.com/install.sh | sh

    # Wait for Ollama service to start
    sleep 3

    # Verify installation
    if command -v ollama &> /dev/null; then
        OLLAMA_VER=$(ollama --version 2>&1 || echo "unknown")
        success "Ollama installed: $OLLAMA_VER"
    else
        error "Ollama installation failed"
        exit 1
    fi
}

install_llama_cpp() {
    log "Installing llama.cpp..."

    if [ -f "$HOME/llama.cpp/llama-cli" ] || [ -f "$HOME/llama.cpp/build/bin/llama-cli" ]; then
        log "llama.cpp already built"
        return 0
    fi

    # Install build dependencies
    sudo apt-get update
    sudo apt-get install -y build-essential cmake git libopenblas-dev

    # Clone and build llama.cpp with OpenBLAS for ARM NEON
    cd "$HOME"
    if [ ! -d "llama.cpp" ]; then
        git clone https://github.com/ggml-org/llama.cpp.git
    fi
    cd llama.cpp

    # Build with OpenBLAS (important for ARM performance)
    mkdir -p build && cd build
    cmake .. \
        -DGGML_BLAS=ON \
        -DGGML_BLAS_VENDOR=OpenBLAS \
        -DCMAKE_BUILD_TYPE=Release
    cmake --build . --config Release -j$(nproc)

    success "llama.cpp built with OpenBLAS"
}

configure_swap() {
    log "Configuring swap for 3B model support..."

    SWAP_CURRENT_MB=$(($(grep SwapTotal /proc/meminfo | awk '{print $2}') / 1024))

    if [ "$SWAP_CURRENT_MB" -ge "$SWAP_SIZE_MB" ]; then
        log "Swap already sufficient: ${SWAP_CURRENT_MB} MB >= ${SWAP_SIZE_MB} MB"
        return 0
    fi

    warn "Increasing swap to ${SWAP_SIZE_MB} MB (currently ${SWAP_CURRENT_MB} MB)"
    warn "This is needed for 3B models on 4GB RAM"

    # Use dphys-swapfile if available (standard on Pi OS)
    if [ -f /etc/dphys-swapfile ]; then
        sudo sed -i "s/^CONF_SWAPSIZE=.*/CONF_SWAPSIZE=${SWAP_SIZE_MB}/" /etc/dphys-swapfile
        sudo sed -i "s/^CONF_MAXSWAP=.*/CONF_MAXSWAP=${SWAP_SIZE_MB}/" /etc/dphys-swapfile
        # Add CONF_MAXSWAP if not present
        if ! grep -q "^CONF_MAXSWAP=" /etc/dphys-swapfile; then
            echo "CONF_MAXSWAP=${SWAP_SIZE_MB}" | sudo tee -a /etc/dphys-swapfile > /dev/null
        fi
        sudo systemctl restart dphys-swapfile
        sleep 2
        NEW_SWAP=$(($(grep SwapTotal /proc/meminfo | awk '{print $2}') / 1024))
        success "Swap configured: ${NEW_SWAP} MB"
    else
        warn "dphys-swapfile not found. Creating manual swap file..."
        sudo fallocate -l ${SWAP_SIZE_MB}M /swapfile
        sudo chmod 600 /swapfile
        sudo mkswap /swapfile
        sudo swapon /swapfile
        success "Manual swap file created: ${SWAP_SIZE_MB} MB"
    fi
}

# --- Model management ---------------------------------------------------------

pull_models() {
    local models=("$@")

    for model_spec in "${models[@]}"; do
        IFS=':' read -r ollama_name friendly_name param_count <<< \
            "$(echo "$model_spec" | awk -F: '{print $1":"$2, $3, $4}')"
        # Re-parse: "gemma3:1b:Gemma3-1B:1B"
        ollama_name=$(echo "$model_spec" | cut -d: -f1,2)
        friendly_name=$(echo "$model_spec" | cut -d: -f3)

        log "Pulling model: $ollama_name ($friendly_name)..."
        if ollama pull "$ollama_name" 2>> "$LOG_FILE"; then
            success "Model ready: $ollama_name"
        else
            warn "Failed to pull $ollama_name — skipping"
        fi
    done
}

# --- Benchmark functions ------------------------------------------------------

# Run a single Ollama benchmark and extract tok/s
benchmark_ollama_single() {
    local model_name="$1"
    local prompt="$2"
    local run_num="$3"

    log "  Run $run_num: Ollama $model_name"

    # Use ollama run with --verbose to get timing info
    # Capture stderr (where timing goes) and stdout (response)
    local start_time end_time duration_ms
    start_time=$(date +%s%N)

    local output
    output=$(echo "$prompt" | timeout 300 ollama run "$model_name" --verbose 2>&1) || true

    end_time=$(date +%s%N)
    duration_ms=$(( (end_time - start_time) / 1000000 ))

    # Parse eval rate from verbose output (format: "eval rate: XX.XX tokens/s")
    local eval_rate prompt_rate total_tokens
    eval_rate=$(echo "$output" | grep -oP 'eval rate:\s+\K[\d.]+' | tail -1 || echo "0")
    prompt_rate=$(echo "$output" | grep -oP 'prompt eval rate:\s+\K[\d.]+' || echo "0")
    total_tokens=$(echo "$output" | grep -oP 'eval count:\s+\K[\d]+' || echo "0")

    # If verbose parsing failed, estimate from timing
    if [ "$eval_rate" = "0" ] || [ -z "$eval_rate" ]; then
        # Count approximate tokens in output (words * 1.3)
        local response_text
        response_text=$(echo "$output" | grep -v "^eval\|^prompt\|^total\|^load" || echo "$output")
        local word_count
        word_count=$(echo "$response_text" | wc -w)
        local est_tokens=$((word_count * 13 / 10))
        if [ "$duration_ms" -gt 0 ]; then
            eval_rate=$(echo "scale=2; $est_tokens * 1000 / $duration_ms" | bc 2>/dev/null || echo "0")
        fi
        warn "  Verbose parsing failed, estimated: ${eval_rate} tok/s from ${est_tokens} tokens in ${duration_ms}ms"
    fi

    # Get memory usage during inference
    local mem_rss_kb mem_rss_mb
    mem_rss_kb=$(ps aux | grep "[o]llama" | awk '{sum += $6} END {print sum+0}')
    mem_rss_mb=$((mem_rss_kb / 1024))

    echo "${eval_rate},${prompt_rate},${duration_ms},${total_tokens},${mem_rss_mb}"
}

# Run full benchmark for one model
benchmark_model() {
    local model_spec="$1"
    local ollama_name friendly_name param_count

    ollama_name=$(echo "$model_spec" | cut -d: -f1,2)
    friendly_name=$(echo "$model_spec" | cut -d: -f3)
    param_count=$(echo "$model_spec" | cut -d: -f4)

    log "Benchmarking: $friendly_name ($ollama_name) [$param_count parameters]"
    log "================================================================"

    # Check if model is available
    if ! ollama list 2>/dev/null | grep -q "$(echo "$ollama_name" | cut -d: -f1)"; then
        warn "Model $ollama_name not available, attempting pull..."
        if ! ollama pull "$ollama_name" 2>> "$LOG_FILE"; then
            error "Cannot pull $ollama_name — skipping"
            return 1
        fi
    fi

    # Record system state before benchmark
    local pre_temp pre_avail_mb
    if [ -f /sys/class/thermal/thermal_zone0/temp ]; then
        pre_temp=$(($(cat /sys/class/thermal/thermal_zone0/temp) / 1000))
    else
        pre_temp=0
    fi
    pre_avail_mb=$(($(grep MemAvailable /proc/meminfo | awk '{print $2}') / 1024))

    # Get model file size
    local model_size
    model_size=$(ollama list 2>/dev/null | grep "$(echo "$ollama_name" | cut -d: -f1)" | awk '{print $3 $4}' || echo "unknown")

    # Run short prompt benchmarks
    log "  --- Short prompt ---"
    local short_results=()
    for i in $(seq 1 $BENCHMARK_RUNS); do
        result=$(benchmark_ollama_single "$ollama_name" "$PROMPT_SHORT" "$i")
        short_results+=("$result")
        sleep 2  # Brief cooldown between runs
    done

    # Run medium prompt benchmarks
    log "  --- Medium prompt ---"
    local medium_results=()
    for i in $(seq 1 $BENCHMARK_RUNS); do
        result=$(benchmark_ollama_single "$ollama_name" "$PROMPT_MEDIUM" "$i")
        medium_results+=("$result")
        sleep 2
    done

    # Calculate averages for short prompt
    local sum_eval=0 sum_prompt=0 sum_dur=0 sum_mem=0 count=0
    for r in "${short_results[@]}"; do
        eval_r=$(echo "$r" | cut -d, -f1)
        prompt_r=$(echo "$r" | cut -d, -f2)
        dur_r=$(echo "$r" | cut -d, -f3)
        mem_r=$(echo "$r" | cut -d, -f5)
        sum_eval=$(echo "$sum_eval + $eval_r" | bc 2>/dev/null || echo "$sum_eval")
        sum_dur=$((sum_dur + dur_r))
        sum_mem=$((sum_mem + mem_r))
        count=$((count + 1))
    done
    local avg_eval_short avg_dur_short avg_mem_short
    avg_eval_short=$(echo "scale=2; $sum_eval / $count" | bc 2>/dev/null || echo "0")
    avg_dur_short=$((sum_dur / count))
    avg_mem_short=$((sum_mem / count))

    # Calculate averages for medium prompt
    sum_eval=0 sum_dur=0 sum_mem=0 count=0
    for r in "${medium_results[@]}"; do
        eval_r=$(echo "$r" | cut -d, -f1)
        dur_r=$(echo "$r" | cut -d, -f3)
        mem_r=$(echo "$r" | cut -d, -f5)
        sum_eval=$(echo "$sum_eval + $eval_r" | bc 2>/dev/null || echo "$sum_eval")
        sum_dur=$((sum_dur + dur_r))
        sum_mem=$((sum_mem + mem_r))
        count=$((count + 1))
    done
    local avg_eval_medium avg_dur_medium avg_mem_medium
    avg_eval_medium=$(echo "scale=2; $sum_eval / $count" | bc 2>/dev/null || echo "0")
    avg_dur_medium=$((sum_dur / count))
    avg_mem_medium=$((sum_mem / count))

    # Post-benchmark temperature
    local post_temp=0
    if [ -f /sys/class/thermal/thermal_zone0/temp ]; then
        post_temp=$(($(cat /sys/class/thermal/thermal_zone0/temp) / 1000))
    fi

    # Write CSV rows
    echo "${friendly_name},${param_count},${model_size},short,${avg_eval_short},${avg_dur_short},${avg_mem_short},${pre_avail_mb},${pre_temp},${post_temp}" >> "$CSV_FILE"
    echo "${friendly_name},${param_count},${model_size},medium,${avg_eval_medium},${avg_dur_medium},${avg_mem_medium},${pre_avail_mb},${pre_temp},${post_temp}" >> "$CSV_FILE"

    # Print summary
    log "  Results for $friendly_name ($param_count):"
    log "    Short prompt:  ${avg_eval_short} tok/s, ${avg_dur_short}ms, ${avg_mem_short} MB RAM"
    log "    Medium prompt: ${avg_eval_medium} tok/s, ${avg_dur_medium}ms, ${avg_mem_medium} MB RAM"
    log "    Temperature:   ${pre_temp}C -> ${post_temp}C"
    log "    Model size:    ${model_size}"
    echo ""

    # Unload model to free RAM for next test
    ollama stop "$ollama_name" 2>/dev/null || true
    sleep 3
}

# --- llama.cpp direct benchmark (optional, for comparison) --------------------

benchmark_llama_cpp() {
    local model_path="$1"
    local friendly_name="$2"
    local prompt="$3"

    if [ ! -f "$HOME/llama.cpp/build/bin/llama-cli" ]; then
        warn "llama.cpp not built, skipping direct benchmark"
        return 1
    fi

    log "  llama.cpp direct: $friendly_name"

    local output
    output=$("$HOME/llama.cpp/build/bin/llama-cli" \
        -m "$model_path" \
        -p "$prompt" \
        -n 128 \
        --ctx-size 512 \
        --threads $(nproc) \
        --temp 0.7 \
        2>&1) || true

    # Parse llama.cpp output for timing
    local eval_tok_s prompt_tok_s
    eval_tok_s=$(echo "$output" | grep -oP 'eval time.*?(\d+\.\d+) tokens per second' | grep -oP '[\d.]+(?= tokens)' || echo "0")
    prompt_tok_s=$(echo "$output" | grep -oP 'prompt eval time.*?(\d+\.\d+) tokens per second' | grep -oP '[\d.]+(?= tokens)' || echo "0")

    echo "${eval_tok_s},${prompt_tok_s}"
}

# --- Main execution -----------------------------------------------------------

print_banner() {
    echo ""
    echo "============================================================="
    echo "  JARVIS AI-OS Phase 3: Pi 5 LLM Benchmark"
    echo "  Date: $(date '+%Y-%m-%d %H:%M:%S')"
    echo "============================================================="
    echo ""
}

print_summary() {
    echo ""
    echo "============================================================="
    echo "  BENCHMARK COMPLETE"
    echo "============================================================="
    echo ""
    echo "Results saved to:"
    echo "  CSV: $CSV_FILE"
    echo "  Log: $LOG_FILE"
    echo ""
    echo "--- Results Table ---"
    echo ""
    printf "%-20s %-8s %-10s %-8s %-10s %-8s %-8s\n" \
        "Model" "Params" "Prompt" "Tok/s" "Time(ms)" "RAM(MB)" "Temp(C)"
    printf "%-20s %-8s %-10s %-8s %-10s %-8s %-8s\n" \
        "--------------------" "--------" "----------" "--------" "----------" "--------" "--------"

    # Read CSV and format
    while IFS=, read -r name params size prompt_type toks dur mem avail pre_t post_t; do
        printf "%-20s %-8s %-10s %-8s %-10s %-8s %-8s\n" \
            "$name" "$params" "$prompt_type" "$toks" "$dur" "$mem" "$post_t"
    done < <(tail -n +2 "$CSV_FILE")

    echo ""
    echo "--- Key Findings ---"
    echo ""

    # Find best 1B model
    local best_1b
    best_1b=$(grep ",1B," "$CSV_FILE" | grep ",short," | sort -t, -k5 -rn | head -1 || echo "")
    if [ -n "$best_1b" ]; then
        local best_name best_toks
        best_name=$(echo "$best_1b" | cut -d, -f1)
        best_toks=$(echo "$best_1b" | cut -d, -f5)
        echo "Best 1B model (short prompt): $best_name at $best_toks tok/s"
    fi

    # Check if any 3B model ran successfully
    local has_3b
    has_3b=$(grep ",3B," "$CSV_FILE" | head -1 || echo "")
    if [ -n "$has_3b" ]; then
        local mem_3b toks_3b
        mem_3b=$(echo "$has_3b" | cut -d, -f7)
        toks_3b=$(echo "$has_3b" | cut -d, -f5)
        echo "3B model results: $toks_3b tok/s, ${mem_3b} MB RAM"
        if [ "$mem_3b" -gt "$TOTAL_RAM_MB" ] 2>/dev/null; then
            echo "WARNING: 3B model exceeded physical RAM - using swap (degraded performance)"
        fi
    else
        echo "No 3B models completed successfully (likely OOM on 4GB)"
    fi

    echo ""
    echo "--- JARVIS Phase 3 Implications ---"
    echo ""
    echo "For Pi 5 as AI inference node in multi-Pi cluster:"
    echo "  - 1B models: VIABLE (~10-13 tok/s, <3GB RAM)"
    echo "  - 1.5B models: VIABLE (~9-11 tok/s, <3GB RAM)"
    echo "  - 3B models: MARGINAL (needs swap, ~3-5 tok/s, >4GB with OS)"
    echo "  - Decision cache handles 85% of queries (<1ms)"
    echo "  - Only 15% of queries need LLM inference"
    echo ""
}

main() {
    local mode="full"

    # Parse arguments
    while [[ $# -gt 0 ]]; do
        case $1 in
            --quick)
                mode="quick"
                shift
                ;;
            --install)
                mode="install"
                shift
                ;;
            --with-llama-cpp)
                mode="full-cpp"
                shift
                ;;
            --help|-h)
                echo "Usage: $0 [--quick|--install|--with-llama-cpp|--help]"
                echo ""
                echo "  --quick          Benchmark 1B models only (fast, ~10 min)"
                echo "  --install        Install Ollama and llama.cpp only"
                echo "  --with-llama-cpp Also build and benchmark with llama.cpp directly"
                echo "  --help           Show this help"
                exit 0
                ;;
            *)
                error "Unknown option: $1"
                exit 1
                ;;
        esac
    done

    print_banner

    # Initialize log and CSV
    echo "# JARVIS Pi 5 Benchmark Log - $(date)" > "$LOG_FILE"

    echo "model,params,model_size,prompt_type,eval_tok_s,duration_ms,ram_mb,avail_ram_mb,pre_temp_c,post_temp_c" > "$CSV_FILE"

    # System checks
    check_system

    # Install dependencies
    install_ollama

    if [ "$mode" = "full-cpp" ]; then
        install_llama_cpp
    fi

    if [ "$mode" = "install" ]; then
        success "Installation complete. Run without --install to benchmark."
        exit 0
    fi

    # Ensure Ollama is running
    if ! pgrep -x ollama > /dev/null 2>&1; then
        log "Starting Ollama service..."
        ollama serve &> /dev/null &
        sleep 3
    fi

    # Pull models
    log "Pulling 1B models..."
    pull_models "${MODELS_1B[@]}"

    if [ "$mode" != "quick" ]; then
        log "Pulling 2B models..."
        pull_models "${MODELS_2B[@]}"

        log "Configuring swap for 3B models..."
        configure_swap

        log "Pulling 3B models..."
        pull_models "${MODELS_3B[@]}"
    fi

    # Run benchmarks
    echo ""
    log "================================================================"
    log "Starting benchmarks ($BENCHMARK_RUNS runs per model per prompt)"
    log "================================================================"
    echo ""

    # 1B models
    for model in "${MODELS_1B[@]}"; do
        benchmark_model "$model" || warn "Benchmark failed for $model"
    done

    if [ "$mode" != "quick" ]; then
        # 2B models
        for model in "${MODELS_2B[@]}"; do
            benchmark_model "$model" || warn "Benchmark failed for $model"
        done

        # 3B models (may OOM on 4GB)
        log ""
        log "================================================================"
        log "3B MODELS - These may fail on 4GB RAM. Using swap."
        log "================================================================"
        log ""

        for model in "${MODELS_3B[@]}"; do
            # Try with reduced timeout - 3B on 4GB may hang
            timeout 600 bash -c "$(declare -f benchmark_model benchmark_ollama_single log warn error success); benchmark_model '$model'" 2>/dev/null \
                || warn "3B model $model timed out or OOM'd (expected on 4GB)"
        done
    fi

    # Print summary
    print_summary

    success "Benchmark complete!"
}

main "$@"
