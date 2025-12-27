#!/bin/bash
# ============================================================================
# JARVIS AI-OS - Raspberry Pi 4 Local AI Setup
# ============================================================================
#
# IMPORTANT: This script is for RASPBERRY PI OS - NOT bare-metal seL4!
#
# Phase 2 Reality:
# - Pi 4 runs bare-metal seL4 in production (no Linux, no Python)
# - This script is for TESTING/DEVELOPMENT with Pi OS (separate SD card)
# - Production seL4 uses UART to PC for AI inference
# - Phase 3 will add native llama.cpp on seL4 (future scope)
#
# Dual SD Card Workflow:
# - SD Card 1: seL4 bare-metal (production)
# - SD Card 2: Raspberry Pi OS (for testing local AI with this script)
#
# This script sets up local AI inference capability on Raspberry Pi 4.
# Run this AFTER basic Pi 4 setup (OS installed, network configured).
#
# What this script does:
# 1. Installs required Python packages (llama-cpp-python for ARM64)
# 2. Creates models directory structure
# 3. Downloads TinyLlama 1.1B Q4_K_M model (~640MB)
# 4. Verifies installation
#
# Usage:
#   # On Pi 4:
#   chmod +x setup_pi4_local_ai.sh
#   ./setup_pi4_local_ai.sh
#
#   # Or from your PC via SSH:
#   scp setup_pi4_local_ai.sh pi@raspberrypi:~/
#   ssh pi@raspberrypi "./setup_pi4_local_ai.sh"
#
# Requirements:
# - Raspberry Pi 4 with Raspberry Pi OS (64-bit recommended)
# - Internet connection for downloads
# - ~2GB free disk space
# - ~30 minutes for first-time setup
#
# ============================================================================

set -e  # Exit on error

echo "============================================================"
echo "  JARVIS AI-OS - Pi 4 Local AI Setup"
echo "============================================================"
echo ""

# Check if running on Pi 4
check_pi4() {
    echo "[1/6] Checking hardware..."
    if [ -f /proc/device-tree/model ]; then
        MODEL=$(cat /proc/device-tree/model)
        if [[ "$MODEL" == *"Raspberry Pi 4"* ]]; then
            echo "      ✓ Raspberry Pi 4 detected: $MODEL"
            return 0
        fi
    fi
    echo "      ⚠ Warning: Not running on Raspberry Pi 4"
    echo "      Continuing anyway (for testing)..."
    return 0
}

# Check available RAM
check_ram() {
    echo "[2/6] Checking available RAM..."
    TOTAL_RAM=$(free -m | awk '/^Mem:/{print $2}')
    echo "      Total RAM: ${TOTAL_RAM}MB"

    if [ "$TOTAL_RAM" -lt 2000 ]; then
        echo "      ⚠ Warning: Less than 2GB RAM. Model may not load."
        echo "      Recommended: Pi 4 with 4GB or 8GB RAM"
    else
        echo "      ✓ Sufficient RAM for TinyLlama (~1.5GB needed)"
    fi
}

# Install dependencies
install_deps() {
    echo "[3/6] Installing dependencies..."

    # Update package list
    sudo apt-get update -qq

    # Install Python and pip if not present
    sudo apt-get install -y -qq python3 python3-pip python3-venv

    # Install build dependencies for llama-cpp-python
    sudo apt-get install -y -qq build-essential cmake

    echo "      ✓ System dependencies installed"

    # Install llama-cpp-python (ARM64 wheel)
    echo "      Installing llama-cpp-python (this takes ~5-10 minutes)..."
    pip3 install --user llama-cpp-python \
        --extra-index-url https://abetlen.github.io/llama-cpp-python/whl/cpu \
        2>&1 | tail -3

    echo "      ✓ llama-cpp-python installed"
}

# Create models directory
setup_models_dir() {
    echo "[4/6] Setting up models directory..."

    MODELS_DIR="$HOME/models"
    mkdir -p "$MODELS_DIR"

    echo "      Models directory: $MODELS_DIR"
    echo "      ✓ Directory created"
}

# Download TinyLlama model
download_model() {
    echo "[5/6] Downloading TinyLlama 1.1B Q4_K_M..."

    MODELS_DIR="$HOME/models"
    MODEL_FILE="tinyllama-1.1b-chat-v1.0.Q4_K_M.gguf"
    MODEL_PATH="$MODELS_DIR/$MODEL_FILE"

    # Check if model already exists
    if [ -f "$MODEL_PATH" ]; then
        SIZE=$(stat -f%z "$MODEL_PATH" 2>/dev/null || stat -c%s "$MODEL_PATH")
        SIZE_MB=$((SIZE / 1024 / 1024))
        echo "      Model already exists: $MODEL_PATH (${SIZE_MB}MB)"
        echo "      ✓ Skipping download"
        return 0
    fi

    # Download from Hugging Face
    MODEL_URL="https://huggingface.co/TheBloke/TinyLlama-1.1B-Chat-v1.0-GGUF/resolve/main/tinyllama-1.1b-chat-v1.0.Q4_K_M.gguf"

    echo "      Downloading ~640MB model..."
    echo "      URL: $MODEL_URL"
    echo "      This may take 5-15 minutes depending on your connection..."
    echo ""

    # Download with progress
    wget -O "$MODEL_PATH" "$MODEL_URL" --progress=bar:force 2>&1

    # Verify download
    if [ -f "$MODEL_PATH" ]; then
        SIZE=$(stat -f%z "$MODEL_PATH" 2>/dev/null || stat -c%s "$MODEL_PATH")
        SIZE_MB=$((SIZE / 1024 / 1024))
        echo ""
        echo "      ✓ Download complete: ${SIZE_MB}MB"
    else
        echo "      ✗ Download failed!"
        exit 1
    fi
}

# Verify installation
verify_install() {
    echo "[6/6] Verifying installation..."

    MODELS_DIR="$HOME/models"
    MODEL_FILE="tinyllama-1.1b-chat-v1.0.Q4_K_M.gguf"
    MODEL_PATH="$MODELS_DIR/$MODEL_FILE"

    # Check model file
    if [ ! -f "$MODEL_PATH" ]; then
        echo "      ✗ Model file not found: $MODEL_PATH"
        exit 1
    fi

    # Test Python import
    python3 -c "from llama_cpp import Llama; print('      ✓ llama-cpp-python import OK')" 2>/dev/null || {
        echo "      ✗ Failed to import llama-cpp-python"
        exit 1
    }

    # Quick load test (optional, takes ~30-60s)
    echo "      Testing model load (this takes 30-60 seconds)..."
    python3 << EOF
import time
from llama_cpp import Llama

start = time.time()
model = Llama(
    model_path="$MODEL_PATH",
    n_ctx=512,
    n_gpu_layers=0,
    n_threads=4,
    verbose=False
)
load_time = time.time() - start
print(f"      ✓ Model loaded successfully ({load_time:.1f}s)")

# Quick inference test
start = time.time()
result = model("Hello", max_tokens=5)
infer_time = time.time() - start
print(f"      ✓ Inference test passed ({infer_time:.1f}s)")

del model
EOF

    echo "      ✓ All verification tests passed"
}

# Main
main() {
    check_pi4
    check_ram
    install_deps
    setup_models_dir
    download_model
    verify_install

    echo ""
    echo "============================================================"
    echo "  ✓ Local AI Setup Complete!"
    echo "============================================================"
    echo ""
    echo "  Model: TinyLlama 1.1B Q4_K_M"
    echo "  Location: ~/models/tinyllama-1.1b-chat-v1.0.Q4_K_M.gguf"
    echo "  Performance: ~3-5 tokens/sec on Pi 4"
    echo ""
    echo "  To use local AI mode, set in your config:"
    echo "    config = {'local_ai_mode': True}"
    echo ""
    echo "  Or test directly:"
    echo "    python3 -c \"from local_ai_handler import LocalAIHandler; h = LocalAIHandler(); h.initialize(); print(h.generate('Hello'))\""
    echo ""
}

main "$@"
