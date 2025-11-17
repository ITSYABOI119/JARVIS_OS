#!/usr/bin/env python3
"""
download_phi3.py - Download Phi-3 Mini model for benchmarking
"""

from huggingface_hub import hf_hub_download
import os

# Phi-3 Mini 3.8B in GGUF format (quantized for efficient inference)
# Using microsoft/Phi-3-mini-4k-instruct-gguf repository

MODEL_REPO = "microsoft/Phi-3-mini-4k-instruct-gguf"
MODEL_FILE = "Phi-3-mini-4k-instruct-q4.gguf"  # Q4 quantization (smaller, faster)

print("="*70)
print("Downloading Phi-3 Mini 3.8B Model")
print("="*70)
print()
print(f"Repository: {MODEL_REPO}")
print(f"Model file: {MODEL_FILE}")
print()

# Download to models directory
models_dir = "C:/Users/jluca/Documents/JARVIS_OS/models"
os.makedirs(models_dir, exist_ok=True)

print("Downloading... (this may take a few minutes)")
print()

try:
    model_path = hf_hub_download(
        repo_id=MODEL_REPO,
        filename=MODEL_FILE,
        local_dir=models_dir,
        local_dir_use_symlinks=False
    )

    print()
    print("="*70)
    print("[DOWNLOAD COMPLETE]")
    print("="*70)
    print()
    print(f"Model downloaded to: {model_path}")
    print()

    # Get file size
    file_size = os.path.getsize(model_path)
    print(f"File size: {file_size / (1024**3):.2f} GB")
    print()

except Exception as e:
    print(f"Error downloading model: {e}")
    print()
    print("Alternative: Try manually downloading from:")
    print(f"https://huggingface.co/{MODEL_REPO}")
