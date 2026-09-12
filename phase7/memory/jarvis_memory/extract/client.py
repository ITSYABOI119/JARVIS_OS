"""A llama.cpp server, started and stopped by this module, and one schema-constrained call per span.

Stdlib only (`subprocess`, `urllib`, `json`): the model runs out of process, so nothing here imports
torch and CI can exercise `build_request` / `parse_response` with no model on disk.

A parse failure NEVER raises. The bake-off's validity band counts invalid calls, so an exception
here would turn a measurement into a crash and lose the very number the milestone exists to record.
"""
import json
import os
import tempfile
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
                  max_tokens=512, temperature=0.0, seed=1, thinking_switch=False,
                  think=None, think_extra=None) -> dict:
    """The chat-completions body. Pure, so the schema wiring is testable without a server.

    `response_format: json_schema` is what makes the enums binding rather than advisory: the server
    constrains generation to the grammar the schema compiles to, so an off-registry predicate id
    cannot be produced at all. temperature 0 and a fixed seed make a run reproducible.
    """
    body = {
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
    # The thinking state is REQUESTED here and ASSERTED at the renderer before an arm runs
    # (`bench_ms1b`'s render pre-flight). Two paths, and the older one is preserved bit for bit:
    #
    #   `think` is None  - the legacy field's behaviour. `thinking_switch` True sends
    #                      {"enable_thinking": False}; False sends no kwarg at all.
    #   `think` given    - addendum 2's keys, which declare the state they measure: "off" sends
    #                      False, "on" sends True, merged with `think_extra` when a key needs more
    #                      (NuExtract3's template raises unless it also gets a `mode`).
    #
    # CORRECTED 2026-09-12: this comment used to say Gemma 4's channel has none. It HAS one -
    # every Gemma 4 template reads `enable_thinking` - and because these keys sent no kwarg,
    # llama.cpp's `--reasoning auto` default supplied true and every Gemma run in both fields
    # thought. Sending nothing is NOT the same as sending false, which is why addendum 2 measures a
    # switchable model in BOTH states instead of assuming either.
    if think is not None:
        if think == "off":
            body["chat_template_kwargs"] = {"enable_thinking": False}
        elif think == "on":
            kwargs = {"enable_thinking": True}
            kwargs.update(think_extra or {})
            body["chat_template_kwargs"] = kwargs
        else:
            raise ValueError("think must be 'on', 'off' or None, not %r" % (think,))
    elif thinking_switch:
        body["chat_template_kwargs"] = {"enable_thinking": False}
    return body


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
                 max_tokens=512, temperature=0.0, thinking_switch=False,
                 think=None, think_extra=None) -> dict:
    """One call. Returns the parsed object, the raw text, token counts and wall ms; never raises.

    Also returns `reasoning_present`: whether the server handed back a non-empty
    `reasoning_content`. The render check asserts what went IN; this records what came BACK, and a
    run counts it as `reasoning_calls` - so a thinking state is evidenced at both ends.
    """
    body = build_request(text, cluster, day, names, span_id, schema,
                         max_tokens=max_tokens, temperature=temperature,
                         thinking_switch=thinking_switch, think=think, think_extra=think_extra)
    data = json.dumps(body).encode("utf-8")
    req = urllib.request.Request(base_url.rstrip("/") + "/v1/chat/completions", data=data,
                                 headers={"Content-Type": "application/json"})
    t0 = time.time()
    status = None
    try:
        with urllib.request.urlopen(req, timeout=180) as fh:
            status = fh.status
            raw = fh.read().decode("utf-8")
    except urllib.error.HTTPError as exc:                      # noqa: BLE001 - counted, not raised
        # The status is CARRIED rather than folded into the message: a 400 means the server refused
        # the request itself - under json_schema that means the schema did not compile to a grammar,
        # which invalidates an entire run rather than costing one call, and the smoke gate must be
        # able to tell it apart from a timeout or a parse failure.
        body = ""
        try:
            body = exc.read().decode("utf-8", "replace")[:400]
        except Exception:
            pass
        return {"candidates": None, "raw": "HTTP %s: %s" % (exc.code, body), "status": exc.code,
                "finish_reason": None, "reasoning_present": False,
                "ms": round((time.time() - t0) * 1000, 1), "tokens_in": 0, "tokens_out": 0}
    except Exception as exc:                                   # noqa: BLE001 - counted, not raised
        return {"candidates": None, "raw": "REQUEST FAILED: %r" % (exc,), "status": None,
                "finish_reason": None, "reasoning_present": False,
                "ms": round((time.time() - t0) * 1000, 1), "tokens_in": 0, "tokens_out": 0}
    ms = round((time.time() - t0) * 1000, 1)
    obj, txt = parse_response(raw)
    usage = {}
    try:
        usage = json.loads(raw).get("usage") or {}
    except Exception:
        pass
    finish = None
    reasoning = ""
    try:
        choice = json.loads(raw)["choices"][0] or {}
        finish = choice.get("finish_reason")
        reasoning = (choice.get("message") or {}).get("reasoning_content") or ""
    except Exception:
        pass
    return {
        "candidates": (obj or {}).get("candidates") if obj is not None else None,
        "raw": txt,
        "status": status,
        "finish_reason": finish,
        "reasoning_present": bool(str(reasoning).strip()),
        "ms": ms,
        "tokens_in": usage.get("prompt_tokens", 0),
        "tokens_out": usage.get("completion_tokens", 0),
    }


class LlamaServer:
    """Start a llama-server on a free port, wait for /health, and always kill it on exit."""

    def __init__(self, model_path, port=8089, ctx=4096, ngl=99, bin_dir=None, extra_args=None,
                 log_path=None, env=None):
        self.model_path = str(model_path)
        self.port = int(port)
        self.ctx = int(ctx)
        self.ngl = int(ngl)
        self.bin_dir = bin_dir or LLAMA_BIN
        # Per-model server arguments, declared beside the model in the bake-off's own table and
        # appended AFTER the shared ones so a model can never quietly change the shared contract
        # (`--jinja`, the context, the port). Their whole purpose is to make a model RUN - the
        # Q8_0 arm needs `--n-cpu-ffn` to fit an 8.6 GB file on an 8 GB card - never to make one
        # score better, so a run records them and the report names them.
        self.extra_args = list(extra_args or [])
        self.proc = None
        self.version = None
        self.base_url = "http://127.0.0.1:%d" % self.port
        # WHERE THE SERVER'S OWN OUTPUT GOES, and it must be a FILE rather than a pipe. See
        # `server_log_tail` for the measurement that forced this.
        self.log_path = str(log_path) if log_path else None
        self._log_fh = None
        # Merged OVER os.environ for THIS server only: the render pre-flight sets
        # CUDA_VISIBLE_DEVICES=-1 so a CPU-only render can never touch a card an arm is using.
        self.env = dict(env) if env else None

    def server_log_tail(self, limit=4000):
        """The last `limit` bytes the server wrote, or a note saying why there are none.

        The server's stdout+stderr go to a FILE, never to `subprocess.PIPE`, and that is a
        correctness requirement rather than a convenience. An undrained pipe has a fixed OS buffer;
        once it fills, the child BLOCKS in write() and stops serving, while /health - answered off
        another path - keeps returning ok. MEASURED on llama.cpp v0.4.0 (build b10809): the server
        writes **812 bytes of log per request**, so a 4 KB pipe fills after ~3 requests, an 8 KB
        pipe after ~8 and a 64 KB pipe after ~79. The frozen b8728 build is far quieter, which is
        why the eleven-model field never hit it.

        What that cost, before it was found: a `granite-3b` arm ran for 16 hours 10 minutes having
        accumulated 68 seconds of server CPU, every call after the pipe filled timing out at 180 s,
        and it would have written a plausible-looking near-zero score for a model that answers each
        call in 0.15 s when its output has somewhere to go.
        """
        if not self.log_path:
            return "(no server log)"
        try:
            with open(self.log_path, "rb") as fh:
                fh.seek(0, 2)
                size = fh.tell()
                fh.seek(max(0, size - int(limit)))
                return fh.read().decode("utf-8", "replace")
        except OSError as exc:
            return "(server log unreadable: %s)" % exc

    def server_command(self):
        """The exact argv this server will run. Pure - no process, no file check - so a per-model
        argument can be pinned by a test without a binary or a 5 GB model on disk."""
        return [os.path.join(self.bin_dir, SERVER_EXE), "-m", self.model_path,
                "-ngl", str(self.ngl), "-c", str(self.ctx), "--port", str(self.port),
                "--jinja"] + self.extra_args

    def __enter__(self):
        exe = os.path.join(self.bin_dir, SERVER_EXE)
        if not os.path.exists(exe):
            raise RuntimeError("no llama-server at %s (set JARVIS_LLAMA_BIN)" % exe)
        if not os.path.exists(self.model_path):
            raise RuntimeError("no model at %s" % self.model_path)
        cmd = self.server_command()
        self.cmd = cmd
        if self.log_path is None:
            fd, self.log_path = tempfile.mkstemp(prefix="llama_server_%d_" % self.port,
                                                 suffix=".log")
            os.close(fd)
        self._log_fh = open(self.log_path, "w", encoding="utf-8", errors="replace")
        run_env = dict(os.environ, **self.env) if self.env else None
        self.proc = subprocess.Popen(cmd, stdout=self._log_fh, stderr=subprocess.STDOUT,
                                     text=True, errors="replace", env=run_env)
        deadline = time.time() + 300
        while time.time() < deadline:
            if self.proc.poll() is not None:
                raise RuntimeError("llama-server exited early:\n%s" % self.server_log_tail())
            try:
                with urllib.request.urlopen(self.base_url + "/health", timeout=2) as fh:
                    if fh.status == 200:
                        break
            except Exception:
                time.sleep(1.0)
        else:
            tail = self.server_log_tail()
            self.__exit__(None, None, None)
            raise RuntimeError("llama-server did not become healthy within 300 s:\n%s" % tail)
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
        if self._log_fh is not None:
            try:
                self._log_fh.close()
            except Exception:                                  # noqa: BLE001
                pass
            self._log_fh = None
        return False
