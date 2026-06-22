/* JARVIS OS — Telemetry Console · SHIELD (passive monitoring)
 * The ONLY live SHIELD datum is q_shield = a passive CHECK COUNT.
 * No risk ladder, no recent-actions feed, no blocked-action icons.
 */

function Shield({ store }) {
  const { Card, Badge } = window.JarvisOSDesignSystem_e0065d;
  const { num, fmtAgo } = window.JConsoleHelpers;
  const rec = store.latest;
  const qShield = rec ? Number(rec.q_shield) || 0 : 0;
  const qErr = rec ? Number(rec.q_errors) || 0 : 0;

  const pillars = [
    ['S', 'Staged execution sandbox'],
    ['H', 'Hardware impact analysis'],
    ['I', 'Irreversibility detection'],
    ['E', 'Escalation intelligence'],
    ['L', 'Learning from failures'],
    ['D', 'Deterministic rollback'],
  ];

  return (
    <div style={{ padding: 'var(--space-6)', overflow: 'auto', display: 'flex', flexDirection: 'column', gap: 'var(--space-5)' }}>
      <div style={{ display: 'flex', alignItems: 'flex-end', justifyContent: 'space-between' }}>
        <div>
          <div style={{ font: 'var(--type-eyebrow)', letterSpacing: 'var(--tracking-caps)', textTransform: 'uppercase',
            color: 'var(--text-muted)', marginBottom: 8 }}>Safety framework · passive monitoring (SEC-039)</div>
          <h1 style={{ margin: 0, font: 'var(--weight-semibold) var(--text-2xl)/1.1 var(--font-display)',
            letterSpacing: 'var(--tracking-tight)', color: 'var(--text-primary)' }}>SHIELD</h1>
        </div>
        <Badge tone="neutral">passive · not a blocker</Badge>
      </div>

      {/* the one live card */}
      <Card title="SHIELD checks" subtitle="passive monitoring count — the box always ALLOWs" padding="md">
        <div style={{ display: 'flex', alignItems: 'flex-end', gap: 'var(--space-8)' }}>
          <div>
            <div style={{ display: 'flex', alignItems: 'baseline', gap: 6 }}>
              <span style={{ font: 'var(--weight-semibold) var(--text-4xl)/1 var(--font-display)', color: 'var(--text-primary)' }}>
                {rec ? num(qShield) : '—'}
              </span>
              <span style={{ font: '400 var(--text-sm)/1 var(--font-mono)', color: 'var(--text-muted)' }}>q_shield</span>
            </div>
            <div style={{ marginTop: 6, font: '400 var(--text-2xs)/1.3 var(--font-mono)', color: 'var(--text-muted)' }}>
              standalone check count · not a fraction of q_total
            </div>
          </div>
          <div style={{ borderLeft: '1px solid var(--border-hairline)', paddingLeft: 'var(--space-6)', display: 'flex', gap: 'var(--space-8)' }}>
            <div>
              <div style={{ font: 'var(--type-eyebrow)', letterSpacing: 'var(--tracking-wide)', textTransform: 'uppercase', color: 'var(--text-muted)', marginBottom: 6 }}>Errors</div>
              <div style={{ font: 'var(--weight-semibold) var(--text-xl)/1 var(--font-mono)', color: qErr > 0 ? 'var(--status-err)' : 'var(--text-primary)' }}>{rec ? num(qErr) : '—'}</div>
            </div>
            <div>
              <div style={{ font: 'var(--type-eyebrow)', letterSpacing: 'var(--tracking-wide)', textTransform: 'uppercase', color: 'var(--text-muted)', marginBottom: 6 }}>Last packet</div>
              <div style={{ font: 'var(--weight-semibold) var(--text-xl)/1 var(--font-mono)', color: 'var(--text-secondary)' }}>{rec ? fmtAgo(rec.recv_ts) : '—'}</div>
            </div>
          </div>
        </div>
      </Card>

      {/* honest framework note — NOT a ladder */}
      <Card title="Framework (designed, not enforced)" padding="md">
        <p style={{ margin: '0 0 16px', font: '400 var(--text-sm)/1.55 var(--font-sans)', color: 'var(--text-secondary)', maxWidth: 760 }}>
          The full S.H.I.E.L.D. framework is designed in <code style={{ font: '400 var(--text-xs) var(--font-mono)', color: 'var(--text-accent)' }}>shield_framework.py</code>,
          but it is <strong>not linked into the deployed build</strong> (<code style={{ font: '400 var(--text-xs) var(--font-mono)', color: 'var(--text-accent)' }}>shield.c</code> absent).
          The running box performs passive checks and always returns ALLOW — there is no risk scoring, no gating, and no blocked actions on this system.
        </p>
        <div style={{ display: 'flex', flexWrap: 'wrap', gap: 10 }}>
          {pillars.map(([k, name]) => (
            <div key={k} style={{ display: 'flex', alignItems: 'center', gap: 10, padding: '8px 12px',
              border: '1px dashed var(--border-strong)', borderRadius: 'var(--radius-md)', opacity: 0.6 }}>
              <span style={{ font: 'var(--weight-bold) var(--text-lg)/1 var(--font-display)', color: 'var(--text-muted)' }}>{k}</span>
              <span style={{ font: '400 var(--text-xs)/1.3 var(--font-sans)', color: 'var(--text-muted)' }}>{name}</span>
            </div>
          ))}
        </div>
      </Card>
    </div>
  );
}

window.Shield = Shield;
