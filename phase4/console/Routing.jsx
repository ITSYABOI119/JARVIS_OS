/* JARVIS OS — Telemetry Console · Routing view
 * Live route split = aggregate ratio only (no per-query routing).
 * The 4 specialists appear ONLY as a static roadmap diagram — not live in v1.0.
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

      <ProcessesCard store={store} />

      {/* roadmap-only specialists — explicitly static, dimmed, no live state */}
      <div style={{ marginTop: 'var(--space-2)' }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: 10, marginBottom: 12 }}>
          <span style={{ font: 'var(--type-eyebrow)', letterSpacing: 'var(--tracking-caps)', textTransform: 'uppercase',
            color: 'var(--text-muted)' }}>Architecture (roadmap)</span>
          <Badge tone="neutral">not live in v1.0</Badge>
        </div>
        <p style={{ margin: '0 0 14px', font: '400 var(--text-sm)/1.5 var(--font-sans)', color: 'var(--text-muted)', maxWidth: 720 }}>
          The deployed box is single-process (A rootserver + B inference). The four specialist agents below are a
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
