# fix_gpu_inference.py - Diagnose and fix GPU inference issue
import sys

print("="*70)
print("JARVIS AI-OS - GPU Inference Diagnostic")
print("="*70)
print()

# Check 1: llama-cpp-python version and build info
print("[1] Checking llama-cpp-python installation...")
print("-" * 70)
try:
    import llama_cpp
    print(f"✅ llama-cpp-python version: {llama_cpp.__version__}")

    # Check if CUDA support is compiled in
    from llama_cpp import llama_cpp
    print(f"   Module location: {llama_cpp.__file__}")

except Exception as e:
    print(f"❌ Error: {e}")

print()

# Check 2: Try to detect CUDA
print("[2] Checking CUDA availability...")
print("-" * 70)
try:
    import torch
    print(f"✅ PyTorch installed")
    print(f"   CUDA available: {torch.cuda.is_available()}")
    if torch.cuda.is_available():
        print(f"   CUDA version: {torch.version.cuda}")
        print(f"   GPU: {torch.cuda.get_device_name(0)}")
        print(f"   VRAM: {torch.cuda.get_device_properties(0).total_memory / (1024**3):.1f}GB")
except ImportError:
    print("⚠️  PyTorch not installed (optional, just for CUDA detection)")
except Exception as e:
    print(f"❌ Error: {e}")

print()

# Check 3: Test minimal GPU inference
print("[3] Testing GPU inference with minimal model...")
print("-" * 70)
print("This will attempt to load the model with verbose output...")
print()

from pathlib import Path
import time

model_path = Path(r"\\wsl.localhost\Ubuntu\home\itsme\jarvis-phase0\models\mistral-7b-instruct-v0.2.Q8_0.gguf")

if not model_path.exists():
    print(f"❌ Model not found: {model_path}")
    sys.exit(1)

try:
    from llama_cpp import Llama

    print("Attempting to load with GPU offloading...")
    print("(This will show if CUDA is actually being used)")
    print()

    start = time.time()
    llm = Llama(
        model_path=str(model_path),
        n_gpu_layers=-1,    # Try to offload ALL layers to GPU
        n_ctx=512,          # Smaller context for faster loading
        verbose=True        # Show detailed loading info
    )
    load_time = time.time() - start

    print()
    print(f"✅ Model loaded in {load_time:.1f}s")
    print()

    # Try one quick inference
    print("Running test inference...")
    start = time.time()
    output = llm("Hello", max_tokens=10, echo=False)
    inference_time = (time.time() - start) * 1000

    print(f"✅ Test inference: {inference_time:.0f}ms")
    print()

    if inference_time < 500:
        print("🎉 GPU inference is working! (fast)")
    else:
        print("⚠️  Slow inference - likely using CPU, not GPU")
        print()
        print("DIAGNOSIS:")
        print("-" * 70)
        print("The model is loading into system RAM (CPU) instead of GPU VRAM.")
        print()
        print("SOLUTION:")
        print("Reinstall llama-cpp-python with CUDA support:")
        print()
        print("  pip uninstall llama-cpp-python -y")
        print("  pip install llama-cpp-python --force-reinstall --no-cache-dir \\")
        print("      --extra-index-url https://abetlen.github.io/llama-cpp-python/whl/cu121")
        print()
        print("Or try the CPU-optimized version (will still be slower):")
        print("  pip install llama-cpp-python[server]")

except Exception as e:
    print(f"❌ Error during model loading: {e}")
    print()
    print("This suggests llama-cpp-python was installed without CUDA support.")

print()
print("="*70)
