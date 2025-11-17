"""
JARVIS AI-OS Phase 1 - AI Agent Module
Week 5: AI Agent Bootstrap

This module contains the AI decision engine and IPC client.

Components:
- agent.py: Main AI agent (Phi-3 Mini model)
- ipc_client.py: IPC client for seL4 communication
"""

from .agent import JARVISAgent
from .ipc_client import IPCClient, RingMessage
from .ipc_client import MSG_COMMAND, MSG_RESPONSE, MSG_QUERY, MSG_EVENT, MSG_CONTROL

__all__ = [
    'JARVISAgent',
    'IPCClient',
    'RingMessage',
    'MSG_COMMAND',
    'MSG_RESPONSE',
    'MSG_QUERY',
    'MSG_EVENT',
    'MSG_CONTROL'
]

__version__ = "0.1.0"
__author__ = "JARVIS AI-OS Team"
