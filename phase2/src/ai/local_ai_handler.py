#!/usr/bin/env python3
"""
JARVIS AI-OS - Local AI Handler
Phase 2: Standalone AI inference for Raspberry Pi 4

IMPORTANT: This module requires Linux (Raspberry Pi OS) - NOT bare-metal seL4!
- seL4 runs C code only, no Python runtime
- Use this for TESTING with Pi OS on a separate SD card
- Production seL4 uses UART to PC for AI inference
- Phase 3 will add native llama.cpp on seL4

This module provides local AI inference capability when running without
a connected PC. Designed for testing/development on Pi 4 with Raspberry Pi OS.

Performance Characteristics (Pi 4 CPU-only, Q4_K_M quantization):
- TinyLlama 1.1B Q4_K_M: ~3-5 tokens/sec
- Model size: ~640MB
- RAM usage: ~1.5GB
- Typical response (50 tokens): ~10-17 seconds
- No GPU acceleration (CPU inference only)

Usage (Raspberry Pi OS only):
    handler = LocalAIHandler()
    if handler.initialize():
        response = handler.generate("list files in current directory")

Author: JARVIS Development Team
Date: December 2025
"""

import os
import sys
import time
import platform
from pathlib import Path
from typing import Optional, Dict, Any
import logging

# Logging setup
logging.basicConfig(level=logging.INFO, format='[%(levelname)s] %(message)s')
logger = logging.getLogger('local_ai')

# Add Phase 1 AI path for imports
PHASE1_AI_PATH = Path(__file__).parent.parent.parent.parent / "phase1" / "src" / "ai"
sys.path.insert(0, str(PHASE1_AI_PATH))


class LocalAIHandler:
    """
    Local AI inference handler for standalone operation.

    Provides TinyLlama 1.1B inference on Pi 4 without needing
    a connected PC. Slower than PC-based inference but fully
    self-contained.

    Architecture:
        Pi 4 runs:
        - seL4 microkernel + decision cache (85% of queries)
        - TinyLlama 1.1B for cache misses (15% of queries)
        - ~10-30 second inference time (acceptable for fallback)
    """

    # Model configuration for ARM64/Pi 4
    TINYLLAMA_FILENAME = "tinyllama-1.1b-chat-v1.0.Q4_K_M.gguf"

    # CPU-only parameters for Pi 4 (no GPU)
    MODEL_PARAMS = {
        'n_ctx': 2048,          # Context window
        'n_gpu_layers': 0,      # No GPU on Pi 4
        'n_threads': 4,         # Use all 4 Cortex-A72 cores
        'verbose': False
    }

    def __init__(self, models_dir: Optional[str] = None):
        """
        Initialize local AI handler.

        Args:
            models_dir: Path to models directory (auto-detected if None)
        """
        self.models_dir = models_dir or self._detect_models_dir()
        self.model = None
        self.model_loaded = False
        self.is_pi4 = self._detect_pi4()

        # Statistics
        self.stats = {
            'queries_processed': 0,
            'total_inference_time': 0.0,
            'avg_inference_time': 0.0,
            'model_load_time': 0.0
        }

        logger.info(f"LocalAIHandler initialized")
        logger.info(f"  Platform: {'Raspberry Pi 4' if self.is_pi4 else platform.machine()}")
        logger.info(f"  Models dir: {self.models_dir}")

    def _detect_pi4(self) -> bool:
        """Detect if running on Raspberry Pi 4"""
        device_tree = Path('/proc/device-tree/model')
        if device_tree.exists():
            try:
                model = device_tree.read_text().lower()
                return 'raspberry pi 4' in model
            except (OSError, IOError):
                pass
        return False

    def _detect_models_dir(self) -> str:
        """
        Auto-detect models directory.

        Searches in order:
        1. ~/models (Pi 4 local storage)
        2. /opt/jarvis/models (system install)
        3. Project models directory
        """
        # Try home directory first (common for Pi 4)
        home_models = Path.home() / 'models'
        if home_models.exists():
            return str(home_models)

        # Try system install location
        system_models = Path('/opt/jarvis/models')
        if system_models.exists():
            return str(system_models)

        # Fall back to project directory
        project_root = Path(__file__).parent.parent.parent.parent
        project_models = project_root / 'models'
        return str(project_models)

    def initialize(self) -> bool:
        """
        Initialize and load the AI model.

        Returns:
            True if model loaded successfully, False otherwise
        """
        logger.info("Initializing local AI model...")

        # Check if llama-cpp-python is available
        try:
            from llama_cpp import Llama
        except ImportError:
            logger.error("llama-cpp-python not installed!")
            logger.error("Install with: pip install llama-cpp-python")
            logger.error("For ARM64: pip install llama-cpp-python --extra-index-url https://abetlen.github.io/llama-cpp-python/whl/cpu")
            return False

        # Check model file exists
        model_path = Path(self.models_dir) / self.TINYLLAMA_FILENAME
        if not model_path.exists():
            logger.error(f"Model file not found: {model_path}")
            logger.error(f"Download TinyLlama Q4 model to: {self.models_dir}")
            return False

        # Load model
        try:
            logger.info(f"Loading TinyLlama 1.1B Q4 from {model_path}")
            logger.info("  This may take 30-60 seconds on Pi 4...")

            start_time = time.time()
            self.model = Llama(
                model_path=str(model_path),
                **self.MODEL_PARAMS
            )
            load_time = time.time() - start_time

            self.stats['model_load_time'] = load_time
            self.model_loaded = True

            logger.info(f"Model loaded successfully ({load_time:.1f}s)")
            return True

        except Exception as e:
            logger.error(f"Failed to load model: {e}")
            return False

    def generate(self, prompt: str, max_tokens: int = 100) -> Optional[str]:
        """
        Generate response using local AI model.

        Args:
            prompt: Input query/prompt
            max_tokens: Maximum tokens to generate

        Returns:
            Generated response or None if error
        """
        if not self.model_loaded:
            logger.error("Model not loaded - call initialize() first")
            return None

        try:
            start_time = time.time()

            # Format prompt for TinyLlama chat format
            formatted_prompt = self._format_prompt(prompt)

            # Generate response
            response = self.model(
                formatted_prompt,
                max_tokens=max_tokens,
                stop=["</s>", "\n\n"],  # Stop tokens
                echo=False
            )

            inference_time = time.time() - start_time

            # Update statistics
            self.stats['queries_processed'] += 1
            self.stats['total_inference_time'] += inference_time
            self.stats['avg_inference_time'] = (
                self.stats['total_inference_time'] / self.stats['queries_processed']
            )

            # Extract text from response
            result = response['choices'][0]['text'].strip()

            logger.debug(f"Generated response in {inference_time:.1f}s")
            return result

        except Exception as e:
            logger.error(f"Inference error: {e}")
            return None

    def _format_prompt(self, query: str) -> str:
        """
        Format query for TinyLlama chat model.

        Args:
            query: User query

        Returns:
            Formatted prompt string
        """
        system_prompt = """You are JARVIS, an AI assistant for operating system control.
Provide concise, actionable responses for system operations.
Focus on practical commands and clear explanations."""

        return f"""<|system|>
{system_prompt}</s>
<|user|>
{query}</s>
<|assistant|>
"""

    def get_statistics(self) -> Dict[str, Any]:
        """Get inference statistics"""
        return {
            **self.stats,
            'model_loaded': self.model_loaded,
            'is_pi4': self.is_pi4,
            'models_dir': self.models_dir
        }

    def shutdown(self):
        """Cleanup and release model"""
        if self.model:
            del self.model
            self.model = None
            self.model_loaded = False
            logger.info("Local AI model unloaded")


class LocalAIFallback:
    """
    Fallback handler that uses local AI when UART to PC is unavailable.

    Provides seamless fallback:
    1. Try UART IPC to PC (fast, ~500ms)
    2. If UART unavailable/timeout, use local AI (slow, ~10-30s)
    3. Cache results to minimize future AI calls
    """

    def __init__(self, uart_client=None, enable_local_fallback: bool = True):
        """
        Initialize fallback handler.

        Args:
            uart_client: UARTIPCClient instance (optional)
            enable_local_fallback: Enable local AI fallback (default: True)
        """
        self.uart_client = uart_client
        self.enable_local_fallback = enable_local_fallback
        self.local_handler: Optional[LocalAIHandler] = None
        self.local_initialized = False

        # Statistics
        self.stats = {
            'uart_queries': 0,
            'local_queries': 0,
            'uart_failures': 0,
            'fallback_activations': 0
        }

    def _init_local_handler(self) -> bool:
        """Lazily initialize local AI handler"""
        if self.local_initialized:
            return self.local_handler is not None and self.local_handler.model_loaded

        self.local_initialized = True
        self.local_handler = LocalAIHandler()
        return self.local_handler.initialize()

    def query(self, prompt: str, timeout: float = 5.0) -> Optional[str]:
        """
        Query with automatic fallback.

        Tries UART first, falls back to local AI if unavailable.

        Args:
            prompt: Query string
            timeout: UART timeout in seconds

        Returns:
            Response string or None
        """
        # Try UART first if available
        if self.uart_client and self.uart_client.is_connected():
            try:
                response = self.uart_client.send_query(prompt, timeout=timeout)
                if response:
                    self.stats['uart_queries'] += 1
                    return response
            except Exception as e:
                logger.warning(f"UART query failed: {e}")
                self.stats['uart_failures'] += 1

        # Fallback to local AI
        if self.enable_local_fallback:
            if not self.local_initialized:
                logger.info("UART unavailable, initializing local AI fallback...")
                self.stats['fallback_activations'] += 1
                if not self._init_local_handler():
                    logger.error("Local AI initialization failed")
                    return None

            if self.local_handler and self.local_handler.model_loaded:
                self.stats['local_queries'] += 1
                logger.info("Using local AI (this will take 10-30 seconds)...")
                return self.local_handler.generate(prompt)

        return None

    def get_statistics(self) -> Dict[str, Any]:
        """Get combined statistics"""
        stats = {**self.stats}
        if self.local_handler:
            stats['local_ai'] = self.local_handler.get_statistics()
        return stats


# ============================================================================
# Self-test
# ============================================================================
if __name__ == "__main__":
    print("JARVIS AI-OS - Local AI Handler Test")
    print("=" * 70)

    handler = LocalAIHandler()

    print("\n[1] Checking model availability...")
    model_path = Path(handler.models_dir) / handler.TINYLLAMA_FILENAME
    if model_path.exists():
        print(f"    Model found: {model_path}")
        print(f"    Size: {model_path.stat().st_size / 1024 / 1024:.1f} MB")
    else:
        print(f"    Model NOT found: {model_path}")
        print("    Download TinyLlama Q4 model to continue")
        sys.exit(1)

    print("\n[2] Initializing model (this may take 30-60 seconds)...")
    if not handler.initialize():
        print("    Failed to initialize model")
        sys.exit(1)

    print("\n[3] Testing inference...")
    test_queries = [
        "What is the current system time?",
        "How do I list files in a directory?",
    ]

    for query in test_queries:
        print(f"\n    Query: {query}")
        start = time.time()
        response = handler.generate(query, max_tokens=50)
        elapsed = time.time() - start
        print(f"    Response ({elapsed:.1f}s): {response[:100]}...")

    print("\n[4] Statistics:")
    stats = handler.get_statistics()
    for key, value in stats.items():
        print(f"    {key}: {value}")

    print("\n" + "=" * 70)
    print("Local AI handler test complete!")
    handler.shutdown()
