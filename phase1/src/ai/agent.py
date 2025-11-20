#!/usr/bin/env python3
"""
JARVIS AI-OS Phase 1 - AI Agent
Week 5: AI Agent Bootstrap
Week 6: Query Processing Pipeline Integration

This is the main AI decision engine that:
1. Loads Phi-3 Mini 3.8B model
2. Processes queries from seL4 kernel via IPC
3. Generates command responses
4. Integrates with decision cache (Week 6)
5. Uses query processor for normalization and command parsing (Week 6)

Model: Phi-3 Mini 3.8B Q4 (2.23GB)
Target: <600ms inference time (Phase 0 validated: 558ms GPU)
"""

import os
import sys
import time
import logging
from pathlib import Path

# Add parent directory to path for imports
sys.path.insert(0, str(Path(__file__).parent.parent.parent))

try:
    from llama_cpp import Llama
except ImportError:
    print("ERROR: llama-cpp-python not installed!")
    print("Install with: pip install llama-cpp-python")
    sys.exit(1)

# Import query processor (Week 6)
try:
    from query_processor import QueryProcessor
except ImportError:
    QueryProcessor = None
    print("WARNING: query_processor not available (Week 5 compatibility mode)")

# ============================================================================
# Configuration
# ============================================================================

def _get_model_path():
    """
    Get the correct model path based on environment (Windows vs WSL)

    Returns:
        str: Absolute path to model file
    """
    # Check if running in WSL
    is_wsl = False
    if sys.platform == 'linux':
        try:
            with open('/proc/version', 'r') as f:
                is_wsl = 'microsoft' in f.read().lower() or 'wsl' in f.read().lower()
        except:
            pass

    # Base Windows path
    windows_path = "C:/Users/jluca/Documents/JARVIS_OS/models/Phi-3-mini-4k-instruct-q4.gguf"

    if is_wsl:
        # Convert Windows path to WSL path: C:/ -> /mnt/c/
        wsl_path = windows_path.replace('C:/', '/mnt/c/')
        return wsl_path
    else:
        return windows_path

# Model path (auto-detects Windows vs WSL)
MODEL_PATH = _get_model_path()

def _get_gpu_layers():
    """
    Auto-detect GPU availability and return appropriate n_gpu_layers

    WSL typically doesn't have GPU access for llama-cpp-python unless
    CUDA passthrough is specifically configured.

    Returns:
        int: Number of layers to offload to GPU (0 = CPU-only, 35 = full GPU)
    """
    # Check if running in WSL
    is_wsl = False
    if sys.platform == 'linux':
        try:
            with open('/proc/version', 'r') as f:
                is_wsl = 'microsoft' in f.read().lower() or 'wsl' in f.read().lower()
        except:
            pass

    # WSL: Default to CPU-only (GPU passthrough is rare)
    if is_wsl:
        return 0

    # Windows/native Linux: Use GPU
    return 35

# Phi-3 configuration (from Phase 0 benchmarks)
PHI3_CONFIG = {
    'n_ctx': 2048,          # Context window (4096 max, but 2048 sufficient for commands)
    'n_gpu_layers': _get_gpu_layers(),  # Auto-detect: 35 (Windows/Linux) or 0 (WSL)
    'n_threads': 4,         # CPU threads for host operations
    'verbose': False        # Quiet loading
}

# Inference parameters
INFERENCE_CONFIG = {
    'max_tokens': 50,       # Short responses for commands
    'temperature': 0.7,     # Moderate creativity
    'top_p': 0.9,           # Nucleus sampling
    'stop': ["\n"],         # Stop at newline for command responses
    'echo': False           # Don't echo prompt back
}

# Logging configuration
logging.basicConfig(
    level=logging.INFO,
    format='[%(asctime)s] [%(levelname)s] %(message)s',
    datefmt='%H:%M:%S'
)
logger = logging.getLogger('jarvis.ai')

# ============================================================================
# AI Agent Class
# ============================================================================

class JARVISAgent:
    """
    JARVIS AI Decision Engine

    Responsibilities:
    - Load and manage Phi-3 Mini model
    - Process user queries
    - Generate command responses
    - Integrate with decision cache (future)
    - Communicate via IPC (future)
    """

    def __init__(self, model_path=MODEL_PATH, use_query_processor=True):
        """
        Initialize JARVIS AI agent

        Args:
            model_path (str): Path to Phi-3 Mini model
            use_query_processor (bool): Enable query processor (Week 6+)
        """
        self.model_path = model_path
        self.model = None
        self.load_time = 0
        self.query_count = 0
        self.total_inference_time = 0

        # Week 6: Query processor integration
        self.use_query_processor = use_query_processor and (QueryProcessor is not None)
        self.query_processor = QueryProcessor() if self.use_query_processor else None

        week = "Week 6" if self.use_query_processor else "Week 5"
        logger.info("="*70)
        logger.info(f"JARVIS AI-OS - Phase 1 {week}")
        logger.info("AI Agent Bootstrap" if not self.use_query_processor else "AI Agent + Query Pipeline")
        logger.info("="*70)
        logger.info("")

        if self.use_query_processor:
            logger.info("[QUERY PROCESSOR] Enabled")
            logger.info("")

    def load_model(self):
        """
        Load Phi-3 Mini model with GPU acceleration

        Returns:
            bool: True if successful, False otherwise
        """
        logger.info("[LOADING MODEL]")
        logger.info(f"  Model: {self.model_path}")
        logger.info(f"  GPU layers: {PHI3_CONFIG['n_gpu_layers']}")
        logger.info(f"  Context: {PHI3_CONFIG['n_ctx']} tokens")
        logger.info("")

        # Check if model file exists
        if not os.path.exists(self.model_path):
            logger.error(f"  ERROR: Model file not found: {self.model_path}")
            return False

        # Get model file size
        model_size_gb = os.path.getsize(self.model_path) / (1024**3)
        logger.info(f"  Model size: {model_size_gb:.2f} GB")

        # Load model
        start_time = time.time()

        try:
            self.model = Llama(
                model_path=self.model_path,
                **PHI3_CONFIG
            )

            self.load_time = time.time() - start_time

            logger.info(f"  ✓ Model loaded successfully")
            logger.info(f"  ✓ Load time: {self.load_time:.2f}s")
            logger.info("")

            return True

        except Exception as e:
            logger.error(f"  ERROR: Failed to load model: {e}")
            return False

    def process_query(self, user_query):
        """
        Process a user query and generate response

        Week 6: Now integrates with query processor for:
        - Query normalization
        - Cache lookup (if available)
        - Command parsing

        Args:
            user_query (str): User's question or command

        Returns:
            dict: {
                'response': str,
                'inference_time_ms': float,
                'tokens_generated': int,
                'success': bool,
                'command': dict (Week 6+),
                'cache_hit': bool (Week 6+)
            }
        """
        if not self.model:
            return {
                'response': 'ERROR: Model not loaded',
                'inference_time_ms': 0,
                'tokens_generated': 0,
                'success': False
            }

        logger.info(f"[QUERY] {user_query}")

        # Week 6: Use query processor if enabled
        if self.use_query_processor:
            return self._process_query_with_pipeline(user_query)
        else:
            return self._process_query_direct(user_query)

    def _process_query_direct(self, user_query):
        """
        Direct AI inference (Week 5 behavior)

        Args:
            user_query (str): Raw user query

        Returns:
            dict: Response with AI inference only
        """

        start_time = time.time()

        try:
            # Generate response
            output = self.model(
                user_query,
                **INFERENCE_CONFIG
            )

            inference_time = (time.time() - start_time) * 1000  # Convert to ms

            # Extract response text
            response_text = output['choices'][0]['text'].strip()

            # Extract token count
            usage = output.get('usage', {})
            tokens_generated = usage.get('completion_tokens', 0)

            # Update statistics
            self.query_count += 1
            self.total_inference_time += inference_time

            logger.info(f"[RESPONSE] {response_text}")
            logger.info(f"  Inference time: {inference_time:.2f}ms")
            logger.info(f"  Tokens: {tokens_generated}")
            logger.info(f"  Tokens/sec: {tokens_generated / (inference_time / 1000):.2f}")
            logger.info("")

            return {
                'response': response_text,
                'inference_time_ms': inference_time,
                'tokens_generated': tokens_generated,
                'success': True
            }

        except Exception as e:
            logger.error(f"  ERROR during inference: {e}")
            return {
                'response': f'ERROR: {str(e)}',
                'inference_time_ms': (time.time() - start_time) * 1000,
                'tokens_generated': 0,
                'success': False
            }

    def _process_query_with_pipeline(self, user_query):
        """
        Process query with query processor pipeline (Week 6)

        Flow:
        1. Normalize query
        2. Check cache
        3. If cache miss → AI inference
        4. Parse response into command
        5. Return command + response

        Args:
            user_query (str): Raw user query

        Returns:
            dict: Response with command structure and cache info
        """
        start_time = time.time()

        # Step 1-2: Process through query processor (normalize + cache check)
        command, cache_hit = self.query_processor.process(user_query)

        if cache_hit:
            # Cache hit - return immediately
            total_time = (time.time() - start_time) * 1000

            logger.info(f"[CACHE HIT]")
            logger.info(f"  Command: {command['command']}")
            logger.info(f"  Total time: {total_time:.2f}ms")
            logger.info("")

            return {
                'response': f"Cached: {command['command']}",
                'inference_time_ms': 0,
                'tokens_generated': 0,
                'success': True,
                'command': command,
                'cache_hit': True
            }

        # Step 3: Cache miss - AI inference
        logger.info(f"[CACHE MISS] - Running AI inference")

        try:
            ai_response = self.model(
                user_query,
                **INFERENCE_CONFIG
            )

            inference_time = (time.time() - start_time) * 1000

            response_text = ai_response['choices'][0]['text'].strip()
            usage = ai_response.get('usage', {})
            tokens_generated = usage.get('completion_tokens', 0)

            # Step 4: Re-process with AI response for parsing
            command, _ = self.query_processor.process(user_query, response_text)

            # Update statistics
            self.query_count += 1
            self.total_inference_time += inference_time

            logger.info(f"[AI RESPONSE] {response_text}")
            logger.info(f"  Inference time: {inference_time:.2f}ms")
            logger.info(f"  Parsed command: {command['command']}")
            logger.info(f"  Tokens: {tokens_generated}")
            logger.info("")

            return {
                'response': response_text,
                'inference_time_ms': inference_time,
                'tokens_generated': tokens_generated,
                'success': True,
                'command': command,
                'cache_hit': False
            }

        except Exception as e:
            logger.error(f"  ERROR during inference: {e}")
            return {
                'response': f'ERROR: {str(e)}',
                'inference_time_ms': (time.time() - start_time) * 1000,
                'tokens_generated': 0,
                'success': False,
                'command': {'command': 'ERROR', 'parameters': {}, 'trust_level': 3, 'success': False},
                'cache_hit': False
            }

    def get_statistics(self):
        """
        Get agent statistics

        Returns:
            dict: Statistics about agent performance
        """
        avg_inference_time = (
            self.total_inference_time / self.query_count
            if self.query_count > 0
            else 0
        )

        return {
            'query_count': self.query_count,
            'total_inference_time_ms': self.total_inference_time,
            'avg_inference_time_ms': avg_inference_time,
            'load_time_s': self.load_time,
            'model_path': self.model_path
        }

    def print_statistics(self):
        """Print agent statistics"""
        stats = self.get_statistics()

        logger.info("="*70)
        logger.info("[AI AGENT STATISTICS]")
        logger.info("="*70)
        logger.info(f"  Total queries: {stats['query_count']}")
        logger.info(f"  Avg inference time: {stats['avg_inference_time_ms']:.2f}ms")
        logger.info(f"  Total inference time: {stats['total_inference_time_ms']:.2f}ms")
        logger.info(f"  Model load time: {stats['load_time_s']:.2f}s")
        logger.info(f"  Model: {Path(stats['model_path']).name}")
        logger.info("="*70)
        logger.info("")

        # Print query processor statistics (Week 6+)
        if self.query_processor:
            self.query_processor.print_statistics()

# ============================================================================
# Main Entry Point (for standalone testing)
# ============================================================================

def main():
    """
    Standalone test of AI agent

    Tests:
    1. Load Phi-3 Mini model
    2. Process sample queries
    3. Measure inference time
    4. Verify <600ms target
    """
    logger.info("Starting JARVIS AI Agent standalone test")
    logger.info("")

    # Create agent
    agent = JARVISAgent()

    # Load model
    if not agent.load_model():
        logger.error("Failed to load model. Exiting.")
        sys.exit(1)

    # Test queries (from Phase 0 benchmarks)
    test_queries = [
        "What is the current system CPU usage?",
        "Check network connectivity status",
        "Show available disk space",
        "List all running processes",
        "What's the system memory usage?",
        "help",
        "status",
        "show CPU info"
    ]

    logger.info("[TESTING INFERENCE]")
    logger.info(f"  Running {len(test_queries)} test queries")
    logger.info("")

    results = []
    for i, query in enumerate(test_queries):
        logger.info(f"Test {i+1}/{len(test_queries)}")
        result = agent.process_query(query)
        results.append(result)

    # Calculate statistics
    successful = [r for r in results if r['success']]
    inference_times = [r['inference_time_ms'] for r in successful]

    if inference_times:
        avg_time = sum(inference_times) / len(inference_times)
        min_time = min(inference_times)
        max_time = max(inference_times)

        logger.info("="*70)
        logger.info("[TEST RESULTS]")
        logger.info("="*70)
        logger.info(f"  Successful queries: {len(successful)}/{len(test_queries)}")
        logger.info(f"  Avg inference time: {avg_time:.2f}ms")
        logger.info(f"  Min inference time: {min_time:.2f}ms")
        logger.info(f"  Max inference time: {max_time:.2f}ms")
        logger.info("")

        # Check against target
        TARGET_MS = 600
        if avg_time < TARGET_MS:
            logger.info(f"  ✓ PASS: Avg time {avg_time:.2f}ms < target {TARGET_MS}ms")
        else:
            logger.warning(f"  ⚠ FAIL: Avg time {avg_time:.2f}ms > target {TARGET_MS}ms")

        logger.info("="*70)
        logger.info("")

    # Print agent statistics
    agent.print_statistics()

    logger.info("[TEST COMPLETE]")
    logger.info("Week 5 Task 2 (AI Agent) ✓ COMPLETE")
    logger.info("")

if __name__ == "__main__":
    main()
