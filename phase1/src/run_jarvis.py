#!/usr/bin/env python3
"""
JARVIS AI-OS - Phase 1 Integrated System Launcher
Weeks 1-17: Complete System with All Features

This script launches JARVIS with all integrated features:
- Week 1-4: Decision cache + IPC
- Week 5-9: AI agent (Phi-3 Mini) + Shell interface
- Week 11: Multi-agent architecture (4 specialist agents)
- Week 12: Health monitoring & failover
- Week 13-14: Dynamic model scaling (TinyLlama/Phi-3)
- Week 15: Optimized transitions & inference
- Week 16: SHIELD safety framework (100 action types)
- Week 17: Shadow execution + snapshots + rollback

Author: JARVIS AI-OS Team
Date: November 25, 2025
"""

import sys
import os
import argparse
from pathlib import Path

# Add AI directory to path
sys.path.insert(0, str(Path(__file__).parent / "ai"))

def check_dependencies():
    """Check if all required dependencies are available"""
    print("=" * 70)
    print("JARVIS AI-OS - Dependency Check")
    print("=" * 70)

    missing = []

    # Check Python packages
    try:
        import llama_cpp
        print("✅ llama-cpp-python: Available")
    except ImportError:
        print("❌ llama-cpp-python: Missing")
        missing.append("llama-cpp-python")

    try:
        import psutil
        print("✅ psutil: Available")
    except ImportError:
        print("❌ psutil: Missing")
        missing.append("psutil")

    # Check models (try multiple locations)
    models_dir_local = Path(__file__).parent / "models"
    models_dir_root = Path(__file__).parent.parent.parent / "models"

    # TinyLlama
    tinyllama_candidates = [
        models_dir_local / "tinyllama-1.1b-chat-v1.0.Q4_K_M.gguf",
        models_dir_root / "tinyllama-1.1b-chat-v1.0.Q4_K_M.gguf",
    ]
    tinyllama_path = next((p for p in tinyllama_candidates if p.exists()), tinyllama_candidates[0])

    # Phi-3
    phi3_candidates = [
        models_dir_local / "Phi-3-mini-4k-instruct-q4.gguf",
        models_dir_root / "Phi-3-mini-4k-instruct-q4.gguf",
    ]
    phi3_path = next((p for p in phi3_candidates if p.exists()), phi3_candidates[0])

    if tinyllama_path.exists():
        print(f"✅ TinyLlama model: {tinyllama_path}")
    else:
        print(f"❌ TinyLlama model: Not found at {tinyllama_path}")
        missing.append("TinyLlama model")

    if phi3_path.exists():
        print(f"✅ Phi-3 model: {phi3_path}")
    else:
        print(f"❌ Phi-3 model: Not found at {phi3_path}")
        missing.append("Phi-3 model")

    # Check unshare (for shadow execution)
    import subprocess
    try:
        result = subprocess.run(['unshare', '--help'], capture_output=True, timeout=1)
        if result.returncode == 0:
            print("✅ unshare: Available (shadow execution enabled)")
        else:
            print("⚠️  unshare: Available but may have issues")
    except:
        print("⚠️  unshare: Not available (shadow execution will be simulated)")

    print()

    if missing:
        print(f"❌ Missing dependencies: {', '.join(missing)}")
        print()
        print("Install missing packages:")
        print("  pip install llama-cpp-python psutil")
        print()
        print("Download models:")
        print("  TinyLlama: https://huggingface.co/TheBloke/TinyLlama-1.1B-Chat-v1.0-GGUF")
        print("  Phi-3: https://huggingface.co/microsoft/Phi-3-mini-4k-instruct-gguf")
        print()
        return False

    print("✅ All dependencies available!")
    print()
    return True


def launch_interactive_shell(enable_ai=True, enable_shield=True, enable_snapshots=True):
    """Launch JARVIS interactive shell with all features"""
    print("=" * 70)
    print("JARVIS AI-OS - Interactive Shell")
    print("=" * 70)
    print()
    print("Features enabled:")
    print(f"  AI Agent (Phi-3 Mini): {'✅' if enable_ai else '❌'}")
    print(f"  SHIELD Safety: {'✅' if enable_shield else '❌'}")
    print(f"  Shadow Execution: {'✅' if enable_shield else '❌'}")
    print(f"  Snapshots & Rollback: {'✅' if enable_snapshots else '❌'}")
    print(f"  Suspend/Resume: {'✅' if enable_ai else '❌'}")
    print()

    # Import components
    from shell.shell import JARVISShell
    from ai.agent import JARVISAgent
    from ai.agent_router import AgentRouter
    from ai.agent_health import AgentHealthMonitor
    from ai.model_loader import ModelLoader
    from ai.system_state_manager import SystemStateManager
    from ai.shield_framework import SHIELDFramework
    from ai.snapshot_manager import EnhancedRollbackManager
    from ai.suspend_manager import SuspendManager

    # Initialize components to None
    model_loader = None
    state_manager = None
    shield = None
    snapshot_manager = None
    agent_router = None
    health_monitor = None
    suspend_manager = None

    if enable_ai:
        print("Initializing AI components...")
        model_loader = ModelLoader()

        if enable_snapshots:
            snapshot_manager = EnhancedRollbackManager(
                max_memory_snapshots=5,
                max_disk_snapshots=20
            )

        state_manager = SystemStateManager(
            model_loader=model_loader,
            snapshot_manager=snapshot_manager
        )

        # Start in IDLE state (TinyLlama)
        print("Loading TinyLlama 1.1B (IDLE state)...")
        from ai.system_state_manager import SystemState
        state_manager.transition_to(SystemState.IDLE, trigger="startup")
        print("✅ TinyLlama loaded")
        print()

    if enable_shield:
        print("Initializing SHIELD safety framework...")
        shield = SHIELDFramework()
        print("✅ SHIELD initialized (100 action types)")
        print()

    # Initialize agent router (Week 11)
    if enable_ai:
        print("Initializing multi-agent router...")
        agent_router = AgentRouter()
        print("✅ Agent router initialized (4 specialist agents)")
        print()

        # Initialize health monitor (Week 12)
        print("Initializing health monitor...")
        # Health monitor needs agent list - use router's agents
        agent_list = list(agent_router.agents.values())
        health_monitor = AgentHealthMonitor(agent_list)
        print("✅ Health monitor initialized")
        print()

        # Initialize suspend manager (Week 22)
        print("Initializing suspend manager...")
        suspend_manager = SuspendManager()

        # Register components that support suspend/resume
        if state_manager:
            suspend_manager.register_component("state_manager", state_manager)

        # Register agents from router
        if agent_router:
            for agent_name, agent in agent_router.agents.items():
                suspend_manager.register_component(agent_name, agent)

        print("✅ Suspend manager initialized")
        print(f"   Registered components: state_manager + 4 agents")
        print()

    # Launch shell with ALL components
    print("Starting JARVIS shell...")
    print("Type 'help' for available commands")
    print("Type 'exit' to quit")
    print()

    shell = JARVISShell(
        enable_ai=enable_ai,
        auto_load_model=False,  # Model already loaded in state_manager
        agent_router=agent_router if enable_ai else None,
        health_monitor=health_monitor if enable_ai else None,
        state_manager=state_manager if enable_ai else None,
        shield=shield if enable_shield else None,
        snapshot_manager=snapshot_manager if enable_snapshots else None,
        suspend_manager=suspend_manager if enable_ai else None
    )

    shell.start()


def run_demo_script():
    """Run a demonstration script showing all features"""
    print("=" * 70)
    print("JARVIS AI-OS - Feature Demonstration")
    print("=" * 70)
    print()

    from ai.model_loader import ModelLoader
    from ai.system_state_manager import SystemStateManager, SystemState
    from ai.shield_framework import SHIELDFramework, SystemSnapshot
    from ai.snapshot_manager import EnhancedRollbackManager
    from ai.shadow_executor import RealShadowEnvironment

    # 1. Dynamic Model Scaling
    print("1. Dynamic Model Scaling (Weeks 13-15)")
    print("-" * 70)
    model_loader = ModelLoader()
    snapshot_manager = EnhancedRollbackManager()
    state_manager = SystemStateManager(model_loader=model_loader, snapshot_manager=snapshot_manager)

    print(f"Initial state: {state_manager.get_current_state()}")
    print(f"Memory snapshots: {len(snapshot_manager.memory_snapshots)}")
    print()

    print("Transitioning to CRITICAL (will create snapshot)...")
    state_manager.transition_to(SystemState.CRITICAL, trigger="demo_risky_operation")
    print(f"State: {state_manager.get_current_state()}")
    print(f"Memory snapshots: {len(snapshot_manager.memory_snapshots)}")
    print()

    # 2. SHIELD Safety Framework
    print("2. SHIELD Safety Framework (Week 16)")
    print("-" * 70)
    shield = SHIELDFramework()

    system_state = SystemSnapshot(
        timestamp=0,
        cpu_usage=50.0,
        memory_usage=60.0,
        disk_usage=70.0,
        network_active=True,
        file_count=1000,
        service_states={}
    )

    test_actions = [
        {'type': 'file_read', 'parameters': {'path': '/etc/passwd'}},
        {'type': 'file_write', 'parameters': {'path': '/tmp/test.txt'}},
        {'type': 'file_delete', 'parameters': {'path': '/home/user/important.txt'}},
    ]

    for action in test_actions:
        result = shield.validate_action(action, system_state)
        print(f"Action: {action['type']:20} Risk: {result['adjusted_risk']:.2f}  Mode: {result['execution_mode']}")
        if result.get('shadow_result'):
            shadow = result['shadow_result']
            print(f"  → Shadow: {'✅' if shadow['success'] else '❌'} (isolated: {shadow.get('isolated')}, {shadow.get('execution_time_ms', 0):.1f}ms)")
    print()

    # 3. Shadow Execution
    print("3. Shadow Execution (Week 17)")
    print("-" * 70)
    from ai.shield_action_db import ACTION_DATABASE

    shadow_env = RealShadowEnvironment(timeout=5.0)

    test_shadow_actions = [
        {'type': 'file_read', 'parameters': {'path': '/etc/passwd'}},
        {'type': 'process_list', 'parameters': {}},
    ]

    for action in test_shadow_actions:
        action_meta = ACTION_DATABASE.get(action['type'])
        if action_meta:
            result = shadow_env.execute_shadow_real(action, action_meta)
            print(f"Action: {action['type']:20} Success: {'✅' if result.success else '❌'}  "
                  f"Time: {result.execution_time_ms:.1f}ms  Isolated: {result.namespace_isolated}")
    print()

    # 4. Snapshot & Rollback
    print("4. Snapshot & Rollback (Week 17)")
    print("-" * 70)
    print(f"Total snapshots: {len(snapshot_manager.memory_snapshots)} memory, "
          f"{len(list(snapshot_manager.disk_snapshot_dir.glob('*.json')))} disk")

    if snapshot_manager.memory_snapshots:
        latest = snapshot_manager.memory_snapshots[-1]
        print(f"Latest snapshot: {latest.snapshot_id}")
        print(f"  State: {latest.ai_model_state}")
        print(f"  Trigger: {latest.snapshot_trigger}")
        print(f"  Time: {latest.timestamp}")

    print()

    # Statistics
    print("=" * 70)
    print("System Statistics")
    print("=" * 70)

    state_stats = state_manager.get_statistics()
    print(f"State Manager:")
    print(f"  Current state: {state_stats['current_state']}")
    print(f"  Total transitions: {state_stats['total_transitions']}")
    print(f"  Avg transition time: {state_stats.get('avg_transition_time', 0):.2f}s")
    print()

    shield_stats = shield.get_stats()
    print(f"SHIELD Framework:")
    print(f"  Total validations: {shield_stats['total_validations']}")
    print(f"  Blocked rate: {shield_stats['blocked_rate']*100:.1f}%")
    print(f"  Shadow tested rate: {shield_stats['shadow_rate']*100:.1f}%")
    print(f"  Automatic rate: {shield_stats['automatic_rate']*100:.1f}%")
    print()

    snapshot_stats = snapshot_manager.get_statistics()
    print(f"Snapshot Manager:")
    print(f"  Total snapshots: {snapshot_stats['total_snapshots']}")
    print(f"  Rollbacks executed: {snapshot_stats['rollbacks_executed']}")
    print(f"  Avg snapshot time: {snapshot_stats['avg_snapshot_time_ms']:.1f}ms")
    print(f"  Avg rollback time: {snapshot_stats['avg_rollback_time_ms']:.1f}ms")
    print()

    print("✅ Demo complete!")


def main():
    parser = argparse.ArgumentParser(
        description='JARVIS AI-OS Launcher - Phase 1 with Weeks 1-17 Features',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python run_jarvis.py                    # Full system with all features
  python run_jarvis.py --demo             # Run feature demonstration
  python run_jarvis.py --no-ai            # Shell only (no AI)
  python run_jarvis.py --no-shield        # Disable SHIELD safety
  python run_jarvis.py --check-deps       # Check dependencies only
        """
    )

    parser.add_argument('--demo', action='store_true',
                       help='Run feature demonstration instead of interactive shell')
    parser.add_argument('--no-ai', action='store_true',
                       help='Disable AI agent (shell only)')
    parser.add_argument('--no-shield', action='store_true',
                       help='Disable SHIELD safety framework')
    parser.add_argument('--no-snapshots', action='store_true',
                       help='Disable snapshot/rollback system')
    parser.add_argument('--check-deps', action='store_true',
                       help='Check dependencies and exit')

    args = parser.parse_args()

    # Check dependencies
    if not check_dependencies():
        if not args.check_deps:
            print("❌ Cannot start JARVIS due to missing dependencies")
            sys.exit(1)
        else:
            sys.exit(1)

    if args.check_deps:
        print("✅ All checks passed!")
        sys.exit(0)

    # Run demo or interactive shell
    try:
        if args.demo:
            run_demo_script()
        else:
            launch_interactive_shell(
                enable_ai=not args.no_ai,
                enable_shield=not args.no_shield,
                enable_snapshots=not args.no_snapshots
            )
    except KeyboardInterrupt:
        print("\n\n👋 JARVIS shutting down...")
    except Exception as e:
        print(f"\n❌ Error: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)


if __name__ == "__main__":
    main()
