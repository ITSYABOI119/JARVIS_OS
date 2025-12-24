#!/usr/bin/env python3
"""
JARVIS AI-OS - SystemBootstrap Unit Tests
Phase 2 Week 29

Comprehensive test suite for the SystemBootstrap class:
- 10 tests for startup sequence
- 8 tests for error handling
- 5 tests for graceful degradation

Total: 23 unit tests

Author: JARVIS Development Team
Date: December 2025
"""

import unittest
import sys
from pathlib import Path
from unittest.mock import Mock, patch, MagicMock

# Add phase2/src/ai to path
sys.path.insert(0, str(Path(__file__).parent))
# Add phase1/src/ai to path for mocking
sys.path.insert(0, str(Path(__file__).parent.parent.parent.parent / "phase1" / "src" / "ai"))

from system_bootstrap import SystemBootstrap, BootstrapError


class TestStartupSequence(unittest.TestCase):
    """Test startup sequence (10 tests)"""

    def setUp(self):
        """Set up test fixtures"""
        self.bootstrap = SystemBootstrap(config={
            'enable_ai': True,
            'enable_shield': True,
            'enable_snapshots': True
        })

    @patch('system_bootstrap.IPCClient', create=True)
    @patch('system_bootstrap.ModelLoader', create=True)
    @patch('system_bootstrap.EnhancedRollbackManager', create=True)
    @patch('system_bootstrap.SystemStateManager', create=True)
    @patch('system_bootstrap.SHIELDFramework', create=True)
    @patch('system_bootstrap.AgentRouter', create=True)
    @patch('system_bootstrap.AgentHealthMonitor', create=True)
    @patch('system_bootstrap.SuspendManager', create=True)
    def test_01_components_initialize_in_order(self, *mocks):
        """Test components initialize in correct dependency order"""
        # Mock all imports at module level
        with patch.dict('sys.modules', {
            'ipc_client': MagicMock(),
            'model_loader': MagicMock(),
            'snapshot_manager': MagicMock(),
            'system_state_manager': MagicMock(),
            'shield_framework': MagicMock(),
            'agent_router': MagicMock(),
            'agent_health': MagicMock(),
            'suspend_manager': MagicMock(),
        }):
            # Track initialization order
            init_order = []

            def track_ipc(*args, **kwargs):
                init_order.append('ipc_client')
                mock = Mock()
                mock.connect.return_value = True
                return mock

            def track_model(*args, **kwargs):
                init_order.append('model_loader')
                return Mock()

            def track_snapshot(*args, **kwargs):
                init_order.append('snapshot_manager')
                return Mock()

            def track_state(*args, **kwargs):
                init_order.append('state_manager')
                mock = Mock()
                mock.transition_to = Mock()
                return mock

            def track_shield(*args, **kwargs):
                init_order.append('shield')
                return Mock()

            def track_router(*args, **kwargs):
                init_order.append('agent_router')
                mock = Mock()
                mock.agents = {'device': Mock(), 'network': Mock()}
                return mock

            def track_health(*args, **kwargs):
                init_order.append('health_monitor')
                return Mock()

            def track_suspend(*args, **kwargs):
                init_order.append('suspend_manager')
                mock = Mock()
                mock.register_component = Mock()
                return mock

            sys.modules['ipc_client'].IPCClient = track_ipc
            sys.modules['model_loader'].ModelLoader = track_model
            sys.modules['snapshot_manager'].EnhancedRollbackManager = track_snapshot
            sys.modules['system_state_manager'].SystemStateManager = track_state
            sys.modules['system_state_manager'].SystemState = Mock()
            sys.modules['system_state_manager'].SystemState.IDLE = 'IDLE'
            sys.modules['shield_framework'].SHIELDFramework = track_shield
            sys.modules['agent_router'].AgentRouter = track_router
            sys.modules['agent_health'].AgentHealthMonitor = track_health
            sys.modules['suspend_manager'].SuspendManager = track_suspend

            bootstrap = SystemBootstrap()
            components = bootstrap.initialize_all()

            # Verify order
            expected_order = [
                'ipc_client', 'model_loader', 'snapshot_manager', 'state_manager',
                'shield', 'agent_router', 'health_monitor', 'suspend_manager'
            ]
            self.assertEqual(init_order, expected_order)

    def test_02_ipc_client_graceful_fallback(self):
        """Test IPC client fails gracefully when seL4 unavailable"""
        with patch.dict('sys.modules', {'ipc_client': MagicMock()}):
            mock_client = Mock()
            mock_client.connect.return_value = False
            sys.modules['ipc_client'].IPCClient = Mock(return_value=mock_client)

            result = self.bootstrap._init_ipc_client()

            self.assertIsNone(result)
            self.assertIn('ipc_client', self.bootstrap.components)
            self.assertIsNone(self.bootstrap.components['ipc_client'])
            self.assertTrue(len(self.bootstrap.warnings) > 0)

    def test_03_ipc_client_import_error_graceful(self):
        """Test IPC client import error handled gracefully"""
        # Mock the import to raise ImportError
        original_import = __builtins__.__import__ if hasattr(__builtins__, '__import__') else __import__

        def mock_import(name, *args, **kwargs):
            if name == 'ipc_client':
                raise ImportError("No module named 'ipc_client'")
            return original_import(name, *args, **kwargs)

        # Clear cached module
        saved_module = sys.modules.pop('ipc_client', None)

        try:
            with patch('builtins.__import__', side_effect=mock_import):
                bootstrap = SystemBootstrap()
                result = bootstrap._init_ipc_client()

                self.assertIsNone(result)
                self.assertIsNone(bootstrap.components.get('ipc_client'))
        finally:
            # Restore module if it was there
            if saved_module:
                sys.modules['ipc_client'] = saved_module

    def test_04_model_loader_failure_exception(self):
        """Test model loader exception raises BootstrapError (FATAL)"""
        # Test that an exception during model loader init is FATAL
        # (Different from test_02 in TestErrorHandling which mocks the class itself)
        with patch.dict('sys.modules', {'model_loader': MagicMock()}):
            # Make ModelLoader raise when instantiated
            mock_loader = Mock(side_effect=RuntimeError("Model initialization failed"))
            sys.modules['model_loader'].ModelLoader = mock_loader

            with self.assertRaises(BootstrapError):
                self.bootstrap._init_model_loader()

            self.assertTrue(len(self.bootstrap.fatal_errors) > 0)

    def test_05_state_manager_depends_on_model_loader(self):
        """Test state manager requires model_loader"""
        # No model_loader in components
        self.bootstrap.components['model_loader'] = None

        with self.assertRaises(BootstrapError) as context:
            self.bootstrap._init_state_manager()

        self.assertIn('Model loader required', str(context.exception))

    def test_06_get_component_returns_correct_instance(self):
        """Test get_component() returns correct instances"""
        mock_shield = Mock()
        self.bootstrap.components['shield'] = mock_shield
        self.bootstrap.components['ipc_client'] = None

        self.assertEqual(self.bootstrap.get_component('shield'), mock_shield)
        self.assertIsNone(self.bootstrap.get_component('ipc_client'))
        self.assertIsNone(self.bootstrap.get_component('nonexistent'))

    def test_07_get_status_shows_initialization_state(self):
        """Test get_status() shows complete initialization state"""
        self.bootstrap.components = {
            'ipc_client': None,
            'model_loader': Mock(),
            'snapshot_manager': Mock(),
            'state_manager': Mock(),
            'shield': None,
            'agent_router': Mock(),
            'health_monitor': Mock(),
            'suspend_manager': None
        }
        self.bootstrap.warnings = ['IPC unavailable', 'SHIELD failed']

        status = self.bootstrap.get_status()

        self.assertEqual(status['total_components'], 8)
        self.assertEqual(status['initialized_count'], 5)  # 5 non-None
        self.assertEqual(len(status['warnings']), 2)
        self.assertEqual(status['components']['ipc_client'], 'unavailable')
        self.assertEqual(status['components']['model_loader'], 'initialized')

    def test_08_config_flags_respected(self):
        """Test configuration flags are respected"""
        bootstrap_no_ai = SystemBootstrap(config={'enable_ai': False})
        self.assertFalse(bootstrap_no_ai.enable_ai)

        bootstrap_no_shield = SystemBootstrap(config={'enable_shield': False})
        self.assertFalse(bootstrap_no_shield.enable_shield)

        bootstrap_no_snapshots = SystemBootstrap(config={'enable_snapshots': False})
        self.assertFalse(bootstrap_no_snapshots.enable_snapshots)

    def test_09_shutdown_all_calls_shutdown_methods(self):
        """Test shutdown_all() calls shutdown on components that support it"""
        mock_state = Mock()
        mock_state.shutdown = Mock()
        mock_router = Mock()
        mock_router.shutdown = Mock()
        mock_no_shutdown = Mock(spec=[])  # No shutdown method

        self.bootstrap.components = {
            'state_manager': mock_state,
            'agent_router': mock_router,
            'shield': mock_no_shutdown,
            'ipc_client': None
        }

        self.bootstrap.shutdown_all()

        mock_state.shutdown.assert_called_once()
        mock_router.shutdown.assert_called_once()

    def test_10_default_config_values(self):
        """Test default configuration values"""
        bootstrap = SystemBootstrap()  # No config

        self.assertTrue(bootstrap.enable_ai)
        self.assertTrue(bootstrap.enable_shield)
        self.assertTrue(bootstrap.enable_snapshots)


class TestErrorHandling(unittest.TestCase):
    """Test error handling (8 tests)"""

    def setUp(self):
        """Set up test fixtures"""
        self.bootstrap = SystemBootstrap()

    def test_01_ipc_client_failure_is_warning(self):
        """Test IPC client failure is WARNING, not FATAL"""
        with patch.dict('sys.modules', {'ipc_client': MagicMock()}):
            sys.modules['ipc_client'].IPCClient = Mock(side_effect=Exception("Connection failed"))

            # Should NOT raise, just return None
            result = self.bootstrap._init_ipc_client()

            self.assertIsNone(result)
            self.assertTrue(len(self.bootstrap.warnings) > 0)
            self.assertEqual(len(self.bootstrap.fatal_errors), 0)

    def test_02_model_loader_failure_is_fatal(self):
        """Test model loader failure raises BootstrapError (FATAL)"""
        with patch.dict('sys.modules', {'model_loader': MagicMock()}):
            sys.modules['model_loader'].ModelLoader = Mock(side_effect=Exception("Model load failed"))

            with self.assertRaises(BootstrapError):
                self.bootstrap._init_model_loader()

            self.assertTrue(len(self.bootstrap.fatal_errors) > 0)

    def test_03_snapshot_manager_failure_is_warning(self):
        """Test snapshot manager failure is WARNING, not FATAL"""
        with patch.dict('sys.modules', {'snapshot_manager': MagicMock()}):
            sys.modules['snapshot_manager'].EnhancedRollbackManager = Mock(
                side_effect=Exception("Snapshot init failed")
            )

            result = self.bootstrap._init_snapshot_manager()

            self.assertIsNone(result)
            self.assertTrue(len(self.bootstrap.warnings) > 0)

    def test_04_state_manager_failure_is_fatal(self):
        """Test state manager failure raises BootstrapError (FATAL)"""
        self.bootstrap.components['model_loader'] = Mock()
        self.bootstrap.components['snapshot_manager'] = Mock()

        with patch.dict('sys.modules', {'system_state_manager': MagicMock()}):
            sys.modules['system_state_manager'].SystemStateManager = Mock(
                side_effect=Exception("State manager failed")
            )
            sys.modules['system_state_manager'].SystemState = Mock()

            with self.assertRaises(BootstrapError):
                self.bootstrap._init_state_manager()

    def test_05_shield_failure_is_warning(self):
        """Test SHIELD failure is WARNING, not FATAL"""
        with patch.dict('sys.modules', {'shield_framework': MagicMock()}):
            sys.modules['shield_framework'].SHIELDFramework = Mock(
                side_effect=Exception("SHIELD init failed")
            )

            result = self.bootstrap._init_shield()

            self.assertIsNone(result)
            self.assertTrue(len(self.bootstrap.warnings) > 0)

    def test_06_agent_router_failure_is_fatal(self):
        """Test agent router failure raises BootstrapError (FATAL)"""
        with patch.dict('sys.modules', {'agent_router': MagicMock()}):
            sys.modules['agent_router'].AgentRouter = Mock(
                side_effect=Exception("Router init failed")
            )

            with self.assertRaises(BootstrapError):
                self.bootstrap._init_agent_router()

    def test_07_health_monitor_failure_is_warning(self):
        """Test health monitor failure is WARNING, not FATAL"""
        mock_router = Mock()
        mock_router.agents = {'device': Mock()}
        self.bootstrap.components['agent_router'] = mock_router

        with patch.dict('sys.modules', {'agent_health': MagicMock()}):
            sys.modules['agent_health'].AgentHealthMonitor = Mock(
                side_effect=Exception("Health monitor failed")
            )

            result = self.bootstrap._init_health_monitor()

            self.assertIsNone(result)
            self.assertTrue(len(self.bootstrap.warnings) > 0)

    def test_08_suspend_manager_failure_is_warning(self):
        """Test suspend manager failure is WARNING, not FATAL"""
        with patch.dict('sys.modules', {'suspend_manager': MagicMock()}):
            sys.modules['suspend_manager'].SuspendManager = Mock(
                side_effect=Exception("Suspend manager failed")
            )

            result = self.bootstrap._init_suspend_manager()

            self.assertIsNone(result)
            self.assertTrue(len(self.bootstrap.warnings) > 0)


class TestGracefulDegradation(unittest.TestCase):
    """Test graceful degradation (5 tests)"""

    def test_01_system_works_with_core_only(self):
        """Test system works with only 3 FATAL components"""
        bootstrap = SystemBootstrap()

        # Simulate minimal initialization (only FATAL components)
        bootstrap.components = {
            'ipc_client': None,  # Optional - unavailable
            'model_loader': Mock(),  # FATAL - required
            'snapshot_manager': None,  # Optional - unavailable
            'state_manager': Mock(),  # FATAL - required
            'shield': None,  # Optional - unavailable
            'agent_router': Mock(),  # FATAL - required
            'health_monitor': None,  # Optional - unavailable
            'suspend_manager': None  # Optional - unavailable
        }
        bootstrap.warnings = [
            'IPC client unavailable',
            'Snapshot manager unavailable',
            'SHIELD unavailable',
            'Health monitor unavailable',
            'Suspend manager unavailable'
        ]

        status = bootstrap.get_status()

        # Should have 3 initialized (core only)
        self.assertEqual(status['initialized_count'], 3)
        self.assertEqual(len(status['warnings']), 5)

        # Core components should be present
        self.assertIsNotNone(bootstrap.get_component('model_loader'))
        self.assertIsNotNone(bootstrap.get_component('state_manager'))
        self.assertIsNotNone(bootstrap.get_component('agent_router'))

    def test_02_system_works_with_six_components(self):
        """Test system works with 6/8 components (core + some optional)"""
        bootstrap = SystemBootstrap()

        bootstrap.components = {
            'ipc_client': Mock(),  # Available
            'model_loader': Mock(),
            'snapshot_manager': Mock(),  # Available
            'state_manager': Mock(),
            'shield': Mock(),  # Available
            'agent_router': Mock(),
            'health_monitor': None,  # Unavailable
            'suspend_manager': None  # Unavailable
        }
        bootstrap.warnings = ['Health monitor unavailable', 'Suspend manager unavailable']

        status = bootstrap.get_status()

        self.assertEqual(status['initialized_count'], 6)
        self.assertEqual(len(status['warnings']), 2)

    def test_03_system_works_with_all_components(self):
        """Test system works with 8/8 components (full system)"""
        bootstrap = SystemBootstrap()

        bootstrap.components = {
            'ipc_client': Mock(),
            'model_loader': Mock(),
            'snapshot_manager': Mock(),
            'state_manager': Mock(),
            'shield': Mock(),
            'agent_router': Mock(),
            'health_monitor': Mock(),
            'suspend_manager': Mock()
        }

        status = bootstrap.get_status()

        self.assertEqual(status['initialized_count'], 8)
        self.assertEqual(status['total_components'], 8)
        self.assertEqual(len(status['warnings']), 0)

    def test_04_status_report_shows_missing_components(self):
        """Test status report clearly shows missing components"""
        bootstrap = SystemBootstrap()

        bootstrap.components = {
            'ipc_client': None,
            'model_loader': Mock(),
            'snapshot_manager': None,
            'state_manager': Mock(),
            'shield': None,
            'agent_router': Mock(),
            'health_monitor': None,
            'suspend_manager': None
        }

        status = bootstrap.get_status()

        # Check component status
        self.assertEqual(status['components']['ipc_client'], 'unavailable')
        self.assertEqual(status['components']['model_loader'], 'initialized')
        self.assertEqual(status['components']['snapshot_manager'], 'unavailable')
        self.assertEqual(status['components']['state_manager'], 'initialized')
        self.assertEqual(status['components']['shield'], 'unavailable')
        self.assertEqual(status['components']['agent_router'], 'initialized')
        self.assertEqual(status['components']['health_monitor'], 'unavailable')
        self.assertEqual(status['components']['suspend_manager'], 'unavailable')

    def test_05_criticality_levels_correct(self):
        """Test FATAL vs WARNING component classification"""
        # Verify class-level constants
        self.assertIn('model_loader', SystemBootstrap.FATAL_COMPONENTS)
        self.assertIn('state_manager', SystemBootstrap.FATAL_COMPONENTS)
        self.assertIn('agent_router', SystemBootstrap.FATAL_COMPONENTS)

        self.assertIn('ipc_client', SystemBootstrap.WARNING_COMPONENTS)
        self.assertIn('snapshot_manager', SystemBootstrap.WARNING_COMPONENTS)
        self.assertIn('shield', SystemBootstrap.WARNING_COMPONENTS)
        self.assertIn('health_monitor', SystemBootstrap.WARNING_COMPONENTS)
        self.assertIn('suspend_manager', SystemBootstrap.WARNING_COMPONENTS)

        # Verify no overlap
        overlap = SystemBootstrap.FATAL_COMPONENTS & SystemBootstrap.WARNING_COMPONENTS
        self.assertEqual(len(overlap), 0)


class TestBootstrapError(unittest.TestCase):
    """Test BootstrapError exception"""

    def test_bootstrap_error_is_exception(self):
        """Test BootstrapError inherits from Exception"""
        self.assertTrue(issubclass(BootstrapError, Exception))

    def test_bootstrap_error_message(self):
        """Test BootstrapError carries message"""
        error = BootstrapError("Critical component failed")
        self.assertEqual(str(error), "Critical component failed")


def run_tests():
    """Run all tests and return results"""
    loader = unittest.TestLoader()
    suite = unittest.TestSuite()

    # Add test classes
    suite.addTests(loader.loadTestsFromTestCase(TestStartupSequence))
    suite.addTests(loader.loadTestsFromTestCase(TestErrorHandling))
    suite.addTests(loader.loadTestsFromTestCase(TestGracefulDegradation))
    suite.addTests(loader.loadTestsFromTestCase(TestBootstrapError))

    # Run with verbosity
    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(suite)

    # Print summary
    print()
    print("=" * 70)
    total = result.testsRun
    passed = total - len(result.failures) - len(result.errors)
    print(f"Test Results: {passed} passed, {len(result.failures)} failed, {len(result.errors)} errors")
    print("=" * 70)

    if result.wasSuccessful():
        print("ALL TESTS PASSED")
    else:
        print("SOME TESTS FAILED")
        if result.failures:
            print("\nFailures:")
            for test, traceback in result.failures:
                print(f"  - {test}: {traceback.split(chr(10))[0]}")
        if result.errors:
            print("\nErrors:")
            for test, traceback in result.errors:
                print(f"  - {test}: {traceback.split(chr(10))[0]}")

    return result


if __name__ == "__main__":
    result = run_tests()
    sys.exit(0 if result.wasSuccessful() else 1)
