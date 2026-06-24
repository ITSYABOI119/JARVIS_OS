/* JARVIS OS — Telemetry Console · Command Center (live overview) */

function CommandCenter({ store }) {
  const { Card, Badge } = window.JarvisOSDesignSystem_e0065d;
  const { Icon } = window.JarvisShell;
  const { num, pct, fmtClock, hasFlag } = window.JConsoleHelpers;
  const rec = store.latest;

  // state eyebrow — never "nominal"/"verified"
  let stateWord = 'DISCONNECTED', stateColor = 'var(--status-err)';
  if (rec) {
    if (store.connState === 'live') {
      const err = hasFlag(rec, 'HAS_ERROR') || (Number(rec.q_errors) || 0) > 0;
      stateWord = err ? 'ERROR' : 'READY';
      stateColor = err ? 'var(--status-err)' : 'var(--status-ok)';
    } else if (store.connState === 'stale') { stateWord = 'STALE'; stateColor = 'var(--status-warn)'; }
    else if (store.connState === 'crcfail') { stateWord = 'CRC FAIL'; stateColor = 'var(--status-err)'; }
  }

  const qTotal = rec ? Number(rec.q_total) || 0 : 0;
  const cacheP = rec ? pct(rec.q_hits, rec.q_total) : null;
  const llmP = rec ? pct(rec.q_infer, rec.q_total) : null;
  const qErr = rec ? Number(rec.q_errors) || 0 : 0;
  const qps = window.JarvisTelemetry.queriesPerSec();

  function Tile({ label, children, foot }) {
    return (
      <Card padding="md">
        <span style={{ font: 'var(--type-eyebrow)', letterSpacing: 'var(--tracking-wide)', textTransform: 'uppercase',
          color: 'var(--text-muted)' }}>{label}</span>
        <div style={{ marginTop: 10 }}>{children}</div>
        {foot && <div style={{ marginTop: 6, font: '400 var(--text-2xs)/1.3 var(--font-mono)', color: 'var(--text-muted)' }}>{foot}</div>}
      </Card>
    );
  }
  const big = { font: 'var(--weight-semibold) var(--text-3xl)/1 var(--font-display)', color: 'var(--text-primary)' };
  const unit = { font: '400 var(--text-sm)/1 var(--font-mono)', color: 'var(--text-muted)' };

  // real rolling-buffer sparkline (queries/sec) — collecting state until enough samples
  function RateSpark() {
    const s = store.rateSamples || [];
    if (s.length < 4) {
      return <div style={{ height: 28, display: 'flex', alignItems: 'center',
        font: '400 var(--text-2xs)/1 var(--font-mono)', color: 'var(--text-muted)' }}>collecting…</div>;
    }
    const deltas = [];
    for (let i = 1; i < s.length; i++) {
      const dt = s[i].t - s[i - 1].t;
      deltas.push(dt > 0 ? Math.max(0, (s[i].q - s[i - 1].q) / dt) : 0);
    }
    const max = Math.max(...deltas, 1);
    return (
      <div style={{ display: 'flex', alignItems: 'flex-end', gap: 2, height: 28 }}>
        {deltas.map((v, i) => (
          <span key={i} style={{ width: 4, height: `${(v / max) * 100}%`, minHeight: 2,
            background: 'var(--accent)', opacity: 0.4 + (i / deltas.length) * 0.6, borderRadius: 1 }} />
        ))}
      </div>
    );
  }

  // activity feed from live SSE records, keyed on kind_name
  const kindMeta = {
    STATS: { icon: 'activity', color: 'var(--text-accent)' },
    INFER: { icon: 'message-square', color: 'var(--status-live)' },
    STATE: { icon: 'git-commit-horizontal', color: 'var(--text-secondary)' },
  };

  return (
    <div style={{ padding: 'var(--space-6)', overflow: 'auto', display: 'flex', flexDirection: 'column', gap: 'var(--space-5)' }}>
      <div style={{ display: 'flex', alignItems: 'flex-end', justifyContent: 'space-between' }}>
        <div>
          <div style={{ font: 'var(--type-eyebrow)', letterSpacing: 'var(--tracking-caps)', textTransform: 'uppercase',
            color: stateColor, marginBottom: 8 }}>
            seL4 · x86-64 (functional-but-unverified · perf config) · {stateWord}
          </div>
          <h1 style={{ margin: 0, font: 'var(--weight-semibold) var(--text-3xl)/1.1 var(--font-display)',
            letterSpacing: 'var(--tracking-tight)', color: 'var(--text-primary)' }}>
            {rec ? 'JARVIS telemetry' : 'Awaiting telemetry'}
          </h1>
        </div>
        <a href="#" onClick={(e) => { e.preventDefault(); store._setView && store._setView('last'); }}
          style={{ display: 'inline-flex', alignItems: 'center', gap: 8, height: 34, padding: '0 14px',
            borderRadius: 'var(--radius-sm)', border: '1px solid var(--border-strong)', background: 'var(--surface-inset)',
            color: 'var(--text-primary)', font: 'var(--weight-medium) var(--text-sm)/1 var(--font-sans)', textDecoration: 'none' }}>
          <Icon name="eye" size={15} /> View last response
        </a>
      </div>

      {/* Live model-load bar — UI-feature parity with the on-box HUD progress bar.
          Sourced ONLY from the live record's model_load_pct (already on /events). */}
      {rec && (() => {
        const lp = Math.max(0, Math.min(100, Number(rec.model_load_pct) || 0));
        const done = lp >= 100;
        return (
          <div style={{ display: 'flex', alignItems: 'center', gap: 12 }}>
            <span style={{ font: 'var(--type-eyebrow)', letterSpacing: 'var(--tracking-wide)', textTransform: 'uppercase',
              color: 'var(--text-muted)', minWidth: 78 }}>Model load</span>
            <div style={{ flex: 1, maxWidth: 360, height: 8, background: 'var(--surface-inset)',
              border: '1px solid var(--border-strong)', borderRadius: 'var(--radius-full)', overflow: 'hidden' }}>
              <div style={{ width: lp + '%', height: '100%',
                background: done ? 'var(--status-ok)' : 'var(--accent)' }} />
            </div>
            <span style={{ font: '400 var(--text-xs)/1 var(--font-mono)',
              color: done ? 'var(--status-ok)' : 'var(--text-secondary)' }}>
              {done ? 'loaded' : lp + '%'}
            </span>
          </div>
        );
      })()}

      <div style={{ display: 'grid', gridTemplateColumns: 'repeat(4, 1fr)', gap: 'var(--space-4)' }}>
        <Tile label="Queries" foot={qps == null ? 'rate: collecting…' : qps.toFixed(1) + ' q/s (rolling)'}>
          <div style={{ display: 'flex', alignItems: 'flex-end', justifyContent: 'space-between' }}>
            <div style={{ display: 'flex', alignItems: 'baseline', gap: 4 }}>
              <span style={big}>{rec ? num(qTotal) : '—'}</span>
              <span style={unit}>q_total</span>
            </div>
            <RateSpark />
          </div>
        </Tile>

        <Tile label="Route" foot={qTotal === 0 ? null : 'cache vs → LLM (aggregate)'}>
          {qTotal === 0 ? (
            <div style={{ font: '400 var(--text-sm)/1.3 var(--font-mono)', color: 'var(--text-muted)', paddingTop: 6 }}>no queries yet</div>
          ) : (
            <div style={{ display: 'flex', alignItems: 'baseline', gap: 10 }}>
              <span style={{ ...big, fontSize: 'var(--text-2xl)', color: 'var(--status-ok)' }}>{cacheP.toFixed(0)}%</span>
              <span style={unit}>cache</span>
              <span style={{ ...big, fontSize: 'var(--text-2xl)', color: 'var(--text-accent)' }}>{llmP.toFixed(0)}%</span>
              <span style={unit}>→ LLM</span>
            </div>
          )}
        </Tile>

        <Tile label="Errors" foot="q_errors">
          <div style={{ display: 'flex', alignItems: 'baseline', gap: 4 }}>
            <span style={{ ...big, color: qErr > 0 ? 'var(--status-err)' : 'var(--text-primary)' }}>{rec ? num(qErr) : '—'}</span>
            {qErr === 0 && rec && <Badge tone="ok">clean</Badge>}
          </div>
        </Tile>

        <Tile label="Throughput" foot="deployed benchmark · not live">
          <div style={{ display: 'flex', alignItems: 'baseline', gap: 4 }}>
            <span style={big}>5.46</span><span style={unit}>tok/s</span>
          </div>
          <div style={{ marginTop: 4, font: '400 var(--text-2xs)/1.3 var(--font-mono)', color: 'var(--text-muted)' }}>
            Gemma 4 E2B · CPU · 6 cores
          </div>
        </Tile>
      </div>

      <div style={{ display: 'grid', gridTemplateColumns: '0.85fr 1.15fr', gap: 'var(--space-4)' }}>
        <ProcessesCard store={store} />

        <Card title="System activity" subtitle="live SSE records · keyed on kind_name"
          right={store.droppedPackets > 0 ? <Badge tone="warn">{store.droppedPackets} dropped</Badge> : <Badge tone="neutral">{num(store.records.length)}</Badge>}
          padding="none">
          <div style={{ padding: '4px var(--space-3) var(--space-3)', maxHeight: 320, overflow: 'auto' }}>
            {(!rec) && <div style={{ padding: 'var(--space-4)', font: '400 var(--text-sm)/1 var(--font-mono)', color: 'var(--text-muted)' }}>waiting for first packet…</div>}
            {store.records.map((r, i) => {
              const meta = kindMeta[r.kind_name] || kindMeta.STATS;
              return (
                <div key={r.seq + '-' + i} style={{ display: 'flex', gap: 12, alignItems: 'center', padding: 'var(--space-2) var(--space-2)',
                  borderBottom: i < store.records.length - 1 ? '1px solid var(--border-hairline)' : 'none' }}>
                  <span style={{ width: 22, height: 22, flex: 'none', borderRadius: 'var(--radius-sm)',
                    display: 'flex', alignItems: 'center', justifyContent: 'center', background: 'var(--surface-inset)', color: meta.color }}>
                    <Icon name={meta.icon} size={12} />
                  </span>
                  <span style={{ font: '500 var(--text-2xs)/1 var(--font-mono)', color: 'var(--text-secondary)', width: 46, textTransform: 'uppercase' }}>{r.kind_name}</span>
                  <span style={{ font: '400 var(--text-xs)/1.3 var(--font-mono)', color: 'var(--text-muted)', flex: 1, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>
                    seq {r.seq} · q={num(r.q_total)}{r.kind_name === 'INFER' && r.last_text ? ' · “…' + r.last_text + '”' : ''}
                  </span>
                  <span style={{ font: '400 var(--text-2xs)/1.6 var(--font-mono)', color: 'var(--text-muted)', flex: 'none' }}>{fmtClock(r.recv_ts)}</span>
                </div>
              );
            })}
          </div>
        </Card>
      </div>
    </div>
  );
}

/* shared Processes card (used by Command Center + Routing) */
function ProcessesCard({ store }) {
  const { Card, StatusDot } = window.JarvisOSDesignSystem_e0065d;
  const { processStatuses } = window.JarvisShell;
  const rec = store.latest;
  const { procA, procB } = processStatuses(rec, store.live);
  const row = (name, sub, st) => (
    <div style={{ display: 'flex', alignItems: 'center', gap: 12, padding: 'var(--space-3)', borderRadius: 'var(--radius-sm)' }}>
      <StatusDot status={st.dot} pulse={st.pulse} />
      <span style={{ font: 'var(--weight-medium) var(--text-sm)/1 var(--font-mono)', color: 'var(--text-primary)', width: 96 }}>{name}</span>
      <span style={{ font: '400 var(--text-xs)/1 var(--font-mono)', color: 'var(--text-secondary)', flex: 1 }}>{sub}</span>
      <span style={{ font: '500 var(--text-2xs)/1 var(--font-mono)', textTransform: 'uppercase', color: st.color }}>{st.label}</span>
    </div>
  );
  return (
    <Card title="Processes" subtitle="single-process box · A rootserver + B inference" padding="none">
      <div style={{ padding: '4px var(--space-2) var(--space-2)' }}>
        {row('Process A', 'seL4 rootserver', procA)}
        {row('Process B', rec ? rec.model_name + ' inference' : 'inference', procB)}
        <div style={{ padding: '6px var(--space-3) var(--space-2)', font: '400 var(--text-2xs)/1.5 var(--font-mono)', color: 'var(--text-muted)' }}>
          self-test 5/5 — 2 of 5 are vacuous
        </div>
      </div>
    </Card>
  );
}

window.CommandCenter = CommandCenter;
window.ProcessesCard = ProcessesCard;
