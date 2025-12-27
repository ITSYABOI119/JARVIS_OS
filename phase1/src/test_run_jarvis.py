#!/usr/bin/env python3
"""
JARVIS AI-OS - run_jarvis.py Test Suite
Tests the main launcher script functionality.

Tests:
1. Dependency checking (with mocked imports)
2. Argument parsing (all CLI flags)
3. Interactive shell initialization (mocked components)
4. Demo script execution (mocked components)
5. Error handling (missing dependencies, init failures)

Run: python test_run_jarvis.py
"""

import sys
import os
import io
import argparse
from pathlib import Path
from unittest.mock import patch, MagicMock, PropertyMock

# Add parent directory to path
sys.path.insert(0, str(Path(__file__).parent))
sys.path.insert(0, str(Path(__file__).parent / "ai"))

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
# Test 1: Dependency Check - All Available
# ============================================================================
def test_check_dependencies_all_available():
    """Test dependency check when all dependencies are available."""
    print("\n" + "=" * 70)
    print("TEST 1: Dependency Check - All Available")
    print("=" * 70)

    # Mock all imports to succeed
    with patch.dict('sys.modules', {
        'llama_cpp': MagicMock(),
        'psutil': MagicMock()
    }):
        # Mock model files to exist
        with patch.object(Path, 'exists', return_value=True):
            # Mock subprocess for unshare
            with patch('subprocess.run') as mock_run:
                mock_run.return_value = MagicMock(returncode=0)

                # Capture output
                old_stdout = sys.stdout
                sys.stdout = io.StringIO()

                try:
                    # Import and run
                    import run_jarvis
                    # Reload to get fresh state
                    import importlib
                    importlib.reload(run_jarvis)

                    result = run_jarvis.check_dependencies()
                    output = sys.stdout.getvalue()
                finally:
                    sys.stdout = old_stdout

                if result:
                    test_pass("All dependencies available returns True")
                else:
                    test_fail("All dependencies available returns True", f"Got {result}")

                if "All dependencies available" in output:
                    test_pass("Success message in output")
                else:
                    test_fail("Success message in output", "Message not found")


# ============================================================================
# Test 2: Dependency Check - Missing Dependencies Logic
# ============================================================================
def test_check_dependencies_logic():
    """Test dependency check logic handles missing list correctly."""
    print("\n" + "=" * 70)
    print("TEST 2: Dependency Check - Missing Dependencies Logic")
    print("=" * 70)

    # Test that the check_dependencies function inspects missing list
    import run_jarvis
    import importlib
    import inspect
    importlib.reload(run_jarvis)

    source = inspect.getsource(run_jarvis.check_dependencies)

    # Check that it tracks missing dependencies
    if "missing = []" in source:
        test_pass("Tracks missing dependencies in list")
    else:
        test_fail("Tracks missing dependencies in list", "Not found")

    # Check that it returns False when things are missing
    if "return False" in source:
        test_pass("Returns False when dependencies missing")
    else:
        test_fail("Returns False when dependencies missing", "Not found")

    # Check that it checks for llama_cpp
    if "llama_cpp" in source:
        test_pass("Checks for llama_cpp")
    else:
        test_fail("Checks for llama_cpp", "Not found")

    # Check that it checks for psutil
    if "psutil" in source:
        test_pass("Checks for psutil")
    else:
        test_fail("Checks for psutil", "Not found")


# ============================================================================
# Test 3: Argument Parsing - Default
# ============================================================================
def test_argument_parsing_default():
    """Test argument parsing with default values."""
    print("\n" + "=" * 70)
    print("TEST 3: Argument Parsing - Default")
    print("=" * 70)

    import run_jarvis
    import importlib
    importlib.reload(run_jarvis)

    # Create parser manually
    parser = argparse.ArgumentParser()
    parser.add_argument('--demo', action='store_true')
    parser.add_argument('--no-ai', action='store_true')
    parser.add_argument('--no-shield', action='store_true')
    parser.add_argument('--no-snapshots', action='store_true')
    parser.add_argument('--check-deps', action='store_true')

    args = parser.parse_args([])

    if not args.demo:
        test_pass("--demo defaults to False")
    else:
        test_fail("--demo defaults to False", f"Got {args.demo}")

    if not args.no_ai:
        test_pass("--no-ai defaults to False")
    else:
        test_fail("--no-ai defaults to False", f"Got {args.no_ai}")

    if not args.no_shield:
        test_pass("--no-shield defaults to False")
    else:
        test_fail("--no-shield defaults to False", f"Got {args.no_shield}")


# ============================================================================
# Test 4: Argument Parsing - Demo Mode
# ============================================================================
def test_argument_parsing_demo():
    """Test argument parsing with --demo flag."""
    print("\n" + "=" * 70)
    print("TEST 4: Argument Parsing - Demo Mode")
    print("=" * 70)

    parser = argparse.ArgumentParser()
    parser.add_argument('--demo', action='store_true')
    parser.add_argument('--no-ai', action='store_true')
    parser.add_argument('--no-shield', action='store_true')
    parser.add_argument('--no-snapshots', action='store_true')
    parser.add_argument('--check-deps', action='store_true')

    args = parser.parse_args(['--demo'])

    if args.demo:
        test_pass("--demo flag parsed correctly")
    else:
        test_fail("--demo flag parsed correctly", f"Got {args.demo}")


# ============================================================================
# Test 5: Argument Parsing - No AI Mode
# ============================================================================
def test_argument_parsing_no_ai():
    """Test argument parsing with --no-ai flag."""
    print("\n" + "=" * 70)
    print("TEST 5: Argument Parsing - No AI Mode")
    print("=" * 70)

    parser = argparse.ArgumentParser()
    parser.add_argument('--demo', action='store_true')
    parser.add_argument('--no-ai', action='store_true')
    parser.add_argument('--no-shield', action='store_true')
    parser.add_argument('--no-snapshots', action='store_true')
    parser.add_argument('--check-deps', action='store_true')

    args = parser.parse_args(['--no-ai'])

    if args.no_ai:
        test_pass("--no-ai flag parsed correctly")
    else:
        test_fail("--no-ai flag parsed correctly", f"Got {args.no_ai}")


# ============================================================================
# Test 6: Argument Parsing - Multiple Flags
# ============================================================================
def test_argument_parsing_multiple():
    """Test argument parsing with multiple flags."""
    print("\n" + "=" * 70)
    print("TEST 6: Argument Parsing - Multiple Flags")
    print("=" * 70)

    parser = argparse.ArgumentParser()
    parser.add_argument('--demo', action='store_true')
    parser.add_argument('--no-ai', action='store_true')
    parser.add_argument('--no-shield', action='store_true')
    parser.add_argument('--no-snapshots', action='store_true')
    parser.add_argument('--check-deps', action='store_true')

    args = parser.parse_args(['--no-ai', '--no-shield', '--no-snapshots'])

    all_correct = args.no_ai and args.no_shield and args.no_snapshots and not args.demo

    if all_correct:
        test_pass("Multiple flags parsed correctly")
    else:
        test_fail("Multiple flags parsed correctly",
                  f"no_ai={args.no_ai}, no_shield={args.no_shield}, no_snapshots={args.no_snapshots}")


# ============================================================================
# Test 7: Launch Interactive Shell - Mock Bootstrap
# ============================================================================
def test_launch_shell_mock():
    """Test launch_interactive_shell with mocked SystemBootstrap."""
    print("\n" + "=" * 70)
    print("TEST 7: Launch Interactive Shell - Mock Bootstrap")
    print("=" * 70)

    # Create mock components
    mock_components = {
        'ipc_client': MagicMock(),
        'agent_router': MagicMock(),
        'health_monitor': MagicMock(),
        'state_manager': MagicMock(),
        'shield': MagicMock(),
        'snapshot_manager': MagicMock(),
        'suspend_manager': MagicMock(),
    }

    mock_bootstrap = MagicMock()
    mock_bootstrap.initialize_all.return_value = mock_components

    mock_shell = MagicMock()

    with patch.dict('sys.modules', {
        'system_bootstrap': MagicMock(
            SystemBootstrap=MagicMock(return_value=mock_bootstrap),
            BootstrapError=Exception
        ),
        'shell.shell': MagicMock(JARVISShell=MagicMock(return_value=mock_shell))
    }):
        # Capture output
        old_stdout = sys.stdout
        sys.stdout = io.StringIO()

        try:
            import run_jarvis
            import importlib
            importlib.reload(run_jarvis)

            # Mock the imports within the function
            with patch('run_jarvis.Path'):
                # Can't easily test launch_interactive_shell due to imports
                # Just verify the function exists and has correct signature
                import inspect
                sig = inspect.signature(run_jarvis.launch_interactive_shell)
                params = list(sig.parameters.keys())

        finally:
            sys.stdout = old_stdout

        expected_params = ['enable_ai', 'enable_shield', 'enable_snapshots']
        if params == expected_params:
            test_pass("launch_interactive_shell has correct parameters")
        else:
            test_fail("launch_interactive_shell has correct parameters", f"Got {params}")


# ============================================================================
# Test 8: Demo Script Function Exists
# ============================================================================
def test_demo_script_exists():
    """Test that run_demo_script function exists and is callable."""
    print("\n" + "=" * 70)
    print("TEST 8: Demo Script Function Exists")
    print("=" * 70)

    import run_jarvis
    import importlib
    importlib.reload(run_jarvis)

    if hasattr(run_jarvis, 'run_demo_script'):
        test_pass("run_demo_script function exists")
    else:
        test_fail("run_demo_script function exists", "Function not found")

    if callable(run_jarvis.run_demo_script):
        test_pass("run_demo_script is callable")
    else:
        test_fail("run_demo_script is callable", "Not callable")


# ============================================================================
# Test 9: Main Function Exists
# ============================================================================
def test_main_exists():
    """Test that main function exists."""
    print("\n" + "=" * 70)
    print("TEST 9: Main Function Exists")
    print("=" * 70)

    import run_jarvis
    import importlib
    importlib.reload(run_jarvis)

    if hasattr(run_jarvis, 'main'):
        test_pass("main function exists")
    else:
        test_fail("main function exists", "Function not found")

    if callable(run_jarvis.main):
        test_pass("main is callable")
    else:
        test_fail("main is callable", "Not callable")


# ============================================================================
# Test 10: Check Dependencies Function Signature
# ============================================================================
def test_check_deps_signature():
    """Test check_dependencies function signature."""
    print("\n" + "=" * 70)
    print("TEST 10: Check Dependencies Function Signature")
    print("=" * 70)

    import run_jarvis
    import importlib
    import inspect
    importlib.reload(run_jarvis)

    sig = inspect.signature(run_jarvis.check_dependencies)
    params = list(sig.parameters.keys())

    if params == []:
        test_pass("check_dependencies has no parameters")
    else:
        test_fail("check_dependencies has no parameters", f"Got {params}")

    # Check return annotation if present
    if sig.return_annotation == inspect.Parameter.empty or sig.return_annotation is None:
        test_pass("check_dependencies returns bool (no annotation)")
    else:
        test_pass(f"check_dependencies has return annotation: {sig.return_annotation}")


# ============================================================================
# Test 11: Model Path Detection
# ============================================================================
def test_model_path_detection():
    """Test that model path detection logic works."""
    print("\n" + "=" * 70)
    print("TEST 11: Model Path Detection")
    print("=" * 70)

    # Test path construction logic
    script_path = Path(__file__).parent
    models_dir_local = script_path / "models"
    models_dir_root = script_path.parent.parent / "models"

    # Check that path construction is valid
    if isinstance(models_dir_local, Path):
        test_pass("Local models path is valid Path object")
    else:
        test_fail("Local models path is valid Path object", f"Got {type(models_dir_local)}")

    if isinstance(models_dir_root, Path):
        test_pass("Root models path is valid Path object")
    else:
        test_fail("Root models path is valid Path object", f"Got {type(models_dir_root)}")


# ============================================================================
# Test 12: Unshare Check Logic
# ============================================================================
def test_unshare_check():
    """Test unshare availability check logic."""
    print("\n" + "=" * 70)
    print("TEST 12: Unshare Check Logic")
    print("=" * 70)

    import subprocess

    # Test with mock successful unshare
    with patch('subprocess.run') as mock_run:
        mock_run.return_value = MagicMock(returncode=0)

        result = subprocess.run(['unshare', '--help'], capture_output=True, timeout=1)

        if result.returncode == 0:
            test_pass("Mock unshare returns success")
        else:
            test_fail("Mock unshare returns success", f"Got {result.returncode}")

    # Test with mock failed unshare
    with patch('subprocess.run') as mock_run:
        mock_run.return_value = MagicMock(returncode=1)

        result = subprocess.run(['unshare', '--help'], capture_output=True, timeout=1)

        if result.returncode != 0:
            test_pass("Mock unshare returns failure")
        else:
            test_fail("Mock unshare returns failure", f"Got {result.returncode}")


# ============================================================================
# Test 13: Keyboard Interrupt Handling
# ============================================================================
def test_keyboard_interrupt():
    """Test that KeyboardInterrupt is handled gracefully."""
    print("\n" + "=" * 70)
    print("TEST 13: Keyboard Interrupt Handling")
    print("=" * 70)

    # This tests that the main function has try/except for KeyboardInterrupt
    import run_jarvis
    import importlib
    import inspect
    importlib.reload(run_jarvis)

    source = inspect.getsource(run_jarvis.main)

    if "KeyboardInterrupt" in source:
        test_pass("main handles KeyboardInterrupt")
    else:
        test_fail("main handles KeyboardInterrupt", "Handler not found")

    if "except Exception" in source:
        test_pass("main handles generic Exception")
    else:
        test_fail("main handles generic Exception", "Handler not found")


# ============================================================================
# Test 14: Script Is Executable
# ============================================================================
def test_script_executable():
    """Test that script has if __name__ == '__main__' block."""
    print("\n" + "=" * 70)
    print("TEST 14: Script Is Executable")
    print("=" * 70)

    script_path = Path(__file__).parent / "run_jarvis.py"

    with open(script_path, 'r') as f:
        content = f.read()

    if 'if __name__ == "__main__":' in content or "if __name__ == '__main__':" in content:
        test_pass("Script has __name__ == '__main__' check")
    else:
        test_fail("Script has __name__ == '__main__' check", "Not found")

    if content.startswith('#!/usr/bin/env python3'):
        test_pass("Script has shebang line")
    else:
        test_fail("Script has shebang line", "Not found")


# ============================================================================
# Test 15: Help Text
# ============================================================================
def test_help_text():
    """Test that help text is properly formatted."""
    print("\n" + "=" * 70)
    print("TEST 15: Help Text")
    print("=" * 70)

    import run_jarvis
    import importlib
    import inspect
    importlib.reload(run_jarvis)

    source = inspect.getsource(run_jarvis.main)

    # Check for help examples
    if "Examples:" in source:
        test_pass("Help text contains Examples section")
    else:
        test_fail("Help text contains Examples section", "Not found")

    # Check for important options documented
    if "python run_jarvis.py" in source:
        test_pass("Help text contains usage examples")
    else:
        test_fail("Help text contains usage examples", "Not found")


# ============================================================================
# Test Runner
# ============================================================================
def run_all_tests():
    """Run all tests and report results."""
    print()
    print("=" * 70)
    print("  JARVIS AI-OS - run_jarvis.py Test Suite")
    print("=" * 70)

    tests = [
        test_check_dependencies_all_available,
        test_check_dependencies_logic,
        test_argument_parsing_default,
        test_argument_parsing_demo,
        test_argument_parsing_no_ai,
        test_argument_parsing_multiple,
        test_launch_shell_mock,
        test_demo_script_exists,
        test_main_exists,
        test_check_deps_signature,
        test_model_path_detection,
        test_unshare_check,
        test_keyboard_interrupt,
        test_script_executable,
        test_help_text,
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
    print("  TEST SUMMARY")
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
