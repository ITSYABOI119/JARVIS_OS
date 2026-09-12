"""The chat template that lives inside a GGUF, read from its header. Standard library only.

Why this exists: a model's thinking state is a property of what the SERVER renders, and what the
server renders comes from the template embedded in the GGUF it loaded. Two quantisations of one
model can carry different templates (measured 2026-09-12: the Gemma 4 E4B Q6_K and Q8_0 files carry
a template one newline longer than the Q4_K_M file's, which is one extra prompt token per call), so
a controlled comparison has to be able to hand one file's template to another file's server.

Only the header is read - never tensor data - so this costs a few milliseconds on an 8 GB file.
"""
import hashlib
import os
import struct
import tempfile

# GGUF scalar value types, by the code stored in the file.
_SCALAR = {0: "<B", 1: "<b", 2: "<H", 3: "<h", 4: "<I", 5: "<i", 6: "<f", 7: "<?", 10: "<Q",
           11: "<q", 12: "<d"}
_TYPE_STRING = 8
_TYPE_ARRAY = 9
CHAT_TEMPLATE_KEY = "tokenizer.chat_template"


class _Reader:
    def __init__(self, fh):
        self.fh = fh

    def read(self, n):
        b = self.fh.read(n)
        if len(b) != n:
            raise EOFError("truncated GGUF header")
        return b

    def u32(self):
        return struct.unpack("<I", self.read(4))[0]

    def u64(self):
        return struct.unpack("<Q", self.read(8))[0]

    def string(self):
        return self.read(self.u64()).decode("utf-8", "replace")

    def skip_string(self):
        self.fh.seek(self.u64(), 1)


def _skip_value(r, vtype):
    """Advance past one value of any GGUF type, including arrays of strings and of arrays."""
    if vtype in _SCALAR:
        r.read(struct.calcsize(_SCALAR[vtype]))
        return
    if vtype == _TYPE_STRING:
        r.skip_string()
        return
    if vtype == _TYPE_ARRAY:
        etype, count = r.u32(), r.u64()
        if etype == _TYPE_STRING:
            for _ in range(count):
                r.skip_string()
        elif etype in _SCALAR:
            r.fh.seek(struct.calcsize(_SCALAR[etype]) * count, 1)
        else:
            for _ in range(count):
                _skip_value(r, etype)
        return
    raise ValueError("unknown GGUF value type %d" % vtype)


def read_chat_template(path):
    """The file's `tokenizer.chat_template`, or None if it declares none.

    Returns None rather than raising for a GGUF without the key: a model with no template is a fact
    about the model, not an error. A file that is not a GGUF at all DOES raise.
    """
    with open(path, "rb", buffering=1 << 20) as fh:
        r = _Reader(fh)
        if r.read(4) != b"GGUF":
            raise ValueError("%s is not a GGUF (bad magic)" % path)
        r.u32()                                   # version
        r.u64()                                   # tensor count
        n_kv = r.u64()
        for _ in range(n_kv):
            key = r.string()
            vtype = r.u32()
            if key == CHAT_TEMPLATE_KEY:
                if vtype != _TYPE_STRING:
                    raise ValueError("%s holds %s as type %d, not a string"
                                     % (path, CHAT_TEMPLATE_KEY, vtype))
                return r.string()
            _skip_value(r, vtype)
    return None


def template_sha256(text) -> str:
    """The template's own identity, so a run JSON can record WHICH template rendered it."""
    return hashlib.sha256((text or "").encode("utf-8")).hexdigest()


def write_template_file(text, dir=None) -> str:
    """Write the template to `jarvis_tpl_<sha16>.jinja` and return the path; idempotent.

    Named by content, so the same template is written once and two callers asking for it get the
    same file. It lives outside the repo (the system temp directory by default) because it is a
    derived artifact of a GGUF that is itself outside the repo.
    """
    if text is None:
        raise ValueError("no template to write")
    digest = template_sha256(text)
    root = dir or tempfile.gettempdir()
    path = os.path.join(root, "jarvis_tpl_%s.jinja" % digest[:16])
    if not os.path.exists(path):
        tmp = path + ".part"
        with open(tmp, "w", encoding="utf-8", newline="") as fh:
            fh.write(text)
        os.replace(tmp, path)
    return path
