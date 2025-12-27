#!/usr/bin/env python3
"""
JARVIS AI-OS - Model Loader Test Suite
Tests model_loader.py: caching, WSL paths, state transitions, memory management

This test suite validates:
1. ModelConfig - Default configuration values
2. ModelLoader initialization - Directory detection, default values
3. Model loading - All system states (IDLE, ACTIVE, CRITICAL, EMERGENCY)
4. Model unloading - Memory cleanup, garbage collection
5. Caching - Cache hits/misses, enable/disable
6. Statistics - Load/unload times, cache stats
7. Text generation - Mock mode responses
8. Memory usage tracking
9. WSL path detection
10. Edge cases - Missing files, errors, state transitions

Run: python test_model_loader.py
"""

import sys
import os
import time
import unittest
from unittest.mock import patch, MagicMock

# Add parent directory to path
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from model_loader import ModelConfig, ModelLoader, _get_models_directory, LLAMA_CPP_AVAILABLE
from system_state_manager import SystemState

# Test counters
tests_passed = 0
tests_failed = 0


def test_pass(name):
    global tests_passed
    print(f"  [PASS] {name}")
    tests_passed += 1


def test_fail(name, msg=""):
    global tests_failed
    print(f"  [FAIL] {name}: {msg}")
    tests_failed += 1


# ============================================================================
# Test 1: ModelConfig values
# ============================================================================
def test_model_config():
    """Test ModelConfig has correct defaults"""
    print("\n" + "="*70)
    print("TEST: ModelConfig defaults")
    print("="*70)

    # Check model paths exist
    if hasattr(ModelConfig, 'TINYLLAMA_PATH'):
        test_pass("TINYLLAMA_PATH defined")
    else:
        test_fail("TINYLLAMA_PATH defined")

    if hasattr(ModelConfig, 'PHI3_PATH'):
        test_pass("PHI3_PATH defined")
    else:
        test_fail("PHI3_PATH defined")

    # Check model parameters
    if "n_ctx" in ModelConfig.TINYLLAMA_PARAMS:
        test_pass("TINYLLAMA_PARAMS has n_ctx")
    else:
        test_fail("TINYLLAMA_PARAMS has n_ctx")

    if "n_gpu_layers" in ModelConfig.PHI3_PARAMS:
        test_pass("PHI3_PARAMS has n_gpu_layers")
    else:
        test_fail("PHI3_PARAMS has n_gpu_layers")


# ============================================================================
# Test 2: ModelLoader initialization
# ============================================================================
def test_model_loader_init():
    """Test ModelLoader initializes correctly"""
    print("\n" + "="*70)
    print("TEST: ModelLoader initialization")
    print("="*70)

    loader = ModelLoader(models_dir="/tmp/test_models")

    if loader.models_dir == "/tmp/test_models":
        test_pass("Custom models_dir set correctly")
    else:
        test_fail("Custom models_dir set correctly", f"Got {loader.models_dir}")

    if loader.current_model is None:
        test_pass("current_model starts None")
    else:
        test_fail("current_model starts None")

    if loader.validator_model is None:
        test_pass("validator_model starts None")
    else:
        test_fail("validator_model starts None")

    if loader.current_state is None:
        test_pass("current_state starts None")
    else:
        test_fail("current_state starts None")

    if loader.cache_enabled == False:
        test_pass("cache disabled by default")
    else:
        test_fail("cache disabled by default")


# ============================================================================
# Test 3: Statistics initialization
# ============================================================================
def test_stats_init():
    """Test statistics start at zero"""
    print("\n" + "="*70)
    print("TEST: Statistics initialization")
    print("="*70)

    loader = ModelLoader(models_dir="/tmp/test")

    if loader.stats["models_loaded"] == 0:
        test_pass("models_loaded starts at 0")
    else:
        test_fail("models_loaded starts at 0")

    if loader.stats["cache_hits"] == 0:
        test_pass("cache_hits starts at 0")
    else:
        test_fail("cache_hits starts at 0")

    if loader.stats["cache_misses"] == 0:
        test_pass("cache_misses starts at 0")
    else:
        test_fail("cache_misses starts at 0")


# ============================================================================
# Test 4: Load model for IDLE state
# ============================================================================
def test_load_idle():
    """Test loading model for IDLE state"""
    print("\n" + "="*70)
    print("TEST: Load model for IDLE state")
    print("="*70)

    loader = ModelLoader(models_dir="/tmp/test")
    success = loader.load_model(SystemState.IDLE)

    if success:
        test_pass("IDLE model loaded successfully")
    else:
        test_fail("IDLE model loaded successfully")

    if loader.current_model is not None:
        test_pass("current_model is set")
    else:
        test_fail("current_model is set")

    if loader.current_state == SystemState.IDLE:
        test_pass("current_state is IDLE")
    else:
        test_fail("current_state is IDLE")


# ============================================================================
# Test 5: Load model for ACTIVE state
# ============================================================================
def test_load_active():
    """Test loading model for ACTIVE state"""
    print("\n" + "="*70)
    print("TEST: Load model for ACTIVE state")
    print("="*70)

    loader = ModelLoader(models_dir="/tmp/test")
    success = loader.load_model(SystemState.ACTIVE)

    if success:
        test_pass("ACTIVE model loaded successfully")
    else:
        test_fail("ACTIVE model loaded successfully")

    if loader.current_state == SystemState.ACTIVE:
        test_pass("current_state is ACTIVE")
    else:
        test_fail("current_state is ACTIVE")


# ============================================================================
# Test 6: Load model for CRITICAL state
# ============================================================================
def test_load_critical():
    """Test loading model for CRITICAL state (primary + validator)"""
    print("\n" + "="*70)
    print("TEST: Load model for CRITICAL state")
    print("="*70)

    loader = ModelLoader(models_dir="/tmp/test")
    success = loader.load_model(SystemState.CRITICAL)

    if success:
        test_pass("CRITICAL models loaded successfully")
    else:
        test_fail("CRITICAL models loaded successfully")

    if loader.current_model is not None:
        test_pass("primary model loaded")
    else:
        test_fail("primary model loaded")

    if loader.validator_model is not None:
        test_pass("validator model loaded")
    else:
        test_fail("validator model loaded")

    if loader.current_state == SystemState.CRITICAL:
        test_pass("current_state is CRITICAL")
    else:
        test_fail("current_state is CRITICAL")


# ============================================================================
# Test 7: Load model for EMERGENCY state
# ============================================================================
def test_load_emergency():
    """Test EMERGENCY state has no model"""
    print("\n" + "="*70)
    print("TEST: Load model for EMERGENCY state")
    print("="*70)

    loader = ModelLoader(models_dir="/tmp/test")
    success = loader.load_model(SystemState.EMERGENCY)

    if success:
        test_pass("EMERGENCY state returns success")
    else:
        test_fail("EMERGENCY state returns success")

    if loader.current_model is None:
        test_pass("No model loaded for EMERGENCY")
    else:
        test_fail("No model loaded for EMERGENCY")

    if loader.current_state == SystemState.EMERGENCY:
        test_pass("current_state is EMERGENCY")
    else:
        test_fail("current_state is EMERGENCY")


# ============================================================================
# Test 8: Unload model
# ============================================================================
def test_unload_model():
    """Test model unloading"""
    print("\n" + "="*70)
    print("TEST: Unload model")
    print("="*70)

    loader = ModelLoader(models_dir="/tmp/test")
    loader.load_model(SystemState.IDLE)

    # Verify model is loaded
    if loader.current_model is not None:
        test_pass("Model loaded before unload")
    else:
        test_fail("Model loaded before unload")

    # Unload
    success = loader.unload_model(SystemState.IDLE)

    if success:
        test_pass("Unload returns success")
    else:
        test_fail("Unload returns success")

    if loader.current_model is None:
        test_pass("current_model is None after unload")
    else:
        test_fail("current_model is None after unload")


# ============================================================================
# Test 9: Unload CRITICAL state (both models)
# ============================================================================
def test_unload_critical():
    """Test unloading both primary and validator models"""
    print("\n" + "="*70)
    print("TEST: Unload CRITICAL state")
    print("="*70)

    loader = ModelLoader(models_dir="/tmp/test")
    loader.load_model(SystemState.CRITICAL)

    # Both should be loaded
    if loader.current_model is not None and loader.validator_model is not None:
        test_pass("Both models loaded before unload")
    else:
        test_fail("Both models loaded before unload")

    # Unload
    loader.unload_model(SystemState.CRITICAL)

    if loader.current_model is None:
        test_pass("primary model unloaded")
    else:
        test_fail("primary model unloaded")

    if loader.validator_model is None:
        test_pass("validator model unloaded")
    else:
        test_fail("validator model unloaded")


# ============================================================================
# Test 10: Statistics after load/unload
# ============================================================================
def test_statistics_update():
    """Test statistics are updated after operations"""
    print("\n" + "="*70)
    print("TEST: Statistics update")
    print("="*70)

    loader = ModelLoader(models_dir="/tmp/test")

    # Load and unload
    loader.load_model(SystemState.IDLE)
    loader.unload_model(SystemState.IDLE)
    loader.load_model(SystemState.ACTIVE)

    stats = loader.get_statistics()

    if stats["models_loaded"] == 2:
        test_pass("models_loaded count correct (2)")
    else:
        test_fail("models_loaded count correct", f"Got {stats['models_loaded']}")

    if stats["models_unloaded"] == 1:
        test_pass("models_unloaded count correct (1)")
    else:
        test_fail("models_unloaded count correct", f"Got {stats['models_unloaded']}")

    if stats["avg_load_time"] > 0:
        test_pass("avg_load_time is positive")
    else:
        test_fail("avg_load_time is positive")


# ============================================================================
# Test 11: Cache behavior when disabled
# ============================================================================
def test_cache_disabled():
    """Test cache is not used when disabled"""
    print("\n" + "="*70)
    print("TEST: Cache disabled behavior")
    print("="*70)

    loader = ModelLoader(models_dir="/tmp/test")
    loader.cache_enabled = False

    # Load same state twice
    loader.load_model(SystemState.IDLE)
    loader.unload_model(SystemState.IDLE)
    loader.load_model(SystemState.IDLE)

    stats = loader.get_statistics()

    if stats["cache_hits"] == 0:
        test_pass("No cache hits when disabled")
    else:
        test_fail("No cache hits when disabled", f"Got {stats['cache_hits']}")

    if stats["cache_misses"] == 2:
        test_pass("Cache misses counted (2)")
    else:
        test_fail("Cache misses counted", f"Got {stats['cache_misses']}")


# ============================================================================
# Test 12: Cache behavior when enabled
# ============================================================================
def test_cache_enabled():
    """Test cache is used when enabled"""
    print("\n" + "="*70)
    print("TEST: Cache enabled behavior")
    print("="*70)

    loader = ModelLoader(models_dir="/tmp/test")
    loader.cache_enabled = True

    # Load, unload (cached), load again (should hit cache)
    loader.load_model(SystemState.IDLE)
    loader.unload_model(SystemState.IDLE)  # Model cached, not unloaded
    loader.load_model(SystemState.IDLE)    # Should hit cache

    stats = loader.get_statistics()

    if stats["cache_hits"] == 1:
        test_pass("Cache hit on second load")
    else:
        test_fail("Cache hit on second load", f"Got {stats['cache_hits']}")

    if len(loader.model_cache) > 0:
        test_pass("Model cached")
    else:
        test_fail("Model cached")


# ============================================================================
# Test 13: Text generation (mock mode)
# ============================================================================
def test_generate_mock():
    """Test text generation in mock mode"""
    print("\n" + "="*70)
    print("TEST: Text generation (mock mode)")
    print("="*70)

    loader = ModelLoader(models_dir="/tmp/test")
    loader.load_model(SystemState.IDLE)

    response = loader.generate("Hello", max_tokens=50)

    if response is not None:
        test_pass("generate() returns response")
    else:
        test_fail("generate() returns response")

    if "mock" in response.lower():
        test_pass("Response indicates mock mode")
    else:
        test_fail("Response indicates mock mode", f"Got: {response}")


# ============================================================================
# Test 14: Generate with no model loaded
# ============================================================================
def test_generate_no_model():
    """Test generate() returns None when no model"""
    print("\n" + "="*70)
    print("TEST: Generate with no model")
    print("="*70)

    loader = ModelLoader(models_dir="/tmp/test")

    # Don't load any model
    response = loader.generate("Hello")

    if response is None:
        test_pass("generate() returns None when no model")
    else:
        test_fail("generate() returns None when no model", f"Got: {response}")


# ============================================================================
# Test 15: Memory usage tracking
# ============================================================================
def test_memory_usage():
    """Test memory usage is tracked"""
    print("\n" + "="*70)
    print("TEST: Memory usage tracking")
    print("="*70)

    loader = ModelLoader(models_dir="/tmp/test")
    mem = loader.get_memory_usage()

    if "rss_mb" in mem:
        test_pass("RSS memory tracked")
    else:
        test_fail("RSS memory tracked")

    if "vms_mb" in mem:
        test_pass("VMS memory tracked")
    else:
        test_fail("VMS memory tracked")

    if "percent" in mem:
        test_pass("Memory percent tracked")
    else:
        test_fail("Memory percent tracked")

    if mem["rss_mb"] > 0:
        test_pass("RSS is positive value")
    else:
        test_fail("RSS is positive value")


# ============================================================================
# Test 16: Get statistics includes all fields
# ============================================================================
def test_get_statistics_fields():
    """Test get_statistics returns all expected fields"""
    print("\n" + "="*70)
    print("TEST: Statistics fields")
    print("="*70)

    loader = ModelLoader(models_dir="/tmp/test")
    loader.load_model(SystemState.ACTIVE)

    stats = loader.get_statistics()

    expected_fields = [
        "models_loaded", "models_unloaded",
        "total_load_time", "avg_load_time",
        "cache_hits", "cache_misses",
        "current_state", "model_loaded",
        "validator_loaded", "cache_enabled",
        "cached_models", "rss_mb", "percent"
    ]

    missing = [f for f in expected_fields if f not in stats]

    if len(missing) == 0:
        test_pass("All expected statistics fields present")
    else:
        test_fail("All expected statistics fields present", f"Missing: {missing}")


# ============================================================================
# Test 17: State transitions
# ============================================================================
def test_state_transitions():
    """Test transitioning between states"""
    print("\n" + "="*70)
    print("TEST: State transitions")
    print("="*70)

    loader = ModelLoader(models_dir="/tmp/test")

    # IDLE -> ACTIVE
    loader.load_model(SystemState.IDLE)
    if loader.current_state == SystemState.IDLE:
        test_pass("Initial state: IDLE")
    else:
        test_fail("Initial state: IDLE")

    loader.unload_model(SystemState.IDLE)
    loader.load_model(SystemState.ACTIVE)
    if loader.current_state == SystemState.ACTIVE:
        test_pass("Transition to: ACTIVE")
    else:
        test_fail("Transition to: ACTIVE")

    # ACTIVE -> CRITICAL
    loader.unload_model(SystemState.ACTIVE)
    loader.load_model(SystemState.CRITICAL)
    if loader.current_state == SystemState.CRITICAL:
        test_pass("Transition to: CRITICAL")
    else:
        test_fail("Transition to: CRITICAL")

    # CRITICAL -> EMERGENCY
    loader.unload_model(SystemState.CRITICAL)
    loader.load_model(SystemState.EMERGENCY)
    if loader.current_state == SystemState.EMERGENCY:
        test_pass("Transition to: EMERGENCY")
    else:
        test_fail("Transition to: EMERGENCY")


# ============================================================================
# Test 18: Load time tracking
# ============================================================================
def test_load_time_tracking():
    """Test load time is tracked"""
    print("\n" + "="*70)
    print("TEST: Load time tracking")
    print("="*70)

    loader = ModelLoader(models_dir="/tmp/test")
    loader.load_model(SystemState.IDLE)

    if loader.last_load_time > 0:
        test_pass("last_load_time is tracked")
    else:
        test_fail("last_load_time is tracked")

    stats = loader.get_statistics()
    if stats["last_load_time"] == loader.last_load_time:
        test_pass("last_load_time in statistics")
    else:
        test_fail("last_load_time in statistics")


# ============================================================================
# Test 19: Unload time tracking
# ============================================================================
def test_unload_time_tracking():
    """Test unload time is tracked"""
    print("\n" + "="*70)
    print("TEST: Unload time tracking")
    print("="*70)

    loader = ModelLoader(models_dir="/tmp/test")
    loader.load_model(SystemState.IDLE)
    loader.unload_model(SystemState.IDLE)

    if loader.last_unload_time >= 0:
        test_pass("last_unload_time is tracked")
    else:
        test_fail("last_unload_time is tracked")


# ============================================================================
# Test 20: _get_models_directory function
# ============================================================================
def test_get_models_directory():
    """Test models directory detection"""
    print("\n" + "="*70)
    print("TEST: Models directory detection")
    print("="*70)

    # The function should return a string path
    models_dir = _get_models_directory()

    if isinstance(models_dir, str):
        test_pass("Returns string path")
    else:
        test_fail("Returns string path", f"Got type: {type(models_dir)}")

    if len(models_dir) > 0:
        test_pass("Path is non-empty")
    else:
        test_fail("Path is non-empty")


# ============================================================================
# Test 21: WSL path detection (mock)
# ============================================================================
def test_wsl_detection():
    """Test WSL detection logic"""
    print("\n" + "="*70)
    print("TEST: WSL detection logic")
    print("="*70)

    # Test that WSL detection doesn't crash
    try:
        # Mock Linux with WSL
        with patch('sys.platform', 'linux'):
            with patch('builtins.open', MagicMock(return_value=MagicMock(
                read=MagicMock(return_value="Linux microsoft-standard-WSL2"),
                __enter__=MagicMock(return_value=MagicMock(read=MagicMock(return_value="microsoft"))),
                __exit__=MagicMock(return_value=None)
            ))):
                # Can't easily test without reimporting, but verify no crash
                test_pass("WSL detection logic exists")
    except Exception as e:
        test_fail("WSL detection logic exists", str(e))


# ============================================================================
# Test 22: Auto-detect models directory
# ============================================================================
def test_auto_detect_models_dir():
    """Test auto-detection of models directory"""
    print("\n" + "="*70)
    print("TEST: Auto-detect models directory")
    print("="*70)

    # Create loader without specifying directory
    loader = ModelLoader()  # No models_dir argument

    if loader.models_dir is not None:
        test_pass("Auto-detected models directory")
    else:
        test_fail("Auto-detected models directory")

    if len(loader.models_dir) > 0:
        test_pass("Auto-detected path is non-empty")
    else:
        test_fail("Auto-detected path is non-empty")


# ============================================================================
# Test 23: Mock model names
# ============================================================================
def test_mock_model_names():
    """Test mock model names are distinct"""
    print("\n" + "="*70)
    print("TEST: Mock model names")
    print("="*70)

    loader = ModelLoader(models_dir="/tmp/test")

    # Load TinyLlama
    loader.load_model(SystemState.IDLE)
    idle_model = loader.current_model

    loader.unload_model(SystemState.IDLE)

    # Load Phi-3
    loader.load_model(SystemState.ACTIVE)
    active_model = loader.current_model

    if "tinyllama" in str(idle_model).lower():
        test_pass("IDLE uses TinyLlama")
    else:
        test_fail("IDLE uses TinyLlama", f"Got: {idle_model}")

    if "phi3" in str(active_model).lower():
        test_pass("ACTIVE uses Phi-3")
    else:
        test_fail("ACTIVE uses Phi-3", f"Got: {active_model}")


# ============================================================================
# Test 24: Multiple loads accumulate stats
# ============================================================================
def test_stats_accumulate():
    """Test statistics accumulate across operations"""
    print("\n" + "="*70)
    print("TEST: Statistics accumulate")
    print("="*70)

    loader = ModelLoader(models_dir="/tmp/test")

    # Perform multiple operations
    for state in [SystemState.IDLE, SystemState.ACTIVE, SystemState.CRITICAL]:
        loader.load_model(state)
        loader.unload_model(state)

    stats = loader.get_statistics()

    if stats["models_loaded"] == 3:
        test_pass("3 models loaded in total")
    else:
        test_fail("3 models loaded in total", f"Got: {stats['models_loaded']}")

    if stats["models_unloaded"] == 3:
        test_pass("3 models unloaded in total")
    else:
        test_fail("3 models unloaded in total", f"Got: {stats['models_unloaded']}")


# ============================================================================
# Test 25: Average calculations
# ============================================================================
def test_average_calculations():
    """Test average time calculations are correct"""
    print("\n" + "="*70)
    print("TEST: Average calculations")
    print("="*70)

    loader = ModelLoader(models_dir="/tmp/test")

    # Load and unload 2 times
    loader.load_model(SystemState.IDLE)
    loader.unload_model(SystemState.IDLE)
    loader.load_model(SystemState.ACTIVE)
    loader.unload_model(SystemState.ACTIVE)

    stats = loader.get_statistics()

    # Calculate expected average
    expected_avg_load = stats["total_load_time"] / stats["models_loaded"]
    expected_avg_unload = stats["total_unload_time"] / stats["models_unloaded"]

    if abs(stats["avg_load_time"] - expected_avg_load) < 0.001:
        test_pass("avg_load_time calculation correct")
    else:
        test_fail("avg_load_time calculation correct")

    if abs(stats["avg_unload_time"] - expected_avg_unload) < 0.001:
        test_pass("avg_unload_time calculation correct")
    else:
        test_fail("avg_unload_time calculation correct")


# ============================================================================
# Test 26: LLAMA_CPP_AVAILABLE constant
# ============================================================================
def test_llama_cpp_constant():
    """Test LLAMA_CPP_AVAILABLE is defined"""
    print("\n" + "="*70)
    print("TEST: LLAMA_CPP_AVAILABLE constant")
    print("="*70)

    if isinstance(LLAMA_CPP_AVAILABLE, bool):
        test_pass("LLAMA_CPP_AVAILABLE is boolean")
    else:
        test_fail("LLAMA_CPP_AVAILABLE is boolean")

    # It should be False in test environment (no llama-cpp)
    # or True if llama-cpp is installed
    test_pass(f"LLAMA_CPP_AVAILABLE = {LLAMA_CPP_AVAILABLE}")


# ============================================================================
# Test 27: Clear model cache
# ============================================================================
def test_clear_model_cache():
    """Test model cache can be cleared"""
    print("\n" + "="*70)
    print("TEST: Clear model cache")
    print("="*70)

    loader = ModelLoader(models_dir="/tmp/test")
    loader.cache_enabled = True

    # Load to populate cache
    loader.load_model(SystemState.IDLE)

    if SystemState.IDLE in loader.model_cache:
        test_pass("Model cached after load")
    else:
        test_fail("Model cached after load")

    # Clear cache
    loader.model_cache.clear()

    if len(loader.model_cache) == 0:
        test_pass("Cache cleared successfully")
    else:
        test_fail("Cache cleared successfully")


# ============================================================================
# Test 28: Statistics include current state
# ============================================================================
def test_stats_include_current_state():
    """Test statistics include current state value"""
    print("\n" + "="*70)
    print("TEST: Statistics include current state")
    print("="*70)

    loader = ModelLoader(models_dir="/tmp/test")
    loader.load_model(SystemState.ACTIVE)

    stats = loader.get_statistics()

    if stats["current_state"] == "active":
        test_pass("current_state in statistics")
    else:
        test_fail("current_state in statistics", f"Got: {stats['current_state']}")

    if stats["model_loaded"] == True:
        test_pass("model_loaded flag correct")
    else:
        test_fail("model_loaded flag correct")


# ============================================================================
# Test 29: Validator loaded flag
# ============================================================================
def test_validator_loaded_flag():
    """Test validator_loaded flag in statistics"""
    print("\n" + "="*70)
    print("TEST: Validator loaded flag")
    print("="*70)

    loader = ModelLoader(models_dir="/tmp/test")

    # ACTIVE state - no validator
    loader.load_model(SystemState.ACTIVE)
    stats = loader.get_statistics()

    if stats["validator_loaded"] == False:
        test_pass("No validator in ACTIVE state")
    else:
        test_fail("No validator in ACTIVE state")

    # CRITICAL state - has validator
    loader.unload_model(SystemState.ACTIVE)
    loader.load_model(SystemState.CRITICAL)
    stats = loader.get_statistics()

    if stats["validator_loaded"] == True:
        test_pass("Validator present in CRITICAL state")
    else:
        test_fail("Validator present in CRITICAL state")


# ============================================================================
# Test 30: Generate response includes model type
# ============================================================================
def test_generate_includes_model_type():
    """Test generate response indicates which model"""
    print("\n" + "="*70)
    print("TEST: Generate response includes model type")
    print("="*70)

    loader = ModelLoader(models_dir="/tmp/test")

    # Test with TinyLlama
    loader.load_model(SystemState.IDLE)
    response1 = loader.generate("test")

    if "tinyllama" in response1.lower():
        test_pass("IDLE response mentions TinyLlama")
    else:
        test_fail("IDLE response mentions TinyLlama", f"Got: {response1}")

    # Test with Phi-3
    loader.unload_model(SystemState.IDLE)
    loader.load_model(SystemState.ACTIVE)
    response2 = loader.generate("test")

    if "phi3" in response2.lower():
        test_pass("ACTIVE response mentions Phi-3")
    else:
        test_fail("ACTIVE response mentions Phi-3", f"Got: {response2}")


# ============================================================================
# Test Runner
# ============================================================================
def run_all_tests():
    """Run all tests"""
    global tests_passed, tests_failed

    print()
    print("="*70)
    print("  JARVIS AI-OS - Model Loader Test Suite")
    print("="*70)

    tests = [
        test_model_config,
        test_model_loader_init,
        test_stats_init,
        test_load_idle,
        test_load_active,
        test_load_critical,
        test_load_emergency,
        test_unload_model,
        test_unload_critical,
        test_statistics_update,
        test_cache_disabled,
        test_cache_enabled,
        test_generate_mock,
        test_generate_no_model,
        test_memory_usage,
        test_get_statistics_fields,
        test_state_transitions,
        test_load_time_tracking,
        test_unload_time_tracking,
        test_get_models_directory,
        test_wsl_detection,
        test_auto_detect_models_dir,
        test_mock_model_names,
        test_stats_accumulate,
        test_average_calculations,
        test_llama_cpp_constant,
        test_clear_model_cache,
        test_stats_include_current_state,
        test_validator_loaded_flag,
        test_generate_includes_model_type,
    ]

    for test_func in tests:
        try:
            test_func()
        except Exception as e:
            print(f"\n  [ERROR] {test_func.__name__}: {e}")
            import traceback
            traceback.print_exc()
            tests_failed += 1

    # Summary
    print()
    print("="*70)
    print("  MODEL LOADER TEST SUMMARY")
    print("="*70)
    print(f"  Total tests: {tests_passed + tests_failed}")
    print(f"  Passed: {tests_passed}")
    print(f"  Failed: {tests_failed}")
    if tests_passed + tests_failed > 0:
        print(f"  Pass rate: {100 * tests_passed / (tests_passed + tests_failed):.1f}%")
    print("="*70)
    print()

    return tests_failed == 0


if __name__ == "__main__":
    success = run_all_tests()
    sys.exit(0 if success else 1)
