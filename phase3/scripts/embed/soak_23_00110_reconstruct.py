#!/usr/bin/env python3
"""Reconstruct soak control-IN turn [23:00110] from the box's OWN stored vectors.

WHAT THIS ANSWERS
-----------------
During the 2026-08 unattended soak, turn [23:00110] ("whats my favourite colour?") answered
"Your favorite color is blue." while recording provenance `recall=semantic src=93 cos=944`.
Record 93's stored answer is "As an AI, I do not know what your favorite color is." — the
answer contradicts its own recorded source. Three hypotheses stood:

  H1  provenance UNDER-REPORTS: the preamble held something other than, or MORE than, src 93.
  H2  confabulation: the model ignored the preamble and "blue" is just the argmax colour.
  H3  cross-query state leakage from the [23:00108] generation two turns earlier.

The mode-4 probe (5e9dedb / 23f31f1) demonstrated the MECHANISM for H1 on a *reproduction* —
a preamble can be MULTI-FACT while provenance records only ctrl_sel[0].seq — but the original
turn's preamble bytes were never logged, so the original turn itself stayed unexplained.

They are recoverable after all. Every vector the selector compared that day is still in the
JVEC store at LBA 21,150,000, INCLUDING the query's own (embed-on-write reuses the vector the
recall lane computed). So the turn can be re-run through the DEPLOYED g3_select_semantic and
g3_build_preamble_answer_only with the box's own float32s and NO host embedding at all — which
also means the measured ~0.0094 box-vs-host cosine delta, which would otherwise sit right on
top of a 0.55-floor decision, does not apply.

The C driver (soak_prov_driver.c) links the real phase3/src/ai/g3_retrieval.c. This script only
assembles its input and reads its output; it deliberately does NOT reimplement the selector.

POSITIVE CONTROL
----------------
The box recorded src=93 cos_x1000=944 for this very turn. The reconstruction must reproduce
`SEL 1 93` with cos_x1000 in [943, 945] before any other number may be read. That control is
built in: it fails loudly rather than producing a confident wrong story.

Usage:
  python3 soak_23_00110_reconstruct.py --epi ctrl_epi.json --vec jvec.json \\
      --epi-bin ctrl_epi.bin --vec-bin jvec.bin --target 110 \\
      --driver ./soak_prov_driver --out results.json

Model-gated-local / box-gated: needs a dump of the box's stores, so it is not a CI step (the
cm2_floor.py / punct_remeasure.py precedent). The driver itself IS compiled and smoke-tested
in CI.
"""
import argparse
import json
import os
import struct
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import parse_episodic as pe          # noqa: E402  (layout constants; no reimplementation)
import parse_embed_store as pv       # noqa: E402

# --- the DEPLOYED constants, mirrored from the headers (values asserted against them below) ---
EPI_ACT_CONTROL_IN = 3      # episodic_store.h
EPI_OUT_OK = 0              # episodic_store.h
CTRL_SEM_MAX_CANDS = 32     # main_x86.c:678
EMBED_STORE_DIM = 128       # embed_store.h
EMBED_STORE_ST_VALID = 1    # embed_store.h
EMBED_STORE_MAX_VECS = 4096 # embed_store.h — also the g_embed_idx bound in main_x86.c


def g3_candidate_usable(action, outcome, resp_len,
                        want_action=EPI_ACT_CONTROL_IN, ok_outcome=EPI_OUT_OK):
    """g3_retrieval.h: (action == want) && (outcome == ok) && (resp_len > 0).

    This is the tag-4 exclusion: a locally-served answer ("up 476 seconds", "I don't track
    that.") is not a memory, and under cosine — unlike exact-key — a stale system fact can
    semantically match an unrelated question.
    """
    return action == want_action and outcome == ok_outcome and resp_len > 0


def gather_candidates(epi_records, vec_records, target_seq, max_vec_store_seq):
    """Mirror of ctrl_sem_gather() (main_x86.c:2858-2894), clause by clause.

    The deployed function walks g_ctrl_epi_index — which holds ONLY recallable records — and
    it walks it BACKWARDS, because the index is built oldest->newest:

        for (int i = g_ctrl_ix.n - 1; i >= 0 && n < CTRL_SEM_MAX_CANDS; i--) {
            ... epi_store_read(li) ...                       -> re-read the record
            if (rec.query_key != index[i].key) continue;      -> post-wrap aliasing guard
            int eli = embed_idx_find(rec.seq);                -> FIRST valid vector for that seq
            if (eli < 0) continue;                            -> not vectored yet: not a candidate
            if (!embed_rec_is_valid(&w)) continue;            -> status/dim/2-sector checksum
            if (!embed_rec_matches(&w, rec.seq, rec.query_key)) continue;   -> BOTH identity fields
            ... n++;
        }

    Reproduced here as:
      * membership   : g3_candidate_usable(action, outcome, resp_len) — what the index holds;
      * horizon      : seq < target_seq. The turn's own record is written at the exit, AFTER
                       the recall, so it was not in the index when the selector ran;
      * order        : seq DESCENDING. epi_record_t.seq is hdr.total_entries at write time, so
                       it is monotonic, and both index-maintenance sites append in ascending
                       logical order — the backwards walk is therefore exactly seq-descending;
      * no wrap      : the deployed code re-reads each record and re-checks
                       `rec.query_key != index[i].key`, which guards a post-wrap logical_index
                       aliasing a re-used slot. That check is a tautology while the store is
                       unrolled, which is why it is not mirrored — so non-wrap is ASSERTED in
                       main() rather than assumed here. A wrapped store makes this mirror
                       unfaithful and the script refuses to run;
      * vector horizon: the vector must have EXISTED when the turn ran. g_embed_idx is the boot
                       scan of the store PLUS whatever ctrl_embed_store_one appended live, and
                       the backfill keeps vectoring old records one per [STATS] window long
                       after a turn — so "a vector is in the dump" is NOT "a vector was in the
                       index that day". JVEC records are appended, so the store's own seq is
                       write order, and eligibility is jvec_seq < the TARGET's own jvec_seq.
                       That boundary is exact rather than approximate: the target's vector is
                       written by ctrl_embed_store_one at its own exit, inside the same
                       pa_ctrl_gate call as the recall, with no [STATS] window and no other
                       control-IN turn in between — so the only vector written between the
                       recall and that boundary is the target's own;
      * vector match : embed_idx_find returns the first index entry for that owner_seq, and the
                       BOOT SCAN ONLY INDEXES STRUCTURALLY VALID RECORDS
                       (`if (!embed_rec_is_valid(&w)) { _ebad++; continue; }`). So the search is
                       over VALID records — filtering after picking the first would drop a
                       candidate the box would have admitted. Identity (BOTH owner fields) is
                       then checked, and the deployed code does NOT retry another vector when
                       identity fails — it skips the candidate — so neither does this;
      * cap          : CTRL_SEM_MAX_CANDS.

    max_vec_store_seq is the target's own vector's store seq; pass it explicitly so the horizon
    is a parameter of the mirror rather than a post-hoc warning printed after the fact.
    """
    by_owner = {}
    for li, rec in enumerate(vec_records):          # JSON order == logical-index order
        # Only VALID records reach g_embed_idx (the boot scan skips the rest), and only records
        # written before the turn were in it. Both filters belong HERE, before "first match".
        if rec['status'] != EMBED_STORE_ST_VALID or rec['dim'] != EMBED_STORE_DIM \
                or not rec['checksum_ok']:
            continue                                 # embed_rec_is_valid, at index-build time
        if rec['seq'] >= max_vec_store_seq:
            continue                                 # written after the turn: not yet indexed
        by_owner.setdefault(rec['owner_seq'], []).append((li, rec))

    cands = []
    for rec in sorted((r for r in epi_records if r['seq'] < target_seq),
                      key=lambda r: r['seq'], reverse=True):
        if len(cands) >= CTRL_SEM_MAX_CANDS:
            break
        if not g3_candidate_usable(rec['action'], rec['outcome'], rec['resp_len']):
            continue
        hits = by_owner.get(rec['seq'])
        if not hits:
            continue                                 # embed_idx_find < 0
        _li, v = hits[0]                             # FIRST valid entry; no retry on failure
        if v['owner_seq'] != rec['seq'] or v['owner_key'] != rec['query_key']:
            continue                                 # embed_rec_matches — BOTH fields
        cands.append((rec, v))
    return cands


def raw_resp(epi_bin, epi_records, seq):
    """The EXACT stored response bytes for `seq`, read from the sector.

    NOT epi_records[..]['resp']: parse_episodic decodes with errors='replace', which is lossy,
    and these bytes are the input to g3_clean_answer_len. (Measured on this dump: all 141
    responses round-trip exactly, so this changes nothing here — it removes the class.)
    """
    hdr = pe.parse_header(epi_bin)
    slots = (len(epi_bin) // 512) - 1
    order = pe._wrap_order(hdr['cursor'], hdr['total'], slots)
    for j, slot in enumerate(order):
        off = (slot + 1) * 512
        if off + 512 > len(epi_bin):
            continue
        if epi_records[j]['seq'] != seq:
            continue
        n = min(epi_records[j]['resp_len'], pe.EPI_RESP_MAX)
        return epi_bin[off + pe.EPI_RESP_OFF: off + pe.EPI_RESP_OFF + n]
    raise SystemExit('no sector found for seq %d' % seq)


def raw_vec(vec_bin, li):
    """The EXACT stored float32 vector bytes at logical index `li` (record = 2 sectors,
    vector first — sector order is load-bearing in embed_store.c)."""
    off = 512 + li * pv.EMBED_REC_SIZE
    return vec_bin[off: off + EMBED_STORE_DIM * 4]


def build_blob(qvec_bytes, cands, epi_bin, epi_records, vec_bin, vec_records):
    """Little-endian blob; layout documented in soak_prov_driver.c."""
    li_of = {}
    for li, rec in enumerate(vec_records):
        li_of.setdefault(rec['owner_seq'], li)

    out = bytearray()
    out += struct.pack('<I', len(cands))
    out += qvec_bytes
    for rec, v in cands:
        rb = raw_resp(epi_bin, epi_records, rec['seq'])
        assert len(rb) <= pe.EPI_RESP_MAX
        out += struct.pack('<IHBBHH', rec['seq'], rec['action'], rec['outcome'], 0,
                           len(rb), 0)
        out += rb + b'\x00' * (pe.EPI_RESP_MAX - len(rb))
        vb = raw_vec(vec_bin, li_of[rec['seq']])
        assert len(vb) == EMBED_STORE_DIM * 4
        out += vb
    return bytes(out)


def run_driver(driver, blob_path):
    """Run the driver and parse DOT/SEL/PRE_*.

    The preamble is read as RAW BYTES and its length is asserted against PRE_LEN — that
    assertion is the channel-integrity guard (a text-mode stdout on Windows would rewrite
    every '\\n' separator and silently corrupt the exact bytes this is trying to report).
    """
    p = subprocess.run([driver, blob_path], capture_output=True)
    if p.returncode != 0:
        raise SystemExit('driver failed rc=%d: %s' % (p.returncode, p.stderr.decode('utf-8', 'replace')))
    raw = p.stdout

    head = raw[:raw.index(b'PRE_BEGIN\n')].decode('ascii')
    dots, sels, pre_len = [], [], None
    for line in head.splitlines():
        f = line.split()
        if not f:
            continue
        if f[0] == 'DOT':
            dots.append({'seq': int(f[1]), 'cos_x1000': int(f[2])})
        elif f[0] == 'SEL':
            sels.append({'rank': int(f[1]), 'seq': int(f[2]), 'cos_x1000': int(f[3])})
        elif f[0] == 'PRE_LEN':
            pre_len = int(f[1])

    if pre_len is None:
        raise SystemExit('driver printed no PRE_LEN')

    # PRE_LEN is authoritative: take exactly that many bytes, then require the closing marker
    # to sit immediately after them. Searching backwards for "\nPRE_END" instead would eat the
    # preamble's own final '\n' (every fact is newline-terminated) — measured, not hypothesised.
    b0 = raw.index(b'PRE_BEGIN\n') + len(b'PRE_BEGIN\n')
    pre = raw[b0:b0 + pre_len]
    if len(pre) != pre_len:
        raise SystemExit('TRUNCATED OUTPUT: PRE_LEN says %d, only %d bytes followed the marker.'
                         % (pre_len, len(pre)))
    if raw[b0 + pre_len:b0 + pre_len + len(b'\nPRE_END\n')] != b'\nPRE_END\n':
        raise SystemExit('CHANNEL CORRUPTION: the closing marker is not immediately after '
                         'PRE_LEN=%d bytes. The preamble bytes ARE the evidence and they did '
                         'not survive the transport — refusing to report a verdict.' % pre_len)
    return dots, sels, pre_len, pre


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--epi', required=True)
    ap.add_argument('--vec', required=True)
    ap.add_argument('--epi-bin', required=True)
    ap.add_argument('--vec-bin', required=True)
    ap.add_argument('--target', type=int, default=110)
    ap.add_argument('--driver', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--blob', default=None)
    a = ap.parse_args()

    epi = json.load(open(a.epi, encoding='utf-8'))
    vjson = json.load(open(a.vec, encoding='utf-8'))
    vecs = vjson['records']
    epi_bin = open(a.epi_bin, 'rb').read()
    vec_bin = open(a.vec_bin, 'rb').read()

    # --- preconditions that the mirror's faithfulness depends on. These were prose in an
    # earlier draft; an adversarial review pointed out that a docstring is not a check. ---
    vhdr = vjson['header']
    if not vhdr.get('magic_ok') or not vhdr.get('checksum_ok'):
        raise SystemExit('JVEC header rejected (magic_ok=%s checksum_ok=%s) — record order and '
                         'contents cannot be trusted' % (vhdr.get('magic_ok'), vhdr.get('checksum_ok')))
    if vhdr.get('dim') != EMBED_STORE_DIM:
        raise SystemExit('JVEC dim %s != %d' % (vhdr.get('dim'), EMBED_STORE_DIM))
    if len(vecs) > EMBED_STORE_MAX_VECS:
        raise SystemExit('JVEC holds %d vectors > EMBED_STORE_MAX_VECS %d — g_embed_idx would '
                         'have been full and the mirror is no longer faithful'
                         % (len(vecs), EMBED_STORE_MAX_VECS))
    ehdr = pe.parse_header(epi_bin)
    eslots = (len(epi_bin) // 512) - 1
    if ehdr['total'] > eslots:
        raise SystemExit('control-IN store has WRAPPED (total %d > %d slots). The deployed '
                         'query_key re-check exists for exactly this case and becomes '
                         'load-bearing; logical indices can alias re-used slots, so this '
                         'reconstruction would no longer mirror ctrl_sem_gather.'
                         % (ehdr['total'], eslots))
    if not ehdr['checksum_ok']:
        raise SystemExit('control-IN store header checksum bad')

    tgt = [r for r in epi if r['seq'] == a.target]
    if len(tgt) != 1:
        raise SystemExit('target seq %d: found %d records' % (a.target, len(tgt)))
    tgt = tgt[0]

    # --- the query's OWN stored vector (embed-on-write reuses the recall lane's vector) ---
    qv = [(li, v) for li, v in enumerate(vecs)
          if v['owner_seq'] == a.target and v['owner_key'] == tgt['query_key']
          and v['status'] == EMBED_STORE_ST_VALID and v['dim'] == EMBED_STORE_DIM
          and v['checksum_ok']]
    if len(qv) != 1:
        raise SystemExit('query vector for seq %d: found %d matching records' % (a.target, len(qv)))
    q_li, _qrec = qv[0]
    qvec_bytes = raw_vec(vec_bin, q_li)

    # The target's own vector's store seq IS the horizon (see gather_candidates). JVEC records
    # are appended, so the store's own seq is write order.
    tgt_vec_seq = vecs[q_li]['seq']

    cands = gather_candidates(epi, vecs, a.target, tgt_vec_seq)
    if not cands:
        raise SystemExit('no candidates — nothing to reconstruct')

    # The horizon is ENFORCED inside gather_candidates; this only reports how much it changed,
    # i.e. whether the naive "a vector exists in the dump" rule would have differed. It is a
    # measurement, no longer a warning that fires after the fact.
    naive = gather_candidates(epi, vecs, a.target, max_vec_store_seq=1 << 62)
    excluded = sorted({c[0]['seq'] for c in naive} - {c[0]['seq'] for c in cands})
    fidelity = {
        'target_vector_store_seq': tgt_vec_seq,
        'horizon_enforced_in_gather': True,
        'candidates_excluded_by_the_horizon': len(excluded),
        'excluded_seqs': excluded,
        'note': ('0 means the naive "a vector exists in the dump" rule and the temporally '
                 'faithful "a vector existed BY THEN" rule select the IDENTICAL candidate set '
                 'for this turn. The faithful rule is the one that ran either way.'),
    }
    if excluded:
        print('NOTE: the horizon excluded %d candidate(s) the naive rule would have admitted: %s'
              % (len(excluded), excluded))

    blob_path = a.blob or (os.path.splitext(a.out)[0] + '.blob')
    blob = build_blob(qvec_bytes, cands, epi_bin, epi, vec_bin, vecs)
    with open(blob_path, 'wb') as fh:
        fh.write(blob)

    dots, sels, pre_len, pre = run_driver(a.driver, blob_path)

    # ---------------- POSITIVE CONTROL (§4.4) ----------------
    control_ok = bool(sels) and sels[0]['seq'] == 93 and 943 <= sels[0]['cos_x1000'] <= 945
    control = {
        'expected': 'SEL 1 seq=93 with cos_x1000 in [943,945] (the box recorded 944)',
        'observed': (sels[0] if sels else None),
        'pass': control_ok,
    }

    contains_blue = b'blue' in pre
    sel2 = sels[1] if len(sels) > 1 else None

    # ---------------- PRE-REGISTERED VERDICT (§4.5) — not re-basable ----------------
    if not control_ok:
        verdict = 'CONTROL-FAIL'
    elif sel2 and sel2['seq'] == 108 and sel2['cos_x1000'] >= 550 and contains_blue:
        verdict = 'MECHANISM-CONFIRMED'
    else:
        verdict = 'MECHANISM-REFUTED'

    ranking = []
    sel_by_seq = {s['seq']: s['rank'] for s in sels}
    for d in sorted(dots, key=lambda x: -x['cos_x1000']):
        ranking.append({'seq': d['seq'], 'cos_x1000': d['cos_x1000'],
                        'selected_rank': sel_by_seq.get(d['seq'])})

    # rule 8: TEXT for exactly the three records already public in the soak report.
    public = {}
    for s in (93, 108, a.target):
        r = [x for x in epi if x['seq'] == s]
        if r:
            public[str(s)] = {'query': r[0]['query'], 'resp': r[0]['resp'],
                              'boot_id': r[0]['boot_id']}

    res = {
        'method': ('Re-ran soak control-IN turn [23:%05d] through the DEPLOYED '
                   'g3_select_semantic + g3_build_preamble_answer_only (soak_prov_driver.c '
                   'links phase3/src/ai/g3_retrieval.c) using the box\'s OWN stored float32 '
                   'vectors from the JVEC store at LBA 21,150,000 — including the query\'s '
                   'own, so no host embedding is involved and no host-vs-box cosine delta '
                   'applies. Candidate set mirrors ctrl_sem_gather (main_x86.c:2858-2894).'
                   % a.target),
        'target_seq': a.target,
        'target_recorded_provenance': {
            'recall_kind': tgt['recall_kind_name'],
            'recall_src_seq': tgt['recall_src_seq'],
            'recall_cos_x1000': tgt['recall_cos_x1000'],
        },
        'candidate_count': len(cands),
        'ranking': ranking,
        'selected': [{'rank': s['rank'], 'seq': s['seq'], 'cos_x1000': s['cos_x1000']} for s in sels],
        'preamble_len': pre_len,
        'preamble': pre.decode('utf-8', errors='replace'),
        'preamble_sha256_of_bytes': __import__('hashlib').sha256(pre).hexdigest(),
        'contains_blue': contains_blue,
        'target_stored_answer': tgt['resp'],
        'public_records': public,
        'fidelity_check': fidelity,
        'control': control,
        'verdict': verdict,
    }

    with open(a.out, 'w', encoding='utf-8') as fh:
        json.dump(res, fh, indent=2, ensure_ascii=False)
        fh.write('\n')

    print('candidates: %d   (cap %d)' % (len(cands), CTRL_SEM_MAX_CANDS))
    print('--- FULL RANKING (seq, cos_x1000, selected rank) ---')
    for r in ranking:
        print('  seq=%-5d cos_x1000=%-6d %s'
              % (r['seq'], r['cos_x1000'],
                 ('SELECTED rank %d' % r['selected_rank']) if r['selected_rank'] else ''))
    print('--- SEL ---')
    for s in sels:
        print('  SEL %d %d %d' % (s['rank'], s['seq'], s['cos_x1000']))
    print('--- PREAMBLE (len %d) ---' % pre_len)
    sys.stdout.write(pre.decode('utf-8', errors='replace'))
    print('--- END PREAMBLE ---')
    print('contains_blue = %s' % contains_blue)
    print('CONTROL: %s  (%s)' % ('PASS' if control_ok else 'FAIL', control['expected']))
    print('fidelity: %d candidate(s) excluded by the vector horizon (0 == naive rule agrees)'
          % fidelity['candidates_excluded_by_the_horizon'])
    print('VERDICT: %s' % verdict)
    print('wrote %s' % a.out)
    return 0


if __name__ == '__main__':
    sys.exit(main())
