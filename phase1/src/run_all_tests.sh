#!/bin/bash
################################################################################
# JARVIS AI-OS - Full System Test Suite
#
# Tests all components from Weeks 1-11:
#   - Decision Cache (C)
#   - IPC Ring Buffer (C)
#   - AI Agent + Query Pipeline (Python)
#   - Interactive Shell (Python)
#   - Multi-Agent System (Python)
#
# Usage:
#   cd /mnt/c/Users/jluca/Documents/JARVIS_OS/phase1/src
#   bash run_all_tests.sh
#
# Output:
#   - Console output (real-time)
#   - test_results.txt (complete log)
################################################################################

# Get script directory
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$SCRIPT_DIR"

# Output file
OUTPUT_FILE="$SCRIPT_DIR/test_results.txt"

# Colors for terminal output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function to print and log
log() {
    echo -e "$1" | tee -a "$OUTPUT_FILE"
}

# Function to run test and capture result
run_test() {
    local test_name="$1"
    local test_cmd="$2"

    log ""
    log "${BLUE}======================================================================${NC}"
    log "${BLUE}TEST: $test_name${NC}"
    log "${BLUE}======================================================================${NC}"
    log ""

    # Run test and capture output
    if eval "$test_cmd" 2>&1 | tee -a "$OUTPUT_FILE"; then
        log ""
        log "${GREEN}[PASS] $test_name${NC}"
        return 0
    else
        log ""
        log "${RED}[FAIL] $test_name${NC}"
        return 1
    fi
}

# Initialize output file
log "========================================================================"
log "JARVIS AI-OS - Full System Test Suite"
log "========================================================================"
log ""
log "Date: $(date)"
log "Directory: $SCRIPT_DIR"
log ""
log "Testing all components from Weeks 1-11..."
log ""

# Test counters
total_tests=0
passed_tests=0
failed_tests=0

################################################################################
# C Component Tests
################################################################################

log "========================================================================"
log "SECTION 1: C Component Tests"
log "========================================================================"

# Test 1: Decision Cache (Week 3)
total_tests=$((total_tests + 1))
if run_test "Decision Cache (Week 3)" \
    "cd cache && gcc -O2 decision_cache.c cache_patterns.c test_cache.c -o test_cache -lm && ./test_cache && cd .."; then
    passed_tests=$((passed_tests + 1))
else
    failed_tests=$((failed_tests + 1))
fi

# Test 2: IPC Ring Buffer (Week 4)
total_tests=$((total_tests + 1))
if run_test "IPC Ring Buffer (Week 4)" \
    "cd ipc && gcc -O2 ring_buffer.c test_ipc.c -o test_ipc -lm && ./test_ipc && cd .."; then
    passed_tests=$((passed_tests + 1))
else
    failed_tests=$((failed_tests + 1))
fi

################################################################################
# Python Component Tests
################################################################################

log ""
log "========================================================================"
log "SECTION 2: Python Component Tests"
log "========================================================================"

# Test 3: Python IPC Integration (Week 8)
total_tests=$((total_tests + 1))
if run_test "Python IPC Integration (Week 8)" \
    "cd ai && python3 test_ipc_integration.py && cd .."; then
    passed_tests=$((passed_tests + 1))
else
    failed_tests=$((failed_tests + 1))
fi

# Test 4: Interactive Shell (Week 7)
total_tests=$((total_tests + 1))
if run_test "Interactive Shell (Week 7)" \
    "cd shell && python3 test_shell.py && cd .."; then
    passed_tests=$((passed_tests + 1))
else
    failed_tests=$((failed_tests + 1))
fi

# Test 5: Multi-Agent System (Week 11)
total_tests=$((total_tests + 1))
if run_test "Multi-Agent System (Week 11)" \
    "cd ai && python3 test_multi_agent.py && cd .."; then
    passed_tests=$((passed_tests + 1))
else
    failed_tests=$((failed_tests + 1))
fi

################################################################################
# Summary
################################################################################

log ""
log "========================================================================"
log "TEST SUMMARY"
log "========================================================================"
log ""
log "Total Tests:  $total_tests"
log "${GREEN}Passed:       $passed_tests${NC}"

if [ $failed_tests -gt 0 ]; then
    log "${RED}Failed:       $failed_tests${NC}"
else
    log "Failed:       $failed_tests"
fi

log ""

if [ $failed_tests -eq 0 ]; then
    log "${GREEN}========================================================================"
    log "[SUCCESS] All tests passed! (100%)"
    log "========================================================================${NC}"
    exit_code=0
else
    pass_rate=$((passed_tests * 100 / total_tests))
    log "${YELLOW}========================================================================"
    log "[PARTIAL] $passed_tests/$total_tests tests passed ($pass_rate%)"
    log "========================================================================${NC}"
    exit_code=1
fi

log ""
log "Complete results saved to: $OUTPUT_FILE"
log ""

exit $exit_code
