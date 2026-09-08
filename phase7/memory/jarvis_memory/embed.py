"""The embedding lane's vector maths and its two embedders.

The design's §6. Two things here are deliberate and worth reading before changing anything:

  * **Nothing heavy is imported at module level.** `sentence_transformers` and `torch` are imported
    inside `QwenEmbedder.__init__`, and `numpy` inside the one function that can use it. CI runs
    this file on a bare `python3` with neither, and the whole test suite drives `DictEmbedder`
    instead — so the package stays standard-library by default and the GPU is touched only when a
    benchmark run asks for it.
  * **The model loads from the DEFAULT Hugging Face cache with `local_files_only=True`.** It is
    already there (1.2 GB). Do NOT set `HF_HOME` to the voice package's folder: the voice package
    points HF_HOME at its own models directory for Whisper and ECAPA, this model is not in it, and
    redirecting the cache turns a local load into a download attempt.

The query is embedded SYMMETRICALLY by default — the same plain text form as the stored rendering,
no instruction prefix — because C/M0.5 measured that form better for query-to-query retrieval. The
instruction-prefixed arm exists so it can be REPORTED beside the symmetric one, never silently
adopted.
"""
import array
import math
import time

EMBED_MODEL = "Qwen/Qwen3-Embedding-0.6B"
EMBED_DIM = 1024
QUERY_INSTRUCTION = (
    "Instruct: Given a question about a household, retrieve the stored fact or preference it "
    "concerns\nQuery: "
)


# ------------------------------------------------------------------ the maths
def pack(vec) -> bytes:
    """float32 little-endian, 4 bytes per component. The store keeps this in `embedding.vec`."""
    a = array.array("f", [float(x) for x in vec])
    if array.array("f", [1.0]).tobytes() != b"\x00\x00\x80\x3f":  # big-endian host
        a.byteswap()
    return a.tobytes()


def unpack(blob: bytes, dim: int) -> list:
    a = array.array("f")
    a.frombytes(bytes(blob))
    if array.array("f", [1.0]).tobytes() != b"\x00\x00\x80\x3f":
        a.byteswap()
    return list(a[:dim])


def cosine(a, b) -> float:
    """Cosine similarity. A zero vector scores 0 rather than raising: an un-embeddable row must
    sink, not break the query."""
    dot = na = nb = 0.0
    for x, y in zip(a, b):
        dot += x * y
        na += x * x
        nb += y * y
    if na <= 0.0 or nb <= 0.0:
        return 0.0
    return dot / (math.sqrt(na) * math.sqrt(nb))


def _topk_pure(query_vec, rows, k: int) -> list:
    """The reference implementation: pure Python, no dependencies."""
    scored = [(obj, cosine(query_vec, vec)) for obj, vec in rows]
    scored.sort(key=lambda t: -t[1])
    return scored[:k]


def topk(query_vec, rows, k: int) -> list:
    """The k best rows by cosine, descending. Uses numpy when it is importable and falls back to
    the pure path otherwise; the test pins the two to agree to 1e-6.

    A flat scan is the design's choice at household scale — thousands of rows, milliseconds — and
    an index is added only when a measured p99 says so.
    """
    if not rows:
        return []
    try:
        import numpy as np
    except ImportError:
        return _topk_pure(query_vec, rows, k)
    try:
        mat = np.asarray([v for _, v in rows], dtype=np.float32)
        q = np.asarray(query_vec, dtype=np.float32)
        qn = float(np.linalg.norm(q))
        if qn <= 0.0:
            return _topk_pure(query_vec, rows, k)
        norms = np.linalg.norm(mat, axis=1)
        sims = mat.dot(q) / qn
        with np.errstate(divide="ignore", invalid="ignore"):
            sims = np.where(norms > 0, sims / norms, 0.0)
        order = np.argsort(-sims, kind="stable")[:k]
        return [(rows[i][0], float(sims[i])) for i in order]
    except (ValueError, TypeError):
        # Ragged vectors (a dimension change mid-store) - fall back rather than guess.
        return _topk_pure(query_vec, rows, k)


# -------------------------------------------------------------- the embedders
class DictEmbedder:
    """A lookup-table embedder for the tests: deterministic, dependency-free, no model.

    A text absent from the table embeds to a zero vector, which cosines to 0 — present in the lane
    but always last, which is what an un-embeddable row should be.
    """

    def __init__(self, table: dict, dim: int):
        self.dim = dim
        self.model_id = "dict"
        self.version = "n/a"
        self.load_s = 0.0
        self._table = {}
        for text, vec in table.items():
            n = math.sqrt(sum(x * x for x in vec))
            self._table[text] = [x / n for x in vec] if n > 0 else [0.0] * dim

    def embed(self, texts) -> list:
        return [self._table.get(t, [0.0] * self.dim) for t in texts]

    def embed_query(self, text: str) -> list:
        return self.embed([text])[0]


class QwenEmbedder:
    """The deployed Qwen3-Embedding-0.6B, through sentence-transformers on the RTX 2070.

    Imports torch and sentence_transformers HERE, so importing `jarvis_memory` never pulls them in.
    """

    def __init__(self, device=None, instruction: bool = False):
        from sentence_transformers import SentenceTransformer  # noqa: PLC0415
        import sentence_transformers  # noqa: PLC0415
        import torch  # noqa: PLC0415

        if device is None:
            device = "cuda" if torch.cuda.is_available() else "cpu"
        t0 = time.perf_counter()
        self._model = SentenceTransformer(EMBED_MODEL, device=device, local_files_only=True)
        self.load_s = round(time.perf_counter() - t0, 3)
        self.instruction = bool(instruction)
        self.device = device
        self.version = sentence_transformers.__version__
        self.model_id = EMBED_MODEL + ("+instruct" if self.instruction else "")
        self.dim = int(self._model.get_sentence_embedding_dimension())

    def embed(self, texts) -> list:
        if not texts:
            return []
        vecs = self._model.encode(list(texts), normalize_embeddings=True, batch_size=64,
                                  show_progress_bar=False, convert_to_numpy=True)
        return [[float(x) for x in v] for v in vecs]

    def embed_query(self, text: str) -> list:
        """Symmetric by default — the stored rendering and the question are embedded the same way.
        The instruction prefix is applied to the QUERY only, and only in the reported arm."""
        payload = (QUERY_INSTRUCTION + text) if self.instruction else text
        return self.embed([payload])[0]
