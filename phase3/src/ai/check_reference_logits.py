#!/usr/bin/env python3
"""Get reference logits from llama.cpp for comparison with our forward pass."""

from llama_cpp import Llama
import numpy as np

print("Loading model (CPU only)...")
llm = Llama(
    model_path="phase3/models/Llama-3.2-1B-Instruct-Q4_K_M.gguf",
    n_gpu_layers=0,
    n_ctx=512,
    verbose=False,
    logits_all=True,
)

# Test 1: After BOS only
print("\n--- After BOS (128000) ---")
llm.reset()
llm.eval([128000])
logits = np.array(llm.scores[0])
top5 = np.argsort(logits)[-5:][::-1]
for t in top5:
    print(f"  token {t}: logit={logits[t]:.4f}")
print(f"  Our answer (41942): logit={logits[41942]:.4f}")

# Test 2: After full prompt "The seL4 microkernel is"
print("\n--- After BOS + 'The seL4 microkernel is' ---")
llm.reset()
tokens = [128000, 791, 513, 43, 19, 8162, 24127, 374]
llm.eval(tokens)
logits = np.array(llm.scores[len(tokens) - 1])
top5 = np.argsort(logits)[-5:][::-1]
for t in top5:
    print(f"  token {t}: logit={logits[t]:.4f}")
print(f"  Our answer (117079): logit={logits[117079]:.4f}")
