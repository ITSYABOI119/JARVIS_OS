#!/bin/bash
# JARVIS OS - WSL Dependency Setup
# Installs required and optional Python packages for Phase 0-1 testing
#
# Usage:
#   chmod +x setup_wsl_dependencies.sh
#   ./setup_wsl_dependencies.sh

echo "========================================="
echo "JARVIS OS - WSL Dependency Setup"
echo "========================================="
echo ""

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function to check if package is installed
check_package() {
    local pkg="$1"
    if python3 -c "import $pkg" 2>/dev/null; then
        echo -e "${GREEN}[OK]${NC} $pkg already installed"
        return 0
    else
        echo -e "${YELLOW}[MISSING]${NC} $pkg not found"
        return 1
    fi
}

# Function to install package
install_package() {
    local pkg="$1"
    local pip_name="${2:-$pkg}"  # Use package name if pip name not specified

    echo -e "${BLUE}[INSTALLING]${NC} $pip_name"
    if pip3 install "$pip_name" --user; then
        echo -e "${GREEN}[INSTALLED]${NC} $pip_name"
        return 0
    else
        echo -e "${RED}[FAILED]${NC} Failed to install $pip_name"
        return 1
    fi
}

echo "Checking Python version..."
PYTHON_VERSION=$(python3 --version 2>&1)
echo "  $PYTHON_VERSION"
echo ""

echo "========================================="
echo "Required Packages (Critical)"
echo "========================================="
echo ""

REQUIRED_MISSING=0

# Check llama-cpp-python (CRITICAL - needed for all AI tests)
if ! check_package "llama_cpp"; then
    echo "  llama-cpp-python is CRITICAL for AI inference"
    echo "  This package requires compilation and may take 5-10 minutes..."
    echo ""
    read -p "Install llama-cpp-python with GPU support? [y/N] " -n 1 -r
    echo ""
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        # Install with GPU support (CUDA)
        export CMAKE_ARGS="-DLLAMA_CUBLAS=on"
        if ! install_package "llama_cpp" "llama-cpp-python"; then
            REQUIRED_MISSING=1
        fi
        unset CMAKE_ARGS
    else
        echo -e "${YELLOW}[SKIPPED]${NC} llama-cpp-python"
        REQUIRED_MISSING=1
    fi
fi
echo ""

# Check psutil (needed for resource monitoring)
if ! check_package "psutil"; then
    if ! install_package "psutil"; then
        REQUIRED_MISSING=1
    fi
fi
echo ""

# Check numpy (used by llama-cpp-python and tests)
if ! check_package "numpy"; then
    if ! install_package "numpy"; then
        REQUIRED_MISSING=1
    fi
fi
echo ""

echo "========================================="
echo "Optional Packages (Recommended)"
echo "========================================="
echo ""

OPTIONAL_MISSING=0

# Check pynvml (GPU monitoring - optional, falls back to nvidia-smi)
if ! check_package "pynvml"; then
    echo "  pynvml provides GPU memory monitoring (optional)"
    if ! install_package "pynvml"; then
        OPTIONAL_MISSING=1
    fi
fi
echo ""

# Check huggingface_hub (model download - optional)
if ! check_package "huggingface_hub"; then
    echo "  huggingface_hub is only needed for downloading models"
    read -p "Install huggingface_hub? [y/N] " -n 1 -r
    echo ""
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        if ! install_package "huggingface_hub"; then
            OPTIONAL_MISSING=1
        fi
    else
        echo -e "${YELLOW}[SKIPPED]${NC} huggingface_hub"
    fi
fi
echo ""

echo "========================================="
echo "System Packages Check"
echo "========================================="
echo ""

# Check GCC
if gcc --version > /dev/null 2>&1; then
    GCC_VER=$(gcc --version | head -1)
    echo -e "${GREEN}[OK]${NC} GCC: $GCC_VER"
else
    echo -e "${RED}[MISSING]${NC} GCC not found"
    echo "  Install with: sudo apt-get update && sudo apt-get install build-essential"
    REQUIRED_MISSING=1
fi
echo ""

# Check GPU
if nvidia-smi > /dev/null 2>&1; then
    GPU_NAME=$(nvidia-smi --query-gpu=name --format=csv,noheader | head -1)
    GPU_MEM=$(nvidia-smi --query-gpu=memory.total --format=csv,noheader | head -1)
    echo -e "${GREEN}[OK]${NC} GPU: $GPU_NAME ($GPU_MEM)"
else
    echo -e "${YELLOW}[WARNING]${NC} No NVIDIA GPU detected"
    echo "  Tests will run on CPU (much slower)"
fi
echo ""

# Check models
echo "Checking models..."
if ls /mnt/c/Users/jluca/Documents/JARVIS_OS/models/*.gguf > /dev/null 2>&1; then
    echo -e "${GREEN}[OK]${NC} Models found:"
    ls -lh /mnt/c/Users/jluca/Documents/JARVIS_OS/models/*.gguf | awk '{print "  - " $9 " (" $5 ")"}'
else
    echo -e "${YELLOW}[WARNING]${NC} No .gguf models found"
    echo "  Expected location: /mnt/c/Users/jluca/Documents/JARVIS_OS/models/"
    echo "  Download with: cd phase0/experiments && python3 download_phi3.py"
fi
echo ""

echo "========================================="
echo "Setup Summary"
echo "========================================="
echo ""

if [ $REQUIRED_MISSING -eq 0 ]; then
    echo -e "${GREEN}✓ All required packages installed!${NC}"
else
    echo -e "${RED}✗ Some required packages missing${NC}"
    echo "  Review errors above and retry installation"
fi
echo ""

if [ $OPTIONAL_MISSING -eq 0 ]; then
    echo -e "${GREEN}✓ All optional packages installed!${NC}"
else
    echo -e "${YELLOW}⚠ Some optional packages missing (tests will still work)${NC}"
fi
echo ""

if [ $REQUIRED_MISSING -eq 0 ]; then
    echo "========================================="
    echo -e "${GREEN}Setup Complete!${NC}"
    echo "========================================="
    echo ""
    echo "Next steps:"
    echo "  1. Run comprehensive tests: ./run_all_tests_wsl.sh"
    echo "  2. Or run individual tests from phase1/src/"
    echo ""
    echo "See COMPREHENSIVE_TEST_PLAN.md for details."
    exit 0
else
    echo "========================================="
    echo -e "${RED}Setup Incomplete${NC}"
    echo "========================================="
    echo ""
    echo "Please install missing required packages before running tests."
    exit 1
fi
