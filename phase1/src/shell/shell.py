#!/usr/bin/env python3
"""
JARVIS AI-OS Phase 1 - Interactive Shell Interface
Week 7: Text-based REPL for user interaction

This module provides:
1. Interactive REPL (Read-Eval-Print Loop)
2. Built-in shell commands (help, exit, status, cache, agent)
3. AI query routing through query processor
4. Command history and editing
5. User-friendly error handling and feedback

Architecture:
  User Input → Shell → Command Dispatcher → [Built-in | AI Agent] → Response → Display
"""

import sys
import os
import time
import logging
from pathlib import Path
from typing import Dict, Optional, List

# Add parent directories for imports
sys.path.insert(0, str(Path(__file__).parent.parent / 'ai'))

try:
    from agent import JARVISAgent
except ImportError:
    JARVISAgent = None
    print("ERROR: AI agent not available!")

try:
    from query_processor import QueryProcessor
except ImportError:
    QueryProcessor = None
    print("WARNING: Query processor not available")

try:
    from agent_router import AgentRouter
except ImportError:
    AgentRouter = None
    print("WARNING: Multi-agent router not available")

try:
    from suspend_manager import SuspendManager
except ImportError:
    SuspendManager = None
    print("WARNING: Suspend manager not available (Week 22)")

try:
    from ipc_client import IPCClient
except ImportError:
    IPCClient = None
    print("WARNING: IPC client not available (Phase 2 Week 28)")

# Try to import readline for command history (may not be available on Windows)
# DISABLED on Windows due to pyreadline3 bugs
try:
    import platform
    if platform.system().lower() == "windows":
        # Disable readline on Windows - it's buggy
        READLINE_AVAILABLE = False
    else:
        import readline
        READLINE_AVAILABLE = True
except ImportError:
    READLINE_AVAILABLE = False

# ============================================================================
# Constants
# ============================================================================

SHELL_VERSION = "0.1.0"
SHELL_NAME = "JARVIS Shell"
WEEK = "Week 7"

# Shell prompt
PROMPT = f"JARVIS [{WEEK}]> "

# Built-in commands
BUILTIN_COMMANDS = {
    'help': 'Show available commands and usage',
    'exit': 'Exit the shell',
    'quit': 'Exit the shell (alias for exit)',
    'q': 'Exit the shell (alias for exit)',
    'status': 'Show system status',
    'cache': 'Show cache statistics',
    'agent': 'Show AI agent statistics',
    'clear': 'Clear screen',
    'cls': 'Clear screen (alias for clear)',
    'agents': 'Show multi-agent routing statistics (Week 11)',
    'health': 'Show agent health monitoring status (Week 12)',
    'scaling': 'Show dynamic model scaling state (Week 13-15)',
    'shield': 'Show SHIELD safety framework statistics (Week 16)',
    'snapshots': 'List available snapshots and rollback status (Week 17)',
    # Week 20: File operations
    'ls': 'List directory contents [path]',
    'cat': 'Display file contents <file>',
    'mkdir': 'Create directory <path>',
    'rm': 'Remove file or directory <path>',
    # Week 20: Process management
    'ps': 'List running processes',
    'kill': 'Terminate process <pid>',
    'top': 'Show top processes by CPU usage',
    # Week 20: System control
    'shutdown': 'Shutdown system (SHIELD blocked)',
    'reboot': 'Reboot system (SHIELD blocked)',
    'battery': 'Show battery status',
    'uptime': 'Show system uptime',
    # Week 20: Network diagnostics
    'ping': 'Ping network host <host>',
    'ifconfig': 'Show network interface configuration',
    # Week 22: Power management
    'suspend': 'Suspend system (ACPI S3 simulation)',
    'resume': 'Show suspend/resume metrics',
}

# Logging configuration
logging.basicConfig(
    level=logging.WARNING,  # Only show warnings and errors
    format='[%(levelname)s] %(message)s'
)
logger = logging.getLogger('jarvis.shell')

# ============================================================================
# Shell Class
# ============================================================================

class JARVISShell:
    """
    JARVIS Interactive Shell

    Provides text-based interface for:
    - Executing built-in commands
    - Querying AI agent
    - Viewing system statistics
    - Managing shell session
    """

    def __init__(self,
                 enable_ai=True,
                 auto_load_model=False,
                 # Week 11-17: Component injection
                 agent_router=None,
                 health_monitor=None,
                 state_manager=None,
                 shield=None,
                 snapshot_manager=None,
                 # Week 22: Suspend/resume
                 suspend_manager=None,
                 # Phase 2 Week 28: IPC client
                 ipc_client=None):
        """
        Initialize JARVIS shell

        Args:
            enable_ai (bool): Enable AI agent (default True)
            auto_load_model (bool): Load AI model on startup (default False)
            agent_router (AgentRouter): Multi-agent router (Week 11)
            health_monitor (AgentHealthMonitor): Health monitor (Week 12)
            state_manager (SystemStateManager): State manager (Week 13-15)
            shield (SHIELDFramework): Safety framework (Week 16)
            snapshot_manager (EnhancedRollbackManager): Snapshot manager (Week 17)
            suspend_manager (SuspendManager): Suspend/resume manager (Week 22)
            ipc_client (IPCClient): IPC client for seL4 cache communication (Phase 2 Week 28)
        """
        self.enable_ai = enable_ai
        self.auto_load_model = auto_load_model
        self.running = False
        self.command_count = 0
        self.ai_agent = None

        # Store injected components (to pass to ai_agent later)
        self._injected_agent_router = agent_router
        self._injected_health_monitor = health_monitor
        self._injected_state_manager = state_manager
        self._injected_shield = shield
        self._injected_snapshot_manager = snapshot_manager
        self._injected_suspend_manager = suspend_manager

        # Phase 2 Week 28: IPC client for seL4 cache
        self.ipc_client = ipc_client

        # Multi-agent router (Week 11)
        # Use injected router if provided, otherwise create new one
        if agent_router is not None:
            self.agent_router = agent_router
            print("[Multi-Agent] Using injected router")
        elif AgentRouter:
            try:
                self.agent_router = AgentRouter()
                print("[Multi-Agent] Router initialized (4 agents: device/network/filesystem/user)")
            except Exception as e:
                print(f"[Multi-Agent] Router initialization failed: {e}")
                self.agent_router = None
        else:
            self.agent_router = None

        self.history = []

        # Statistics
        self.stats = {
            'builtin_commands': 0,
            'ai_queries': 0,
            'cache_hits': 0,
            'cache_misses': 0,
            'errors': 0,
        }

    def start(self):
        """
        Start the shell REPL

        Main loop:
        1. Display prompt
        2. Read user input
        3. Parse and execute command
        4. Display response
        5. Repeat until exit
        """
        self.running = True

        # Display welcome message
        self._display_welcome()

        # Initialize AI agent if enabled
        if self.enable_ai:
            self._initialize_ai_agent()

        # Setup command history (if readline available)
        if READLINE_AVAILABLE:
            self._setup_readline()

        # Main REPL loop
        try:
            while self.running:
                try:
                    # Read user input
                    user_input = input(PROMPT).strip()

                    # Skip empty input
                    if not user_input:
                        continue

                    # Add to history
                    self.history.append(user_input)
                    self.command_count += 1

                    # Process command
                    self._process_command(user_input)

                except KeyboardInterrupt:
                    # Ctrl+C pressed
                    print()
                    print("Use 'exit' or 'quit' to leave the shell.")
                    continue

                except EOFError:
                    # Ctrl+D pressed
                    print()
                    self._execute_exit()
                    break

        except Exception as e:
            logger.error(f"Unexpected error in shell loop: {e}")
            self.stats['errors'] += 1

        finally:
            self._display_goodbye()

    def _display_welcome(self):
        """Display welcome message"""
        print("=" * 70)
        print(f"{SHELL_NAME} v{SHELL_VERSION} - Phase 1 {WEEK}")
        print("=" * 70)
        print()
        print("Welcome to JARVIS AI-OS Interactive Shell!")
        print("Type 'help' for available commands, 'exit' to quit.")
        print()

        if not self.enable_ai:
            print("[WARNING] AI agent disabled - only built-in commands available")
            print()

        if READLINE_AVAILABLE:
            print("[INFO] Command history enabled (use arrow keys)")
        else:
            print("[INFO] Command history not available (readline not installed)")

        print()

    def _display_goodbye(self):
        """Display goodbye message"""
        print()
        print("=" * 70)
        print("JARVIS Shell Session Summary")
        print("=" * 70)
        print(f"  Commands executed:  {self.command_count}")
        print(f"  Built-in commands:  {self.stats['builtin_commands']}")
        print(f"  AI queries:         {self.stats['ai_queries']}")

        if self.stats['ai_queries'] > 0:
            hit_rate = (self.stats['cache_hits'] / self.stats['ai_queries']) * 100
            print(f"  Cache hits:         {self.stats['cache_hits']} ({hit_rate:.1f}%)")
            print(f"  Cache misses:       {self.stats['cache_misses']}")

        print(f"  Errors:             {self.stats['errors']}")
        print("=" * 70)
        print()
        print("Thank you for using JARVIS AI-OS. Goodbye!")
        print()

    def _setup_readline(self):
        """Setup readline for command history"""
        # Set history file
        history_file = Path.home() / '.jarvis_history'

        try:
            # Load existing history
            if history_file.exists():
                readline.read_history_file(str(history_file))

            # Set history length
            readline.set_history_length(1000)

            # Save history on exit
            import atexit
            atexit.register(readline.write_history_file, str(history_file))

        except Exception as e:
            logger.warning(f"Could not setup command history: {e}")

    def _initialize_ai_agent(self):
        """Initialize AI agent with injected components"""
        if not JARVISAgent:
            print("[ERROR] AI agent module not available")
            self.enable_ai = False
            return

        print("[INITIALIZING AI AGENT]")
        print()

        try:
            # Create agent with query processor AND injected components
            self.ai_agent = JARVISAgent(
                use_query_processor=True,
                agent_router=self._injected_agent_router or self.agent_router,
                health_monitor=self._injected_health_monitor,
                state_manager=self._injected_state_manager,
                shield=self._injected_shield,
                snapshot_manager=self._injected_snapshot_manager
            )

            # Load model if auto_load enabled AND no state_manager
            # (if state_manager exists, model already loaded there)
            if self.auto_load_model and self._injected_state_manager is None:
                print("[LOADING AI MODEL]")
                print("  This may take 4-5 seconds...")
                print()

                if not self.ai_agent.load_model():
                    print("[ERROR] Failed to load AI model")
                    self.enable_ai = False
                    return

                print(f"  [OK] Model loaded in {self.ai_agent.load_time:.2f}s")
                print()
            elif self._injected_state_manager is not None:
                print("[AI MODEL] Using model from SystemStateManager")
                print()
            else:
                print("[INFO] AI model will be loaded on first query")
                print()

        except Exception as e:
            logger.error(f"Failed to initialize AI agent: {e}")
            print(f"[ERROR] Failed to initialize AI agent: {e}")
            self.enable_ai = False
            self.stats['errors'] += 1

    def _process_command(self, user_input: str):
        """
        Process user command

        Workflow:
        1. Check if built-in command
        2. If yes → execute built-in
        3. If no → route to AI agent

        Args:
            user_input (str): User's command or query
        """
        # Parse command (first word)
        parts = user_input.split()
        command = parts[0].lower() if parts else ""

        # Check if built-in command
        if command in BUILTIN_COMMANDS:
            self.stats['builtin_commands'] += 1
            self._execute_builtin(command, user_input)
        else:
            # Route to AI agent
            if self.enable_ai:
                self.stats['ai_queries'] += 1
                self._execute_ai_query(user_input)
            else:
                print(f"[ERROR] Unknown command: {command}")
                print("Type 'help' for available commands.")
                self.stats['errors'] += 1

    def _execute_builtin(self, command: str, full_input: str):
        """
        Execute built-in command

        Args:
            command (str): Command name
            full_input (str): Full user input
        """
        # Parse arguments from full_input
        parts = full_input.split(maxsplit=1)
        args = parts[1] if len(parts) > 1 else ''

        if command == 'help':
            self._execute_help()
        elif command in ['exit', 'quit', 'q']:
            self._execute_exit()
        elif command == 'status':
            self._execute_status()
        elif command == 'cache':
            self._execute_cache()
        elif command == 'agent':
            self._execute_agent()
        elif command == 'agents':
            self._execute_agents()
        elif command == 'health':
            self._execute_health()
        elif command == 'scaling':
            self._execute_scaling()
        elif command == 'shield':
            self._execute_shield()
        elif command == 'snapshots':
            self._execute_snapshots()
        elif command in ['clear', 'cls']:
            self._execute_clear()
        # Week 20: File operations
        elif command == 'ls':
            self._execute_ls(args)
        elif command == 'cat':
            self._execute_cat(args)
        elif command == 'mkdir':
            self._execute_mkdir(args)
        elif command == 'rm':
            self._execute_rm(args)
        # Week 20: Process management
        elif command == 'ps':
            self._execute_ps()
        elif command == 'kill':
            self._execute_kill(args)
        elif command == 'top':
            self._execute_top()
        # Week 20: System control
        elif command == 'shutdown':
            self._execute_shutdown()
        elif command == 'reboot':
            self._execute_reboot()
        elif command == 'battery':
            self._execute_battery()
        elif command == 'uptime':
            self._execute_uptime()
        # Week 20: Network diagnostics
        elif command == 'ping':
            self._execute_ping(args)
        elif command == 'ifconfig':
            self._execute_ifconfig()
        # Week 22: Power management
        elif command == 'suspend':
            self._execute_suspend()
        elif command == 'resume':
            self._execute_resume()
        else:
            print(f"[ERROR] Command not implemented: {command}")
            self.stats['errors'] += 1

    def _execute_help(self):
        """Display help message"""
        print("\n" + "=" * 70)
        print("JARVIS AI-OS Shell - Available Commands")
        print("=" * 70)

        # Group commands by category
        basic_cmds = ["help", "exit", "quit", "q", "clear", "cls"]
        system_cmds = ["status", "cache", "agent"]
        week11_17_cmds = ["agents", "health", "scaling", "shield", "snapshots"]
        # Week 20 commands
        file_cmds = ["ls", "cat", "mkdir", "rm"]
        process_cmds = ["ps", "kill", "top"]
        system_control_cmds = ["shutdown", "reboot", "battery", "uptime"]
        network_cmds = ["ping", "ifconfig"]
        # Week 22 commands
        power_cmds = ["suspend", "resume"]

        print("\n📌 Basic Commands:")
        for cmd in basic_cmds:
            if cmd in BUILTIN_COMMANDS:
                print(f"  {cmd:12} - {BUILTIN_COMMANDS[cmd]}")

        print("\n📊 System Status Commands:")
        for cmd in system_cmds:
            if cmd in BUILTIN_COMMANDS:
                print(f"  {cmd:12} - {BUILTIN_COMMANDS[cmd]}")

        print("\n📁 File Operations (Week 20):")
        for cmd in file_cmds:
            if cmd in BUILTIN_COMMANDS:
                print(f"  {cmd:12} - {BUILTIN_COMMANDS[cmd]}")

        print("\n⚙️  Process Management (Week 20):")
        for cmd in process_cmds:
            if cmd in BUILTIN_COMMANDS:
                print(f"  {cmd:12} - {BUILTIN_COMMANDS[cmd]}")

        print("\n🔧 System Control (Week 20):")
        for cmd in system_control_cmds:
            if cmd in BUILTIN_COMMANDS:
                print(f"  {cmd:12} - {BUILTIN_COMMANDS[cmd]}")

        print("\n🌐 Network Diagnostics (Week 20):")
        for cmd in network_cmds:
            if cmd in BUILTIN_COMMANDS:
                print(f"  {cmd:12} - {BUILTIN_COMMANDS[cmd]}")

        print("\n⚡ Power Management (Week 22):")
        for cmd in power_cmds:
            if cmd in BUILTIN_COMMANDS:
                print(f"  {cmd:12} - {BUILTIN_COMMANDS[cmd]}")

        print("\n🚀 Advanced Features (Weeks 11-17):")
        for cmd in week11_17_cmds:
            if cmd in BUILTIN_COMMANDS:
                print(f"  {cmd:12} - {BUILTIN_COMMANDS[cmd]}")

        if self.enable_ai:
            print("\n💬 Natural Language:")
            print("  Type any question to query the AI agent")
            print("  Example: 'list files in current directory'")

        print("\n" + "=" * 70 + "\n")

    def _execute_exit(self):
        """Exit the shell"""
        self.running = False

    def _execute_status(self):
        """Show system status"""
        print()
        print("=" * 70)
        print("JARVIS System Status")
        print("=" * 70)
        print()

        # AI agent status
        if self.ai_agent:
            model_status = "LOADED" if self.ai_agent.model else "NOT LOADED"
            print(f"  AI Agent:         {model_status}")

            if self.ai_agent.query_processor:
                print(f"  Query Processor:  ACTIVE")
            else:
                print(f"  Query Processor:  DISABLED")

            if self.ai_agent.model:
                model_name = Path(self.ai_agent.model_path).name
                print(f"  Model:            {model_name}")

            # Query statistics
            if self.ai_agent.query_processor:
                stats = self.ai_agent.query_processor.get_statistics()

                if stats['total_queries'] > 0:
                    print(f"  Cache Hit Rate:   {stats['cache_hit_rate']:.1f}%")
                    print(f"  Total Queries:    {stats['total_queries']}")

                    # AI agent stats
                    agent_stats = self.ai_agent.get_statistics()
                    if agent_stats['query_count'] > 0:
                        print(f"  Avg Response:     {agent_stats['avg_inference_time_ms']:.0f}ms")
        else:
            print(f"  AI Agent:         DISABLED")

        # Shell session stats
        print()
        print(f"  Session Commands: {self.command_count}")
        print(f"  Built-in Cmds:    {self.stats['builtin_commands']}")
        print(f"  AI Queries:       {self.stats['ai_queries']}")

        print()
        print("=" * 70)
        print()

    def _execute_cache(self):
        """
        Show cache statistics

        Phase 2 Week 28: Display both Python cache and seL4 cache statistics
        """
        print()
        print("=" * 70)
        print("Cache Statistics")
        print("=" * 70)
        print()

        # Phase 2 Week 28: Show seL4 cache statistics (source of truth)
        if self.ipc_client and hasattr(self.ipc_client, 'connected') and self.ipc_client.connected:
            try:
                # Request full statistics from seL4
                msg_id = self.ipc_client.send_cache_stats_request()

                if msg_id > 0:
                    # Wait for response (10ms timeout)
                    stats = self.ipc_client.recv_cache_stats(msg_id, timeout_ms=10)

                    if stats:
                        print("🔷 seL4 Decision Cache (Source of Truth)")
                        print("-" * 70)
                        print(f"  Capacity:      {stats['used']}/{stats['capacity']} entries ({stats['used']/stats['capacity']*100:.1f}% full)")
                        print(f"  Total Lookups: {stats['lookups']}")
                        print(f"  Cache Hits:    {stats['hits']} ({stats['hit_rate']:.1f}%)")
                        print(f"  Cache Misses:  {stats['misses']}")
                        print(f"  Evictions:     {stats['evictions']}")
                        print()

                        # Show real-time Python tracking
                        sel4_hits = self.stats['cache_hits']
                        sel4_total = sel4_hits + self.stats['cache_misses']
                        sel4_hit_rate = (sel4_hits / sel4_total * 100) if sel4_total > 0 else 0

                        print("📊 Real-Time Tracking (This Session)")
                        print("-" * 70)
                        print(f"  seL4 Cache Queries: {sel4_total}")
                        print(f"  Hits:   {sel4_hits} ({sel4_hit_rate:.1f}%)")
                        print(f"  Misses: {self.stats['cache_misses']}")
                        print()

            except Exception as e:
                print(f"⚠️  Failed to retrieve seL4 cache statistics: {e}")
                print()

        else:
            print("⚠️  seL4 IPC not connected - showing Python cache only")
            print()

        # Show Python-side cache (QueryProcessor)
        if self.ai_agent and self.ai_agent.query_processor:
            print("🐍 Python Query Processor Cache")
            print("-" * 70)
            self.ai_agent.query_processor.print_statistics()
        else:
            print("Python query processor not available")
            print()

        print("=" * 70)
        print()

    def _execute_agent(self):
        """Show AI agent statistics"""
        if not self.ai_agent:
            print("[ERROR] AI agent not available")
            return

        if not self.ai_agent.model:
            print("[WARNING] AI model not loaded yet")
            print()
            return

        print()
        self.ai_agent.print_statistics()

    def _execute_agents(self):
        """Show multi-agent routing statistics"""
        # Check if agent_router is injected
        if self._injected_agent_router is None:
            print("⚠️  Multi-agent routing not available (Week 11 feature)")
            print("💡 To enable multi-agent routing, run with: python3 run_jarvis.py")
            return

        router = self._injected_agent_router

        print("\n" + "=" * 60)
        print("Multi-Agent Architecture (Week 11)")
        print("=" * 60)

        # Show all specialist agents
        print("\n📋 Specialist Agents:")
        agents_info = [
            ("Device Manager", "Hardware/driver operations"),
            ("Network Agent", "Network operations"),
            ("FileSystem Agent", "File/storage operations"),
            ("User Interaction", "User-facing operations")
        ]
        for name, desc in agents_info:
            print(f"  • {name:20} - {desc}")

        # Show routing statistics
        stats = router.get_routing_stats()
        print(f"\n📊 Routing Statistics:")
        print(f"  Total queries: {stats.get('total_queries', 0)}")
        print(f"  Routing accuracy: {stats.get('routing_accuracy', 0.0)*100:.1f}%")
        print(f"  Avg routing time: {stats.get('avg_routing_time_ms', 0.0):.2f}ms")

        # Show per-agent routing
        routed_to = stats.get('routed_to', {})
        if any(routed_to.values()):
            print(f"\n🔄 Queries Routed To:")
            for agent_name, count in routed_to.items():
                print(f"  • {agent_name.title():15}: {count} queries")

        print()

    def _execute_health(self):
        """Show agent health monitoring status"""
        # Check if health monitor is injected
        if self._injected_health_monitor is None:
            print("⚠️  Health monitoring not available (Week 12 feature)")
            print("💡 To enable health monitoring, run with: python3 run_jarvis.py")
            return

        monitor = self._injected_health_monitor

        print("\n" + "=" * 60)
        print("Agent Health Monitoring (Week 12)")
        print("=" * 60)

        # Show overall health status
        stats = monitor.get_statistics()
        print(f"\n💚 Monitoring Status:")
        print(f"  Agents monitored: {stats.get('agents_monitored', 0)}")
        print(f"  Healthy agents: {stats.get('healthy_agents', 0)}")
        print(f"  Degraded agents: {stats.get('degraded_agents', 0)}")
        print(f"  Failed agents: {stats.get('failed_agents', 0)}")
        print(f"  Uptime: {stats.get('monitoring_uptime', 0):.1f}s")

        # Show statistics
        print(f"\n📊 Statistics:")
        print(f"  Total heartbeats: {stats.get('total_heartbeats', 0)}")
        print(f"  Total failures: {stats.get('total_failures', 0)}")
        print(f"  Total recoveries: {stats.get('total_recoveries', 0)}")
        print(f"  State changes: {stats.get('total_state_changes', 0)}")

        print()

    def _execute_scaling(self):
        """Show dynamic model scaling state"""
        # Check if state manager is injected
        if self._injected_state_manager is None:
            print("⚠️  Dynamic scaling not available (Week 13-15 feature)")
            print("💡 To enable dynamic scaling, run with: python3 run_jarvis.py")
            return

        state_mgr = self._injected_state_manager

        print("\n" + "=" * 60)
        print("Dynamic Model Scaling (Week 13-15)")
        print("=" * 60)

        # Show current state
        stats = state_mgr.get_statistics()
        current_state = stats.get('current_state', 'unknown').upper()

        state_icons = {
            'IDLE': '😴',
            'ACTIVE': '⚡',
            'CRITICAL': '🔥',
            'EMERGENCY': '🚨'
        }

        icon = state_icons.get(current_state, '❓')
        print(f"\n{icon} Current State: {current_state}")

        # Show state details
        state_details = {
            'IDLE': 'TinyLlama 1.1B, 2GB RAM, 1 core, 50ms',
            'ACTIVE': 'Phi-3 Mini 3.8B, 8GB RAM, 3 cores, 200ms',
            'CRITICAL': 'Phi-3 + Validator, 10GB RAM, 6 cores, 500ms',
            'EMERGENCY': 'Rule-based fallback, 100MB RAM, 1 core, <1ms'
        }
        print(f"  Configuration: {state_details.get(current_state, 'Unknown')}")

        # Show system metrics
        print(f"\n💻 System Metrics:")
        print(f"  CPU usage: {stats.get('cpu_usage', 0):.1f}%")
        print(f"  Memory usage: {stats.get('memory_usage', 0):.1f}%")
        print(f"  Disk usage: {stats.get('disk_usage', 0):.1f}%")

        # Show transition statistics
        print(f"\n📊 Transition Statistics:")
        print(f"  Total transitions: {stats.get('total_transitions', 0)}")
        print(f"  Successful: {stats.get('successful_transitions', 0)}")
        print(f"  Failed: {stats.get('failed_transitions', 0)}")
        print(f"  Avg transition time: {stats.get('avg_transition_time', 0):.2f}s")

        # Show state time distribution
        print(f"\n⏱️  Time in States:")
        print(f"  IDLE: {stats.get('time_in_idle', 0):.1f}s")
        print(f"  ACTIVE: {stats.get('time_in_active', 0):.1f}s")
        print(f"  CRITICAL: {stats.get('time_in_critical', 0):.1f}s")
        print(f"  EMERGENCY: {stats.get('time_in_emergency', 0):.1f}s")

        print()

    def _execute_shield(self):
        """Show SHIELD safety framework statistics"""
        # Use injected SHIELD directly (it's used for command validation anyway)
        if self._injected_shield is None:
            print("⚠️  SHIELD framework not available (Week 16 feature)")
            print("💡 To enable SHIELD, run with: python run_jarvis.py")
            return

        shield = self._injected_shield

        print("\n" + "=" * 60)
        print("SHIELD Safety Framework (Week 16)")
        print("=" * 60)

        # Show framework overview
        print("\n[SHIELD] Components:")
        print("  • Staged Execution Sandbox")
        print("  • Hardware Impact Analysis")
        print("  • Irreversibility Detection")
        print("  • Escalation Intelligence")
        print("  • Learning from Failures")
        print("  • Deterministic Rollback")

        # Show statistics
        stats = shield.get_stats()
        total = stats.get('total_validations', 0)
        print(f"\n📊 Validation Statistics:")
        print(f"  Total validations: {total}")
        print(f"  Blocked rate: {stats.get('blocked_rate', 0)*100:.1f}%")
        print(f"  Shadow tested rate: {stats.get('shadow_rate', 0)*100:.1f}%")
        print(f"  Approval required rate: {stats.get('approval_rate', 0)*100:.1f}%")
        print(f"  Automatic execution rate: {stats.get('automatic_rate', 0)*100:.1f}%")

        # Show action type coverage
        print(f"\n📋 Action Type Coverage:")
        print(f"  100 action types across 10 categories")
        print(f"  Categories: File, Process, Network, System, Service,")
        print(f"              User, Security, Power, Package, Hardware")

        print()

    def _execute_snapshots(self):
        """List available snapshots and rollback status"""
        # Use injected snapshot manager directly
        if self._injected_snapshot_manager is None:
            print("⚠️  Snapshot manager not available (Week 17 feature)")
            print("💡 To enable snapshots, run with: python run_jarvis.py")
            return

        snap_mgr = self._injected_snapshot_manager

        print("\n" + "=" * 60)
        print("Snapshots & Rollback (Week 17)")
        print("=" * 60)

        # Show statistics
        stats = snap_mgr.get_statistics()
        print(f"\n💾 Storage Configuration:")
        print(f"  Memory snapshots: {stats.get('current_memory_snapshots', 0)} / 5 max")
        print(f"  Disk snapshots: {stats.get('current_disk_snapshots', 0)} / 20 max")

        # Show statistics
        print(f"\n📊 Statistics:")
        print(f"  Total snapshots created: {stats.get('total_snapshots', 0)}")
        print(f"  Memory snapshots: {stats.get('memory_snapshots', 0)}")
        print(f"  Disk snapshots: {stats.get('disk_snapshots', 0)}")
        print(f"  Rollbacks executed: {stats.get('rollbacks_executed', 0)}")
        print(f"  Rollback success rate: {stats.get('rollback_success_rate', 0)*100:.1f}%")
        print(f"  Avg snapshot time: {stats.get('avg_snapshot_time_ms', 0):.1f}ms")
        print(f"  Avg rollback time: {stats.get('avg_rollback_time_ms', 0):.1f}ms")

        print()

    def _execute_clear(self):
        """Clear screen"""
        # Windows
        if os.name == 'nt':
            os.system('cls')
        # Unix/Linux/Mac
        else:
            os.system('clear')

    # ==================================================================
    # Week 20: New Shell Commands
    # ==================================================================

    def _get_system_snapshot(self):
        """Get current system state for SHIELD validation"""
        try:
            import psutil

            class SystemSnapshot:
                def __init__(self):
                    self.cpu_usage = psutil.cpu_percent(interval=0.1)
                    self.memory_usage = psutil.virtual_memory().percent
                    try:
                        self.disk_usage = psutil.disk_usage('/').percent
                    except:
                        self.disk_usage = 0.0
                    self.network_active = True
                    self.file_count = 1000  # Placeholder
                    self.service_states = {}

            return SystemSnapshot()
        except Exception as e:
            # Fallback if psutil fails
            class FallbackSnapshot:
                cpu_usage = 50.0
                memory_usage = 60.0
                disk_usage = 70.0
                network_active = True
                file_count = 1000
                service_states = {}
            return FallbackSnapshot()

    # Week 20: File Operations

    def _execute_ls(self, args=''):
        """List directory contents"""
        import os

        path = args.strip() or '.'

        try:
            if not os.path.exists(path):
                print(f"[ERROR] Path '{path}' does not exist")
                self.stats['errors'] += 1
                return

            if os.path.isfile(path):
                # Just show the file
                print(f"\n{path}")
                return

            # List directory
            entries = sorted(os.listdir(path))
            print()
            print("=" * 70)
            print(f"Directory: {os.path.abspath(path)}")
            print("=" * 70)

            for entry in entries:
                full_path = os.path.join(path, entry)
                if os.path.isdir(full_path):
                    print(f"  [DIR]  {entry}")
                else:
                    try:
                        size = os.path.getsize(full_path)
                        print(f"  [FILE] {entry:40} ({size:,} bytes)")
                    except:
                        print(f"  [FILE] {entry}")

            print(f"\nTotal: {len(entries)} entries")
            print()
        except Exception as e:
            print(f"[ERROR] {e}")
            self.stats['errors'] += 1

    def _execute_cat(self, args=''):
        """Display file contents"""
        import os

        if not args:
            print("[ERROR] Missing file argument. Usage: cat <file>")
            self.stats['errors'] += 1
            return

        try:
            if not os.path.isfile(args):
                print(f"[ERROR] '{args}' is not a file")
                self.stats['errors'] += 1
                return

            with open(args, 'r') as f:
                content = f.read()

            print()
            print("=" * 70)
            print(f"File: {args}")
            print("=" * 70)
            print(content)
            print("=" * 70)
            print()
        except UnicodeDecodeError:
            print(f"[ERROR] Cannot display binary file: {args}")
            self.stats['errors'] += 1
        except Exception as e:
            print(f"[ERROR] {e}")
            self.stats['errors'] += 1

    def _execute_mkdir(self, args=''):
        """Create directory (with SHIELD validation)"""
        import os

        if not args:
            print("[ERROR] Missing path argument. Usage: mkdir <path>")
            self.stats['errors'] += 1
            return

        # SHIELD validation
        if self._injected_shield:
            action = {
                'type': 'dir_create',
                'parameters': {'path': args}
            }
            result = self._injected_shield.validate_action(action, self._get_system_snapshot())

            if result['execution_mode'] == 'blocked':
                print(f"[SHIELD] BLOCKED: mkdir {args}")
                print(f"    Risk: {result.get('adjusted_risk', 0.9):.2f}")
                print(f"    Reason: {result.get('reason', 'Directory creation blocked')}")
                self.stats['errors'] += 1
                return

        try:
            os.makedirs(args, exist_ok=True)
            print(f"[OK] Created directory: {args}")
        except Exception as e:
            print(f"[ERROR] {e}")
            self.stats['errors'] += 1

    def _execute_rm(self, args=''):
        """Remove file (with SHIELD validation)"""
        import os
        import shutil

        if not args:
            print("[ERROR] Missing file argument. Usage: rm <file>")
            self.stats['errors'] += 1
            return

        # SHIELD validation
        if self._injected_shield:
            action = {
                'type': 'file_delete',
                'parameters': {'path': args}
            }
            result = self._injected_shield.validate_action(action, self._get_system_snapshot())

            if result['execution_mode'] == 'blocked':
                print(f"[SHIELD] BLOCKED: rm {args}")
                print(f"    Risk: {result.get('adjusted_risk', 0.9):.2f}")
                print(f"    Reason: {result.get('reason', 'File deletion blocked')}")
                self.stats['errors'] += 1
                return

            if result['execution_mode'] == 'shadow':
                print(f"[SHIELD] SHADOW MODE: rm {args}")
                print(f"    Risk: {result.get('adjusted_risk', 0.5):.2f}")
                print(f"    Proceeding with caution...")

        try:
            if os.path.isfile(args):
                os.remove(args)
                print(f"[OK] Removed file: {args}")
            elif os.path.isdir(args):
                shutil.rmtree(args)
                print(f"[OK] Removed directory: {args}")
            else:
                print(f"[ERROR] '{args}' not found")
                self.stats['errors'] += 1
        except Exception as e:
            print(f"[ERROR] {e}")
            self.stats['errors'] += 1

    # Week 20: Process Management

    def _execute_ps(self):
        """List running processes"""
        try:
            import psutil

            print()
            print("=" * 80)
            print("Running Processes (Top 20 by CPU)")
            print("=" * 80)
            print(f"{'PID':<8} {'Name':<30} {'CPU%':<8} {'Memory%':<10}")
            print("-" * 80)

            for proc in sorted(psutil.process_iter(['pid', 'name', 'cpu_percent', 'memory_percent']),
                              key=lambda p: p.info.get('cpu_percent') or 0, reverse=True)[:20]:
                info = proc.info
                pid = info.get('pid', 0)
                name = info.get('name', 'Unknown')[:29]
                cpu = info.get('cpu_percent') or 0.0
                mem = info.get('memory_percent') or 0.0
                print(f"{pid:<8} {name:<30} {cpu:<8.1f} {mem:<10.1f}")

            print()
        except ImportError:
            print("[ERROR] psutil not available. Install with: pip install psutil")
            self.stats['errors'] += 1
        except Exception as e:
            print(f"[ERROR] {e}")
            self.stats['errors'] += 1

    def _execute_kill(self, args=''):
        """Terminate process (with SHIELD validation)"""
        try:
            import psutil

            if not args or not args.strip().isdigit():
                print("[ERROR] Invalid PID. Usage: kill <pid>")
                self.stats['errors'] += 1
                return

            pid = int(args.strip())

            # SHIELD validation
            if self._injected_shield:
                action = {
                    'type': 'process_kill',
                    'parameters': {'pid': pid}
                }
                result = self._injected_shield.validate_action(action, self._get_system_snapshot())

                if result['execution_mode'] == 'blocked':
                    print(f"[SHIELD] BLOCKED: kill {pid}")
                    print(f"    Risk: {result.get('adjusted_risk', 0.9):.2f}")
                    print(f"    Reason: {result.get('reason', 'Process termination blocked')}")
                    self.stats['errors'] += 1
                    return

            proc = psutil.Process(pid)
            name = proc.name()
            proc.terminate()
            print(f"[OK] Terminated process {pid} ({name})")
        except psutil.NoSuchProcess:
            print(f"[ERROR] Process {pid} not found")
            self.stats['errors'] += 1
        except psutil.AccessDenied:
            print(f"[ERROR] Permission denied to kill process {pid}")
            self.stats['errors'] += 1
        except ImportError:
            print("[ERROR] psutil not available. Install with: pip install psutil")
            self.stats['errors'] += 1
        except Exception as e:
            print(f"[ERROR] {e}")
            self.stats['errors'] += 1

    def _execute_top(self):
        """Show top processes by CPU usage"""
        try:
            import psutil

            print()
            print("=" * 80)
            print("Top Processes (by CPU usage)")
            print("=" * 80)
            print(f"{'PID':<8} {'Name':<30} {'CPU%':<8} {'Memory%':<10}")
            print("-" * 80)

            for proc in sorted(psutil.process_iter(['pid', 'name', 'cpu_percent', 'memory_percent']),
                              key=lambda p: p.info.get('cpu_percent') or 0, reverse=True)[:10]:
                info = proc.info
                pid = info.get('pid', 0)
                name = info.get('name', 'Unknown')[:29]
                cpu = info.get('cpu_percent') or 0.0
                mem = info.get('memory_percent') or 0.0
                print(f"{pid:<8} {name:<30} {cpu:<8.1f} {mem:<10.1f}")

            print()
        except ImportError:
            print("[ERROR] psutil not available. Install with: pip install psutil")
            self.stats['errors'] += 1
        except Exception as e:
            print(f"[ERROR] {e}")
            self.stats['errors'] += 1

    # Week 20: System Control

    def _execute_shutdown(self):
        """Shutdown system (SHIELD blocked)"""
        # SHIELD validation
        if self._injected_shield:
            action = {
                'type': 'system_shutdown',
                'parameters': {}
            }
            result = self._injected_shield.validate_action(action, self._get_system_snapshot())

            if result['execution_mode'] == 'blocked':
                print(f"[SHIELD] BLOCKED: shutdown")
                print(f"    Risk: {result.get('adjusted_risk', 1.0):.2f}")
                print(f"    Reason: Critical system operation")
                self.stats['errors'] += 1
                return

        print("[WARNING] Shutdown would execute here (disabled for safety)")
        # os.system('shutdown -h now')  # Disabled for development safety

    def _execute_reboot(self):
        """Reboot system (SHIELD blocked)"""
        # SHIELD validation
        if self._injected_shield:
            action = {
                'type': 'system_reboot',
                'parameters': {}
            }
            result = self._injected_shield.validate_action(action, self._get_system_snapshot())

            if result['execution_mode'] == 'blocked':
                print(f"[SHIELD] BLOCKED: reboot")
                print(f"    Risk: {result.get('adjusted_risk', 1.0):.2f}")
                print(f"    Reason: Critical system operation")
                self.stats['errors'] += 1
                return

        print("[WARNING] Reboot would execute here (disabled for safety)")
        # os.system('reboot')  # Disabled for development safety

    def _execute_battery(self):
        """Show battery status"""
        try:
            import psutil

            print()
            print("=" * 70)
            print("Battery Status")
            print("=" * 70)

            battery = psutil.sensors_battery()
            if battery:
                print(f"  Charge: {battery.percent}%")
                print(f"  Plugged In: {'Yes' if battery.power_plugged else 'No'}")

                if not battery.power_plugged and battery.secsleft > 0:
                    hours = battery.secsleft // 3600
                    mins = (battery.secsleft % 3600) // 60
                    print(f"  Time Remaining: {hours}h {mins}m")
            else:
                print("  No battery detected (desktop system)")

            print()
        except ImportError:
            print("[ERROR] psutil not available. Install with: pip install psutil")
            self.stats['errors'] += 1
        except Exception as e:
            print(f"[ERROR] {e}")
            self.stats['errors'] += 1

    def _execute_uptime(self):
        """Show system uptime"""
        try:
            import psutil
            import datetime

            boot_time = datetime.datetime.fromtimestamp(psutil.boot_time())
            uptime = datetime.datetime.now() - boot_time

            days = uptime.days
            hours = uptime.seconds // 3600
            mins = (uptime.seconds % 3600) // 60

            print()
            print("=" * 70)
            print("System Uptime")
            print("=" * 70)
            print(f"  Boot Time: {boot_time.strftime('%Y-%m-%d %H:%M:%S')}")
            print(f"  Uptime: {days} days, {hours} hours, {mins} minutes")
            print()
        except ImportError:
            print("[ERROR] psutil not available. Install with: pip install psutil")
            self.stats['errors'] += 1
        except Exception as e:
            print(f"[ERROR] {e}")
            self.stats['errors'] += 1

    # Week 20: Network Diagnostics

    def _execute_ping(self, args=''):
        """Ping network host"""
        import platform
        import subprocess

        if not args:
            print("[ERROR] Missing host argument. Usage: ping <host>")
            self.stats['errors'] += 1
            return

        # Determine ping command based on platform
        param = '-n' if platform.system().lower() == 'windows' else '-c'
        command = ['ping', param, '4', args.strip()]

        print(f"\nPinging {args}...")
        try:
            result = subprocess.run(command, capture_output=True, text=True, timeout=10)
            print(result.stdout)
            if result.returncode != 0:
                print(f"[WARNING] Ping failed with exit code {result.returncode}")
        except subprocess.TimeoutExpired:
            print(f"[ERROR] Ping timeout for {args}")
            self.stats['errors'] += 1
        except Exception as e:
            print(f"[ERROR] {e}")
            self.stats['errors'] += 1

    def _execute_ifconfig(self):
        """Show network interface configuration"""
        try:
            import psutil

            print()
            print("=" * 70)
            print("Network Interfaces")
            print("=" * 70)

            addrs = psutil.net_if_addrs()
            stats = psutil.net_if_stats()

            for interface, addr_list in addrs.items():
                print(f"\n{interface}:")

                if interface in stats:
                    st = stats[interface]
                    print(f"  Status: {'Up' if st.isup else 'Down'}")
                    print(f"  Speed: {st.speed} Mbps")

                for addr in addr_list:
                    if addr.family == 2:  # AF_INET (IPv4)
                        print(f"  IPv4: {addr.address}")
                        if addr.netmask:
                            print(f"  Netmask: {addr.netmask}")
                    elif addr.family == 17:  # AF_LINK (MAC)
                        print(f"  MAC: {addr.address}")

            print()
        except ImportError:
            print("[ERROR] psutil not available. Install with: pip install psutil")
            self.stats['errors'] += 1
        except Exception as e:
            print(f"[ERROR] {e}")
            self.stats['errors'] += 1

    # ========================================================================
    # Week 22: Power Management Commands
    # ========================================================================

    def _execute_suspend(self):
        """Suspend system (simulate ACPI S3)"""
        print()
        print("=" * 70)
        print("Suspend System (ACPI S3 Simulation)")
        print("=" * 70)
        print()

        # Check if suspend manager is available
        if not self._injected_suspend_manager:
            print("⚠️  Suspend manager not available")
            print("   Suspend/resume requires SuspendManager initialization")
            self.stats['errors'] += 1
            return

        suspend_mgr = self._injected_suspend_manager

        # SHIELD check: suspend requires elevated privilege
        if self._injected_shield:
            try:
                action = {
                    'type': 'system_suspend',
                    'parameters': {}
                }
                result = self._injected_shield.validate_action(action, self._get_system_snapshot())

                if result['execution_mode'] == 'blocked':
                    print(f"❌ SHIELD BLOCKED: {result['reason']}")
                    print(f"   Risk: {result['adjusted_risk']:.2f}")
                    print(f"   Requires: Admin privileges")
                    self.stats['errors'] += 1
                    return
            except Exception as e:
                print(f"⚠️  SHIELD validation skipped: {e}")

        print("Preparing to suspend...")
        print()

        # Suspend
        print("[1/3] Saving system state...")
        start = time.time()

        if suspend_mgr.suspend():
            suspend_time = time.time() - start
            print(f"✓ State saved ({suspend_time:.2f}s)")
            print()

            # Simulate ACPI S3 sleep
            print("[2/3] Entering S3 sleep state...")
            print()
            print("💤 System suspended (simulated)")
            print("   Press ENTER to resume (simulates power button press)...")

            try:
                input()
            except (EOFError, KeyboardInterrupt):
                print()

            # Resume
            print()
            print("[3/3] Waking from S3...")
            start = time.time()

            if suspend_mgr.resume():
                resume_time = time.time() - start
                print(f"✓ System resumed ({resume_time:.2f}s)")
                print()

                # Show metrics
                metrics = suspend_mgr.get_metrics()
                print("Resume Metrics:")
                print(f"  Components restored: {metrics['component_count']}")
                print(f"  Resume time: {resume_time:.2f}s (target: <15s)")
                print(f"  State size: {suspend_mgr.get_state_size() / 1024:.1f} KB")
                print()

                # Check if target met
                if resume_time < 15.0:
                    print("✅ Resume time target MET (<15s)")
                else:
                    print("⚠️  Resume time target EXCEEDED (>15s)")
                print()
            else:
                print("❌ Resume FAILED")
                self.stats['errors'] += 1
        else:
            print("❌ Suspend FAILED")
            self.stats['errors'] += 1

    def _execute_resume(self):
        """Show suspend/resume metrics"""
        print()
        print("=" * 70)
        print("Suspend/Resume Metrics")
        print("=" * 70)
        print()

        # Check if suspend manager is available
        if not self._injected_suspend_manager:
            print("⚠️  Suspend manager not available")
            self.stats['errors'] += 1
            return

        suspend_mgr = self._injected_suspend_manager
        metrics = suspend_mgr.get_metrics()

        # Current state
        print("Status:")
        print(f"  State: {metrics['state'].upper()}")
        print(f"  Registered components: {metrics['component_count']}")
        print(f"  Components: {', '.join(metrics['components'])}")
        print()

        # Timing metrics
        print("Performance:")
        print(f"  Last suspend time: {metrics['last_suspend_time']:.2f}s (target: <5s)")
        print(f"  Last resume time: {metrics['last_resume_time']:.2f}s (target: <15s)")
        print()

        # Statistics
        print("Statistics:")
        print(f"  Total suspends: {metrics['total_suspends']}")
        print(f"  Successful: {metrics['successful_suspends']}")
        print(f"  Failed: {metrics['failed_suspends']}")
        print()
        print(f"  Total resumes: {metrics['total_resumes']}")
        print(f"  Successful: {metrics['successful_resumes']}")
        print(f"  Failed: {metrics['failed_resumes']}")
        print()

        # Success rates
        if metrics['total_suspends'] > 0:
            suspend_rate = (metrics['successful_suspends'] / metrics['total_suspends']) * 100
            print(f"  Suspend success rate: {suspend_rate:.1f}%")

        if metrics['total_resumes'] > 0:
            resume_rate = (metrics['successful_resumes'] / metrics['total_resumes']) * 100
            print(f"  Resume success rate: {resume_rate:.1f}%")
        print()

        # State size
        state_size = suspend_mgr.get_state_size()
        print(f"Saved State Size: {state_size / 1024:.1f} KB (target: <10 MB)")
        print(f"  Location: {metrics['state_dir']}")
        print()

    def _execute_ai_query(self, query: str):
        """
        Execute AI query

        Phase 2 Week 28: Query seL4 cache first via IPC before routing to AI

        Args:
            query (str): User's natural language query
        """
        if not self.ai_agent:
            print("[ERROR] AI agent not initialized")
            self.stats['errors'] += 1
            return

        # Phase 2 Week 28: Try seL4 cache first (if IPC connected)
        if self.ipc_client and hasattr(self.ipc_client, 'connected') and self.ipc_client.connected:
            # Normalize query (lowercase, collapse spaces, trim)
            normalized = ' '.join(query.lower().split())

            try:
                # Send cache lookup to seL4
                msg_id = self.ipc_client.send_cache_lookup(normalized)

                if msg_id > 0:
                    # Wait for response (10ms timeout = 100× safety margin over <100μs IPC target)
                    response = self.ipc_client.recv_cache_response(msg_id, timeout_ms=10)

                    if response and response['hit']:
                        # seL4 cache HIT!
                        self.stats['cache_hits'] += 1

                        print(f"[seL4 CACHE HIT] {response['action']} (trust={response['trust']})")
                        print()
                        print(f"  Cached action from seL4 decision cache")
                        print(f"  IPC latency: <10ms (target: <100μs)")
                        print()
                        return

            except Exception as e:
                # IPC error - fall back to AI
                logger.warning(f"seL4 IPC cache lookup failed: {e}")

        # seL4 cache miss or IPC unavailable - route to AI
        if self.ipc_client and hasattr(self.ipc_client, 'connected') and self.ipc_client.connected:
            self.stats['cache_misses'] += 1

        # Show processing indicator
        print("[PROCESSING] ", end='', flush=True)

        try:
            # Use multi-agent router if available (Week 11+)
            if self.agent_router:
                # Route through multi-agent system
                start_time = time.time()
                response = self.agent_router.route_query(query)
                total_time = (time.time() - start_time) * 1000

                # Check if this is a general query that needs AI model
                agent = response['routing']['selected_agent']
                action = response['action']

                if agent == 'user' and action == 'general_query':
                    # This is a general query - use AI model instead
                    print()
                    print("[ROUTING TO AI MODEL] General query detected")

                    # Load model if not loaded
                    if not self.ai_agent.model:
                        print()
                        print("[LOADING AI MODEL]")
                        print("  This may take 4-5 seconds...")
                        print()

                        if not self.ai_agent.load_model():
                            print("[ERROR] Failed to load AI model")
                            self.stats['errors'] += 1
                            return

                        print(f"  Model loaded in {self.ai_agent.load_time:.2f}s")
                        print()
                        print("[PROCESSING] ", end='', flush=True)

                    # Process with AI model
                    start_time = time.time()
                    result = self.ai_agent.process_query(query)
                    total_time = (time.time() - start_time) * 1000

                    # Clear processing indicator
                    print(f"({total_time:.0f}ms)")
                    print()

                    # Display result
                    if result['success']:
                        print(f"[AI MODEL] Response:")
                        print()
                        print(f"{result['response']}")
                        print()
                        print(f"  Inference time: {result['inference_time_ms']:.0f}ms")
                        print(f"  Tokens: {result['tokens_generated']}")
                        print()
                    else:
                        print(f"[ERROR] AI inference failed: {result.get('response', 'Unknown error')}")
                        self.stats['errors'] += 1
                else:
                    # Regular multi-agent response
                    # Clear processing indicator
                    print(f"({total_time:.0f}ms)")
                    print()

                    # Display result
                    # Fix #4: Validate response structure before accessing
                    result = response.get('result', {})
                    routing_time = response.get('routing', {}).get('routing_time_ms', 0)

                    # Show agent and action
                    print(f"[{agent.upper()} AGENT] Action: {action}")
                    print(f"  Routing: {routing_time:.3f}ms | Response: {response.get('inference_time_ms', 0):.2f}ms")
                    print()

                    # Show result
                    if 'error' in response:
                        print(f"[ERROR] {response['error']}")
                        self.stats['errors'] += 1
                    elif isinstance(result, dict) and 'summary' in result:
                        print(f"Result: {result['summary']}")
                    else:
                        print(f"Result: {result}")

                    print()

            else:
                # Fallback to single AI agent (Week 5-10)
                # Load model if not loaded
                if not self.ai_agent.model:
                    print()
                    print("[LOADING AI MODEL]")
                    print("  This may take 4-5 seconds...")
                    print()

                    if not self.ai_agent.load_model():
                        print("[ERROR] Failed to load AI model")
                        self.stats['errors'] += 1
                        return

                    print(f"  Model loaded in {self.ai_agent.load_time:.2f}s")
                    print()
                    print("[PROCESSING] ", end='', flush=True)

                start_time = time.time()
                result = self.ai_agent.process_query(query)
                total_time = (time.time() - start_time) * 1000

                # Clear processing indicator
                print(f"({total_time:.0f}ms)")
                print()

                # Display result
                if result['success']:
                    # Show cache hit/miss
                    if result.get('cache_hit'):
                        print(f"[CACHE HIT] {result['command']['command']}")
                        self.stats['cache_hits'] += 1
                    else:
                        print(f"[AI INFERENCE] {result['command']['command']}")
                        self.stats['cache_misses'] += 1

                    # Show response
                    print()
                    print(f"Response: {result['response']}")

                    # Show timing details
                    if not result.get('cache_hit'):
                        print()
                        print(f"  Inference time: {result['inference_time_ms']:.0f}ms")
                        print(f"  Tokens: {result['tokens_generated']}")

                    print()
                else:
                    print(f"[ERROR] Query processing failed: {result.get('response', 'Unknown error')}")
                    self.stats['errors'] += 1

        except Exception as e:
            print()
            print(f"[ERROR] Query execution failed: {e}")
            logger.error(f"AI query error: {e}", exc_info=True)
            self.stats['errors'] += 1

# ============================================================================
# Main Entry Point
# ============================================================================

def main():
    """
    Main entry point for JARVIS shell

    Usage:
        python shell.py                 # Start shell (AI enabled, model loads on first query)
        python shell.py --no-ai         # Start shell without AI
        python shell.py --load-model    # Start shell and load AI model immediately
    """
    import argparse

    parser = argparse.ArgumentParser(description='JARVIS AI-OS Interactive Shell')
    parser.add_argument('--no-ai', action='store_true',
                       help='Disable AI agent (built-in commands only)')
    parser.add_argument('--load-model', action='store_true',
                       help='Load AI model on startup (default: load on first query)')

    args = parser.parse_args()

    # Phase 2 Week 28: Create IPC client for seL4 cache
    ipc_client = None
    if IPCClient:
        try:
            ipc_client = IPCClient()
            if ipc_client.connect():
                print("[IPC] Connected to seL4 cache (/dev/shm/jarvis_ipc)")
            else:
                print("[IPC] seL4 not available, using AI fallback")
                ipc_client = None
        except Exception as e:
            print(f"[IPC] Connection failed: {e}, using AI fallback")
            ipc_client = None
    else:
        print("[IPC] IPC client not available (Phase 2 Week 28 feature)")

    # Create and start shell
    shell = JARVISShell(
        enable_ai=not args.no_ai,
        auto_load_model=args.load_model,
        ipc_client=ipc_client
    )

    try:
        shell.start()
    except Exception as e:
        print()
        print(f"[FATAL ERROR] Shell crashed: {e}")
        logger.error(f"Shell crash: {e}", exc_info=True)
        sys.exit(1)

if __name__ == "__main__":
    main()
