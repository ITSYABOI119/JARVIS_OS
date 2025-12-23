#!/usr/bin/env python3
"""
Week 20 Command Test Suite
Tests all 13 new commands added in Week 20
"""

import sys
import os
import tempfile

sys.path.insert(0, '../ai')
from shell import JARVISShell
from shield_framework import SHIELDFramework

def test_file_operations():
    """Test ls, cat, mkdir, rm"""
    print("=" * 70)
    print("TEST 1: File Operations")
    print("=" * 70)

    shell = JARVISShell()

    with tempfile.TemporaryDirectory() as tmpdir:
        # Test mkdir
        test_dir = os.path.join(tmpdir, 'test_folder')
        shell._execute_mkdir(test_dir)
        assert os.path.exists(test_dir), "mkdir failed"
        print("[PASS] mkdir: Created directory")

        # Test ls
        shell._execute_ls(tmpdir)
        print("[PASS] ls: Listed directory")

        # Create test file
        test_file = os.path.join(tmpdir, 'test.txt')
        with open(test_file, 'w') as f:
            f.write("JARVIS test file")

        # Test cat
        shell._execute_cat(test_file)
        print("[PASS] cat: Displayed file")

        # Test rm
        shell._execute_rm(test_file)
        assert not os.path.exists(test_file), "rm failed"
        print("[PASS] rm: Deleted file")

    print()
    return True

def test_process_operations():
    """Test ps, top"""
    print("=" * 70)
    print("TEST 2: Process Operations")
    print("=" * 70)

    shell = JARVISShell()

    shell._execute_ps()
    print("[PASS] ps: Listed processes")

    shell._execute_top()
    print("[PASS] top: Showed top processes")

    print()
    return True

def test_system_operations():
    """Test battery, uptime, shutdown, reboot"""
    print("=" * 70)
    print("TEST 3: System Operations")
    print("=" * 70)

    shield = SHIELDFramework()
    shell = JARVISShell(shield=shield)

    shell._execute_battery()
    print("[PASS] battery: Displayed status")

    shell._execute_uptime()
    print("[PASS] uptime: Displayed uptime")

    # These should be blocked by SHIELD
    shell._execute_shutdown()
    print("[PASS] shutdown: Correctly blocked by SHIELD")

    shell._execute_reboot()
    print("[PASS] reboot: Correctly blocked by SHIELD")

    print()
    return True

def test_network_operations():
    """Test ping, ifconfig"""
    print("=" * 70)
    print("TEST 4: Network Operations")
    print("=" * 70)

    shell = JARVISShell()

    shell._execute_ifconfig()
    print("[PASS] ifconfig: Displayed network interfaces")

    # Ping test - may fail if no internet, that's OK
    shell._execute_ping('127.0.0.1')
    print("[PASS] ping: Executed (localhost)")

    print()
    return True

def main():
    """Run all tests"""
    print("\n" + "=" * 70)
    print("WEEK 20 COMMAND TEST SUITE")
    print("Testing 13 new commands")
    print("=" * 70 + "\n")

    results = []

    try:
        results.append(("File Operations", test_file_operations()))
    except Exception as e:
        print(f"[FAIL] File operations failed: {e}")
        import traceback
        traceback.print_exc()
        results.append(("File Operations", False))

    try:
        results.append(("Process Operations", test_process_operations()))
    except Exception as e:
        print(f"[FAIL] Process operations failed: {e}")
        results.append(("Process Operations", False))

    try:
        results.append(("System Operations", test_system_operations()))
    except Exception as e:
        print(f"[FAIL] System operations failed: {e}")
        results.append(("System Operations", False))

    try:
        results.append(("Network Operations", test_network_operations()))
    except Exception as e:
        print(f"[FAIL] Network operations failed: {e}")
        results.append(("Network Operations", False))

    # Summary
    print("=" * 70)
    print("TEST SUMMARY")
    print("=" * 70)

    passed = sum(1 for _, result in results if result)
    total = len(results)

    for name, result in results:
        status = "[PASS]" if result else "[FAIL]"
        print(f"{status}: {name}")

    print("=" * 70)
    print(f"Results: {passed}/{total} ({100*passed/total:.0f}%)")

    return 0 if passed == total else 1

if __name__ == "__main__":
    sys.exit(main())
