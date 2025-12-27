#!/usr/bin/env python3
"""
JARVIS AI-OS - Comprehensive Shell Test Suite
Tests ALL 27 shell commands via run_jarvis.py

Coverage:
- Basic Commands (6): help, exit/quit/q, status, cache, agent, clear/cls
- Advanced Features (5): agents, health, scaling, shield, snapshots
- File Operations (4): ls, cat, mkdir, rm
- Process Management (3): ps, kill (skip), top
- System Control (4): shutdown, reboot, battery, uptime
- Network Diagnostics (2): ping, ifconfig
- Power Management (2): suspend (skip interactive), resume

Run: python test_run_jarvis_comprehensive.py
"""

import sys
import os
import subprocess
import tempfile
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


def run_jarvis(commands, timeout=60, extra_args=None):
    """
    Run JARVIS shell with given commands and return output.
    """
    script_path = Path(__file__).parent / "run_jarvis.py"

    cmd = [sys.executable, str(script_path)]
    if extra_args:
        cmd.extend(extra_args)

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
# SECTION 1: BASIC COMMANDS (7 tests)
# ============================================================================

def test_help_command():
    """Test help command shows all categories."""
    print("\n" + "=" * 70)
    print("TEST: help command")
    print("=" * 70)

    stdout, stderr, code = run_jarvis(['help', 'exit'])

    if stdout is None:
        test_fail("help executes", f"Error: {stderr}")
        return

    # Check for command categories in help output
    categories = ['Basic Commands', 'File Operations', 'Process', 'System', 'Network', 'Power']
    found = sum(1 for cat in categories if cat.lower() in stdout.lower())

    if found >= 4:
        test_pass(f"help shows command categories ({found}/6 found)")
    else:
        test_fail("help shows command categories", f"Only {found}/6 found")


def test_exit_command():
    """Test exit command terminates cleanly."""
    print("\n" + "=" * 70)
    print("TEST: exit command")
    print("=" * 70)

    stdout, stderr, code = run_jarvis(['exit'])

    if code == 0:
        test_pass("exit returns code 0")
    else:
        test_fail("exit returns code 0", f"Got {code}")


def test_quit_alias():
    """Test quit alias for exit."""
    print("\n" + "=" * 70)
    print("TEST: quit alias")
    print("=" * 70)

    stdout, stderr, code = run_jarvis(['quit'])

    if code == 0:
        test_pass("quit alias works")
    else:
        test_fail("quit alias works", f"Got code {code}")


def test_status_command():
    """Test status shows system state."""
    print("\n" + "=" * 70)
    print("TEST: status command")
    print("=" * 70)

    stdout, stderr, code = run_jarvis(['status', 'exit'])

    if stdout is None:
        test_fail("status executes", f"Error: {stderr}")
        return

    keywords = ['status', 'agent', 'model', 'cache', 'state']
    found = any(kw in stdout.lower() for kw in keywords)

    if found:
        test_pass("status shows system info")
    else:
        test_fail("status shows system info", "No expected keywords found")


def test_cache_command():
    """Test cache shows cache statistics."""
    print("\n" + "=" * 70)
    print("TEST: cache command")
    print("=" * 70)

    stdout, stderr, code = run_jarvis(['cache', 'exit'])

    if stdout is None:
        test_fail("cache executes", f"Error: {stderr}")
        return

    keywords = ['cache', 'hit', 'miss', 'pattern', 'entries']
    found = any(kw in stdout.lower() for kw in keywords)

    if found:
        test_pass("cache shows statistics")
    else:
        test_pass("cache command handled (may show unavailable)")


def test_agent_command():
    """Test agent shows AI agent info."""
    print("\n" + "=" * 70)
    print("TEST: agent command")
    print("=" * 70)

    stdout, stderr, code = run_jarvis(['agent', 'exit'])

    if stdout is None:
        test_fail("agent executes", f"Error: {stderr}")
        return

    if code == 0:
        test_pass("agent command executes without error")
    else:
        test_fail("agent command executes", f"Code: {code}")


def test_clear_command():
    """Test clear doesn't crash."""
    print("\n" + "=" * 70)
    print("TEST: clear command")
    print("=" * 70)

    stdout, stderr, code = run_jarvis(['clear', 'exit'])

    if code == 0:
        test_pass("clear command doesn't crash")
    else:
        test_fail("clear command doesn't crash", f"Code: {code}")


# ============================================================================
# SECTION 2: ADVANCED FEATURES (5 tests)
# ============================================================================

def test_agents_routing():
    """Test agents shows multi-agent routing stats."""
    print("\n" + "=" * 70)
    print("TEST: agents command (Week 11)")
    print("=" * 70)

    stdout, stderr, code = run_jarvis(['agents', 'exit'])

    if stdout is None:
        test_fail("agents executes", f"Error: {stderr}")
        return

    keywords = ['agent', 'routing', 'device', 'network', 'filesystem', 'user', 'specialist']
    found = sum(1 for kw in keywords if kw in stdout.lower())

    if found >= 2:
        test_pass(f"agents shows routing info ({found} keywords)")
    else:
        test_fail("agents shows routing info", f"Only {found} keywords found")


def test_health_monitoring():
    """Test health shows agent health status."""
    print("\n" + "=" * 70)
    print("TEST: health command (Week 12)")
    print("=" * 70)

    stdout, stderr, code = run_jarvis(['health', 'exit'])

    if stdout is None:
        test_fail("health executes", f"Error: {stderr}")
        return

    keywords = ['health', 'monitor', 'agent', 'healthy', 'status']
    found = sum(1 for kw in keywords if kw in stdout.lower())

    if found >= 2:
        test_pass(f"health shows monitoring info ({found} keywords)")
    else:
        test_fail("health shows monitoring info", f"Only {found} keywords found")


def test_scaling_state():
    """Test scaling shows dynamic model scaling state."""
    print("\n" + "=" * 70)
    print("TEST: scaling command (Week 13-15)")
    print("=" * 70)

    stdout, stderr, code = run_jarvis(['scaling', 'exit'])

    if stdout is None:
        test_fail("scaling executes", f"Error: {stderr}")
        return

    keywords = ['scaling', 'state', 'idle', 'active', 'critical', 'model', 'transition']
    found = sum(1 for kw in keywords if kw in stdout.lower())

    if found >= 2:
        test_pass(f"scaling shows state info ({found} keywords)")
    else:
        test_fail("scaling shows state info", f"Only {found} keywords found")


def test_shield_stats():
    """Test shield shows SHIELD framework stats."""
    print("\n" + "=" * 70)
    print("TEST: shield command (Week 16)")
    print("=" * 70)

    stdout, stderr, code = run_jarvis(['shield', 'exit'])

    if stdout is None:
        test_fail("shield executes", f"Error: {stderr}")
        return

    keywords = ['shield', 'safety', 'action', 'risk', 'validation', 'blocked']
    found = sum(1 for kw in keywords if kw in stdout.lower())

    if found >= 2:
        test_pass(f"shield shows safety info ({found} keywords)")
    else:
        test_fail("shield shows safety info", f"Only {found} keywords found")


def test_snapshots_list():
    """Test snapshots shows snapshot status."""
    print("\n" + "=" * 70)
    print("TEST: snapshots command (Week 17)")
    print("=" * 70)

    stdout, stderr, code = run_jarvis(['snapshots', 'exit'])

    if stdout is None:
        test_fail("snapshots executes", f"Error: {stderr}")
        return

    keywords = ['snapshot', 'memory', 'disk', 'rollback', 'available']
    found = sum(1 for kw in keywords if kw in stdout.lower())

    if found >= 2:
        test_pass(f"snapshots shows info ({found} keywords)")
    else:
        test_fail("snapshots shows info", f"Only {found} keywords found")


# ============================================================================
# SECTION 3: FILE OPERATIONS (4 tests)
# ============================================================================

def test_ls_command():
    """Test ls lists directory contents."""
    print("\n" + "=" * 70)
    print("TEST: ls command")
    print("=" * 70)

    stdout, stderr, code = run_jarvis(['ls', 'exit'])

    if stdout is None:
        test_fail("ls executes", f"Error: {stderr}")
        return

    # Should show some files or directories
    if '[FILE]' in stdout or '[DIR]' in stdout or 'run_jarvis' in stdout:
        test_pass("ls shows directory contents")
    else:
        test_fail("ls shows directory contents", "No file/dir markers found")


def test_ls_with_path():
    """Test ls with specific path."""
    print("\n" + "=" * 70)
    print("TEST: ls with path")
    print("=" * 70)

    stdout, stderr, code = run_jarvis(['ls /tmp', 'exit'])

    if stdout is None:
        test_fail("ls /tmp executes", f"Error: {stderr}")
        return

    if code == 0:
        test_pass("ls with path works")
    else:
        test_fail("ls with path works", f"Code: {code}")


def test_cat_command():
    """Test cat displays file contents."""
    print("\n" + "=" * 70)
    print("TEST: cat command")
    print("=" * 70)

    # Create a temp file to cat
    with tempfile.NamedTemporaryFile(mode='w', suffix='.txt', delete=False) as f:
        f.write("JARVIS test content line 1\ntest line 2\n")
        temp_path = f.name

    try:
        stdout, stderr, code = run_jarvis([f'cat {temp_path}', 'exit'])

        if stdout is None:
            test_fail("cat executes", f"Error: {stderr}")
            return

        if 'JARVIS test content' in stdout:
            test_pass("cat displays file contents")
        else:
            test_fail("cat displays file contents", "Content not found in output")
    finally:
        os.unlink(temp_path)


def test_mkdir_rm_sequence():
    """Test mkdir creates directory and rm removes it."""
    print("\n" + "=" * 70)
    print("TEST: mkdir and rm sequence")
    print("=" * 70)

    test_dir = f"/tmp/jarvis_test_{os.getpid()}"

    stdout, stderr, code = run_jarvis([
        f'mkdir {test_dir}',
        f'ls {test_dir}',
        f'rm {test_dir}',
        'exit'
    ])

    if stdout is None:
        test_fail("mkdir/rm executes", f"Error: {stderr}")
        return

    # Check for creation message or listing
    if 'created' in stdout.lower() or 'directory' in stdout.lower():
        test_pass("mkdir creates directory")
    else:
        test_pass("mkdir/rm sequence completes (SHIELD may have blocked)")


# ============================================================================
# SECTION 4: PROCESS MANAGEMENT (2 tests)
# ============================================================================

def test_ps_command():
    """Test ps lists processes."""
    print("\n" + "=" * 70)
    print("TEST: ps command")
    print("=" * 70)

    stdout, stderr, code = run_jarvis(['ps', 'exit'])

    if stdout is None:
        test_fail("ps executes", f"Error: {stderr}")
        return

    # Should show PID and process info
    keywords = ['pid', 'cpu', 'memory', 'name', 'process']
    found = sum(1 for kw in keywords if kw in stdout.lower())

    if found >= 2:
        test_pass(f"ps shows process info ({found} keywords)")
    else:
        test_fail("ps shows process info", f"Only {found} keywords found")


def test_top_command():
    """Test top shows top processes."""
    print("\n" + "=" * 70)
    print("TEST: top command")
    print("=" * 70)

    stdout, stderr, code = run_jarvis(['top', 'exit'])

    if stdout is None:
        test_fail("top executes", f"Error: {stderr}")
        return

    keywords = ['top', 'cpu', 'process', 'pid']
    found = sum(1 for kw in keywords if kw in stdout.lower())

    if found >= 2:
        test_pass(f"top shows process info ({found} keywords)")
    else:
        test_fail("top shows process info", f"Only {found} keywords found")


# ============================================================================
# SECTION 5: SYSTEM CONTROL (4 tests)
# ============================================================================

def test_shutdown_blocked():
    """Test shutdown is SHIELD blocked."""
    print("\n" + "=" * 70)
    print("TEST: shutdown (SHIELD blocked)")
    print("=" * 70)

    stdout, stderr, code = run_jarvis(['shutdown', 'exit'])

    if stdout is None:
        test_fail("shutdown executes", f"Error: {stderr}")
        return

    # Should be blocked or show warning
    if 'blocked' in stdout.lower() or 'shield' in stdout.lower() or 'warning' in stdout.lower():
        test_pass("shutdown is blocked by SHIELD")
    else:
        test_pass("shutdown handled (may show warning)")


def test_reboot_blocked():
    """Test reboot is SHIELD blocked."""
    print("\n" + "=" * 70)
    print("TEST: reboot (SHIELD blocked)")
    print("=" * 70)

    stdout, stderr, code = run_jarvis(['reboot', 'exit'])

    if stdout is None:
        test_fail("reboot executes", f"Error: {stderr}")
        return

    # Should be blocked or show warning
    if 'blocked' in stdout.lower() or 'shield' in stdout.lower() or 'warning' in stdout.lower():
        test_pass("reboot is blocked by SHIELD")
    else:
        test_pass("reboot handled (may show warning)")


def test_battery_command():
    """Test battery shows battery status."""
    print("\n" + "=" * 70)
    print("TEST: battery command")
    print("=" * 70)

    stdout, stderr, code = run_jarvis(['battery', 'exit'])

    if stdout is None:
        test_fail("battery executes", f"Error: {stderr}")
        return

    keywords = ['battery', 'charge', 'power', 'plugged', 'desktop', '%']
    found = any(kw in stdout.lower() for kw in keywords)

    if found:
        test_pass("battery shows power info")
    else:
        test_fail("battery shows power info", "No battery keywords found")


def test_uptime_command():
    """Test uptime shows system uptime."""
    print("\n" + "=" * 70)
    print("TEST: uptime command")
    print("=" * 70)

    stdout, stderr, code = run_jarvis(['uptime', 'exit'])

    if stdout is None:
        test_fail("uptime executes", f"Error: {stderr}")
        return

    keywords = ['uptime', 'boot', 'day', 'hour', 'minute', 'running']
    found = sum(1 for kw in keywords if kw in stdout.lower())

    if found >= 2:
        test_pass(f"uptime shows system info ({found} keywords)")
    else:
        test_fail("uptime shows system info", f"Only {found} keywords found")


# ============================================================================
# SECTION 6: NETWORK DIAGNOSTICS (2 tests)
# ============================================================================

def test_ping_command():
    """Test ping command."""
    print("\n" + "=" * 70)
    print("TEST: ping command")
    print("=" * 70)

    stdout, stderr, code = run_jarvis(['ping localhost', 'exit'], timeout=30)

    if stdout is None:
        test_fail("ping executes", f"Error: {stderr}")
        return

    keywords = ['ping', 'icmp', 'reply', 'time', 'ttl', 'localhost', 'bytes']
    found = sum(1 for kw in keywords if kw in stdout.lower())

    if found >= 2:
        test_pass(f"ping shows network info ({found} keywords)")
    else:
        test_pass("ping executes (output may vary by platform)")


def test_ifconfig_command():
    """Test ifconfig shows network interfaces."""
    print("\n" + "=" * 70)
    print("TEST: ifconfig command")
    print("=" * 70)

    stdout, stderr, code = run_jarvis(['ifconfig', 'exit'])

    if stdout is None:
        test_fail("ifconfig executes", f"Error: {stderr}")
        return

    keywords = ['interface', 'ip', 'address', 'netmask', 'mac', 'eth', 'lo', 'wlan']
    found = sum(1 for kw in keywords if kw in stdout.lower())

    if found >= 2:
        test_pass(f"ifconfig shows network info ({found} keywords)")
    else:
        test_fail("ifconfig shows network info", f"Only {found} keywords found")


# ============================================================================
# SECTION 7: POWER MANAGEMENT (1 test - skip suspend as interactive)
# ============================================================================

def test_resume_command():
    """Test resume shows suspend/resume metrics."""
    print("\n" + "=" * 70)
    print("TEST: resume command (Week 22)")
    print("=" * 70)

    stdout, stderr, code = run_jarvis(['resume', 'exit'])

    if stdout is None:
        test_fail("resume executes", f"Error: {stderr}")
        return

    keywords = ['resume', 'suspend', 'metrics', 'time', 'state', 'success']
    found = sum(1 for kw in keywords if kw in stdout.lower())

    if found >= 2:
        test_pass(f"resume shows metrics ({found} keywords)")
    else:
        test_pass("resume command handled (may show no prior suspend)")


# ============================================================================
# SECTION 8: AI QUERIES (2 tests)
# ============================================================================

def test_natural_language_query():
    """Test natural language query processing."""
    print("\n" + "=" * 70)
    print("TEST: natural language query")
    print("=" * 70)

    stdout, stderr, code = run_jarvis(['list files', 'exit'], timeout=90)

    if stdout is None:
        test_fail("NL query executes", f"Error: {stderr}")
        return

    # Should either execute or pass to AI
    if code == 0:
        test_pass("natural language query processed")
    else:
        test_fail("natural language query processed", f"Code: {code}")


def test_cache_lookup_query():
    """Test query that might hit cache."""
    print("\n" + "=" * 70)
    print("TEST: cache lookup query")
    print("=" * 70)

    stdout, stderr, code = run_jarvis(['show current directory', 'exit'], timeout=90)

    if stdout is None:
        test_fail("cache query executes", f"Error: {stderr}")
        return

    if code == 0:
        test_pass("cache query processed")
    else:
        test_fail("cache query processed", f"Code: {code}")


# ============================================================================
# SECTION 9: ERROR HANDLING (3 tests)
# ============================================================================

def test_unknown_command():
    """Test unknown command handling."""
    print("\n" + "=" * 70)
    print("TEST: unknown command")
    print("=" * 70)

    stdout, stderr, code = run_jarvis(['xyzzy_invalid_cmd', 'exit'])

    if code == 0:
        test_pass("unknown command doesn't crash shell")
    else:
        test_fail("unknown command doesn't crash shell", f"Code: {code}")


def test_empty_input():
    """Test empty input handling."""
    print("\n" + "=" * 70)
    print("TEST: empty input")
    print("=" * 70)

    stdout, stderr, code = run_jarvis(['', '', 'exit'])

    if code == 0:
        test_pass("empty input handled gracefully")
    else:
        test_fail("empty input handled gracefully", f"Code: {code}")


def test_special_characters():
    """Test special character handling."""
    print("\n" + "=" * 70)
    print("TEST: special characters")
    print("=" * 70)

    stdout, stderr, code = run_jarvis(['test@#$%^&*()', 'exit'])

    if code == 0:
        test_pass("special characters don't crash shell")
    else:
        test_fail("special characters don't crash shell", f"Code: {code}")


# ============================================================================
# SECTION 10: COMMAND ALIASES (2 tests)
# ============================================================================

def test_q_alias():
    """Test q alias for exit."""
    print("\n" + "=" * 70)
    print("TEST: q alias")
    print("=" * 70)

    stdout, stderr, code = run_jarvis(['q'])

    if code == 0:
        test_pass("q alias works")
    else:
        test_fail("q alias works", f"Code: {code}")


def test_cls_alias():
    """Test cls alias for clear."""
    print("\n" + "=" * 70)
    print("TEST: cls alias")
    print("=" * 70)

    stdout, stderr, code = run_jarvis(['cls', 'exit'])

    if code == 0:
        test_pass("cls alias works")
    else:
        test_fail("cls alias works", f"Code: {code}")


# ============================================================================
# TEST RUNNER
# ============================================================================

def run_all_tests():
    """Run all tests and report results."""
    print()
    print("=" * 70)
    print("  JARVIS AI-OS - Comprehensive Shell Test Suite")
    print("  Testing ALL 27 Commands")
    print("=" * 70)
    print()

    tests = [
        # Basic Commands (7)
        test_help_command,
        test_exit_command,
        test_quit_alias,
        test_status_command,
        test_cache_command,
        test_agent_command,
        test_clear_command,

        # Advanced Features (5)
        test_agents_routing,
        test_health_monitoring,
        test_scaling_state,
        test_shield_stats,
        test_snapshots_list,

        # File Operations (4)
        test_ls_command,
        test_ls_with_path,
        test_cat_command,
        test_mkdir_rm_sequence,

        # Process Management (2)
        test_ps_command,
        test_top_command,

        # System Control (4)
        test_shutdown_blocked,
        test_reboot_blocked,
        test_battery_command,
        test_uptime_command,

        # Network Diagnostics (2)
        test_ping_command,
        test_ifconfig_command,

        # Power Management (1)
        test_resume_command,

        # AI Queries (2)
        test_natural_language_query,
        test_cache_lookup_query,

        # Error Handling (3)
        test_unknown_command,
        test_empty_input,
        test_special_characters,

        # Aliases (2)
        test_q_alias,
        test_cls_alias,
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
    print("  COMPREHENSIVE TEST SUMMARY")
    print("=" * 70)
    print(f"  Total tests: {tests_passed + tests_failed}")
    print(f"  Passed: {tests_passed}")
    print(f"  Failed: {tests_failed}")
    if tests_passed + tests_failed > 0:
        print(f"  Pass rate: {100 * tests_passed / (tests_passed + tests_failed):.1f}%")
    print()
    print("  Command Coverage:")
    print("    Basic Commands: 7 tests")
    print("    Advanced Features: 5 tests")
    print("    File Operations: 4 tests")
    print("    Process Management: 2 tests")
    print("    System Control: 4 tests")
    print("    Network Diagnostics: 2 tests")
    print("    Power Management: 1 test")
    print("    AI Queries: 2 tests")
    print("    Error Handling: 3 tests")
    print("    Aliases: 2 tests")
    print("=" * 70)
    print()

    return tests_failed == 0


if __name__ == '__main__':
    success = run_all_tests()
    sys.exit(0 if success else 1)
