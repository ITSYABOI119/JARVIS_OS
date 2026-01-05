#!/bin/bash
# JARVIS OS - Comprehensive Test Suite (WSL)
# Run from: /mnt/c/Users/jluca/Documents/JARVIS_OS/
#
# Usage:
#   chmod +x run_all_tests_wsl.sh
#   ./run_all_tests_wsl.sh
#
# Or with output logging:
#   ./run_all_tests_wsl.sh | tee test_results_$(date +%Y%m%d_%H%M%S).log

# Note: Do NOT use 'set -e' - we want to continue running all tests even if some fail

echo "========================================="
echo "JARVIS OS - Comprehensive Test Suite"
echo "Phase 0 + Phase 1 (Weeks 1-16)"
echo "========================================="
echo ""

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

PASS_COUNT=0
FAIL_COUNT=0
SKIP_COUNT=0

# Helper function to run test
run_test() {
    local test_name="$1"
    local test_cmd="$2"
    local required="${3:-yes}"  # Default: required

    echo -e "${BLUE}[RUNNING]${NC} $test_name"

    if eval "$test_cmd" > /tmp/jarvis_test.log 2>&1; then
        echo -e "${GREEN}[PASS]${NC} $test_name"
        PASS_COUNT=$((PASS_COUNT + 1))
    else
        if [ "$required" = "yes" ]; then
            echo -e "${RED}[FAIL]${NC} $test_name"
            echo "Error log (last 20 lines):"
            tail -20 /tmp/jarvis_test.log
            FAIL_COUNT=$((FAIL_COUNT + 1))
        else
            echo -e "${YELLOW}[SKIP]${NC} $test_name (optional, failed)"
            SKIP_COUNT=$((SKIP_COUNT + 1))
        fi
    fi
    echo ""
}

# Change to project root
cd /mnt/c/Users/jluca/Documents/JARVIS_OS

echo "========================================="
echo "Phase 1: Prerequisites Check"
echo "========================================="
echo ""

echo "Checking GPU availability..."
if nvidia-smi > /dev/null 2>&1; then
    GPU_NAME=$(nvidia-smi --query-gpu=name --format=csv,noheader | head -1)
    GPU_MEM=$(nvidia-smi --query-gpu=memory.total --format=csv,noheader | head -1)
    echo -e "${GREEN}[OK]${NC} GPU detected: $GPU_NAME ($GPU_MEM)"
else
    echo -e "${YELLOW}[WARNING]${NC} No GPU detected - tests will be slower"
fi
echo ""

echo "Checking models..."
if ls models/*.gguf > /dev/null 2>&1; then
    echo -e "${GREEN}[OK]${NC} Models found:"
    ls -lh models/*.gguf | awk '{print "  - " $9 " (" $5 ")"}'
else
    echo -e "${RED}[ERROR]${NC} Models missing! Run: cd phase0/experiments && python3 download_phi3.py"
    exit 1
fi
echo ""

echo "Checking Python packages..."
MISSING_PKGS=""

if ! python3 -c "import llama_cpp" 2>/dev/null; then
    MISSING_PKGS="$MISSING_PKGS llama-cpp-python"
fi

if ! python3 -c "import psutil" 2>/dev/null; then
    MISSING_PKGS="$MISSING_PKGS psutil"
fi

if ! python3 -c "import numpy" 2>/dev/null; then
    MISSING_PKGS="$MISSING_PKGS numpy"
fi

if [ -z "$MISSING_PKGS" ]; then
    echo -e "${GREEN}[OK]${NC} All required Python packages installed"
    python3 -c "import llama_cpp; print('  - llama-cpp-python:', llama_cpp.__version__)"
    python3 -c "import psutil; print('  - psutil:', psutil.__version__)"
    python3 -c "import numpy; print('  - numpy:', numpy.__version__)"
else
    echo -e "${RED}[ERROR]${NC} Missing Python packages:$MISSING_PKGS"
    echo "Install with: pip3 install$MISSING_PKGS"
    exit 1
fi
echo ""

echo "Checking GCC..."
if gcc --version > /dev/null 2>&1; then
    GCC_VER=$(gcc --version | head -1)
    echo -e "${GREEN}[OK]${NC} GCC installed: $GCC_VER"
else
    echo -e "${RED}[ERROR]${NC} GCC not found! Install: sudo apt-get install build-essential"
    exit 1
fi
echo ""

START_TIME=$(date +%s)

echo "========================================="
echo "Phase 2: C Component Tests (2 tests)"
echo "========================================="
echo ""

# Store base directory
BASE_DIR=$(pwd)

# Test 1: Decision Cache
run_test "Decision Cache (hash table, 200 patterns)" \
    "cd $BASE_DIR/phase1/src/cache && gcc -O2 decision_cache.c cache_patterns.c test_cache.c -o test_cache -lm && ./test_cache"

# Test 2: IPC Ring Buffer
run_test "IPC Ring Buffer (lock-free SPSC)" \
    "cd $BASE_DIR/phase1/src/ipc && gcc -O2 ring_buffer.c test_ipc.c -o test_ipc -lm && ./test_ipc"

echo ""
echo "========================================="
echo "Phase 3: Python Tests - Basic (5 tests)"
echo "========================================="
echo ""

run_test "AI Agent (Phi-3 wrapper)" "cd $BASE_DIR/phase1/src && python3 ai/test_agent.py"
run_test "Query Pipeline (normalization)" "cd $BASE_DIR/phase1/src && python3 ai/test_query_pipeline.py"
run_test "Shell Interface (30 tests)" "cd $BASE_DIR/phase1/src && python3 shell/test_shell.py"
run_test "IPC Integration (6 tests)" "cd $BASE_DIR/phase1/src && python3 ai/test_ipc_integration.py"
run_test "IPC + Cache Lookup" "cd $BASE_DIR/phase1/src && python3 ai/test_ipc_cache_lookup.py"

echo ""
echo "========================================="
echo "Phase 4: Python Tests - Multi-Agent (6 tests)"
echo "========================================="
echo ""

run_test "Multi-Agent Architecture (4 agents)" "cd $BASE_DIR/phase1/src && python3 ai/test_multi_agent.py"
run_test "Conflict Resolution (priority-based)" "cd $BASE_DIR/phase1/src && python3 ai/test_conflict_resolution.py"
run_test "Routing Accuracy (100% target)" "cd $BASE_DIR/phase1/src && python3 ai/test_routing_accuracy.py"
run_test "Health Monitoring (agent checks)" "cd $BASE_DIR/phase1/src && python3 ai/test_health_monitoring.py"
run_test "Agent Lifecycle (start/stop/restart)" "cd $BASE_DIR/phase1/src && python3 ai/test_lifecycle.py"
run_test "Fault Tolerance Integration" "cd $BASE_DIR/phase1/src && python3 ai/test_fault_tolerance_integration.py"

echo ""
echo "========================================="
echo "Phase 5: Python Tests - Scaling & SHIELD (4 tests)"
echo "========================================="
echo ""

run_test "Dynamic Model Scaling (25 tests)" "cd $BASE_DIR/phase1/src && python3 ai/test_dynamic_scaling.py"
run_test "Inference Benchmark (Llama 3.2 1B vs Phi-3) [Updated Jan 2026]" "cd $BASE_DIR/phase1/src && python3 ai/test_inference_benchmark.py"
run_test "SHIELD Accuracy (100 scenarios)" "cd $BASE_DIR/phase1/src && python3 ai/test_shield_accuracy.py"
run_test "SHIELD Integration (with SystemStateManager)" "cd $BASE_DIR/phase1/src && python3 ai/test_shield_integration.py"

echo ""
echo "========================================="
echo "Phase 6: Phase 0 Validation (7 tests)"
echo "========================================="
echo ""

run_test "AI Inference Benchmark (GPU baseline)" "cd $BASE_DIR/phase0/experiments && python3 ai_inference_benchmark.py"
run_test "Phi-3 Benchmark (detailed)" "cd $BASE_DIR/phase0/experiments && python3 benchmark_phi3.py"
run_test "Multi-Agent Orchestration" "cd $BASE_DIR/phase0/experiments && python3 multi_agent_orchestration.py"
run_test "Conflict Resolution Tests" "cd $BASE_DIR/phase0/experiments && python3 conflict_resolution_tests.py"
run_test "SHIELD Safety Framework" "cd $BASE_DIR/phase0/experiments && python3 shield_safety_framework.py"
run_test "Adversarial Safety Tests" "cd $BASE_DIR/phase0/experiments && python3 adversarial_safety_tests.py"
run_test "Adversarial Safety (simplified)" "cd $BASE_DIR/phase0/experiments && python3 adversarial_safety_tests_simple.py"

# Optional tests (may fail without specific hardware)
run_test "Power Management Validation (ACPI)" "cd $BASE_DIR/phase0/experiments && python3 power_management_validation.py" "no"

END_TIME=$(date +%s)
ELAPSED=$((END_TIME - START_TIME))
MINUTES=$((ELAPSED / 60))
SECONDS=$((ELAPSED % 60))

echo ""
echo "========================================="
echo "TEST SUMMARY"
echo "========================================="
echo ""
echo -e "${GREEN}PASSED:${NC}  $PASS_COUNT"
echo -e "${RED}FAILED:${NC}  $FAIL_COUNT"
echo -e "${YELLOW}SKIPPED:${NC} $SKIP_COUNT"
echo ""

TOTAL=$((PASS_COUNT + FAIL_COUNT))
if [ $TOTAL -gt 0 ]; then
    PASS_RATE=$((PASS_COUNT * 100 / TOTAL))
    echo "Pass rate: $PASS_RATE% ($PASS_COUNT/$TOTAL required tests)"
else
    echo "No tests completed"
fi

echo "Total runtime: ${MINUTES}m ${SECONDS}s"
echo ""

if [ $FAIL_COUNT -eq 0 ]; then
    echo -e "${GREEN}=========================================${NC}"
    echo -e "${GREEN}ALL REQUIRED TESTS PASSED!${NC}"
    echo -e "${GREEN}=========================================${NC}"
    echo ""
    echo "Phase 0-1 validation complete. Ready for Week 17!"
    exit 0
else
    echo -e "${RED}=========================================${NC}"
    echo -e "${RED}SOME TESTS FAILED${NC}"
    echo -e "${RED}=========================================${NC}"
    echo ""
    echo "Review error logs above for details."
    echo "Common issues:"
    echo "  - GPU not detected: Check nvidia-smi in WSL"
    echo "  - Model not found: Verify paths in /mnt/c/Users/jluca/Documents/JARVIS_OS/models/"
    echo "  - Package missing: Run pip3 install <package>"
    exit 1
fi
