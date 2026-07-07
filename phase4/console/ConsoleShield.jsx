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
  // P5 #5/M2: failure-learning MONITOR fields (v5), flag-gated on TLM_F_SHIELD_LEARN —
  // learned-risk counts only, NEVER a block count (the box never blocks; SEC-039).
  const slReported = !!(rec && rec.flags_list && rec.flags_list.indexOf('SHIELD_LEARN') >= 0);
  const slKeys = rec ? Number(rec.shield_learn_keys) || 0 : 0;
  const slMax = rec ? Number(rec.shield_learn_max_risk_x100) || 0 : 0;

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

      {/* P5 #5/M2: failure-learning — MONITOR-ONLY. Real shield_learn_* fields, flag-gated;
        * '—' until the box reports them. On the benign deployed workload (err=0) they honestly
        * read 0 / stay unreported — that IS the correct display. */}
      <Card title="Failure-learning"
        subtitle="learns which actions fail and raises their risk — monitor-only, not a live blocker; enforcement is Phase 6"
        padding="md">
        <div style={{ display: 'flex', gap: 'var(--space-8)' }}>
          <div>
            <div style={{ font: 'var(--type-eyebrow)', letterSpacing: 'var(--tracking-wide)', textTransform: 'uppercase',
              color: 'var(--text-muted)', marginBottom: 6 }}>Actions with learned risk</div>
            <div style={{ font: 'var(--weight-semibold) var(--text-xl)/1 var(--font-mono)', color: 'var(--text-primary)' }}>
              {slReported ? num(slKeys) : '—'}
            </div>
            <div style={{ marginTop: 3, font: '400 var(--text-2xs)/1.3 var(--font-mono)', color: 'var(--text-muted)' }}>
              {slReported ? 'learned from episodic failure records (shield_learn_keys)' : 'not reported — no failures learned'}
            </div>
          </div>
          <div style={{ borderLeft: '1px solid var(--border-hairline)', paddingLeft: 'var(--space-6)' }}>
            <div style={{ font: 'var(--type-eyebrow)', letterSpacing: 'var(--tracking-wide)', textTransform: 'uppercase',
              color: 'var(--text-muted)', marginBottom: 6 }}>Max learned risk</div>
            <div style={{ font: 'var(--weight-semibold) var(--text-xl)/1 var(--font-mono)', color: 'var(--text-primary)' }}>
              {slReported ? '+' + (slMax / 100).toFixed(2) : '—'}
            </div>
            <div style={{ marginTop: 3, font: '400 var(--text-2xs)/1.3 var(--font-mono)', color: 'var(--text-muted)' }}>
              shield_learn_max_risk_x100 / 100 · cap +0.50 · surfaced, never enforced
            </div>
          </div>
        </div>
      </Card>

      {/* honest framework note — NOT a ladder */}
      <Card title="Framework (designed, not enforced)" padding="md">
        <p style={{ margin: '0 0 16px', font: '400 var(--text-sm)/1.55 var(--font-sans)', color: 'var(--text-secondary)', maxWidth: 760 }}>
          The full S.H.I.E.L.D. framework is designed in <code style={{ font: '400 var(--text-xs) var(--font-mono)', color: 'var(--text-accent)' }}>shield_framework.py</code>,
          but it is <strong>not linked into the deployed build</strong> (<code style={{ font: '400 var(--text-xs) var(--font-mono)', color: 'var(--text-accent)' }}>shield.c</code> absent).
          On the <strong>query</strong> path the box performs passive checks and always returns ALLOW — no risk scoring, no gating, no blocked <em>queries</em>.
          (A <em>separate</em> action gate scores and can BLOCK <strong>autonomous actions</strong>; when live, its <code style={{ font: '400 var(--text-xs) var(--font-mono)', color: 'var(--text-accent)' }}>actions_blocked</code> count appears on the System screen — that is a different path from these query checks.)
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
