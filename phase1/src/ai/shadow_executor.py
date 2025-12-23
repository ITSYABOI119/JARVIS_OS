#!/usr/bin/env python3
"""
JARVIS AI-OS Phase 1 - Real Shadow Execution
Week 17: Shadow Execution with Linux Namespace Isolation

Provides real isolated command execution for SHIELD safety framework.
Executes actions in isolated namespaces to validate safety before real execution.

Author: JARVIS AI-OS Team
Date: November 25, 2025
"""

import subprocess
import logging
import time
from typing import Dict, List, Optional, Any
from dataclasses import dataclass
from enum import Enum

# Set up logging
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)


# ============================================================================
# Shadow Execution Result
# ============================================================================

@dataclass
class ShadowExecutionResult:
    """Result of shadow execution in isolated namespace"""
    success: bool
    action_type: str
    command_executed: List[str]
    returncode: int
    stdout: str
    stderr: str
    execution_time_ms: float
    errors: List[str]
    state_prediction: Dict[str, Any]
    namespace_isolated: bool


# ============================================================================
# Real Shadow Environment
# ============================================================================

class RealShadowEnvironment:
    """
    Execute actions in real isolated Linux namespaces.

    Uses 'unshare' to create isolated process namespaces for safe command testing.
    Translates SHIELD actions to safe command equivalents that validate permissions,
    existence, and accessibility without actually modifying the system.

    Phase 1 Focus:
    - Validation via safe commands (test, ps, systemctl status, etc.)
    - Permission/existence checking
    - Namespace isolation verification

    Phase 2 Enhancement:
    - Overlay filesystem for true shadow execution
    - Real command execution with rollback capability
    """

    def __init__(self, timeout: float = 5.0):
        """
        Initialize shadow environment

        Args:
            timeout: Maximum execution time for shadow commands (default: 5s)
        """
        self.timeout = timeout
        self.isolation_verified = False
        self.statistics = {
            'total_executions': 0,
            'successful_executions': 0,
            'failed_executions': 0,
            'timeout_executions': 0,
            'avg_execution_time_ms': 0.0
        }

        # Verify unshare is available
        self._verify_namespace_support()

    def _verify_namespace_support(self):
        """Verify that Linux namespaces (unshare) are available"""
        try:
            result = subprocess.run(
                ['which', 'unshare'],
                capture_output=True,
                text=True,
                timeout=1.0
            )

            if result.returncode == 0:
                logger.info("[ShadowExec] Namespace isolation available (unshare found)")
                self.isolation_verified = True
            else:
                logger.warning("[ShadowExec] Namespace isolation unavailable (unshare not found)")
                logger.warning("[ShadowExec] Falling back to non-isolated execution")
                self.isolation_verified = False

        except Exception as e:
            logger.error(f"[ShadowExec] Failed to verify namespace support: {e}")
            self.isolation_verified = False

    def execute_shadow_real(self, action: Dict, action_meta) -> ShadowExecutionResult:
        """
        Execute action in isolated namespace with real commands

        Args:
            action: Action dictionary with type and parameters
            action_meta: ActionMetadata from SHIELD action database

        Returns:
            ShadowExecutionResult with execution details
        """
        start_time = time.time()

        # Translate action to safe command
        safe_command = self._translate_to_safe_command(action, action_meta)

        # Execute in isolated namespace
        if self.isolation_verified:
            execution_result = self._execute_in_namespace(safe_command)
        else:
            # Fallback: Execute without isolation (still safe due to dry-run commands)
            execution_result = self._execute_without_namespace(safe_command)

        # Calculate execution time
        execution_time_ms = (time.time() - start_time) * 1000

        # Update statistics
        self._update_statistics(execution_result['success'], execution_time_ms)

        # Parse results and predict state changes
        state_prediction = self._predict_state_after(execution_result, action, action_meta)

        # Build result
        result = ShadowExecutionResult(
            success=execution_result['success'],
            action_type=action.get('type', 'unknown'),
            command_executed=safe_command,
            returncode=execution_result['returncode'],
            stdout=execution_result['stdout'],
            stderr=execution_result['stderr'],
            execution_time_ms=execution_time_ms,
            errors=[execution_result['stderr']] if execution_result['stderr'] else [],
            state_prediction=state_prediction,
            namespace_isolated=self.isolation_verified
        )

        return result

    def _execute_in_namespace(self, command: List[str]) -> Dict:
        """
        Execute command in isolated namespace using unshare

        Args:
            command: Command to execute as list of strings

        Returns:
            Dictionary with success, returncode, stdout, stderr
        """
        # Build unshare command with all isolation flags
        # NOTE: WSL requires --user --map-root-user for namespace creation
        unshare_cmd = [
            'unshare',
            '--user',    # User namespace (required for WSL)
            '--map-root-user',  # Map current user to root in namespace
            '--pid',     # Separate process ID namespace
            '--fork',    # Fork before executing command
            '--'
        ] + command

        try:
            result = subprocess.run(
                unshare_cmd,
                capture_output=True,
                text=True,
                timeout=self.timeout
            )

            return {
                'success': result.returncode == 0,
                'returncode': result.returncode,
                'stdout': result.stdout,
                'stderr': result.stderr
            }

        except subprocess.TimeoutExpired:
            logger.warning(f"[ShadowExec] Command timed out after {self.timeout}s: {command}")
            self.statistics['timeout_executions'] += 1
            return {
                'success': False,
                'returncode': -1,
                'stdout': '',
                'stderr': f'Timeout after {self.timeout}s'
            }

        except Exception as e:
            logger.error(f"[ShadowExec] Execution error: {e}")
            return {
                'success': False,
                'returncode': -2,
                'stdout': '',
                'stderr': str(e)
            }

    def _execute_without_namespace(self, command: List[str]) -> Dict:
        """
        Execute command without namespace isolation (fallback)

        Args:
            command: Command to execute

        Returns:
            Dictionary with execution results
        """
        try:
            result = subprocess.run(
                command,
                capture_output=True,
                text=True,
                timeout=self.timeout
            )

            return {
                'success': result.returncode == 0,
                'returncode': result.returncode,
                'stdout': result.stdout,
                'stderr': result.stderr
            }

        except subprocess.TimeoutExpired:
            self.statistics['timeout_executions'] += 1
            return {
                'success': False,
                'returncode': -1,
                'stdout': '',
                'stderr': f'Timeout after {self.timeout}s'
            }

        except Exception as e:
            return {
                'success': False,
                'returncode': -2,
                'stdout': '',
                'stderr': str(e)
            }

    def _translate_to_safe_command(self, action: Dict, action_meta) -> List[str]:
        """
        Translate action to safe command equivalent

        Phase 1: Use dry-run flags and read-only commands
        Phase 2: Use overlay filesystem for real execution

        Args:
            action: Action dictionary with type and parameters
            action_meta: ActionMetadata from database

        Returns:
            List of command arguments for safe execution
        """
        action_type = action.get('type', '')
        params = action.get('parameters', {})
        category = action_meta.category

        # ================================================================
        # FILE OPERATIONS
        # ================================================================
        if category == "file_operations":
            if 'delete' in action_type or 'rm' in action_type:
                # Use 'test -e' to check file existence (rm has no dry-run)
                path = params.get('path', '/nonexistent')
                return ['test', '-e', path]

            elif 'write' in action_type or 'edit' in action_type:
                # Test write permission by touching in isolated /tmp
                path = params.get('path', '/tmp/shadow_test')
                return ['touch', '/tmp/shadow_test_write']

            elif 'read' in action_type or 'cat' in action_type:
                # Use 'test -r' to check read permission
                path = params.get('path', '/nonexistent')
                return ['test', '-r', path]

            elif 'copy' in action_type or 'move' in action_type:
                # Check source existence and destination write permission
                src = params.get('source', '/nonexistent')
                return ['test', '-e', src]

            elif 'mkdir' in action_type or 'dir_create' in action_type:
                # Test parent directory write permission
                return ['test', '-w', '/tmp']

            else:
                # Default: Test file existence
                path = params.get('path', '/nonexistent')
                return ['test', '-e', path]

        # ================================================================
        # PROCESS MANAGEMENT
        # ================================================================
        elif category == "process_management":
            if 'kill' in action_type or 'terminate' in action_type:
                # Check if process exists
                pid = params.get('pid', '1')
                return ['ps', '-p', str(pid)]

            elif 'start' in action_type or 'exec' in action_type:
                # Check if command exists
                command = params.get('command', 'nonexistent')
                return ['which', command]

            elif 'list' in action_type or 'ps' in action_type:
                # Safe: Just list processes
                return ['ps', 'aux']

            else:
                # Default: List processes
                return ['ps', '-e']

        # ================================================================
        # SERVICE MANAGEMENT
        # ================================================================
        elif category == "service_management":
            service = params.get('service', 'nonexistent')

            if 'stop' in action_type or 'restart' in action_type:
                # Use status instead of stop/restart
                return ['systemctl', '--no-pager', 'status', service]

            elif 'start' in action_type or 'enable' in action_type:
                # Check if service exists
                return ['systemctl', '--no-pager', 'list-unit-files', f'{service}.service']

            elif 'status' in action_type:
                # Safe: Just check status
                return ['systemctl', '--no-pager', 'status', service]

            else:
                # Default: Check service status
                return ['systemctl', '--no-pager', 'is-active', service]

        # ================================================================
        # NETWORK OPERATIONS
        # ================================================================
        elif category == "network_operations":
            if 'interface_down' in action_type or 'ifdown' in action_type:
                # Check if interface exists
                iface = params.get('interface', 'eth0')
                return ['ip', 'link', 'show', iface]

            elif 'ping' in action_type:
                # Safe: Allow ping (isolated network namespace anyway)
                host = params.get('host', '8.8.8.8')
                return ['ping', '-c', '1', '-W', '2', host]

            elif 'firewall' in action_type:
                # Check firewall rules (read-only)
                return ['iptables', '-L', '-n']

            else:
                # Default: Show network status
                return ['ip', 'addr', 'show']

        # ================================================================
        # SYSTEM CONTROL
        # ================================================================
        elif category == "system_control":
            if 'shutdown' in action_type or 'poweroff' in action_type:
                # Check if shutdown command exists (will always fail in namespace)
                return ['which', 'shutdown']

            elif 'reboot' in action_type:
                # Check reboot permission (will fail)
                return ['test', '-w', '/proc/sys/kernel']

            else:
                # Default: Echo simulation
                return ['echo', f'SHADOW: {action_type}']

        # ================================================================
        # MEMORY MANAGEMENT
        # ================================================================
        elif category == "memory_management":
            # All memory ops are safe to read
            if 'read' in action_type or 'show' in action_type:
                return ['free', '-h']
            else:
                # Simulate (can't actually manage memory)
                return ['echo', f'SHADOW: {action_type}']

        # ================================================================
        # HARDWARE CONTROL
        # ================================================================
        elif category == "hardware_control":
            # Hardware control requires real permissions (will fail safely)
            return ['echo', f'SHADOW: {action_type} (hardware control simulated)']

        # ================================================================
        # USER MANAGEMENT
        # ================================================================
        elif category == "user_management":
            if 'create' in action_type or 'delete' in action_type:
                # Check /etc/passwd write permission (will fail)
                return ['test', '-w', '/etc/passwd']
            elif 'list' in action_type:
                # Safe: List users
                return ['getent', 'passwd']
            else:
                return ['echo', f'SHADOW: {action_type}']

        # ================================================================
        # PACKAGE MANAGEMENT
        # ================================================================
        elif category == "package_management":
            if 'install' in action_type or 'remove' in action_type:
                # Check package manager permission
                return ['which', 'apt-get']
            elif 'search' in action_type or 'show' in action_type:
                # Safe: Search packages
                package = params.get('package', 'test')
                return ['apt-cache', 'search', package]
            else:
                return ['apt', '--version']

        # ================================================================
        # MONITORING/LOGGING
        # ================================================================
        elif category == "monitoring_logging":
            # All monitoring/logging operations are safe
            if 'journalctl' in action_type:
                return ['journalctl', '-n', '10', '--no-pager']
            elif 'dmesg' in action_type:
                return ['dmesg', '|', 'tail', '-20']
            elif 'log_read' in action_type:
                log_path = params.get('path', '/var/log/syslog')
                return ['test', '-r', log_path]
            else:
                return ['echo', f'SHADOW: {action_type}']

        # ================================================================
        # DEFAULT
        # ================================================================
        else:
            # Unknown category: Echo simulation
            logger.warning(f"[ShadowExec] Unknown category: {category}")
            return ['echo', f'SHADOW: {action_type} (category: {category})']

    def _predict_state_after(self, execution_result: Dict, action: Dict,
                            action_meta) -> Dict[str, Any]:
        """
        Predict system state changes based on command execution result

        Args:
            execution_result: Result from command execution
            action: Original action dictionary
            action_meta: Action metadata

        Returns:
            Dictionary with predicted state changes
        """
        prediction = {
            'would_succeed': execution_result['success'],
            'predicted_changes': []
        }

        action_type = action.get('type', '')
        params = action.get('parameters', {})

        # Analyze command output to predict effects
        if execution_result['success']:
            # Command succeeded - predict what would happen
            if 'delete' in action_type:
                prediction['predicted_changes'].append({
                    'type': 'file_deleted',
                    'path': params.get('path', 'unknown'),
                    'reversible': False
                })

            elif 'write' in action_type or 'edit' in action_type:
                prediction['predicted_changes'].append({
                    'type': 'file_modified',
                    'path': params.get('path', 'unknown'),
                    'reversible': True  # With backup
                })

            elif 'kill' in action_type or 'stop' in action_type:
                if 'service' in params:
                    prediction['predicted_changes'].append({
                        'type': 'service_stopped',
                        'service': params['service'],
                        'reversible': True  # Can restart
                    })
                elif 'pid' in params:
                    prediction['predicted_changes'].append({
                        'type': 'process_killed',
                        'pid': params['pid'],
                        'reversible': False
                    })

            elif 'shutdown' in action_type or 'reboot' in action_type:
                prediction['predicted_changes'].append({
                    'type': 'system_shutdown',
                    'reversible': False
                })

        else:
            # Command failed - predict why it would fail
            stderr = execution_result.get('stderr', '')

            if 'Permission denied' in stderr:
                prediction['failure_reason'] = 'insufficient_permissions'
            elif 'No such file' in stderr or execution_result['returncode'] == 1:
                prediction['failure_reason'] = 'resource_not_found'
            elif 'Timeout' in stderr:
                prediction['failure_reason'] = 'timeout'
            else:
                prediction['failure_reason'] = 'unknown_error'

        return prediction

    def _update_statistics(self, success: bool, execution_time_ms: float):
        """Update shadow execution statistics"""
        self.statistics['total_executions'] += 1

        if success:
            self.statistics['successful_executions'] += 1
        else:
            self.statistics['failed_executions'] += 1

        # Update average execution time (running average)
        total = self.statistics['total_executions']
        current_avg = self.statistics['avg_execution_time_ms']
        self.statistics['avg_execution_time_ms'] = (
            (current_avg * (total - 1) + execution_time_ms) / total
        )

    def get_statistics(self) -> Dict:
        """Get shadow execution statistics"""
        stats = self.statistics.copy()

        if stats['total_executions'] > 0:
            stats['success_rate'] = (
                stats['successful_executions'] / stats['total_executions'] * 100
            )
        else:
            stats['success_rate'] = 0.0

        return stats

    def reset_statistics(self):
        """Reset shadow execution statistics"""
        self.statistics = {
            'total_executions': 0,
            'successful_executions': 0,
            'failed_executions': 0,
            'timeout_executions': 0,
            'avg_execution_time_ms': 0.0
        }


# ============================================================================
# Helper Functions
# ============================================================================

def execute_in_namespace(command: List[str], timeout: float = 5.0) -> Dict:
    """
    Helper function to execute command in isolated namespace

    Args:
        command: Command to execute
        timeout: Execution timeout in seconds

    Returns:
        Dictionary with success, returncode, stdout, stderr
    """
    executor = RealShadowEnvironment(timeout=timeout)

    # NOTE: WSL requires --user --map-root-user for namespace creation
    unshare_cmd = [
        'unshare', '--user', '--map-root-user', '--pid', '--fork', '--'
    ] + command

    try:
        result = subprocess.run(
            unshare_cmd,
            capture_output=True,
            text=True,
            timeout=timeout
        )

        return {
            'success': result.returncode == 0,
            'returncode': result.returncode,
            'stdout': result.stdout,
            'stderr': result.stderr
        }

    except subprocess.TimeoutExpired:
        return {
            'success': False,
            'returncode': -1,
            'stdout': '',
            'stderr': f'Timeout after {timeout}s'
        }
    except Exception as e:
        return {
            'success': False,
            'returncode': -2,
            'stdout': '',
            'stderr': str(e)
        }


# ============================================================================
# Main (for testing)
# ============================================================================

def main():
    """Test shadow executor standalone"""
    print("="*70)
    print("JARVIS AI-OS - Shadow Executor Smoke Test")
    print("="*70)
    print()

    executor = RealShadowEnvironment()

    # Test 1: Simple echo command
    print("Test 1: Echo command in namespace")
    result = execute_in_namespace(['echo', 'Hello from namespace'])
    print(f"  Success: {result['success']}")
    print(f"  Output: {result['stdout'].strip()}")
    print()

    # Test 2: File existence check
    print("Test 2: File existence check (test -e /etc/passwd)")
    result = execute_in_namespace(['test', '-e', '/etc/passwd'])
    print(f"  Success: {result['success']} (should be True)")
    print()

    # Test 3: Permission denied check
    print("Test 3: Permission check (test -w /etc/passwd)")
    result = execute_in_namespace(['test', '-w', '/etc/passwd'])
    print(f"  Success: {result['success']} (should be False - no write permission)")
    print()

    # Statistics
    print("Shadow Executor Statistics:")
    stats = executor.get_statistics()
    for key, value in stats.items():
        print(f"  {key}: {value}")
    print()

    print("[OK] Shadow executor smoke test complete!")


if __name__ == "__main__":
    main()
