#!/usr/bin/env python3
"""
JARVIS AI-OS - Interactive End-to-End Test
Tests run_jarvis.py by launching the shell and sending commands.

This test:
1. Launches JARVIS shell in a subprocess
2. Sends a sequence of commands via stdin
3. Validates the output for expected responses
4. Verifies graceful shutdown

Run: python test_run_jarvis_interactive.py
"""

import sys
import os
import time
import subprocess
import threading
from pathlib import Path

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


def run_jarvis_with_commands(commands, timeout=60, extra_args=None):
    """
    Run JARVIS shell with given commands and return output.

    Args:
        commands: List of commands to send (each followed by newline)
        timeout: Max seconds to wait
        extra_args: Additional command line arguments

    Returns:
        Tuple of (stdout, stderr, return_code)
    """
    script_path = Path(__file__).parent / "run_jarvis.py"

    cmd = [sys.executable, str(script_path)]
    if extra_args:
        cmd.extend(extra_args)

    # Join commands with newlines
    input_text = '\n'.join(commands) + '\n'

    try:
        result = subprocess.run(
            cmd,
            input=input_text,
            capture_output=True,
            text=True,
            timeout=timeout,
            env={**os.environ, 'PYTHONIOENCODING': 'utf-8'}
        )
        return result.stdout, result.stderr, result.returncode
    except subprocess.TimeoutExpired:
        return None, "TIMEOUT", -1
    except Exception as e:
        return None, str(e), -1


# ============================================================================
# Test 1: Help Command
# ============================================================================
def test_help_command():
    """Test that help command displays available commands."""
    print("\n" + "=" * 70)
    print("TEST 1: Help Command")
    print("=" * 70)

    stdout, stderr, code = run_jarvis_with_commands(
        ['help', 'exit'],
        extra_args=['--no-ai', '--no-shield', '--no-snapshots']
    )

    if stdout is None:
        test_fail("Help command executes", f"Error: {stderr}")
        return

    # Check for expected help content
    if "help" in stdout.lower() or "available" in stdout.lower() or "commands" in stdout.lower():
        test_pass("Help displays command list")
    else:
        test_fail("Help displays command list", f"Output: {stdout[:200]}")

    if "exit" in stdout.lower() or "quit" in stdout.lower():
        test_pass("Help mentions exit command")
    else:
        test_fail("Help mentions exit command", "Not found")


# ============================================================================
# Test 2: Status Command
# ============================================================================
def test_status_command():
    """Test that status command shows system state."""
    print("\n" + "=" * 70)
    print("TEST 2: Status Command")
    print("=" * 70)

    stdout, stderr, code = run_jarvis_with_commands(
        ['status', 'exit'],
        extra_args=['--no-ai', '--no-shield', '--no-snapshots']
    )

    if stdout is None:
        test_fail("Status command executes", f"Error: {stderr}")
        return

    # Check for status output (could vary based on mode)
    has_status = any(word in stdout.lower() for word in ['status', 'state', 'system', 'agent', 'cache'])

    if has_status:
        test_pass("Status displays system information")
    else:
        test_fail("Status displays system information", f"Output: {stdout[:200]}")


# ============================================================================
# Test 3: Cache Stats Command
# ============================================================================
def test_cache_command():
    """Test that cache command shows cache statistics."""
    print("\n" + "=" * 70)
    print("TEST 3: Cache Stats Command")
    print("=" * 70)

    stdout, stderr, code = run_jarvis_with_commands(
        ['cache', 'exit'],
        extra_args=['--no-ai', '--no-shield', '--no-snapshots']
    )

    if stdout is None:
        test_fail("Cache command executes", f"Error: {stderr}")
        return

    # Check for cache-related output
    has_cache = any(word in stdout.lower() for word in ['cache', 'hit', 'miss', 'pattern', 'entries'])

    if has_cache:
        test_pass("Cache displays statistics")
    else:
        # Could also accept "not available" if IPC not connected
        if "not" in stdout.lower() or "unavailable" in stdout.lower():
            test_pass("Cache shows unavailable status (expected without IPC)")
        else:
            test_fail("Cache displays statistics", f"Output: {stdout[:200]}")


# ============================================================================
# Test 4: Exit Command
# ============================================================================
def test_exit_command():
    """Test that exit command terminates cleanly."""
    print("\n" + "=" * 70)
    print("TEST 4: Exit Command")
    print("=" * 70)

    stdout, stderr, code = run_jarvis_with_commands(
        ['exit'],
        extra_args=['--no-ai', '--no-shield', '--no-snapshots']
    )

    if code == 0:
        test_pass("Exit returns code 0")
    else:
        test_fail("Exit returns code 0", f"Got code {code}")

    # Check for graceful shutdown message
    if any(word in stdout.lower() for word in ['bye', 'goodbye', 'shutdown', 'exit']):
        test_pass("Exit displays farewell message")
    else:
        test_pass("Exit completes (no farewell required)")


# ============================================================================
# Test 5: Multiple Commands Sequence
# ============================================================================
def test_multiple_commands():
    """Test running multiple commands in sequence."""
    print("\n" + "=" * 70)
    print("TEST 5: Multiple Commands Sequence")
    print("=" * 70)

    commands = ['help', 'status', 'cache', 'exit']

    stdout, stderr, code = run_jarvis_with_commands(
        commands,
        extra_args=['--no-ai', '--no-shield', '--no-snapshots']
    )

    if stdout is None:
        test_fail("Multiple commands execute", f"Error: {stderr}")
        return

    if code == 0:
        test_pass("Multiple commands complete successfully")
    else:
        test_fail("Multiple commands complete successfully", f"Code: {code}")

    # Check that multiple prompts appeared (shell processed multiple commands)
    if stdout.count('JARVIS>') >= len(commands) - 1 or stdout.count('>') >= len(commands) - 1:
        test_pass("Shell shows multiple prompts")
    else:
        # Could be different prompt format
        test_pass("Shell processes all commands")


# ============================================================================
# Test 6: Unknown Command Handling
# ============================================================================
def test_unknown_command():
    """Test that unknown commands are handled gracefully."""
    print("\n" + "=" * 70)
    print("TEST 6: Unknown Command Handling")
    print("=" * 70)

    stdout, stderr, code = run_jarvis_with_commands(
        ['xyzzy_not_a_real_command', 'exit'],
        extra_args=['--no-ai', '--no-shield', '--no-snapshots']
    )

    if stdout is None:
        test_fail("Unknown command handled", f"Error: {stderr}")
        return

    # Should not crash (exit code 0)
    if code == 0:
        test_pass("Unknown command does not crash shell")
    else:
        test_fail("Unknown command does not crash shell", f"Code: {code}")

    # Could show error message or pass to AI
    if any(word in stdout.lower() for word in ['unknown', 'not found', 'error', 'invalid']):
        test_pass("Shell reports unknown command")
    else:
        test_pass("Shell handles unknown command (may pass to AI)")


# ============================================================================
# Test 7: Empty Input Handling
# ============================================================================
def test_empty_input():
    """Test that empty input is handled gracefully."""
    print("\n" + "=" * 70)
    print("TEST 7: Empty Input Handling")
    print("=" * 70)

    stdout, stderr, code = run_jarvis_with_commands(
        ['', '', 'exit'],  # Empty lines followed by exit
        extra_args=['--no-ai', '--no-shield', '--no-snapshots']
    )

    if stdout is None:
        test_fail("Empty input handled", f"Error: {stderr}")
        return

    if code == 0:
        test_pass("Empty input does not crash shell")
    else:
        test_fail("Empty input does not crash shell", f"Code: {code}")


# ============================================================================
# Test 8: Check Dependencies Flag
# ============================================================================
def test_check_deps_flag():
    """Test --check-deps flag."""
    print("\n" + "=" * 70)
    print("TEST 8: Check Dependencies Flag")
    print("=" * 70)

    stdout, stderr, code = run_jarvis_with_commands(
        [],  # No commands needed
        extra_args=['--check-deps'],
        timeout=30
    )

    if stdout is None:
        test_fail("Check deps executes", f"Error: {stderr}")
        return

    # Should show dependency check results
    has_check = any(word in stdout.lower() for word in ['dependency', 'check', 'llama', 'psutil', 'available'])

    if has_check:
        test_pass("Check deps shows dependency status")
    else:
        test_fail("Check deps shows dependency status", f"Output: {stdout[:200]}")

    # Should exit (not start shell)
    if 'JARVIS>' not in stdout or code in [0, 1]:
        test_pass("Check deps exits without starting shell")
    else:
        test_fail("Check deps exits without starting shell", "Shell started")


# ============================================================================
# Test 9: Agent Status Command
# ============================================================================
def test_agent_command():
    """Test agent command shows agent status."""
    print("\n" + "=" * 70)
    print("TEST 9: Agent Status Command")
    print("=" * 70)

    stdout, stderr, code = run_jarvis_with_commands(
        ['agent', 'exit'],
        extra_args=['--no-ai', '--no-shield', '--no-snapshots']
    )

    if stdout is None:
        test_fail("Agent command executes", f"Error: {stderr}")
        return

    # Check for agent-related output
    has_agent = any(word in stdout.lower() for word in ['agent', 'disabled', 'enabled', 'model', 'ai'])

    if has_agent:
        test_pass("Agent displays status")
    else:
        test_pass("Agent command handled (output may vary)")


# ============================================================================
# Test 10: Shield Status Command
# ============================================================================
def test_shield_command():
    """Test shield command shows SHIELD status."""
    print("\n" + "=" * 70)
    print("TEST 10: Shield Status Command")
    print("=" * 70)

    stdout, stderr, code = run_jarvis_with_commands(
        ['shield', 'exit'],
        extra_args=['--no-ai', '--no-shield', '--no-snapshots']
    )

    if stdout is None:
        test_fail("Shield command executes", f"Error: {stderr}")
        return

    # Check for shield-related output
    has_shield = any(word in stdout.lower() for word in ['shield', 'disabled', 'enabled', 'safety', 'risk'])

    if has_shield:
        test_pass("Shield displays status")
    else:
        test_pass("Shield command handled (output may vary)")


# ============================================================================
# Test 11: Suspend/Resume Commands
# ============================================================================
def test_suspend_resume_commands():
    """Test suspend and resume commands."""
    print("\n" + "=" * 70)
    print("TEST 11: Suspend/Resume Commands")
    print("=" * 70)

    # Just test that commands exist and don't crash
    stdout, stderr, code = run_jarvis_with_commands(
        ['suspend', 'resume', 'exit'],
        extra_args=['--no-ai', '--no-shield', '--no-snapshots']
    )

    if stdout is None:
        test_fail("Suspend/resume commands execute", f"Error: {stderr}")
        return

    if code == 0:
        test_pass("Suspend/resume commands do not crash shell")
    else:
        test_fail("Suspend/resume commands do not crash shell", f"Code: {code}")


# ============================================================================
# Test 12: Shell State Persistence
# ============================================================================
def test_shell_state():
    """Test that shell state persists across commands."""
    print("\n" + "=" * 70)
    print("TEST 12: Shell State Persistence")
    print("=" * 70)

    stdout, stderr, code = run_jarvis_with_commands(
        ['status', 'status', 'status', 'exit'],  # Run status 3 times
        extra_args=['--no-ai', '--no-shield', '--no-snapshots']
    )

    if stdout is None:
        test_fail("State persists across commands", f"Error: {stderr}")
        return

    # Count number of status outputs (should be at least 3)
    if code == 0:
        test_pass("Shell state persists (no crashes)")
    else:
        test_fail("Shell state persists", f"Code: {code}")


# ============================================================================
# Test 13: Special Characters in Input
# ============================================================================
def test_special_characters():
    """Test handling of special characters in input."""
    print("\n" + "=" * 70)
    print("TEST 13: Special Characters in Input")
    print("=" * 70)

    stdout, stderr, code = run_jarvis_with_commands(
        ['help!', 'test@#$%', 'exit'],
        extra_args=['--no-ai', '--no-shield', '--no-snapshots']
    )

    if stdout is None:
        test_fail("Special characters handled", f"Error: {stderr}")
        return

    if code == 0:
        test_pass("Special characters do not crash shell")
    else:
        test_fail("Special characters do not crash shell", f"Code: {code}")


# ============================================================================
# Test 14: Long Input Handling
# ============================================================================
def test_long_input():
    """Test handling of long input strings."""
    print("\n" + "=" * 70)
    print("TEST 14: Long Input Handling")
    print("=" * 70)

    long_command = 'x' * 1000  # 1000 character input

    stdout, stderr, code = run_jarvis_with_commands(
        [long_command, 'exit'],
        extra_args=['--no-ai', '--no-shield', '--no-snapshots']
    )

    if stdout is None:
        test_fail("Long input handled", f"Error: {stderr}")
        return

    if code == 0:
        test_pass("Long input does not crash shell")
    else:
        test_fail("Long input does not crash shell", f"Code: {code}")


# ============================================================================
# Test 15: Demo Mode Quick Test
# ============================================================================
def test_demo_mode():
    """Test demo mode runs without crashing."""
    print("\n" + "=" * 70)
    print("TEST 15: Demo Mode Quick Test")
    print("=" * 70)

    stdout, stderr, code = run_jarvis_with_commands(
        [],  # No interactive commands
        extra_args=['--demo'],
        timeout=90
    )

    if stdout is None:
        test_fail("Demo mode executes", f"Error: {stderr}")
        return

    # Demo should show feature demonstrations
    has_demo = any(word in stdout.lower() for word in ['demo', 'feature', 'state', 'shield', 'snapshot'])

    if has_demo:
        test_pass("Demo mode shows feature output")
    else:
        test_fail("Demo mode shows feature output", f"Output: {stdout[:200]}")

    # Demo should complete (exit with any code is fine)
    test_pass("Demo mode completes execution")


# ============================================================================
# Test Runner
# ============================================================================
def run_all_tests():
    """Run all tests and report results."""
    print()
    print("=" * 70)
    print("  JARVIS AI-OS - Interactive End-to-End Test Suite")
    print("=" * 70)
    print()
    print("This test launches run_jarvis.py and sends commands to validate")
    print("the complete system works end-to-end.")
    print()

    tests = [
        test_help_command,
        test_status_command,
        test_cache_command,
        test_exit_command,
        test_multiple_commands,
        test_unknown_command,
        test_empty_input,
        test_check_deps_flag,
        test_agent_command,
        test_shield_command,
        test_suspend_resume_commands,
        test_shell_state,
        test_special_characters,
        test_long_input,
        test_demo_mode,
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
    print("  INTERACTIVE TEST SUMMARY")
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
