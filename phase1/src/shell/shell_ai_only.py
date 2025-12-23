#!/usr/bin/env python3
"""
JARVIS Shell - AI Model Only (No Multi-Agent)

Forces use of the large AI model for all queries.
Good for testing AI model loading and inference.

Usage:
    python3 shell_ai_only.py
"""

import sys
from pathlib import Path

# Run shell with multi-agent disabled
if __name__ == "__main__":
    # Temporarily disable multi-agent router
    import shell

    # Monkey-patch to disable multi-agent
    original_init = shell.JARVISShell.__init__

    def patched_init(self, enable_ai=True, auto_load_model=False):
        original_init(self, enable_ai, auto_load_model)
        self.agent_router = None  # Force disable multi-agent
        print("[INFO] Multi-agent routing DISABLED - using AI model only")

    shell.JARVISShell.__init__ = patched_init

    # Run shell
    shell.main()
