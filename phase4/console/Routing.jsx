/* JARVIS OS — Telemetry Console · Routing view
 * Live route split = aggregate ratio only (no per-query routing).
 * The 4 specialists appear ONLY as a static roadmap diagram — not live in the
 * deployed build.
 */

function Routing({ store }) {
  const { Card, Badge } = window.JarvisOSDesignSystem_e0065d;
  const { Icon } = window.JarvisShell;
  const { num, pct } = window.JConsoleHelpers;
  const rec = store.latest;
  const qTotal = rec ? Number(rec.q_total) || 0 : 0;

  const splits = qTotal === 0 ? null : [
    { k: 'q_hits', label: 'Cache hit', v: rec.q_hits, p: pct(rec.q_hits, rec.q_total), color: 'var(--status-ok)' },
    { k: 'q_infer', label: '→ LLM (inference)', v: rec.q_infer, p: pct(rec.q_infer, rec.q_total), color: 'var(--text-accent)' },
    { k: 'q_heartbeat', label: 'Heartbeat', v: rec.q_heartbeat, p: pct(rec.q_heartbeat, rec.q_total), color: 'var(--text-secondary)' },
    { k: 'q_shield', label: 'SHIELD check (passive)', v: rec.q_shield, p: pct(rec.q_shield, rec.q_total), color: 'var(--status-warn)' },
  ];

  /* v12 route_* are ROUTING DECISIONS at classification time. route_inited is the live-vs-gated
   * indicator (routing has no flag bit — the u16 flags word is exhausted). A v11/v10 frame carries
   * null for all of them, so an older box renders "—", never a fabricated 0. */
  const routeLive = !!rec && rec.route_inited === 1;
  const routeRows = !routeLive ? [] : [
    { k: 'route_sysfacts', label: 'system-facts (answered from box state)', v: rec.route_sysfacts },
    { k: 'route_decline',  label: 'declined (no source for it)',            v: rec.route_decline },
    { k: 'route_infer',    label: 'inference (to the model)',               v: rec.route_infer },
  ];

  const specialists = [
    { name: 'device-manager', icon: 'cpu', domain: 'Hardware, disk, memory, thermal' },
    { name: 'network', icon: 'wifi', domain: 'Connectivity, ping, ports, routes' },
    { name: 'filesystem', icon: 'folder-tree', domain: 'Files, directories, permissions' },
    { name: 'user-interaction', icon: 'message-square', domain: 'Help, clarify, general queries' },
  ];

  return (
    <div style={{ padding: 'var(--space-6)', overflow: 'auto', display: 'flex', flexDirection: 'column', gap: 'var(--space-5)' }}>
      <div>
        <div style={{ font: 'var(--type-eyebrow)', letterSpacing: 'var(--tracking-caps)', textTransform: 'uppercase',
          color: 'var(--text-accent)', marginBottom: 8 }}>Aggregate routing · live</div>
        <h1 style={{ margin: 0, font: 'var(--weight-semibold) var(--text-2xl)/1.1 var(--font-display)',
          letterSpacing: 'var(--tracking-tight)', color: 'var(--text-primary)' }}>Routing</h1>
      </div>

      {/* live aggregate split */}
      <Card title="Workload split" subtitle="aggregate counters — not per-query routing"
        right={<Badge tone="neutral">{rec ? num(qTotal) + ' total' : '—'}</Badge>} padding="md">
        {qTotal === 0 ? (
          <div style={{ font: '400 var(--text-sm)/1.4 var(--font-mono)', color: 'var(--text-muted)' }}>no queries yet</div>
        ) : (
          <div style={{ display: 'flex', flexDirection: 'column', gap: 14 }}>
            {/* stacked proportional bar */}
            <div style={{ display: 'flex', height: 14, borderRadius: 'var(--radius-xs)', overflow: 'hidden', border: '1px solid var(--border-hairline)' }}>
              {splits.map((s) => <span key={s.k} style={{ width: s.p + '%', background: s.color }} title={s.label} />)}
            </div>
            <div style={{ display: 'grid', gridTemplateColumns: 'repeat(2, 1fr)', gap: 10 }}>
              {splits.map((s) => (
                <div key={s.k} style={{ display: 'flex', alignItems: 'center', gap: 10 }}>
                  <span style={{ width: 9, height: 9, borderRadius: 9, background: s.color, flex: 'none' }} />
                  <span style={{ font: '400 var(--text-sm)/1 var(--font-sans)', color: 'var(--text-secondary)', flex: 1 }}>{s.label}</span>
                  <span style={{ font: '500 var(--text-sm)/1 var(--font-mono)', color: 'var(--text-primary)' }}>{s.p.toFixed(1)}%</span>
                  <span style={{ font: '400 var(--text-xs)/1 var(--font-mono)', color: 'var(--text-muted)', width: 64, textAlign: 'right' }}>{num(s.v)}</span>
                </div>
              ))}
            </div>
          </div>
        )}
      </Card>

      {/* control-IN routing (6-6/B/M2) — a SEPARATE population from the workload aggregate above.
        * Field-derived, NOT a Capabilities auto-row: routing has no flag bit of its own (the u16
        * flags word is exhausted), so route_inited is the live-vs-gated signal. */}
      <Card title="Control-IN routing" subtitle="routing decisions on the conversation path"
        right={<Badge tone={routeLive ? 'accent' : 'neutral'}>{routeLive ? 'live' : 'not live'}</Badge>} padding="md">
        {!routeLive ? (
          <div style={{ font: '400 var(--text-sm)/1.5 var(--font-sans)', color: 'var(--text-muted)' }}>
            {rec ? 'routing gated off on the box (not yet live) — the router is wired but its flag ships off until the flip.'
                 : '—'}
          </div>
        ) : (
          <div style={{ display: 'flex', flexDirection: 'column', gap: 12 }}>
            <div style={{ display: 'grid', gridTemplateColumns: 'repeat(3, 1fr)', gap: 'var(--space-3)' }}>
              {routeRows.map((r) => (
                <div key={r.k} style={{ border: '1px solid var(--border-hairline)', borderRadius: 'var(--radius-lg)', padding: 'var(--space-4)' }}>
                  <div style={{ font: '500 var(--text-xl)/1 var(--font-mono)', color: 'var(--text-primary)' }}>{num(r.v)}</div>
                  <div style={{ font: '400 var(--text-xs)/1.4 var(--font-sans)', color: 'var(--text-secondary)', marginTop: 6 }}>{r.label}</div>
                </div>
              ))}
            </div>
            <p style={{ margin: 0, font: '400 var(--text-xs)/1.5 var(--font-sans)', color: 'var(--text-muted)', maxWidth: 720 }}>
              Each count is one <strong>routing decision</strong>, recorded when the query was classified. They are
              deliberately <strong>not a breakdown of queries answered</strong> and do not add up to it — a query routed
              to the model can still end up degraded or timed out, so it is counted here but never answered.
            </p>
          </div>
        )}
      </Card>

      <ProcessesCard store={store} />

      {/* roadmap-only specialists — explicitly static, dimmed, no live state */}
      <div style={{ marginTop: 'var(--space-2)' }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: 10, marginBottom: 12 }}>
          <span style={{ font: 'var(--type-eyebrow)', letterSpacing: 'var(--tracking-caps)', textTransform: 'uppercase',
            color: 'var(--text-muted)' }}>Architecture (roadmap)</span>
          <Badge tone="neutral">not live in the deployed build</Badge>
        </div>
        <p style={{ margin: '0 0 14px', font: '400 var(--text-sm)/1.5 var(--font-sans)', color: 'var(--text-muted)', maxWidth: 720 }}>
          The deployed box runs two processes (A rootserver + B inference). The four specialist agents below are a
          design from the Phase-1 multi-agent work — they are <strong>not running</strong> in the deployed build and
          emit no telemetry. Shown for context only.
        </p>
        <div style={{ display: 'grid', gridTemplateColumns: 'repeat(4, 1fr)', gap: 'var(--space-3)', opacity: 0.5 }}>
          {specialists.map((s) => (
            <div key={s.name} style={{ border: '1px dashed var(--border-strong)', borderRadius: 'var(--radius-lg)',
              padding: 'var(--space-4)', background: 'transparent' }}>
              <div style={{ display: 'flex', alignItems: 'center', gap: 8, marginBottom: 8 }}>
                <span style={{ color: 'var(--text-muted)' }}><Icon name={s.icon} size={15} /></span>
                <span style={{ font: 'var(--weight-medium) var(--text-sm)/1 var(--font-mono)', color: 'var(--text-secondary)' }}>{s.name}</span>
              </div>
              <div style={{ font: '400 var(--text-xs)/1.4 var(--font-sans)', color: 'var(--text-muted)' }}>{s.domain}</div>
            </div>
          ))}
        </div>
      </div>
    </div>
  );
}

window.Routing = Routing;
