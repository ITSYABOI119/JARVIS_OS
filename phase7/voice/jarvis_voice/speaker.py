"""Speaker embeddings for verification: SpeechBrain ECAPA-TDNN (speechbrain/spkrec-ecapa-voxceleb).

Loaded once, on CUDA when available. `embed(wav, sr)` returns an L2-normalised 192-d vector as a
plain Python list (so the pure modules never need numpy). Imports the GPU stack lazily.
"""
import math
import time
from typing import List, Optional

MODEL_ID = "speechbrain/spkrec-ecapa-voxceleb"


class SpeakerEmbedder:
    def __init__(self, model_id: str = MODEL_ID, device: Optional[str] = None):
        from .paths import set_hf_home, ensure
        hf_home = set_hf_home()
        import torch
        import speechbrain
        from speechbrain.inference.speaker import EncoderClassifier
        from speechbrain.utils.fetching import LocalStrategy
        # "cuda:0", not "cuda": speechbrain's run_opts parser wants an index and otherwise warns + falls back
        self.device = device or ("cuda:0" if torch.cuda.is_available() else "cpu")
        self.model_id = model_id
        self.version = getattr(speechbrain, "__version__", "?")
        savedir = ensure("models") / "speechbrain" / model_id.replace("/", "--")
        t0 = time.perf_counter()
        # COPY, not the default SYMLINK: Windows refuses symlinks without a privilege (WinError 1314,
        # measured 2026-09-06). The HF cache under HF_HOME is still the download target; the files
        # are copied into savedir from it.
        self.model = EncoderClassifier.from_hparams(source=model_id, savedir=str(savedir),
                                                    run_opts={"device": self.device},
                                                    local_strategy=LocalStrategy.COPY)
        self.model.eval()
        self.load_s = time.perf_counter() - t0
        self.hf_home = hf_home
        self.dim: Optional[int] = None
        self.last_embed_s: float = 0.0

    def embed(self, wav, sr: int) -> List[float]:
        import numpy as np
        import torch
        from .audio import resample_to_16k
        wav16, _ = resample_to_16k(np.asarray(wav, dtype="float32"), sr)
        x = torch.from_numpy(np.ascontiguousarray(wav16)).unsqueeze(0).to(self.device)
        t0 = time.perf_counter()
        with torch.no_grad():
            e = self.model.encode_batch(x).squeeze().float().cpu().numpy().tolist()
        self.last_embed_s = time.perf_counter() - t0
        n = math.sqrt(sum(v * v for v in e))
        e = [v / n for v in e]
        self.dim = len(e)
        return e

    def vram_peak_bytes(self) -> Optional[int]:
        import torch
        return torch.cuda.max_memory_allocated() if torch.cuda.is_available() else None
