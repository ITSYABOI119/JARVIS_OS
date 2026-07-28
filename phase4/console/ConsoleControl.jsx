/* JARVIS OS — Telemetry Console · Control-IN (Phase 6 6-5/M3-4b · chat layout Phase C)
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
 * CONTROL_IN flag (key + replay floor + RX ring all up on the box). Control-IN
 * is default-ON in the deployed image (since the 6-5 flip, 2026-07-21), so
 * against the live box this panel renders ENABLED. A record without the flag
 * means THIS frame does not report the channel — a stale frame, an older packet
 * version, or a box on an older build (or a JARVIS_CONTROL_IN=0 build, where
 * the channel really is gated off on the box) — so the composer disables rather
 * than show a live-looking input against a box that may not receive. No
 * telemetry at all, and the box-free simulated preview, are likewise disabled.
 *
 * LAYOUT: a chat — your query as a right-hand bubble, the box's verified reply
 * as a left-hand bubble under the "J" mark, newest at the bottom, the composer
 * pinned at the bottom of the screen. Receiver-side errors and not-yet-replied
 * states render as plain system strips WITHOUT the "J" mark: only a reply that
 * passed HMAC verification is ever presented as coming from the box.
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
  // Functional ONLY on a live record that reports CONTROL_IN. A record without
  // the flag -> disabled (this frame does not report the channel); simulated
  // preview -> disabled; no telemetry -> disabled.
  const channelUp = hasFlag(rec, 'CONTROL_IN');
  const enabled = !!rec && !store.simulated && channelUp;
  const gateReason = !rec
    ? 'No telemetry yet — waiting for the first packet from the box.'
    : store.simulated
      ? 'Box-free preview (SIMULATED): there is no box to send to.'
      : !channelUp
        ? 'This telemetry frame does not report the CONTROL_IN channel. Control-IN is default-ON ' +
          'in the deployed image (since 2026-07-21), so this usually means a stale frame, an ' +
          'older packet version, or a box on an older build — or a JARVIS_CONTROL_IN=0 build, ' +
          'where the channel really is gated off on the box.'
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

  // ---- chat auto-scroll: stick to the newest message ----------------------
  // Stick to the bottom while the operator is at (or near) the bottom; never
  // yank the view while they are reading scrollback. Sending always re-sticks.
  const listRef = React.useRef(null);
  const stickRef = React.useRef(true);
  function onListScroll() {
    const el = listRef.current;
    if (el) stickRef.current = (el.scrollHeight - el.scrollTop - el.clientHeight) < 64;
  }
  React.useEffect(() => {
    const el = listRef.current;
    if (el && stickRef.current) el.scrollTop = el.scrollHeight;
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
    stickRef.current = true;   // the operator just acted — snap the chat to the newest turn
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

  // ---- verdicts + reply body ----------------------------------------------
  // 4 (#9) = the box answered but generation was CUT OFF. Tone 'ok', not 'warn': the box did not
  // fail and the answer is real — it just stops early, and the sub-line says so.
  const VERDICTS = {
    0: { name: 'answered', tone: 'ok' },
    1: { name: 'refused', tone: 'warn' },
    2: { name: 'degraded', tone: 'warn' },
    3: { name: 'failed', tone: 'err' },
    4: { name: 'answered · cut off', tone: 'ok' },
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
    // #9: a CUT-OFF answer. The text is genuine and complete as far as it goes, so it is rendered
    // exactly as verdict 0's is — this is a marker, not an error state, and hiding or dimming the
    // answer would throw away work the box already did. The sub-line is the whole point: before
    // #9 this same text arrived as verdict 0 and was presented as a COMPLETE answer.
    if (v === 4) {
      return { color: C.ok, line: r.text || '(empty answer)',
        sub: 'answered — cut off at the generation limit; there was more to say' };
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

  // A plain system strip — receiver errors and not-yet-replied states. Deliberately
  // NOT under the "J" mark: only an HMAC-verified reply speaks for the box.
  function SystemStrip({ color, children, dot, badge }) {
    return (
      <div style={{ display: 'flex', alignItems: 'flex-start', gap: 8, maxWidth: '86%' }}>
        {dot && <span style={{ marginTop: 3 }}><StatusDot status={dot.status} pulse={!!dot.pulse} /></span>}
        {badge}
        <div style={{ font: '400 var(--text-xs)/1.5 var(--font-mono)', color: color }}>{children}</div>
      </div>
    );
  }

  // ---- one chat turn: your bubble, then the box's side --------------------
  function Turn({ e }) {
    const r = e.reply || null;   // bound once on arrival — never re-derived from the ring
    const verified = !!(r && r.crc_ok && r.hmac_ok);
    const v = verified ? VERDICTS[Number(r.verdict)] : null;
    const body = r ? replyBody(r) : null;
    const waited = e.at ? (Date.now() / 1000 - e.at) : 0;
    const quiet = !r && !e.error && e.seq != null && waited > PENDING_QUIET_S;
    return (
      <div style={{ display: 'flex', flexDirection: 'column', gap: 10 }}>
        {/* your query — right-hand bubble */}
        <div style={{ display: 'flex', flexDirection: 'row-reverse' }}>
          <div style={{ maxWidth: '78%', display: 'flex', flexDirection: 'column', alignItems: 'flex-end', gap: 4 }}>
            <div style={{ font: '400 var(--text-sm)/1.5 var(--font-sans)', color: 'var(--text-primary)',
              background: 'var(--surface-inset)', border: '1px solid var(--border-default)',
              padding: 'var(--space-2) var(--space-3)', borderRadius: 'var(--radius-lg)',
              borderBottomRightRadius: 'var(--radius-xs)', overflowWrap: 'anywhere' }}>
              {e.query}
            </div>
            <span style={{ font: '400 var(--text-2xs)/1 var(--font-mono)', color: 'var(--text-muted)' }}>
              {e.seq != null ? 'you · seq ' + num(e.seq) : 'you · not sent'}
            </span>
          </div>
        </div>

        {/* receiver-side error: the frame never went out (or the receiver refused it) */}
        {e.error && (
          <SystemStrip color="var(--status-err)" dot={{ status: 'err' }}>{e.error}</SystemStrip>
        )}

        {/* sent, no reply yet */}
        {!e.error && !r && !quiet && (
          <SystemStrip color="var(--text-muted)" dot={{ status: 'idle', pulse: true }}>
            sent — awaiting the box reply…
          </SystemStrip>
        )}
        {quiet && (
          <SystemStrip color="var(--status-warn)" dot={{ status: 'warn' }}>
            no reply after {Math.round(waited)}s — the frame was signed and sent, but the box may
            have rejected it before answering (sequence floor or authentication), or the reply was
            lost. Nothing here says the box received it.
          </SystemStrip>
        )}

        {/* a reply record that failed verification: never presented as the box speaking */}
        {r && !verified && body && (
          <SystemStrip color={body.color} badge={<Badge tone="neutral">UNVERIFIED</Badge>}>
            {body.line} · {body.sub}
          </SystemStrip>
        )}

        {/* the box's verified reply — left-hand bubble under the J mark */}
        {verified && body && (
          <div style={{ display: 'flex', gap: 12, alignItems: 'flex-start' }}>
            <div style={{ width: 30, height: 30, flex: 'none', borderRadius: 'var(--radius-sm)',
              background: 'var(--accent)', display: 'flex', alignItems: 'center', justifyContent: 'center',
              boxShadow: 'var(--glow-accent)' }}>
              <span style={{ font: 'var(--weight-bold) 13px/1 var(--font-display)', color: '#fff' }}>J</span>
            </div>
            <div style={{ maxWidth: '82%', minWidth: 0, display: 'flex', flexDirection: 'column', gap: 6 }}>
              <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
                {v && <Badge tone={v.tone === 'ok' ? 'accent' : 'neutral'}>{v.name}</Badge>}
                {r.recv_ts && (
                  <span style={{ font: '400 var(--text-2xs)/1 var(--font-mono)', color: 'var(--text-muted)' }}>
                    {fmtAgo(r.recv_ts)}
                  </span>
                )}
              </div>
              <div style={{ background: 'var(--surface-card)', border: '1px solid var(--border-hairline)',
                borderRadius: 'var(--radius-lg)', borderTopLeftRadius: 'var(--radius-xs)',
                padding: '10px 12px', display: 'flex', flexDirection: 'column', gap: 6 }}>
                <div style={{ font: '400 var(--text-sm)/1.55 var(--font-sans)', color: body.color,
                  overflowWrap: 'anywhere' }}>{body.line}</div>
                <div style={{ font: '400 var(--text-2xs)/1.4 var(--font-mono)', color: 'var(--text-muted)' }}>
                  {body.sub}
                </div>
                {/* Stays on EVERY reply row: it states exactly what was proved (authorship +
                    integrity) and, just as importantly, what was not (confidentiality). */}
                <div style={{ font: '400 var(--text-2xs)/1.4 var(--font-mono)', color: 'var(--text-muted)',
                  borderTop: '1px solid var(--border-hairline)', paddingTop: 6 }}>
                  signed by the box (HMAC-SHA256), verified here · signed, not encrypted — the text
                  was plaintext on the wire
                </div>
              </div>
            </div>
          </div>
        )}
      </div>
    );
  }

  return (
    <div style={{ height: '100%', minHeight: 0, display: 'flex', flexDirection: 'column' }}>
      {/* header */}
      <div style={{ flex: 'none', padding: 'var(--space-5) var(--space-6) var(--space-3)',
        display: 'flex', alignItems: 'flex-end', justifyContent: 'space-between' }}>
        <div>
          <div style={{ font: 'var(--type-eyebrow)', letterSpacing: 'var(--tracking-caps)', textTransform: 'uppercase',
            color: 'var(--text-muted)', marginBottom: 8 }}>
            Two-way · signed by the receiver · {enabled ? 'channel up' : 'channel not reported'}
          </div>
          <h1 style={{ margin: 0, font: 'var(--weight-semibold) var(--text-2xl)/1.1 var(--font-display)',
            letterSpacing: 'var(--tracking-tight)', color: 'var(--text-primary)' }}>Control-IN</h1>
        </div>
        <Badge tone={enabled ? 'accent' : 'neutral'} dot={enabled}>
          {enabled ? 'send enabled' : 'send disabled'}
        </Badge>
      </div>

      {/* conversation — scrolls; newest at the bottom */}
      <div ref={listRef} onScroll={onListScroll}
        style={{ flex: 1, minHeight: 0, overflowY: 'auto', padding: '0 var(--space-6)' }}>
        <div style={{ maxWidth: 780, width: '100%', margin: '0 auto', display: 'flex',
          flexDirection: 'column', gap: 'var(--space-4)', paddingBottom: 'var(--space-4)' }}>

          {/* the honesty ceiling — the channel description, at the top of the conversation */}
          <Card title="What this channel is" padding="md">
            <p style={{ margin: '0 0 12px', font: '400 var(--text-sm)/1.55 var(--font-sans)',
              color: 'var(--text-secondary)' }}>
              A bounded conversation with the local model. Your question is signed by the receiver on this machine,
              checked on the box for authenticity, replay and rate, then scored before it is answered. The answer
              is addressed only to this console — it has no actuator, and inbound text can never select an action:
              the box's action registry is compile-time and human-reviewed.
            </p>
            <p style={{ margin: 0, font: '400 var(--text-sm)/1.55 var(--font-sans)',
              color: 'var(--text-secondary)' }}>
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

          {/* transcript meta / empty state */}
          {entries.length === 0 ? (
            <div style={{ textAlign: 'center', font: '400 var(--text-xs)/1.5 var(--font-mono)',
              color: 'var(--text-muted)', padding: 'var(--space-2) 0' }}>
              No queries sent yet.
            </div>
          ) : (
            <div style={{ textAlign: 'center', font: '400 var(--text-2xs)/1.5 var(--font-mono)',
              color: 'var(--text-muted)' }}>
              {entries.length} sent this session · replies correlated by sequence
            </div>
          )}

          {entries.map((e) => <Turn key={e.id} e={e} />)}
        </div>
      </div>

      {/* composer — pinned at the bottom of the screen */}
      <div style={{ flex: 'none', borderTop: '1px solid var(--border-hairline)', background: 'var(--bg-canvas)',
        padding: 'var(--space-3) var(--space-6) var(--space-4)' }}>
        <div style={{ maxWidth: 780, margin: '0 auto' }}>
          {!enabled && (
            <div style={{ marginBottom: 'var(--space-3)', padding: '10px 12px', borderRadius: 'var(--radius-md)',
              border: '1px dashed var(--border-strong)', font: '400 var(--text-xs)/1.55 var(--font-mono)',
              color: 'var(--text-muted)' }}>
              {gateReason}
            </div>
          )}
          <div style={{ background: 'var(--surface-card)', border: '1px solid var(--border-strong)',
            borderRadius: 'var(--radius-lg)', boxShadow: 'var(--shadow-md)',
            padding: 'var(--space-2) var(--space-3) var(--space-3)' }}>
            <div style={{ display: 'flex', alignItems: 'baseline', gap: 8, marginBottom: 6 }}>
              <span style={{ font: 'var(--type-eyebrow)', letterSpacing: 'var(--tracking-caps)',
                textTransform: 'uppercase', color: 'var(--text-muted)' }}>Send a query</span>
              <span style={{ font: '400 var(--text-2xs)/1.4 var(--font-mono)', color: 'var(--text-muted)' }}>
                one short question in, one bounded answer out
              </span>
            </div>
            <div style={{ display: 'flex', gap: 10, alignItems: 'center' }}>
              <input
                type="text"
                value={text}
                disabled={!enabled || busy}
                maxLength={CONTROL_QUERY_MAX}
                onChange={(ev) => setText(ev.target.value)}
                onKeyDown={onKeyDown}
                placeholder={enabled ? 'e.g. what is a page fault?' : 'channel not reported by this frame'}
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
            <div style={{ marginTop: 8, display: 'flex', justifyContent: 'space-between', gap: 12,
              font: '400 var(--text-2xs)/1.4 var(--font-mono)',
              color: overCap ? 'var(--status-err)' : 'var(--text-muted)' }}>
              <span>
                {remaining} of {CONTROL_QUERY_MAX} bytes remaining (UTF-8, not characters)
                {overCap ? ' — over the wire cap' : ''}
              </span>
              <span>signed by telemetry_receiver.py · the browser never holds the key</span>
            </div>
          </div>
        </div>
      </div>
    </div>
  );
}

window.ControlIn = ControlIn;
