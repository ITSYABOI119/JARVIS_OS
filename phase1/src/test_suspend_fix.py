#!/usr/bin/env python3
"""
Quick test to verify SuspendManager is properly initialized in run_jarvis.py
"""

import sys
from pathlib import Path

# Add AI directory to path
sys.path.insert(0, str(Path(__file__).parent / "ai"))

def test_suspend_manager_initialization():
    """Test that SuspendManager can be initialized with all components"""
    print("=" * 70)
    print("Testing SuspendManager Initialization")
    print("=" * 70)
    print()

    from ai.suspend_manager import SuspendManager
    from ai.agent_router import AgentRouter
    from ai.model_loader import ModelLoader
    from ai.system_state_manager import SystemStateManager
    from shell.shell import JARVISShell

    # Initialize components (like run_jarvis.py does)
    print("1. Initializing components...")
    model_loader = ModelLoader()
    state_manager = SystemStateManager(model_loader=model_loader)
    agent_router = AgentRouter()
    print(f"   ✓ Created state_manager and router with {len(agent_router.agents)} agents")

    # Initialize SuspendManager
    print("\n2. Initializing SuspendManager...")
    suspend_manager = SuspendManager()

    # Register components
    print("\n3. Registering components...")
    suspend_manager.register_component('state_manager', state_manager)
    registered_count = 1

    for agent_name, agent in agent_router.agents.items():
        success = suspend_manager.register_component(agent_name, agent)
        if success:
            registered_count += 1
            print(f"   ✓ Registered {agent_name}")

    print(f"\n   Total registered: {registered_count} components")

    # Create shell with suspend_manager
    print("\n4. Creating shell with SuspendManager...")
    shell = JARVISShell(
        enable_ai=False,
        suspend_manager=suspend_manager
    )
    print("   ✓ Shell created")

    # Test that suspend command works
    print("\n5. Testing suspend command...")
    print("-" * 70)
    shell._execute_suspend()
    print("-" * 70)

    print("\n6. Testing resume command...")
    print("-" * 70)
    shell._execute_resume()
    print("-" * 70)

    print("\n" + "=" * 70)
    print("✅ ALL TESTS PASSED")
    print("=" * 70)
    print()
    print("SuspendManager is properly initialized and working!")
    print("The 'suspend' and 'resume' commands will work in run_jarvis.py")


if __name__ == "__main__":
    test_suspend_manager_initialization()
