#!/bin/bash
#
# Week 24: Comprehensive Test Suite Runner
# Runs all driver framework and cache tests
#
# Usage: ./run_week24_tests.sh
#

set -e

echo "========================================================================"
echo "Week 24: Comprehensive Test Suite"
echo "========================================================================"
echo ""

PASS_COUNT=0
FAIL_COUNT=0
TOTAL_TESTS=0

# Test 1: Decision Cache (with new storage patterns)
echo "=== Test 1: Decision Cache (258 patterns) ==="cd cache
if gcc -O2 decision_cache.c cache_patterns.c test_cache.c -o test_cache -lm 2>&1; then
    if ./test_cache 2>&1 | tee cache_test_output.txt; then
        echo "✓ Decision Cache Test PASSED"
        ((PASS_COUNT++))
    else
        echo "✗ Decision Cache Test FAILED"
        ((FAIL_COUNT++))    fi
    ((TOTAL_TESTS++))else
    echo "✗ Decision Cache compilation FAILED"
    ((FAIL_COUNT++))
    ((TOTAL_TESTS++))
fi
echo ""# Test 2: Driver Registry
echo "=== Test 2: Driver Registry (6 tests) ==="
cd ../drivers
if gcc -O2 -pthread driver_registry.c test_driver_registry.c -o test_driver_registry -lm 2>&1; then
    if ./test_driver_registry 2>&1 | tee registry_test_output.txt; then
        echo "✓ Driver Registry Test PASSED"
        ((PASS_COUNT++))    else
        echo "✗ Driver Registry Test FAILED"        ((FAIL_COUNT++))
    fi    ((TOTAL_TESTS++))
else    echo "✗ Driver Registry compilation FAILED"
    ((FAIL_COUNT++))
    ((TOTAL_TESTS++))fi
echo ""

# Test 3: VirtIO Block Driver
echo "=== Test 3: VirtIO Block Driver (5 tests) ==="
if gcc -O2 -pthread driver_registry.c virtio_core.c virtio_blk.c test_virtio_blk.c -o test_virtio_blk -lm 2>&1; then
    if ./test_virtio_blk 2>&1 | tee virtio_test_output.txt; then
        echo "✓ VirtIO Block Driver Test PASSED"
        ((PASS_COUNT++))
    else        echo "✗ VirtIO Block Driver Test FAILED"
        ((FAIL_COUNT++))    fi
    ((TOTAL_TESTS++))else
    echo "✗ VirtIO Block Driver compilation FAILED"
    ((FAIL_COUNT++))
    ((TOTAL_TESTS++))fi
echo ""

# Test 4: Driver Proxy (Python)
echo "=== Test 4: Driver Proxy (5 tests, mock mode) ==="
cd ../ai
if python3 driver_proxy.py 2>&1 | tee driver_proxy_output.txt; then    echo "✓ Driver Proxy Test PASSED"
    ((PASS_COUNT++))else
    echo "✗ Driver Proxy Test FAILED"    ((FAIL_COUNT++))
fi((TOTAL_TESTS++))
echo ""

# Summary
echo "========================================================================"
echo "Week 24 Test Summary"
echo "========================================================================"
echo "Total Test Suites: $TOTAL_TESTS"echo "Passed: $PASS_COUNT"
echo "Failed: $FAIL_COUNT"echo ""
if [ $FAIL_COUNT -eq 0 ]; then
    echo "✅ ALL TESTS PASSED"
    exit 0
else    echo "❌ SOME TESTS FAILED"
    exit 1fi
