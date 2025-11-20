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

# Try to import readline for command history (may not be available on Windows)
try:
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

    def __init__(self, enable_ai=True, auto_load_model=False):
        """
        Initialize JARVIS shell

        Args:
            enable_ai (bool): Enable AI agent (default True)
            auto_load_model (bool): Load AI model on startup (default False)
        """
        self.enable_ai = enable_ai
        self.auto_load_model = auto_load_model
        self.running = False
        self.command_count = 0
        self.ai_agent = None
        self.agent_router = None  # Multi-agent router (Week 11)
        self.history = []

        # Statistics
        self.stats = {
            'builtin_commands': 0,
            'ai_queries': 0,
            'cache_hits': 0,
            'cache_misses': 0,
            'errors': 0,
        }

        # Initialize multi-agent router if available (Week 11)
        if AgentRouter:
            try:
                self.agent_router = AgentRouter()
                print("[Multi-Agent] Router initialized (4 agents: device/network/filesystem/user)")
            except Exception as e:
                print(f"[Multi-Agent] Router initialization failed: {e}")
                self.agent_router = None

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
        """Initialize AI agent"""
        if not JARVISAgent:
            print("[ERROR] AI agent module not available")
            self.enable_ai = False
            return

        print("[INITIALIZING AI AGENT]")
        print()

        try:
            # Create agent with query processor
            self.ai_agent = JARVISAgent(use_query_processor=True)

            # Load model if auto_load enabled
            if self.auto_load_model:
                print("[LOADING AI MODEL]")
                print("  This may take 4-5 seconds...")
                print()

                if not self.ai_agent.load_model():
                    print("[ERROR] Failed to load AI model")
                    self.enable_ai = False
                    return

                print(f"  ✓ Model loaded in {self.ai_agent.load_time:.2f}s")
                print()
            else:
                print("[INFO] AI model will be loaded on first query")
                print()

        except Exception as e:
            logger.error(f"Failed to initialize AI agent: {e}")
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
        elif command in ['clear', 'cls']:
            self._execute_clear()
        else:
            print(f"[ERROR] Command not implemented: {command}")
            self.stats['errors'] += 1

    def _execute_help(self):
        """Display help message"""
        print()
        print("=" * 70)
        print("JARVIS AI-OS Shell - Available Commands")
        print("=" * 70)
        print()
        print("Built-in Commands:")
        print()

        for cmd, desc in sorted(BUILTIN_COMMANDS.items()):
            print(f"  {cmd:15} - {desc}")

        print()

        if self.enable_ai:
            print("AI Queries:")
            print()
            print("  Just type your question naturally:")
            print("    \"show cpu\"")
            print("    \"what's the memory usage?\"")
            print("    \"check disk space\"")
            print()

        print("=" * 70)
        print()

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
        """Show cache statistics"""
        if not self.ai_agent or not self.ai_agent.query_processor:
            print("[ERROR] Query processor not available")
            return

        print()
        self.ai_agent.query_processor.print_statistics()

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

    def _execute_clear(self):
        """Clear screen"""
        # Windows
        if os.name == 'nt':
            os.system('cls')
        # Unix/Linux/Mac
        else:
            os.system('clear')

    def _execute_ai_query(self, query: str):
        """
        Execute AI query

        Args:
            query (str): User's natural language query
        """
        if not self.ai_agent:
            print("[ERROR] AI agent not initialized")
            self.stats['errors'] += 1
            return

        # Load model if not loaded
        if not self.ai_agent.model:
            print("[LOADING AI MODEL]")
            print("  This may take 4-5 seconds...")
            print()

            if not self.ai_agent.load_model():
                print("[ERROR] Failed to load AI model")
                self.stats['errors'] += 1
                return

            print(f"  ✓ Model loaded in {self.ai_agent.load_time:.2f}s")
            print()

        # Show processing indicator
        print("[PROCESSING] ", end='', flush=True)

        try:
            # Use multi-agent router if available (Week 11+)
            if self.agent_router:
                # Route through multi-agent system
                start_time = time.time()
                response = self.agent_router.route_query(query)
                total_time = (time.time() - start_time) * 1000

                # Clear processing indicator
                print(f"({total_time:.0f}ms)")
                print()

                # Display result
                agent = response['routing']['selected_agent']
                action = response['action']
                result = response['result']
                routing_time = response['routing']['routing_time_ms']

                # Show agent and action
                print(f"[{agent.upper()} AGENT] Action: {action}")
                print(f"  Routing: {routing_time:.3f}ms | Response: {response['inference_time_ms']:.2f}ms")
                print()

                # Show result
                if 'error' in response:
                    print(f"[ERROR] {response['error']}")
                    self.stats['errors'] += 1
                elif 'summary' in result:
                    print(f"Result: {result['summary']}")
                else:
                    print(f"Result: {result}")

                print()

            else:
                # Fallback to single AI agent (Week 5-10)
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

    # Create and start shell
    shell = JARVISShell(
        enable_ai=not args.no_ai,
        auto_load_model=args.load_model
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
