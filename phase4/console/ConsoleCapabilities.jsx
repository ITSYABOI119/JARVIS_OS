/* JARVIS OS — Telemetry Console · Capabilities (auto-populated from telemetry)
 * ---------------------------------------------------------------------------
 * UI–feature-parity surface (CLAUDE.md rule). Renders the box's LIVE
 * capabilities by ITERATING the telemetry record's flags_list — a new feature
 * => a new flag => a new row, automatically, with NO code change here. Unknown
 * flags are surfaced raw (tagged "new capability"), never dropped: that is the
 * auto-pull property. Below the flags, a few rows DERIVED from real numeric
 * fields, each shown ONLY when its field is present/real.
 *
 * Honesty: shows ONLY what the box reports (flags_list + real fields). Nothing
 * aspirational is listed as present; a capability the box does not report is
 * simply absent. When store.simulated, the SIMULATED badge is inherited from
 * the shared Topbar (like every other screen).
 */

function Capabilities({ store }) {
  const { Card, Badge, StatusDot } = window.JarvisOSDesignSystem_e0065d;
  const { num, fmtAgo } = window.JConsoleHelpers;
  const rec = store.latest;

  // KNOWN flag -> human label (a declared LABEL map only — NOT the source of
  // truth for which rows appear; unknown flags still render below).
  const FLAG_LABELS = {
    MODEL_LOADED:  { label: 'LLM model loaded (Gemma 4 E2B)', tone: 'ok' },
    SELFTEST_PASS: { label: 'Self-test passed (5/5 — note: 2 vacuous)', tone: 'ok' },
    FB_DRAWABLE:   { label: 'Framebuffer drawable', tone: 'ok' },
    FB_MAPPED:     { label: 'Framebuffer mapped (on-box HUD)', tone: 'ok' },
    HAS_ERROR:     { label: 'Error state', tone: 'err' },
    MEMORY:        { label: 'Episodic memory store', tone: 'ok' },
  };

  // The live capability set IS the record's flags_list — iterate it, never a
  // hardcoded array. (FLAG_LABELS only prettifies known names.)
  const flags = rec && rec.flags_list ? rec.flags_list : [];

  // Rows derived from real numeric fields — pushed ONLY when the field is real.
  const derived = [];
  if (rec) {
    if ((Number(rec.num_nodes) || 0) > 0)
      derived.push({ key: 'smp', label: 'Multi-core inference — ' + rec.num_nodes + ' cores' });
    if ((Number(rec.model_size_mb) || 0) > 0)
      derived.push({ key: 'nvme', label: 'NVMe model load — ' + num(rec.model_size_mb) + ' MB' });
    if (rec.q_hits != null)
      derived.push({ key: 'cache', label: 'Decision cache — ' + num(rec.q_hits) + ' hits' });
    // This very stream proves network telemetry-OUT — but only for REAL box data.
    if (!store.simulated)
      derived.push({ key: 'net', label: 'Network telemetry-OUT — live (this /events stream)' });
  }

  function Row({ tone, label, tag }) {
    return (
      <div style={{ display: 'flex', alignItems: 'center', gap: 12, padding: '10px 12px',
        border: '1px solid var(--border-hairline)', borderRadius: 'var(--radius-md)', background: 'var(--surface-inset)' }}>
        <StatusDot status={tone === 'err' ? 'err' : 'ok'} pulse={store.live} />
        <span style={{ flex: 1, font: '400 var(--text-sm)/1.3 var(--font-sans)',
          color: tone === 'err' ? 'var(--status-err)' : 'var(--text-primary)' }}>{label}</span>
        {tag && <Badge tone="neutral">{tag}</Badge>}
      </div>
    );
  }

  return (
    <div style={{ padding: 'var(--space-6)', overflow: 'auto', display: 'flex', flexDirection: 'column', gap: 'var(--space-5)' }}>
      <div style={{ display: 'flex', alignItems: 'flex-end', justifyContent: 'space-between' }}>
        <div>
          <div style={{ font: 'var(--type-eyebrow)', letterSpacing: 'var(--tracking-caps)', textTransform: 'uppercase',
            color: 'var(--text-muted)', marginBottom: 8 }}>Auto-populated from live telemetry · {rec ? fmtAgo(rec.recv_ts) : '—'}</div>
          <h1 style={{ margin: 0, font: 'var(--weight-semibold) var(--text-2xl)/1.1 var(--font-display)',
            letterSpacing: 'var(--tracking-tight)', color: 'var(--text-primary)' }}>Capabilities</h1>
        </div>
        <Badge tone="neutral">live state only</Badge>
      </div>

      {!rec && (
        <Card title="Capabilities" subtitle="waiting for the first telemetry packet" padding="md">
          <div style={{ font: '400 var(--text-sm)/1.3 var(--font-mono)', color: 'var(--text-muted)' }}>not reported yet</div>
        </Card>
      )}

      {rec && (
        <Card title="Reported capabilities (flags)" subtitle="one row per flag the box reports — a new flag adds a row automatically" padding="md">
          <div style={{ display: 'flex', flexDirection: 'column', gap: 8 }}>
            {flags.length === 0 && (
              <div style={{ font: '400 var(--text-sm)/1.3 var(--font-mono)', color: 'var(--text-muted)' }}>no capability flags reported</div>
            )}
            {flags.map((f) => {
              const known = FLAG_LABELS[f];
              return known
                ? <Row key={f} tone={known.tone} label={known.label} />
                : <Row key={f} tone="ok" label={f} tag="new capability" />;
            })}
          </div>
        </Card>
      )}

      {rec && derived.length > 0 && (
        <Card title="Derived capabilities (from live fields)" subtitle="shown only when the underlying field is present" padding="md">
          <div style={{ display: 'flex', flexDirection: 'column', gap: 8 }}>
            {derived.map((d) => <Row key={d.key} tone="ok" label={d.label} />)}
          </div>
        </Card>
      )}

      <Card title="Not reported" padding="md">
        <p style={{ margin: 0, font: '400 var(--text-sm)/1.55 var(--font-sans)', color: 'var(--text-muted)', maxWidth: 760 }}>
          This surface lists ONLY capabilities the box actively reports (flags + real fields). A capability that isn't reported is
          simply absent — nothing here is aspirational, and nothing is shown without a live source. As the box gains features (new
          flags), they appear above on their own.
        </p>
      </Card>
    </div>
  );
}

window.Capabilities = Capabilities;
