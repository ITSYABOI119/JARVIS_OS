"""
JARVIS AI-OS - AI Utilities Package
Common utilities for AI components.
"""

from .platform_utils import is_wsl, get_project_root, get_models_directory, get_model_path

__all__ = ['is_wsl', 'get_project_root', 'get_models_directory', 'get_model_path']
