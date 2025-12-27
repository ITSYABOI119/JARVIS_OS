#!/usr/bin/env python3
"""
JARVIS AI-OS - Shared Context Test Suite
Tests SharedContext class for thread safety and correctness.

Coverage:
- Initialization and system state
- Operation tracking (add/remove)
- Cache statistics
- IPC statistics
- Agent status
- Thread safety

Run: python test_shared_context.py
"""

import sys
import time
import threading
from pathlib import Path
from unittest.mock import patch, MagicMock

# Add parent directory to path
sys.path.insert(0, str(Path(__file__).parent))

from shared_context import SharedContext

# Test framework
tests_passed = 0
tests_failed = 0


def test_pass(name):
    global tests_passed
    print(f"  [PASS] {name}")
    tests_passed += 1


def test_fail(name, msg):
    global tests_failed
    print(f"  [FAIL] {name}: {msg}")
    tests_failed += 1


# ============================================================================
# INITIALIZATION TESTS
# ============================================================================

def test_init():
    """Test SharedContext initialization."""
    print("\n" + "=" * 70)
    print("TEST: SharedContext initialization")
    print("=" * 70)

    context = SharedContext()

    # Check initial values
    if context.cache_lookups == 0:
        test_pass("cache_lookups starts at 0")
    else:
        test_fail("cache_lookups starts at 0", f"Got {context.cache_lookups}")

    if context.ipc_sent == 0:
        test_pass("ipc_sent starts at 0")
    else:
        test_fail("ipc_sent starts at 0", f"Got {context.ipc_sent}")

    if len(context.active_operations) == 0:
        test_pass("active_operations starts empty")
    else:
        test_fail("active_operations starts empty", f"Got {len(context.active_operations)}")

    if len(context.agent_status) == 0:
        test_pass("agent_status starts empty")
    else:
        test_fail("agent_status starts empty", f"Got {len(context.agent_status)}")

    if context.cache_capacity == 256:
        test_pass("cache_capacity defaults to 256")
    else:
        test_fail("cache_capacity defaults to 256", f"Got {context.cache_capacity}")


def test_system_state_update():
    """Test system state update from psutil."""
    print("\n" + "=" * 70)
    print("TEST: System state update")
    print("=" * 70)

    context = SharedContext()

    # Force update
    context.update_system_state(force=True)

    # Memory should be populated
    if context.memory_total > 0:
        test_pass(f"memory_total populated ({context.memory_total} MB)")
    else:
        test_fail("memory_total populated", f"Got {context.memory_total}")

    if context.memory_used > 0:
        test_pass(f"memory_used populated ({context.memory_used} MB)")
    else:
        test_fail("memory_used populated", f"Got {context.memory_used}")

    # CPU should be between 0-100
    if 0 <= context.cpu_percent <= 100:
        test_pass(f"cpu_percent in valid range ({context.cpu_percent:.1f}%)")
    else:
        test_fail("cpu_percent in valid range", f"Got {context.cpu_percent}")

    # Disk should be populated
    if context.disk_total > 0:
        test_pass(f"disk_total populated ({context.disk_total} GB)")
    else:
        test_fail("disk_total populated", f"Got {context.disk_total}")


def test_update_interval():
    """Test that updates respect the interval."""
    print("\n" + "=" * 70)
    print("TEST: Update interval throttling")
    print("=" * 70)

    context = SharedContext()
    context._update_interval = 10.0  # Set long interval

    # Force first update
    context.update_system_state(force=True)
    first_update = context._last_update

    # Try to update again without force
    time.sleep(0.1)
    context.update_system_state()  # Should be skipped
    second_update = context._last_update

    if first_update == second_update:
        test_pass("Update skipped when within interval")
    else:
        test_fail("Update skipped when within interval", "Update not skipped")

    # Force update should work
    context.update_system_state(force=True)
    forced_update = context._last_update

    if forced_update > second_update:
        test_pass("Force update ignores interval")
    else:
        test_fail("Force update ignores interval", "Forced update not applied")


# ============================================================================
# OPERATION TRACKING TESTS
# ============================================================================

def test_add_operation():
    """Test adding operations."""
    print("\n" + "=" * 70)
    print("TEST: Add operations")
    print("=" * 70)

    context = SharedContext()

    context.add_operation("pinging host")
    if "pinging host" in context.active_operations:
        test_pass("Operation added successfully")
    else:
        test_fail("Operation added successfully", "Not found")

    if len(context.active_operations) == 1:
        test_pass("Only one operation after add")
    else:
        test_fail("Only one operation after add", f"Got {len(context.active_operations)}")


def test_add_duplicate_operation():
    """Test that duplicate operations are ignored."""
    print("\n" + "=" * 70)
    print("TEST: Duplicate operation handling")
    print("=" * 70)

    context = SharedContext()

    context.add_operation("pinging host")
    context.add_operation("pinging host")  # Duplicate

    if len(context.active_operations) == 1:
        test_pass("Duplicate operation not added")
    else:
        test_fail("Duplicate operation not added", f"Got {len(context.active_operations)}")


def test_remove_operation():
    """Test removing operations."""
    print("\n" + "=" * 70)
    print("TEST: Remove operations")
    print("=" * 70)

    context = SharedContext()

    context.add_operation("task1")
    context.add_operation("task2")

    context.remove_operation("task1")

    if "task1" not in context.active_operations:
        test_pass("Operation removed successfully")
    else:
        test_fail("Operation removed successfully", "Still present")

    if "task2" in context.active_operations:
        test_pass("Other operation still present")
    else:
        test_fail("Other operation still present", "Missing")


def test_remove_nonexistent_operation():
    """Test removing operation that doesn't exist."""
    print("\n" + "=" * 70)
    print("TEST: Remove nonexistent operation")
    print("=" * 70)

    context = SharedContext()

    # Should not raise exception
    try:
        context.remove_operation("nonexistent")
        test_pass("Removing nonexistent operation doesn't crash")
    except Exception as e:
        test_fail("Removing nonexistent operation doesn't crash", str(e))


# ============================================================================
# CACHE STATISTICS TESTS
# ============================================================================

def test_update_cache_stats():
    """Test cache statistics update."""
    print("\n" + "=" * 70)
    print("TEST: Cache statistics update")
    print("=" * 70)

    context = SharedContext()

    context.update_cache_stats(lookups=100, hits=85, misses=15, entries=50)

    if context.cache_lookups == 100:
        test_pass("cache_lookups updated")
    else:
        test_fail("cache_lookups updated", f"Got {context.cache_lookups}")

    if context.cache_hits == 85:
        test_pass("cache_hits updated")
    else:
        test_fail("cache_hits updated", f"Got {context.cache_hits}")

    if context.cache_misses == 15:
        test_pass("cache_misses updated")
    else:
        test_fail("cache_misses updated", f"Got {context.cache_misses}")

    if context.cache_entries == 50:
        test_pass("cache_entries updated")
    else:
        test_fail("cache_entries updated", f"Got {context.cache_entries}")


def test_cache_hit_rate_calculation():
    """Test cache hit rate calculation."""
    print("\n" + "=" * 70)
    print("TEST: Cache hit rate calculation")
    print("=" * 70)

    context = SharedContext()

    # 85% hit rate
    context.update_cache_stats(lookups=100, hits=85, misses=15, entries=50)
    if abs(context.cache_hit_rate - 0.85) < 0.001:
        test_pass("Hit rate calculated correctly (0.85)")
    else:
        test_fail("Hit rate calculated correctly", f"Got {context.cache_hit_rate}")

    # 0 lookups should give 0% hit rate
    context.update_cache_stats(lookups=0, hits=0, misses=0, entries=0)
    if context.cache_hit_rate == 0.0:
        test_pass("Zero lookups gives 0% hit rate")
    else:
        test_fail("Zero lookups gives 0% hit rate", f"Got {context.cache_hit_rate}")


# ============================================================================
# IPC STATISTICS TESTS
# ============================================================================

def test_update_ipc_stats():
    """Test IPC statistics update."""
    print("\n" + "=" * 70)
    print("TEST: IPC statistics update")
    print("=" * 70)

    context = SharedContext()

    context.update_ipc_stats(sent=500, received=495, drops=5)

    if context.ipc_sent == 500:
        test_pass("ipc_sent updated")
    else:
        test_fail("ipc_sent updated", f"Got {context.ipc_sent}")

    if context.ipc_received == 495:
        test_pass("ipc_received updated")
    else:
        test_fail("ipc_received updated", f"Got {context.ipc_received}")

    if context.ipc_drops == 5:
        test_pass("ipc_drops updated")
    else:
        test_fail("ipc_drops updated", f"Got {context.ipc_drops}")


# ============================================================================
# AGENT STATUS TESTS
# ============================================================================

def test_update_agent_status():
    """Test agent status update."""
    print("\n" + "=" * 70)
    print("TEST: Agent status update")
    print("=" * 70)

    context = SharedContext()

    context.update_agent_status("device", {"status": "ready", "queries": 10})

    if "device" in context.agent_status:
        test_pass("Agent status added")
    else:
        test_fail("Agent status added", "Missing")

    if context.agent_status["device"]["status"] == "ready":
        test_pass("Agent status values correct")
    else:
        test_fail("Agent status values correct", f"Got {context.agent_status['device']}")


def test_multiple_agent_status():
    """Test multiple agent statuses."""
    print("\n" + "=" * 70)
    print("TEST: Multiple agent statuses")
    print("=" * 70)

    context = SharedContext()

    context.update_agent_status("device", {"status": "ready"})
    context.update_agent_status("network", {"status": "busy"})
    context.update_agent_status("filesystem", {"status": "ready"})
    context.update_agent_status("user", {"status": "ready"})

    if len(context.agent_status) == 4:
        test_pass("4 agents tracked")
    else:
        test_fail("4 agents tracked", f"Got {len(context.agent_status)}")


# ============================================================================
# TO_DICT TESTS
# ============================================================================

def test_to_dict():
    """Test conversion to dictionary."""
    print("\n" + "=" * 70)
    print("TEST: Conversion to dictionary")
    print("=" * 70)

    context = SharedContext()

    context.update_cache_stats(lookups=100, hits=85, misses=15, entries=50)
    context.add_operation("test operation")
    context.update_agent_status("device", {"status": "ready"})

    result = context.to_dict()

    # Check all expected keys
    expected_keys = [
        "memory_used", "memory_total", "cpu_percent", "disk_used", "disk_total",
        "active_operations", "cache_hit_rate", "cache_lookups", "cache_hits",
        "cache_misses", "cache_entries", "cache_capacity", "ipc_sent",
        "ipc_received", "ipc_drops", "agent_status", "timestamp"
    ]

    missing_keys = [k for k in expected_keys if k not in result]
    if not missing_keys:
        test_pass("All expected keys present")
    else:
        test_fail("All expected keys present", f"Missing: {missing_keys}")

    # Check that operations are copied (not reference)
    if result["active_operations"] is not context.active_operations:
        test_pass("active_operations is a copy")
    else:
        test_fail("active_operations is a copy", "Same reference")

    # Check that agent_status is copied
    if result["agent_status"] is not context.agent_status:
        test_pass("agent_status is a copy")
    else:
        test_fail("agent_status is a copy", "Same reference")

    # Check timestamp is recent
    if time.time() - result["timestamp"] < 1.0:
        test_pass("timestamp is recent")
    else:
        test_fail("timestamp is recent", f"Got {result['timestamp']}")


def test_to_dict_values():
    """Test that to_dict values are correct."""
    print("\n" + "=" * 70)
    print("TEST: to_dict values correctness")
    print("=" * 70)

    context = SharedContext()

    context.update_cache_stats(lookups=100, hits=85, misses=15, entries=50)
    context.update_ipc_stats(sent=200, received=195, drops=5)

    result = context.to_dict()

    if result["cache_lookups"] == 100:
        test_pass("cache_lookups value correct")
    else:
        test_fail("cache_lookups value correct", f"Got {result['cache_lookups']}")

    if result["ipc_sent"] == 200:
        test_pass("ipc_sent value correct")
    else:
        test_fail("ipc_sent value correct", f"Got {result['ipc_sent']}")


# ============================================================================
# GET_SUMMARY TESTS
# ============================================================================

def test_get_summary():
    """Test human-readable summary."""
    print("\n" + "=" * 70)
    print("TEST: Get summary")
    print("=" * 70)

    context = SharedContext()

    context.update_cache_stats(lookups=100, hits=85, misses=15, entries=50)
    context.add_operation("test op")
    context.update_agent_status("device", {"status": "ready"})

    summary = context.get_summary()

    if "Memory:" in summary:
        test_pass("Summary contains Memory info")
    else:
        test_fail("Summary contains Memory info", "Missing")

    if "CPU:" in summary:
        test_pass("Summary contains CPU info")
    else:
        test_fail("Summary contains CPU info", "Missing")

    if "Cache:" in summary:
        test_pass("Summary contains Cache info")
    else:
        test_fail("Summary contains Cache info", "Missing")

    if "IPC:" in summary:
        test_pass("Summary contains IPC info")
    else:
        test_fail("Summary contains IPC info", "Missing")


# ============================================================================
# THREAD SAFETY TESTS
# ============================================================================

def test_concurrent_operations():
    """Test thread safety of operation add/remove."""
    print("\n" + "=" * 70)
    print("TEST: Concurrent operation add/remove")
    print("=" * 70)

    context = SharedContext()
    errors = []

    def add_operations(prefix, count):
        try:
            for i in range(count):
                context.add_operation(f"{prefix}_{i}")
        except Exception as e:
            errors.append(str(e))

    def remove_operations(prefix, count):
        try:
            for i in range(count):
                context.remove_operation(f"{prefix}_{i}")
        except Exception as e:
            errors.append(str(e))

    # Create multiple threads
    threads = []
    for i in range(5):
        t1 = threading.Thread(target=add_operations, args=(f"add_{i}", 20))
        t2 = threading.Thread(target=remove_operations, args=(f"remove_{i}", 20))
        threads.extend([t1, t2])

    # Start all threads
    for t in threads:
        t.start()

    # Wait for completion
    for t in threads:
        t.join()

    if not errors:
        test_pass("No errors during concurrent operations")
    else:
        test_fail("No errors during concurrent operations", f"Errors: {errors}")


def test_concurrent_stats_update():
    """Test thread safety of statistics updates."""
    print("\n" + "=" * 70)
    print("TEST: Concurrent statistics updates")
    print("=" * 70)

    context = SharedContext()
    errors = []

    def update_cache(iterations):
        try:
            for i in range(iterations):
                context.update_cache_stats(
                    lookups=i * 100,
                    hits=i * 85,
                    misses=i * 15,
                    entries=i * 10
                )
        except Exception as e:
            errors.append(str(e))

    def update_ipc(iterations):
        try:
            for i in range(iterations):
                context.update_ipc_stats(
                    sent=i * 50,
                    received=i * 48,
                    drops=i * 2
                )
        except Exception as e:
            errors.append(str(e))

    def update_agent(name, iterations):
        try:
            for i in range(iterations):
                context.update_agent_status(name, {"queries": i, "status": "active"})
        except Exception as e:
            errors.append(str(e))

    # Create threads
    threads = [
        threading.Thread(target=update_cache, args=(100,)),
        threading.Thread(target=update_ipc, args=(100,)),
        threading.Thread(target=update_agent, args=("device", 100)),
        threading.Thread(target=update_agent, args=("network", 100)),
    ]

    for t in threads:
        t.start()

    for t in threads:
        t.join()

    if not errors:
        test_pass("No errors during concurrent stats updates")
    else:
        test_fail("No errors during concurrent stats updates", f"Errors: {errors}")


def test_concurrent_to_dict():
    """Test thread safety of to_dict while updating."""
    print("\n" + "=" * 70)
    print("TEST: Concurrent to_dict while updating")
    print("=" * 70)

    context = SharedContext()
    errors = []
    results = []

    def update_continuously(iterations):
        try:
            for i in range(iterations):
                context.update_cache_stats(i, i, 0, i)
                context.add_operation(f"op_{i}")
                context.remove_operation(f"op_{i}")
        except Exception as e:
            errors.append(str(e))

    def read_continuously(iterations):
        try:
            for i in range(iterations):
                d = context.to_dict()
                results.append(len(d))
        except Exception as e:
            errors.append(str(e))

    threads = [
        threading.Thread(target=update_continuously, args=(100,)),
        threading.Thread(target=read_continuously, args=(100,)),
        threading.Thread(target=read_continuously, args=(100,)),
    ]

    for t in threads:
        t.start()

    for t in threads:
        t.join()

    if not errors:
        test_pass("No errors during concurrent read/write")
    else:
        test_fail("No errors during concurrent read/write", f"Errors: {errors}")

    if len(results) == 200:
        test_pass("All reads completed successfully")
    else:
        test_fail("All reads completed successfully", f"Got {len(results)} reads")


# ============================================================================
# EDGE CASE TESTS
# ============================================================================

def test_psutil_failure():
    """Test graceful handling of psutil failure."""
    print("\n" + "=" * 70)
    print("TEST: psutil failure handling")
    print("=" * 70)

    context = SharedContext()

    # Mock psutil to raise exception
    with patch('shared_context.psutil.virtual_memory', side_effect=Exception("Mock failure")):
        try:
            context.update_system_state(force=True)
            test_pass("psutil failure handled gracefully")
        except Exception as e:
            test_fail("psutil failure handled gracefully", str(e))


def test_division_by_zero_protection():
    """Test protection against division by zero in summary."""
    print("\n" + "=" * 70)
    print("TEST: Division by zero protection")
    print("=" * 70)

    context = SharedContext()
    context.memory_total = 0
    context.disk_total = 0

    try:
        summary = context.get_summary()
        if "0%" in summary or "0" in summary:
            test_pass("Division by zero handled in summary")
        else:
            test_pass("Summary generated without crash")
    except ZeroDivisionError:
        test_fail("Division by zero handled", "ZeroDivisionError raised")


# ============================================================================
# TEST RUNNER
# ============================================================================

def run_all_tests():
    """Run all tests and report results."""
    print()
    print("=" * 70)
    print("  JARVIS AI-OS - Shared Context Test Suite")
    print("=" * 70)

    tests = [
        # Initialization
        test_init,
        test_system_state_update,
        test_update_interval,

        # Operations
        test_add_operation,
        test_add_duplicate_operation,
        test_remove_operation,
        test_remove_nonexistent_operation,

        # Cache stats
        test_update_cache_stats,
        test_cache_hit_rate_calculation,

        # IPC stats
        test_update_ipc_stats,

        # Agent status
        test_update_agent_status,
        test_multiple_agent_status,

        # to_dict
        test_to_dict,
        test_to_dict_values,

        # Summary
        test_get_summary,

        # Thread safety
        test_concurrent_operations,
        test_concurrent_stats_update,
        test_concurrent_to_dict,

        # Edge cases
        test_psutil_failure,
        test_division_by_zero_protection,
    ]

    for test_func in tests:
        try:
            test_func()
        except Exception as e:
            print(f"\n  [ERROR] {test_func.__name__}: {e}")
            import traceback
            traceback.print_exc()
            global tests_failed
            tests_failed += 1

    # Summary
    print()
    print("=" * 70)
    print("  SHARED CONTEXT TEST SUMMARY")
    print("=" * 70)
    print(f"  Total tests: {tests_passed + tests_failed}")
    print(f"  Passed: {tests_passed}")
    print(f"  Failed: {tests_failed}")
    if tests_passed + tests_failed > 0:
        print(f"  Pass rate: {100 * tests_passed / (tests_passed + tests_failed):.1f}%")
    print("=" * 70)
    print()

    return tests_failed == 0


if __name__ == '__main__':
    success = run_all_tests()
    sys.exit(0 if success else 1)
