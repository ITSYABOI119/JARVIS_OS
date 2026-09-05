#!/usr/bin/env bash
# ci_fetch_pinned_gguf.sh -- fetch-if-missing + VERIFY the hash-pinned test model.
#
# ONE copy of the pin, called by the `model-tests` job and by boot-smoke's
# `generation` leg. The artefact is the bartowski Llama-3.2-1B-Instruct-Q4_K_M
# build: 807,694,464 B, sha256 6f85a640... -- the bytes the four Llama-gated
# suites' expectations were tuned against (the box's phase3/models copy hashes
# identical). HASH-PINNED, NEVER NAME-MATCHED: unsloth publishes a DIFFERENT
# file under the identical filename (807,694,368 B, sha256 3f5a2242...).
#
# The sha256 AND size are checked on EVERY run, cache hit or not: a corrupt or
# truncated cached copy is discarded and fetched once more, and if that still
# does not verify the script exits non-zero. Nothing downstream ever runs on
# unverified bytes.
#
# Usage: ci_fetch_pinned_gguf.sh [cache-hit-string]
#   $1        the caller's actions/cache/restore `cache-hit` output, echoed for the log
#   GGUF_DEST overrides the destination path (default: the path the suites hardcode)
set -euo pipefail

F=${GGUF_DEST:-phase3/models/Llama-3.2-1B-Instruct-Q4_K_M.gguf}
URL=https://huggingface.co/bartowski/Llama-3.2-1B-Instruct-GGUF/resolve/main/Llama-3.2-1B-Instruct-Q4_K_M.gguf
SHA=6f85a640a97cf2bf5b8e764087b1e83da0fdb51d7c9fab7d0fece9385611df83
SIZE=807694464
CACHE_HIT=${1:-}

mkdir -p "$(dirname "$F")"

fetch() {
  local s
  s=$(date +%s)
  curl -fL --retry 5 --retry-delay 15 --retry-all-errors --max-time 1200 -o "$F" "$URL"
  echo "T_FETCH=$(( $(date +%s) - s ))s"
}
verify() { echo "$SHA  $F" | sha256sum -c -; }

echo "cache-hit=$CACHE_HIT"
if [ -f "$F" ]; then
  if ! verify; then
    echo "::warning::cached GGUF failed the hash check -- discarding and re-fetching once"
    rm -f "$F"; fetch; verify
  fi
else
  echo "no cached file -- fetching"; fetch; verify
fi
actual=$(stat -c%s "$F")
echo "size=$actual (expected $SIZE)"
[ "$actual" -eq "$SIZE" ] || { echo "::error::size mismatch"; exit 1; }
