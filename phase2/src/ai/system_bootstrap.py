#!/usr/bin/env python3
"""
JARVIS AI-OS - System Bootstrap Manager
Phase 2 Week 29

Coordinates initialization of all system managers in the correct order
with proper error handling and graceful degradation.

Initialization Sequence:
1. IPC Client         → Connect to seL4 (graceful fallback)
2. Model Loader       → Prepare AI model infrastructure
3. Snapshot Manager   → Enable rollback capability
4. System State Mgr   → Initialize in IDLE state, load TinyLlama
5. SHIELD Framework   → Load 100 action types
6. Agent Router       → Initialize 4 specialist agents
7. Health Monitor     → Start monitoring (10s heartbeat)
8. Suspend Manager    → Register all components

Error Handling:
- FATAL: Model Loader, State Manager, Agent Router (core components)
- WARNING: IPC Client, Snapshot Manager, SHIELD, Health Monitor, Suspend Manager (optional)

Author: JARVIS Development Team
Date: December 2025
"""

import sys
import os
from pathlib import Path
from typing import Optional, Dict, Any
import logging

# Add phase1/src/ai to path for imports
sys.path.insert(0, str(Path(__file__).parent.parent.parent.parent / "phase1" / "src" / "ai"))

# Logging setup
logging.basicConfig(level=logging.INFO, format='[%(levelname)s] %(message)s')
logger = logging.getLogger('system_bootstrap')


class BootstrapError(Exception):
    """Fatal error during system bootstrap (critical component failed)"""
    pass


class SystemBootstrap:
    """
    JARVIS AI-OS System Bootstrap Manager

    Coordinates initialization of all system managers in the correct order
    with proper error handling and graceful degradation.

    Graceful Degradation Philosophy:
    - Core AI functionality must work (FATAL if not)
    - Safety features preferred but optional (WARNING if missing)
    - Convenience features optional (WARNING if missing)
    """

    # Component criticality levels
    FATAL_COMPONENTS = {'model_loader', 'state_manager', 'agent_router'}
    WARNING_COMPONENTS = {'ipc_client', 'snapshot_manager', 'shield', 'health_monitor', 'suspend_manager'}

    def __init__(self, config: Optional[Dict[str, Any]] = None):
        """
        Initialize bootstrap manager

        Args:
            config (dict, optional): Configuration options
                - enable_ai: Enable AI components (default: True)
                - enable_shield: Enable SHIELD framework (default: True)
                - enable_snapshots: Enable snapshot/rollback (default: True)
        """
        self.config = config or {}
        self.components: Dict[str, Any] = {}
        self.warnings: list = []
        self.fatal_errors: list = []

        # Configuration flags
        self.enable_ai = self.config.get('enable_ai', True)
        self.enable_shield = self.config.get('enable_shield', True)
        self.enable_snapshots = self.config.get('enable_snapshots', True)

    def initialize_all(self) -> Dict[str, Any]:
        """
        Initialize all managers in correct order.

        Returns:
            dict: Initialized components {name: instance}

        Raises:
            BootstrapError: If critical component fails (FATAL)
        """
        print("\n" + "=" * 70)
        print("JARVIS AI-OS - System Bootstrap")
        print("=" * 70)
        print()
        print("[BOOTSTRAP] Initializing system components...")
        print()

        # Initialization sequence (order matters due to dependencies)
        try:
            # 1. IPC Client (optional - graceful fallback)
            self._init_ipc_client()

            # 2. Model Loader (FATAL if fails)
            if self.enable_ai:
                self._init_model_loader()
            else:
                print("[BOOTSTRAP] AI disabled, skipping model loader")
                self.components['model_loader'] = None

            # 3. Snapshot Manager (optional)
            if self.enable_snapshots:
                self._init_snapshot_manager()
            else:
                print("[BOOTSTRAP] Snapshots disabled, skipping snapshot manager")
                self.components['snapshot_manager'] = None

            # 4. System State Manager (FATAL if fails, depends on model_loader + snapshot_manager)
            if self.enable_ai:
                self._init_state_manager()
            else:
                print("[BOOTSTRAP] AI disabled, skipping state manager")
                self.components['state_manager'] = None

            # 5. SHIELD Framework (optional)
            if self.enable_shield:
                self._init_shield()
            else:
                print("[BOOTSTRAP] SHIELD disabled, skipping SHIELD framework")
                self.components['shield'] = None

            # 6. Agent Router (FATAL if fails)
            if self.enable_ai:
                self._init_agent_router()
            else:
                print("[BOOTSTRAP] AI disabled, skipping agent router")
                self.components['agent_router'] = None

            # 7. Health Monitor (optional, depends on agent_router)
            if self.enable_ai:
                self._init_health_monitor()
            else:
                print("[BOOTSTRAP] AI disabled, skipping health monitor")
                self.components['health_monitor'] = None

            # 8. Suspend Manager (optional, depends on all components)
            if self.enable_ai:
                self._init_suspend_manager()
            else:
                print("[BOOTSTRAP] AI disabled, skipping suspend manager")
                self.components['suspend_manager'] = None

        except BootstrapError as e:
            # Fatal error - cannot continue
            print()
            print("=" * 70)
            print("❌ FATAL ERROR: System bootstrap failed")
            print("=" * 70)
            print(f"Error: {e}")
            print()
            print("Critical component failed to initialize.")
            print("Cannot start JARVIS without core components.")
            raise

        # Print summary
        print()
        print("=" * 70)
        self._print_summary()
        print("=" * 70)
        print()

        return self.components

    def _init_ipc_client(self) -> Optional[Any]:
        """
        Initialize IPC client for seL4 cache communication

        Returns:
            IPCClient instance or None (graceful fallback)
        """
        print("[BOOTSTRAP] Initializing IPC client...")

        try:
            from ipc_client import IPCClient

            ipc_client = IPCClient()
            if ipc_client.connect():
                print("✅ IPC client connected (/dev/shm/jarvis_ipc)")
                self.components['ipc_client'] = ipc_client
                return ipc_client
            else:
                warning = "IPC client: seL4 not available, using AI fallback"
                print(f"⚠️  {warning}")
                print("   System will continue with AI-based cache queries")
                self.warnings.append(warning)
                self.components['ipc_client'] = None
                return None

        except ImportError:
            warning = "IPC client not available (Phase 2 Week 28 feature)"
            print(f"⚠️  {warning}")
            self.warnings.append(warning)
            self.components['ipc_client'] = None
            return None

        except Exception as e:
            warning = f"IPC client initialization failed: {e}"
            print(f"⚠️  {warning}")
            print("   System will continue using AI fallback")
            self.warnings.append(warning)
            self.components['ipc_client'] = None
            return None

    def _init_model_loader(self) -> Any:
        """
        Initialize model loader (FATAL if fails)

        Returns:
            ModelLoader instance

        Raises:
            BootstrapError: If initialization fails
        """
        print("[BOOTSTRAP] Initializing model loader...")

        try:
            from model_loader import ModelLoader

            model_loader = ModelLoader()
            print("✅ Model loader initialized")
            self.components['model_loader'] = model_loader
            return model_loader

        except Exception as e:
            error = f"Model loader initialization failed: {e}"
            print(f"❌ {error}")
            self.fatal_errors.append(error)
            raise BootstrapError(error)

    def _init_snapshot_manager(self) -> Optional[Any]:
        """
        Initialize snapshot manager (WARNING if fails)

        Returns:
            EnhancedRollbackManager instance or None
        """
        print("[BOOTSTRAP] Initializing snapshot manager...")

        try:
            from snapshot_manager import EnhancedRollbackManager

            snapshot_manager = EnhancedRollbackManager(
                max_memory_snapshots=5,
                max_disk_snapshots=20
            )
            print("✅ Snapshot manager initialized (5 memory + 20 disk snapshots)")
            self.components['snapshot_manager'] = snapshot_manager
            return snapshot_manager

        except Exception as e:
            warning = f"Snapshot manager initialization failed: {e}"
            print(f"⚠️  {warning}")
            print("   System will continue without rollback capability")
            self.warnings.append(warning)
            self.components['snapshot_manager'] = None
            return None

    def _init_state_manager(self) -> Any:
        """
        Initialize system state manager (FATAL if fails)

        Returns:
            SystemStateManager instance

        Raises:
            BootstrapError: If initialization fails
        """
        print("[BOOTSTRAP] Initializing system state manager...")

        try:
            from system_state_manager import SystemStateManager, SystemState

            model_loader = self.components.get('model_loader')
            snapshot_manager = self.components.get('snapshot_manager')

            if not model_loader:
                raise BootstrapError("Model loader required for state manager")

            state_manager = SystemStateManager(
                model_loader=model_loader,
                snapshot_manager=snapshot_manager
            )

            # Start in IDLE state (TinyLlama 1.1B)
            print("   Loading TinyLlama 1.1B (IDLE state)...")
            state_manager.transition_to(SystemState.IDLE, trigger="startup")

            print("✅ State manager initialized (IDLE state)")
            print(f"   TinyLlama 1.1B loaded (~2.1 GB RAM)")

            self.components['state_manager'] = state_manager
            return state_manager

        except Exception as e:
            error = f"State manager initialization failed: {e}"
            print(f"❌ {error}")
            self.fatal_errors.append(error)
            raise BootstrapError(error)

    def _init_shield(self) -> Optional[Any]:
        """
        Initialize SHIELD framework (WARNING if fails)

        Returns:
            SHIELDFramework instance or None
        """
        print("[BOOTSTRAP] Initializing SHIELD framework...")

        try:
            from shield_framework import SHIELDFramework

            shield = SHIELDFramework()
            print("✅ SHIELD initialized (100 action types, shadow execution enabled)")
            self.components['shield'] = shield
            return shield

        except Exception as e:
            warning = f"SHIELD initialization failed: {e}"
            print(f"⚠️  {warning}")
            print("   System will continue with reduced safety validation")
            self.warnings.append(warning)
            self.components['shield'] = None
            return None

    def _init_agent_router(self) -> Any:
        """
        Initialize multi-agent router (FATAL if fails)

        Returns:
            AgentRouter instance

        Raises:
            BootstrapError: If initialization fails
        """
        print("[BOOTSTRAP] Initializing agent router...")

        try:
            from agent_router import AgentRouter

            agent_router = AgentRouter()
            print("✅ Agent router initialized (4 specialist agents)")
            print("   Agents: device, network, filesystem, user")
            self.components['agent_router'] = agent_router
            return agent_router

        except Exception as e:
            error = f"Agent router initialization failed: {e}"
            print(f"❌ {error}")
            self.fatal_errors.append(error)
            raise BootstrapError(error)

    def _init_health_monitor(self) -> Optional[Any]:
        """
        Initialize health monitor (WARNING if fails)

        Returns:
            AgentHealthMonitor instance or None
        """
        print("[BOOTSTRAP] Initializing health monitor...")

        try:
            from agent_health import AgentHealthMonitor

            agent_router = self.components.get('agent_router')
            if not agent_router:
                raise Exception("Agent router required for health monitor")

            # Get agent list from router
            agent_list = list(agent_router.agents.values())

            health_monitor = AgentHealthMonitor(agent_list)
            print(f"✅ Health monitor initialized (monitoring {len(agent_list)} agents)")
            print("   Heartbeat interval: 10 seconds")
            self.components['health_monitor'] = health_monitor
            return health_monitor

        except Exception as e:
            warning = f"Health monitor initialization failed: {e}"
            print(f"⚠️  {warning}")
            print("   System will continue without health monitoring")
            self.warnings.append(warning)
            self.components['health_monitor'] = None
            return None

    def _init_suspend_manager(self) -> Optional[Any]:
        """
        Initialize suspend manager and register all components (WARNING if fails)

        Returns:
            SuspendManager instance or None
        """
        print("[BOOTSTRAP] Initializing suspend manager...")

        try:
            from suspend_manager import SuspendManager

            suspend_manager = SuspendManager()

            # Register components that support suspend/resume
            registered_count = 0

            # Register state manager
            if self.components.get('state_manager'):
                suspend_manager.register_component("state_manager", self.components['state_manager'])
                registered_count += 1

            # Register agents from router
            if self.components.get('agent_router'):
                agent_router = self.components['agent_router']
                for agent_name, agent in agent_router.agents.items():
                    suspend_manager.register_component(agent_name, agent)
                    registered_count += 1

            print(f"✅ Suspend manager initialized ({registered_count} components registered)")
            self.components['suspend_manager'] = suspend_manager
            return suspend_manager

        except Exception as e:
            warning = f"Suspend manager initialization failed: {e}"
            print(f"⚠️  {warning}")
            print("   System will continue without suspend/resume capability")
            self.warnings.append(warning)
            self.components['suspend_manager'] = None
            return None

    def _print_summary(self):
        """Print initialization summary"""
        total = len(self.components)
        initialized = sum(1 for c in self.components.values() if c is not None)

        if initialized == total and not self.warnings:
            print(f"✅ System bootstrap complete ({initialized}/{total} components)")
            print("   All components initialized successfully")
        elif self.warnings:
            print(f"⚠️  System bootstrap complete with warnings ({initialized}/{total} components)")
            print(f"   {len(self.warnings)} component(s) unavailable:")
            for warning in self.warnings:
                print(f"     • {warning}")
        else:
            print(f"✅ System bootstrap complete ({initialized}/{total} components)")

        if self.fatal_errors:
            print(f"\n❌ Fatal errors ({len(self.fatal_errors)}):")
            for error in self.fatal_errors:
                print(f"   • {error}")

    def shutdown_all(self):
        """Graceful shutdown of all components"""
        print("\n[BOOTSTRAP] Shutting down system components...")

        # Shutdown in reverse order
        components_to_shutdown = [
            'suspend_manager',
            'health_monitor',
            'agent_router',
            'shield',
            'state_manager',
            'snapshot_manager',
            'model_loader',
            'ipc_client'
        ]

        for component_name in components_to_shutdown:
            component = self.components.get(component_name)
            if component and hasattr(component, 'shutdown'):
                try:
                    component.shutdown()
                    print(f"✅ {component_name} shut down")
                except Exception as e:
                    print(f"⚠️  {component_name} shutdown failed: {e}")

        print("[BOOTSTRAP] Shutdown complete")

    def get_component(self, name: str) -> Optional[Any]:
        """
        Get initialized component by name

        Args:
            name (str): Component name

        Returns:
            Component instance or None
        """
        return self.components.get(name)

    def get_status(self) -> Dict[str, Any]:
        """
        Get initialization status of all components

        Returns:
            dict: Status information
        """
        return {
            'components': {
                name: 'initialized' if comp is not None else 'unavailable'
                for name, comp in self.components.items()
            },
            'total_components': len(self.components),
            'initialized_count': sum(1 for c in self.components.values() if c is not None),
            'warnings': self.warnings,
            'fatal_errors': self.fatal_errors,
            'config': self.config
        }


# Example usage
if __name__ == "__main__":
    """Test bootstrap initialization"""
    bootstrap = SystemBootstrap(config={
        'enable_ai': True,
        'enable_shield': True,
        'enable_snapshots': True
    })

    try:
        components = bootstrap.initialize_all()

        print("\nInitialized components:")
        for name, comp in components.items():
            status = "✅" if comp is not None else "❌"
            print(f"  {status} {name}: {type(comp).__name__ if comp else 'None'}")

        print("\nStatus report:")
        status = bootstrap.get_status()
        print(f"  Total: {status['initialized_count']}/{status['total_components']}")
        print(f"  Warnings: {len(status['warnings'])}")

    except BootstrapError as e:
        print(f"\n❌ Bootstrap failed: {e}")
        sys.exit(1)
