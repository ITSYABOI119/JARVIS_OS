#!/usr/bin/env python3
"""
Week 22: Comprehensive Suspend/Resume Testing

Tests all suspend/resume functionality:
- SuspendManager component registration
- Agent serialize/deserialize (all 4 agents)
- SystemStateManager state transitions
- Full suspend/resume cycle
- Multiple consecutive cycles
- Timing validation (<5s suspend, <15s resume)
- State preservation verification
- Error handling

Author: JARVIS AI-OS Team
Date: December 2025
"""

import unittest
import sys
import time
import tempfile
import shutil
import json
from pathlib import Path
from typing import Dict, Any

# Add AI directory to path
sys.path.insert(0, str(Path(__file__).parent))

from suspend_manager import SuspendManager, SuspendState
from device_agent import DeviceAgent
from network_agent import NetworkAgent
from filesystem_agent import FileSystemAgent
from user_agent import UserAgent
from system_state_manager import SystemStateManager, SystemState


class TestSuspendManager(unittest.TestCase):
    """Test SuspendManager core functionality"""

    def setUp(self):
        """Set up test environment"""
        self.temp_dir = tempfile.mkdtemp()
        self.suspend_mgr = SuspendManager(state_dir=self.temp_dir)

    def tearDown(self):
        """Clean up test environment"""
        shutil.rmtree(self.temp_dir, ignore_errors=True)

    def test_initialization(self):
        """Test SuspendManager initialization"""
        self.assertEqual(self.suspend_mgr.state, SuspendState.RUNNING)
        self.assertEqual(len(self.suspend_mgr.components), 0)
        self.assertEqual(self.suspend_mgr.total_suspends, 0)
        self.assertEqual(self.suspend_mgr.total_resumes, 0)

    def test_component_registration(self):
        """Test component registration"""
        # Create mock component with serialize/deserialize
        class MockComponent:
            def serialize(self):
                return {'test': 'data'}

            def deserialize(self, state):
                pass

        component = MockComponent()

        # Register should succeed
        result = self.suspend_mgr.register_component("test", component)
        self.assertTrue(result)
        self.assertEqual(len(self.suspend_mgr.components), 1)
        self.assertIn("test", self.suspend_mgr.components)

    def test_component_registration_missing_methods(self):
        """Test component registration with missing methods"""
        class BadComponent:
            pass

        component = BadComponent()

        # Should fail (no serialize/deserialize)
        result = self.suspend_mgr.register_component("bad", component)
        self.assertFalse(result)
        self.assertEqual(len(self.suspend_mgr.components), 0)

    def test_unregister_component(self):
        """Test component unregistration"""
        class MockComponent:
            def serialize(self):
                return {}

            def deserialize(self, state):
                pass

        component = MockComponent()
        self.suspend_mgr.register_component("test", component)

        # Unregister should succeed
        result = self.suspend_mgr.unregister_component("test")
        self.assertTrue(result)
        self.assertEqual(len(self.suspend_mgr.components), 0)

        # Unregister non-existent should fail
        result = self.suspend_mgr.unregister_component("nonexistent")
        self.assertFalse(result)

    def test_get_metrics(self):
        """Test metrics retrieval"""
        metrics = self.suspend_mgr.get_metrics()

        self.assertIn('state', metrics)
        self.assertIn('components', metrics)
        self.assertIn('component_count', metrics)
        self.assertIn('total_suspends', metrics)
        self.assertIn('total_resumes', metrics)

        self.assertEqual(metrics['state'], 'running')
        self.assertEqual(metrics['component_count'], 0)


class TestAgentSerialization(unittest.TestCase):
    """Test agent serialize/deserialize methods"""

    def test_device_agent_serialize(self):
        """Test DeviceAgent serialization"""
        agent = DeviceAgent()

        # Set some state
        agent.total_queries = 100
        agent.successful_queries = 95
        agent.failed_queries = 5
        agent.total_response_time_ms = 5000.0
        agent.cache_hits = 80

        # Serialize
        state = agent.serialize()

        # Verify structure
        self.assertEqual(state['type'], 'device_agent')
        self.assertEqual(state['name'], 'device')
        self.assertIn('statistics', state)
        self.assertEqual(state['statistics']['total_queries'], 100)
        self.assertEqual(state['statistics']['successful_queries'], 95)
        self.assertEqual(state['statistics']['failed_queries'], 5)
        self.assertIn('timestamp', state)

    def test_device_agent_deserialize(self):
        """Test DeviceAgent deserialization"""
        agent = DeviceAgent()

        # Create state
        state = {
            'type': 'device_agent',
            'name': 'device',
            'status': 'ready',
            'statistics': {
                'total_queries': 123,
                'successful_queries': 100,
                'failed_queries': 23,
                'total_response_time_ms': 10000.0,
                'cache_hits': 90
            },
            'timestamp': time.time()
        }

        # Deserialize
        agent.deserialize(state)

        # Verify restoration
        self.assertEqual(agent.total_queries, 123)
        self.assertEqual(agent.successful_queries, 100)
        self.assertEqual(agent.failed_queries, 23)
        self.assertEqual(agent.total_response_time_ms, 10000.0)
        self.assertEqual(agent.cache_hits, 90)

    def test_device_agent_round_trip(self):
        """Test DeviceAgent serialize → deserialize round trip"""
        agent1 = DeviceAgent()
        agent1.total_queries = 42
        agent1.successful_queries = 40
        agent1.failed_queries = 2

        # Serialize
        state = agent1.serialize()

        # Deserialize into new agent
        agent2 = DeviceAgent()
        agent2.deserialize(state)

        # Verify match
        self.assertEqual(agent2.total_queries, 42)
        self.assertEqual(agent2.successful_queries, 40)
        self.assertEqual(agent2.failed_queries, 2)

    def test_all_agents_have_methods(self):
        """Test all 4 agents have serialize/deserialize"""
        agents = [
            DeviceAgent(),
            NetworkAgent(),
            FileSystemAgent(),
            UserAgent()
        ]

        for agent in agents:
            self.assertTrue(hasattr(agent, 'serialize'))
            self.assertTrue(hasattr(agent, 'deserialize'))
            self.assertTrue(callable(agent.serialize))
            self.assertTrue(callable(agent.deserialize))

    def test_all_agents_round_trip(self):
        """Test all agents can round-trip their state"""
        agents = [
            ("device", DeviceAgent()),
            ("network", NetworkAgent()),
            ("filesystem", FileSystemAgent()),
            ("user", UserAgent())
        ]

        for name, agent in agents:
            # Set state
            agent.total_queries = 50
            agent.successful_queries = 45

            # Round trip
            state = agent.serialize()
            new_agent = type(agent)()
            new_agent.deserialize(state)

            # Verify
            self.assertEqual(new_agent.total_queries, 50, f"{name} agent failed")
            self.assertEqual(new_agent.successful_queries, 45, f"{name} agent failed")


class TestSystemStateManager(unittest.TestCase):
    """Test SystemStateManager suspend/resume support"""

    def test_suspending_state_exists(self):
        """Test SUSPENDING state exists"""
        self.assertTrue(hasattr(SystemState, 'SUSPENDING'))
        self.assertEqual(SystemState.SUSPENDING.value, 'suspending')

    def test_resuming_state_exists(self):
        """Test RESUMING state exists"""
        self.assertTrue(hasattr(SystemState, 'RESUMING'))
        self.assertEqual(SystemState.RESUMING.value, 'resuming')

    def test_prepare_suspend(self):
        """Test prepare_suspend() method"""
        state_mgr = SystemStateManager(initial_state=SystemState.ACTIVE)

        result = state_mgr.prepare_suspend()
        self.assertTrue(result)
        self.assertEqual(state_mgr.current_state, SystemState.SUSPENDING)
        self.assertEqual(state_mgr.pre_suspend_state, SystemState.ACTIVE)

    def test_complete_resume(self):
        """Test complete_resume() method"""
        state_mgr = SystemStateManager(initial_state=SystemState.SUSPENDING)
        state_mgr.pre_suspend_state = SystemState.ACTIVE

        result = state_mgr.complete_resume()
        self.assertTrue(result)
        self.assertEqual(state_mgr.current_state, SystemState.ACTIVE)

    def test_serialize_deserialize(self):
        """Test SystemStateManager serialize/deserialize"""
        state_mgr = SystemStateManager(initial_state=SystemState.CRITICAL)
        state_mgr.total_transitions = 10

        # Serialize
        state = state_mgr.serialize()

        self.assertEqual(state['current_state'], 'critical')
        self.assertIn('statistics', state)
        self.assertEqual(state['statistics']['total_transitions'], 10)

        # Deserialize into new manager
        new_mgr = SystemStateManager()
        new_mgr.deserialize(state)

        self.assertEqual(new_mgr.current_state, SystemState.CRITICAL)
        self.assertEqual(new_mgr.total_transitions, 10)


class TestFullSuspendResume(unittest.TestCase):
    """Test full suspend/resume cycle"""

    def setUp(self):
        """Set up test environment"""
        self.temp_dir = tempfile.mkdtemp()
        self.suspend_mgr = SuspendManager(state_dir=self.temp_dir)

        # Create components
        self.device_agent = DeviceAgent()
        self.network_agent = NetworkAgent()
        self.fs_agent = FileSystemAgent()
        self.user_agent = UserAgent()
        self.state_mgr = SystemStateManager()

        # Register all components
        self.suspend_mgr.register_component("device_agent", self.device_agent)
        self.suspend_mgr.register_component("network_agent", self.network_agent)
        self.suspend_mgr.register_component("fs_agent", self.fs_agent)
        self.suspend_mgr.register_component("user_agent", self.user_agent)
        self.suspend_mgr.register_component("system_state", self.state_mgr)

    def tearDown(self):
        """Clean up test environment"""
        shutil.rmtree(self.temp_dir, ignore_errors=True)

    def test_basic_suspend_resume(self):
        """Test basic suspend and resume cycle"""
        # Set agent state
        self.device_agent.total_queries = 100
        self.device_agent.successful_queries = 95

        # Suspend
        start = time.time()
        result = self.suspend_mgr.suspend()
        suspend_time = time.time() - start

        self.assertTrue(result, "Suspend should succeed")
        self.assertEqual(self.suspend_mgr.state, SuspendState.SUSPENDED)
        self.assertLess(suspend_time, 5.0, f"Suspend took {suspend_time:.2f}s (target: <5s)")

        # Verify state files exist
        metadata_file = Path(self.temp_dir) / "metadata.json"
        self.assertTrue(metadata_file.exists())

        device_file = Path(self.temp_dir) / "device_agent_state.json"
        self.assertTrue(device_file.exists())

        # Modify agent state (simulate loss)
        self.device_agent.total_queries = 0
        self.device_agent.successful_queries = 0

        # Resume
        start = time.time()
        result = self.suspend_mgr.resume()
        resume_time = time.time() - start

        self.assertTrue(result, "Resume should succeed")
        self.assertEqual(self.suspend_mgr.state, SuspendState.RUNNING)
        self.assertLess(resume_time, 15.0, f"Resume took {resume_time:.2f}s (target: <15s)")

        # Verify agent state restored
        self.assertEqual(self.device_agent.total_queries, 100)
        self.assertEqual(self.device_agent.successful_queries, 95)

    def test_system_state_preservation(self):
        """Test system state manager state preserved"""
        # Set state to CRITICAL
        self.state_mgr.current_state = SystemState.CRITICAL
        self.state_mgr.total_transitions = 25

        # Suspend/Resume
        self.suspend_mgr.suspend()
        self.suspend_mgr.resume()

        # Verify state restored
        self.assertEqual(self.state_mgr.current_state, SystemState.CRITICAL)
        self.assertEqual(self.state_mgr.total_transitions, 25)

    def test_multiple_cycles(self):
        """Test stability over multiple suspend/resume cycles"""
        for i in range(5):
            # Modify state
            self.device_agent.total_queries = i * 10

            # Suspend/Resume
            self.suspend_mgr.suspend()
            time.sleep(0.05)  # Simulate suspend delay
            self.suspend_mgr.resume()

            # Verify state
            self.assertEqual(self.device_agent.total_queries, i * 10)

    def test_timing_targets(self):
        """Test suspend/resume timing meets targets"""
        # Set realistic state
        self.device_agent.total_queries = 1000
        self.network_agent.total_queries = 500
        self.fs_agent.total_queries = 750
        self.user_agent.total_queries = 250

        # Measure suspend
        start = time.time()
        self.suspend_mgr.suspend()
        suspend_time = time.time() - start

        # Measure resume
        start = time.time()
        self.suspend_mgr.resume()
        resume_time = time.time() - start

        # Verify timing
        self.assertLess(suspend_time, 5.0,
                       f"Suspend took {suspend_time:.2f}s (target: <5s)")
        self.assertLess(resume_time, 15.0,
                       f"Resume took {resume_time:.2f}s (target: <15s)")

        # Print timing for analysis
        print(f"\n✓ Suspend time: {suspend_time:.3f}s")
        print(f"✓ Resume time: {resume_time:.3f}s")

    def test_state_file_format(self):
        """Test state files are valid JSON"""
        self.suspend_mgr.suspend()

        # Check all state files are valid JSON
        for state_file in Path(self.temp_dir).glob("*.json"):
            with open(state_file, 'r') as f:
                try:
                    data = json.load(f)
                    self.assertIsInstance(data, dict)
                except json.JSONDecodeError as e:
                    self.fail(f"Invalid JSON in {state_file}: {e}")

    def test_error_handling_missing_state(self):
        """Test error handling when state files missing"""
        # Try to resume without suspend
        result = self.suspend_mgr.resume()

        # Should fail gracefully
        self.assertFalse(result)
        self.assertEqual(self.suspend_mgr.state, SuspendState.FAILED)


class TestPerformance(unittest.TestCase):
    """Test performance characteristics"""

    def setUp(self):
        """Set up test environment"""
        self.temp_dir = tempfile.mkdtemp()
        self.suspend_mgr = SuspendManager(state_dir=self.temp_dir)

        # Create and register all components
        components = {
            "device": DeviceAgent(),
            "network": NetworkAgent(),
            "filesystem": FileSystemAgent(),
            "user": UserAgent(),
            "state": SystemStateManager()
        }

        for name, component in components.items():
            self.suspend_mgr.register_component(name, component)

    def tearDown(self):
        """Clean up test environment"""
        shutil.rmtree(self.temp_dir, ignore_errors=True)

    def test_state_size(self):
        """Test saved state size is reasonable"""
        self.suspend_mgr.suspend()

        total_size = self.suspend_mgr.get_state_size()

        # Should be less than 10MB (target from plan)
        max_size = 10 * 1024 * 1024  # 10MB in bytes
        self.assertLess(total_size, max_size,
                       f"State size {total_size/1024:.1f} KB exceeds 10MB target")

        # Print size for analysis
        print(f"\n✓ State size: {total_size / 1024:.1f} KB (target: <10 MB)")


def run_all_tests():
    """Run all test suites and print summary"""
    print("=" * 70)
    print("Week 22: Comprehensive Suspend/Resume Test Suite")
    print("=" * 70)
    print()

    # Create test suite
    loader = unittest.TestLoader()
    suite = unittest.TestSuite()

    # Add all test classes
    suite.addTests(loader.loadTestsFromTestCase(TestSuspendManager))
    suite.addTests(loader.loadTestsFromTestCase(TestAgentSerialization))
    suite.addTests(loader.loadTestsFromTestCase(TestSystemStateManager))
    suite.addTests(loader.loadTestsFromTestCase(TestFullSuspendResume))
    suite.addTests(loader.loadTestsFromTestCase(TestPerformance))

    # Run tests
    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(suite)

    # Print summary
    print()
    print("=" * 70)
    print("Test Summary")
    print("=" * 70)
    print(f"Tests run: {result.testsRun}")
    print(f"Successes: {result.testsRun - len(result.failures) - len(result.errors)}")
    print(f"Failures: {len(result.failures)}")
    print(f"Errors: {len(result.errors)}")
    print()

    if result.wasSuccessful():
        print("✅ ALL TESTS PASSED")
    else:
        print("❌ SOME TESTS FAILED")

    return 0 if result.wasSuccessful() else 1


if __name__ == '__main__':
    sys.exit(run_all_tests())
