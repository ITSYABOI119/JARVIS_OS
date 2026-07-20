/* JARVIS OS — Telemetry Console · Control-IN (Phase 6 6-5/M3-4b)
 * ---------------------------------------------------------------------------
 * The ONE non-read-only surface. Everything else in this console renders the
 * inbound telemetry stream; this panel can SEND a query to the box.
 *
 * Path (the browser never holds a key and never touches the wire):
 *   this panel  --POST /send-->  telemetry_receiver.py  --signed JCTL/UDP-->  box
 *   box  --JRPL/UDP-->  telemetry_receiver.py  --SSE 'control_reply'-->  this panel
 *
 * HONEST SCOPE — what this is and is not:
 *   - It is a bounded conversation with the local model: a short query in, one
 *     bounded answer out. The answer has no actuator; inbound text can never
 *     select or mint an action on the box (the action registry is compile-time).
 *   - The box refuses a defined abuse class of query and audits the refusal by
 *     its reason label only (the raw query never leaves the box in a refusal).
 *     General prompt injection is contained structurally — by that no-action
 *     boundary and by the answer being addressed only to this console (unicast to
 *     the provisioned address, never broadcast) — and is not detected.
 *   - The box->console reply is AUTHENTICATED: it carries an HMAC-SHA256 tag
 *     under the same shared key as the inbound direction, verified constant-time
 *     by the receiver. A reply that fails that check is dropped before it ever
 *     reaches this panel, so nothing unverified is rendered.
 *   - SIGNED, NOT ENCRYPTED. The tag proves authorship and integrity; the answer
 *     text is plaintext on the wire and anyone who captures the frame can read
 *     it. This stops a forged reply, not eavesdropping.
 *
 * GATING: the composer is FUNCTIONAL only while the live record reports the
 * CONTROL_IN flag (key + replay floor + RX ring all up on the box). The
 * deployed box runs JARVIS_CONTROL_IN=0, so today this panel renders DISABLED —
 * showing a live-looking input against a box that cannot receive would be
 * exactly the fiction this console exists to avoid. No telemetry at all, and
 * the box-free simulated preview, are likewise disabled.
 */

const CONTROL_QUERY_MAX = 172;   // CONTROL_QUERY_MAX (phase3/src/net/control_msg.h), BYTES not chars
// After this long with no reply, say so plainly instead of pulsing "awaiting" forever. A frame the
// box rejects before answering (sequence floor / authentication) produces NO reply at all.
const PENDING_QUIET_S = 15;

function ControlIn({ store }) {
  const { Card, Badge, StatusDot } = window.JarvisOSDesignSystem_e0065d;
  const { num, fmtAgo, hasFlag } = window.JConsoleHelpers;
  const rec = store.latest;

  // ---- gating predicate (honest, load-bearing) ----------------------------
  // Functional ONLY on a live record that reports CONTROL_IN. The deploy is
  // gated off (no flag) -> disabled; simulated preview -> disabled; no
  // telemetry -> disabled.
  const channelUp = hasFlag(rec, 'CONTROL_IN');
  const enabled = !!rec && !store.simulated && channelUp;
  const gateReason = !rec
    ? 'No telemetry yet — waiting for the first packet from the box.'
    : store.simulated
      ? 'Box-free preview (SIMULATED): there is no box to send to.'
      : !channelUp
        ? 'Control-IN is gated off on the box (JARVIS_CONTROL_IN=0). The receive path is built and ' +
          'proven but not enabled in the deployed image — the send path activates at the flip.'
        : null;

  const [text, setText] = React.useState('');
  const [entries, setEntries] = React.useState([]);   // [{id, query, seq, error, at}]
  const [busy, setBusy] = React.useState(false);
  const nextId = React.useRef(1);

  const bytes = React.useMemo(() => {
    try { return new TextEncoder().encode(text).length; } catch (e) { return text.length; }
  }, [text]);
  const remaining = CONTROL_QUERY_MAX - bytes;
  const overCap = remaining < 0;
  const canSend = enabled && !busy && bytes > 0 && !overCap;

  // ---- replies from the SSE stream, correlated by seq ---------------------
  // The box echoes the request sequence as a u16 in its JRPL reply, so both
  // sides are compared masked to 16 bits (a u64 request seq would never match
  // the echo otherwise).
  const replies = store.replies || [];
  const bySeq = React.useMemo(() => {
    const m = {};
    replies.forEach((r) => {
      if (r && typeof r.seq === 'number') m[(r.seq >>> 0) & 0xFFFF] = r;
    });
    return m;
  }, [replies]);

  // Bind an arriving reply ONTO its turn the first time it matches, and render from that
  // copy. store.replies is a BOUNDED ring: a turn that re-derived its reply from the ring
  // on every render would silently revert to "awaiting the box reply" once enough later
  // replies evicted it. Matching is only ever done for turns still waiting.
  React.useEffect(() => {
    if (!replies.length) return;
    setEntries((prev) => {
      let changed = false;
      const next = prev.map((e) => {
        if (e.reply || e.error || e.seq == null) return e;
        const r = bySeq[(e.seq >>> 0) & 0xFFFF];
        if (!r) return e;
        changed = true;
        return Object.assign({}, e, { reply: r });
      });
      return changed ? next : prev;
    });
  }, [bySeq, replies.length]);

  // Re-render once a second while any turn is still waiting, so the no-reply notice below
  // appears on time (nothing else on this screen is time-driven).
  const [, setTick] = React.useState(0);
  React.useEffect(() => {
    if (!entries.some((e) => !e.reply && !e.error && e.seq != null)) return undefined;
    const t = setInterval(() => setTick((n) => n + 1), 1000);
    return () => clearInterval(t);
  }, [entries]);

  async function send() {
    if (!canSend) return;
    const q = text;
    setBusy(true);
    let res = null, body = null;
    try {
      res = await fetch('/send', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ query: q }),
      });
      try { body = await res.json(); } catch (e) { body = null; }
    } catch (e) {
      res = null;
      body = null;
    }
    const id = nextId.current++;
    if (res && res.status === 200 && body && body.sent) {
      setEntries((prev) => prev.concat([{ id, query: q, seq: Number(body.seq), error: null, at: Date.now() / 1000 }]));
      setText('');
    } else {
      let err;
      if (!res) err = 'send failed — the receiver did not answer (is telemetry_receiver.py still running?)';
      else if (body && body.error) err = body.error;
      else err = 'send failed — HTTP ' + res.status;
      if (res && res.status === 503) {
        err += ' — run the receiver with --send (the raw signed frame needs elevation).';
      }
      setEntries((prev) => prev.concat([{ id, query: q, seq: null, error: err, at: Date.now() / 1000 }]));
    }
    setBusy(false);
  }

  function onKeyDown(e) {
    if (e.key === 'Enter' && canSend) { e.preventDefault(); send(); }
  }

  // ---- one transcript row -------------------------------------------------
  const VERDICTS = {
    0: { name: 'answered', tone: 'ok' },
    1: { name: 'refused', tone: 'warn' },
    2: { name: 'degraded', tone: 'warn' },
    3: { name: 'failed', tone: 'err' },
  };

  function replyBody(r) {
    const C = { ok: 'var(--text-secondary)', warn: 'var(--status-warn)', err: 'var(--status-err)', muted: 'var(--text-muted)' };
    // Defence in depth: the receiver already DROPS any reply that fails its HMAC or CRC, so
    // this branch should be unreachable. If a record ever arrives without both, refuse to
    // present it as an answer rather than trust the upstream filter.
    if (!r.crc_ok || !r.hmac_ok) {
      return { color: C.err,
        line: 'reply failed verification — not shown as an answer',
        sub: 'only replies signed by the box (HMAC-SHA256) are rendered' };
    }
    const v = Number(r.verdict);
    if (v === 0) {
      return { color: C.ok, line: r.text || '(empty answer)',
        sub: 'answered — one bounded inference or a cache hit on the box' };
    }
    if (v === 1) {
      return { color: C.warn, line: 'refused — defined abuse class',
        sub: 'reason label: ' + (r.text || '(none)') +
             ' · the box refuses a defined abuse class; general injection is contained structurally, not detected' };
    }
    if (v === 2) {
      return { color: C.warn, line: 'degraded — inference is down, so the box did not route the query',
        sub: 'the box is serving cache-only; nothing was dispatched' };
    }
    if (v === 3) {
      return { color: C.err, line: 'failed — the box accepted the query but no answer came back in time',
        sub: 'a control-IN timeout is never counted as a workload error on the box' };
    }
    return { color: C.muted, line: 'unknown verdict ' + v, sub: 'reported verbatim — not interpreted' };
  }

  function Turn({ e }) {
    const r = e.reply || null;   // bound once on arrival — never re-derived from the ring
    const verified = !!(r && r.crc_ok && r.hmac_ok);
    const v = verified ? VERDICTS[Number(r.verdict)] : null;
    const body = r ? replyBody(r) : null;
    const waited = e.at ? (Date.now() / 1000 - e.at) : 0;
    const quiet = !r && !e.error && e.seq != null && waited > PENDING_QUIET_S;
    return (
      <div style={{ border: '1px solid var(--border-hairline)', borderRadius: 'var(--radius-md)',
        background: 'var(--surface-inset)', padding: '12px 14px', display: 'flex', flexDirection: 'column', gap: 8 }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: 10 }}>
          <StatusDot status={e.error ? 'err' : r ? (v && v.tone === 'ok' ? 'ok' : 'warn') : 'idle'} pulse={!e.error && !r} />
          <span style={{ flex: 1, font: '400 var(--text-sm)/1.4 var(--font-sans)', color: 'var(--text-primary)' }}>{e.query}</span>
          <span style={{ font: '400 var(--text-2xs)/1 var(--font-mono)', color: 'var(--text-muted)' }}>
            {e.seq != null ? 'seq ' + num(e.seq) : 'not sent'}
          </span>
          {verified && v && <Badge tone={v.tone === 'ok' ? 'accent' : 'neutral'}>{v.name}</Badge>}
          {r && !verified && <Badge tone="neutral">UNVERIFIED</Badge>}
        </div>
        {e.error && (
          <div style={{ font: '400 var(--text-xs)/1.5 var(--font-mono)', color: 'var(--status-err)' }}>{e.error}</div>
        )}
        {!e.error && !r && !quiet && (
          <div style={{ font: '400 var(--text-xs)/1.5 var(--font-mono)', color: 'var(--text-muted)' }}>
            sent — awaiting the box reply…
          </div>
        )}
        {quiet && (
          <div style={{ font: '400 var(--text-xs)/1.5 var(--font-mono)', color: 'var(--status-warn)' }}>
            no reply after {Math.round(waited)}s — the frame was signed and sent, but the box may
            have rejected it before answering (sequence floor or authentication), or the reply was
            lost. Nothing here says the box received it.
          </div>
        )}
        {body && (
          <div>
            <div style={{ font: '400 var(--text-sm)/1.5 var(--font-sans)', color: body.color }}>{body.line}</div>
            <div style={{ marginTop: 4, font: '400 var(--text-2xs)/1.4 var(--font-mono)', color: 'var(--text-muted)' }}>
              {body.sub}{r.recv_ts ? ' · ' + fmtAgo(r.recv_ts) : ''}
            </div>
            {/* Stays on EVERY reply row: it states exactly what was proved (authorship +
                integrity) and, just as importantly, what was not (confidentiality). */}
            <div style={{ marginTop: 4, font: '400 var(--text-2xs)/1.4 var(--font-mono)', color: 'var(--text-muted)' }}>
              signed by the box (HMAC-SHA256), verified here · signed, not encrypted — the text
              was plaintext on the wire
            </div>
          </div>
        )}
      </div>
    );
  }

  return (
    <div style={{ padding: 'var(--space-6)', overflow: 'auto', display: 'flex', flexDirection: 'column', gap: 'var(--space-5)' }}>
      <div style={{ display: 'flex', alignItems: 'flex-end', justifyContent: 'space-between' }}>
        <div>
          <div style={{ font: 'var(--type-eyebrow)', letterSpacing: 'var(--tracking-caps)', textTransform: 'uppercase',
            color: 'var(--text-muted)', marginBottom: 8 }}>
            Two-way · signed by the receiver · {enabled ? 'channel up' : 'gated off'}
          </div>
          <h1 style={{ margin: 0, font: 'var(--weight-semibold) var(--text-2xl)/1.1 var(--font-display)',
            letterSpacing: 'var(--tracking-tight)', color: 'var(--text-primary)' }}>Control-IN</h1>
        </div>
        <Badge tone={enabled ? 'accent' : 'neutral'} dot={enabled}>
          {enabled ? 'send enabled' : 'send disabled'}
        </Badge>
      </div>

      {/* composer — DISABLED unless the live record reports CONTROL_IN */}
      <Card title="Send a query"
        subtitle="a bounded conversation with the local model — one short question in, one bounded answer out"
        padding="md">
        {!enabled && (
          <div style={{ marginBottom: 'var(--space-4)', padding: '10px 12px', borderRadius: 'var(--radius-md)',
            border: '1px dashed var(--border-strong)', font: '400 var(--text-xs)/1.55 var(--font-mono)',
            color: 'var(--text-muted)' }}>
            {gateReason}
          </div>
        )}
        <div style={{ display: 'flex', gap: 10, alignItems: 'center' }}>
          <input
            type="text"
            value={text}
            disabled={!enabled || busy}
            maxLength={CONTROL_QUERY_MAX}
            onChange={(ev) => setText(ev.target.value)}
            onKeyDown={onKeyDown}
            placeholder={enabled ? 'e.g. what is a page fault?' : 'send path gated off on the box'}
            style={{ flex: 1, height: 38, padding: '0 12px', borderRadius: 'var(--radius-md)',
              border: '1px solid var(--border-default)', background: 'var(--surface-inset)',
              color: enabled ? 'var(--text-primary)' : 'var(--text-muted)',
              font: '400 var(--text-sm)/1 var(--font-mono)', outline: 'none',
              opacity: enabled ? 1 : 0.6, cursor: enabled ? 'text' : 'not-allowed' }}
          />
          <button
            onClick={send}
            disabled={!canSend}
            style={{ height: 38, padding: '0 18px', borderRadius: 'var(--radius-md)',
              background: canSend ? 'var(--accent)' : 'var(--surface-inset)',
              color: canSend ? '#fff' : 'var(--text-muted)',
              font: 'var(--weight-semibold) var(--text-sm)/1 var(--font-sans)',
              cursor: canSend ? 'pointer' : 'not-allowed',
              border: canSend ? 'none' : '1px solid var(--border-strong)' }}>
            {busy ? 'sending…' : 'Send'}
          </button>
        </div>
        <div style={{ marginTop: 8, display: 'flex', justifyContent: 'space-between',
          font: '400 var(--text-2xs)/1.4 var(--font-mono)',
          color: overCap ? 'var(--status-err)' : 'var(--text-muted)' }}>
          <span>
            {remaining} of {CONTROL_QUERY_MAX} bytes remaining (UTF-8, not characters)
            {overCap ? ' — over the wire cap' : ''}
          </span>
          <span>signed by telemetry_receiver.py · the browser never holds the key</span>
        </div>
      </Card>

      {/* transcript — every sent query and its correlated reply */}
      <Card title="Transcript"
        subtitle={entries.length ? entries.length + ' sent this session · replies correlated by sequence'
                                 : 'nothing sent this session'}
        padding="md">
        {entries.length === 0 && (
          <div style={{ font: '400 var(--text-sm)/1.4 var(--font-mono)', color: 'var(--text-muted)' }}>
            No queries sent yet.
          </div>
        )}
        {entries.length > 0 && (
          <div style={{ display: 'flex', flexDirection: 'column', gap: 10 }}>
            {entries.slice().reverse().map((e) => <Turn key={e.id} e={e} />)}
          </div>
        )}
      </Card>

      {/* the honesty ceiling, on-screen */}
      <Card title="What this channel is" padding="md">
        <p style={{ margin: '0 0 12px', font: '400 var(--text-sm)/1.55 var(--font-sans)',
          color: 'var(--text-secondary)', maxWidth: 780 }}>
          A bounded conversation with the local model. Your question is signed by the receiver on this machine,
          checked on the box for authenticity, replay and rate, then scored before it is answered. The answer
          is addressed only to this console — it has no actuator, and inbound text can never select an action:
          the box's action registry is compile-time and human-reviewed.
        </p>
        <p style={{ margin: 0, font: '400 var(--text-sm)/1.55 var(--font-sans)',
          color: 'var(--text-secondary)', maxWidth: 780 }}>
          The box refuses a <strong>defined abuse class</strong> of query and audits the refusal by its reason
          label alone. General prompt injection is contained <strong>structurally</strong> — by that no-action
          boundary and by the answer being <strong>addressed</strong> only to this console (unicast to the
          provisioned address, never broadcast; that is addressing, not a proof no other host saw it)
          — and is <strong>not detected</strong>. The reply is <strong>authenticated</strong>: it carries an
          HMAC-SHA256 tag under the same shared key as your question, verified here before anything is
          shown, so a reply this console cannot verify is dropped rather than displayed. That is
          <strong> signed, not encrypted</strong> — the answer text travels in plaintext and anyone who
          captures the frame can read it; what the signature stops is a forged answer, not eavesdropping.
        </p>
      </Card>
    </div>
  );
}

window.ControlIn = ControlIn;
