#!/usr/bin/env python3
"""
JARVIS AI-OS - Platform Utilities
Common platform detection and path utilities used across AI components.

This module provides:
- WSL detection
- Project root path computation
- Models directory path resolution

Author: JARVIS AI-OS Team
Date: December 2025
"""

import os
import sys
from typing import Optional


def is_wsl() -> bool:
    """
    Detect if running in Windows Subsystem for Linux.

    Returns:
        True if running in WSL, False otherwise
    """
    if sys.platform != 'linux':
        return False

    try:
        with open('/proc/version', 'r') as f:
            content = f.read().lower()
            return 'microsoft' in content or 'wsl' in content
    except (OSError, IOError):
        return False


def get_project_root(from_file: Optional[str] = None) -> str:
    """
    Get the absolute path to the JARVIS_OS project root.

    Args:
        from_file: Path to file to compute root from (defaults to this file)

    Returns:
        Absolute path to project root directory
    """
    if from_file is None:
        # Default: compute from this file's location
        # platform_utils.py -> utils -> ai -> src -> phase1 -> JARVIS_OS
        script_dir = os.path.dirname(os.path.abspath(__file__))
        return os.path.dirname(os.path.dirname(os.path.dirname(
            os.path.dirname(script_dir)
        )))
    else:
        # Compute from provided file path
        # Assumes file is in phase1/src/ai/
        script_dir = os.path.dirname(os.path.abspath(from_file))
        return os.path.dirname(os.path.dirname(os.path.dirname(script_dir)))


def get_models_directory(from_file: Optional[str] = None) -> str:
    """
    Get the path to the models directory based on environment.

    For WSL:
    1. First tries ~/models (native filesystem, faster)
    2. Falls back to project/models (Windows mount, slower)

    For Windows:
    - Uses project/models

    Args:
        from_file: Path to file to compute project root from

    Returns:
        Absolute path to models directory
    """
    project_root = get_project_root(from_file)

    if is_wsl():
        # Try WSL native filesystem first (MUCH faster - ~5s load time)
        home = os.path.expanduser('~')
        wsl_native_path = os.path.join(home, 'models')
        if os.path.exists(wsl_native_path):
            return wsl_native_path

        # Fallback to project models directory via Windows mount
        return os.path.join(project_root, 'models')
    else:
        # Windows - use project-relative path
        return os.path.join(project_root, 'models')


def get_model_path(model_filename: str, from_file: Optional[str] = None) -> str:
    """
    Get the full path to a specific model file.

    Args:
        model_filename: Name of the model file (e.g., 'Phi-3-mini-4k-instruct-q4.gguf')
        from_file: Path to file to compute project root from

    Returns:
        Absolute path to model file
    """
    models_dir = get_models_directory(from_file)
    return os.path.join(models_dir, model_filename)


# ============================================================================
# Self-test
# ============================================================================
if __name__ == "__main__":
    print("JARVIS AI-OS - Platform Utilities Test")
    print("=" * 70)

    print(f"\nPlatform: {sys.platform}")
    print(f"Running in WSL: {is_wsl()}")
    print(f"Project root: {get_project_root()}")
    print(f"Models directory: {get_models_directory()}")

    # Test model path
    test_model = "Phi-3-mini-4k-instruct-q4.gguf"
    model_path = get_model_path(test_model)
    print(f"Model path ({test_model}): {model_path}")
    print(f"Model exists: {os.path.exists(model_path)}")

    print("\n" + "=" * 70)
    print("Platform utilities test complete!")
