"""A llama.cpp server, started and stopped by this module, and one schema-constrained call per span.

Stdlib only (`subprocess`, `urllib`, `json`): the model runs out of process, so nothing here imports
torch and CI can exercise `build_request` / `parse_response` with no model on disk.

A parse failure NEVER raises. The bake-off's validity band counts invalid calls, so an exception
here would turn a measurement into a crash and lose the very number the milestone exists to record.
"""
import json
import os
import subprocess
import time
import urllib.error
import urllib.request

from .prompt import system_prompt, user_prompt

LLAMA_BIN = os.environ.get(
    "JARVIS_LLAMA_BIN",
    os.path.expanduser("~/llama.cpp/build/bin/Release"))
SERVER_EXE = "llama-server.exe"


def build_request(span_text, speaker_cluster, day, names, span_id, schema,
                  max_tokens=512, temperature=0.0, seed=1) -> dict:
    """The chat-completions body. Pure, so the schema wiring is testable without a server.

    `response_format: json_schema` is what makes the enums binding rather than advisory: the server
    constrains generation to the grammar the schema compiles to, so an off-registry predicate id
    cannot be produced at all. temperature 0 and a fixed seed make a run reproducible.
    """
    return {
        "messages": [
            {"role": "system", "content": system_prompt()},
            {"role": "user", "content": user_prompt(span_text, speaker_cluster, day, names,
                                                    span_id)},
        ],
        "response_format": {
            "type": "json_schema",
            "json_schema": {"name": "candidates", "strict": True, "schema": schema},
        },
        "temperature": temperature,
        "seed": seed,
        "max_tokens": max_tokens,
        "stream": False,
    }


def parse_response(raw_json):
    """(object|None, text). The object is the model's parsed JSON; the text is what it emitted.

    Two failures are folded into `None`: the server returned something that is not a chat
    completion, and the completion's content is not JSON. Both are 'invalid' for the band, and the
    caller keeps `text` so the reason can be read in the report rather than guessed at.
    """
    try:
        body = json.loads(raw_json) if isinstance(raw_json, (str, bytes)) else raw_json
        text = body["choices"][0]["message"]["content"]
    except Exception:
        return None, (raw_json if isinstance(raw_json, str) else repr(raw_json))
    try:
        return json.loads(text), text
    except Exception:
        return None, text


def extract_span(base_url, span_id, text, cluster, day, names, schema,
                 max_tokens=512, temperature=0.0) -> dict:
    """One call. Returns the parsed object, the raw text, token counts and wall ms; never raises."""
    body = build_request(text, cluster, day, names, span_id, schema,
                         max_tokens=max_tokens, temperature=temperature)
    data = json.dumps(body).encode("utf-8")
    req = urllib.request.Request(base_url.rstrip("/") + "/v1/chat/completions", data=data,
                                 headers={"Content-Type": "application/json"})
    t0 = time.time()
    try:
        with urllib.request.urlopen(req, timeout=180) as fh:
            raw = fh.read().decode("utf-8")
    except Exception as exc:                                   # noqa: BLE001 - counted, not raised
        return {"candidates": None, "raw": "REQUEST FAILED: %r" % (exc,),
                "ms": round((time.time() - t0) * 1000, 1), "tokens_in": 0, "tokens_out": 0}
    ms = round((time.time() - t0) * 1000, 1)
    obj, txt = parse_response(raw)
    usage = {}
    try:
        usage = json.loads(raw).get("usage") or {}
    except Exception:
        pass
    return {
        "candidates": (obj or {}).get("candidates") if obj is not None else None,
        "raw": txt,
        "ms": ms,
        "tokens_in": usage.get("prompt_tokens", 0),
        "tokens_out": usage.get("completion_tokens", 0),
    }


class LlamaServer:
    """Start a llama-server on a free port, wait for /health, and always kill it on exit."""

    def __init__(self, model_path, port=8089, ctx=4096, ngl=99, bin_dir=None):
        self.model_path = str(model_path)
        self.port = int(port)
        self.ctx = int(ctx)
        self.ngl = int(ngl)
        self.bin_dir = bin_dir or LLAMA_BIN
        self.proc = None
        self.version = None
        self.base_url = "http://127.0.0.1:%d" % self.port

    def __enter__(self):
        exe = os.path.join(self.bin_dir, SERVER_EXE)
        if not os.path.exists(exe):
            raise RuntimeError("no llama-server at %s (set JARVIS_LLAMA_BIN)" % exe)
        if not os.path.exists(self.model_path):
            raise RuntimeError("no model at %s" % self.model_path)
        cmd = [exe, "-m", self.model_path, "-ngl", str(self.ngl), "-c", str(self.ctx),
               "--port", str(self.port), "--jinja"]
        self.proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                     text=True, errors="replace")
        deadline = time.time() + 300
        while time.time() < deadline:
            if self.proc.poll() is not None:
                out = self.proc.stdout.read() if self.proc.stdout else ""
                raise RuntimeError("llama-server exited early:\n%s" % out[-4000:])
            try:
                with urllib.request.urlopen(self.base_url + "/health", timeout=2) as fh:
                    if fh.status == 200:
                        break
            except Exception:
                time.sleep(1.0)
        else:
            self.__exit__(None, None, None)
            raise RuntimeError("llama-server did not become healthy within 300 s")
        try:
            with urllib.request.urlopen(self.base_url + "/props", timeout=5) as fh:
                props = json.loads(fh.read().decode("utf-8"))
            self.version = props.get("build_info") or props.get("model_path")
        except Exception:
            self.version = None
        return self

    def __exit__(self, *exc):
        if self.proc and self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=20)
            except Exception:
                self.proc.kill()
        return False
