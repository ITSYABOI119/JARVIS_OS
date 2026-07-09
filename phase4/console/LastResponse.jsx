/* JARVIS OS — Telemetry Console · Last Response (read-only) */

function LastResponse({ store }) {
  const { Badge, StatusDot } = window.JarvisOSDesignSystem_e0065d;
  const { num, fmtAgo } = window.JConsoleHelpers;
  const rec = store.latest;
  // '-' is the box's boot placeholder (g_fb_last_resp init) carried in EVERY packet
  // kind until the first inference — never render it as a real response.
  const hasText = rec && rec.last_text && rec.last_text !== '-';

  return (
    <div style={{ display: 'flex', flexDirection: 'column', height: '100%' }}>
      {/* header strip */}
      <div style={{ flex: 'none', padding: 'var(--space-4) var(--space-6)', borderBottom: '1px solid var(--border-hairline)',
        display: 'flex', alignItems: 'center', gap: 14 }}>
        <div style={{ width: 30, height: 30, flex: 'none', borderRadius: 'var(--radius-sm)', background: 'var(--accent)',
          display: 'flex', alignItems: 'center', justifyContent: 'center', boxShadow: 'var(--glow-accent)' }}>
          <span style={{ font: 'var(--weight-bold) 13px/1 var(--font-display)', color: '#fff' }}>J</span>
        </div>
        <div style={{ flex: 1 }}>
          <div style={{ font: 'var(--weight-semibold) var(--text-md)/1.1 var(--font-sans)', color: 'var(--text-primary)' }}>
            {rec ? rec.model_name : 'JARVIS'}
          </div>
          <div style={{ display: 'flex', alignItems: 'center', gap: 8, marginTop: 3 }}>
            <StatusDot status={store.live ? 'live' : 'idle'} pulse={store.live} label={store.live ? 'live' : store.connState} />
          </div>
        </div>
        <Badge tone="neutral">SHIELD: passive · {rec ? num(rec.q_shield) : '—'} checks</Badge>
      </div>

      {/* body */}
      <div style={{ flex: 1, overflow: 'auto', padding: 'var(--space-8) 0' }}>
        <div style={{ maxWidth: 720, margin: '0 auto', padding: '0 var(--space-6)' }}>
          {hasText ? (
            <div style={{ display: 'flex', gap: 14 }}>
              <div style={{ width: 30, height: 30, flex: 'none', borderRadius: 'var(--radius-sm)', background: 'var(--accent)',
                display: 'flex', alignItems: 'center', justifyContent: 'center', boxShadow: 'var(--glow-accent)' }}>
                <span style={{ font: 'var(--weight-bold) 13px/1 var(--font-display)', color: '#fff' }}>J</span>
              </div>
              <div style={{ flex: 1 }}>
                <div style={{ font: '400 var(--text-lg)/1.55 var(--font-sans)', color: 'var(--text-primary)' }}>
                  {rec.last_text}…
                </div>
                <div style={{ marginTop: 10, display: 'flex', gap: 14, font: '400 var(--text-2xs)/1 var(--font-mono)', color: 'var(--text-muted)' }}>
                  {/* the wire carries the response HEAD (first ≤47 chars, main_x86.c cap) — the
                      value is latched into every packet, so freshness = packet arrival, not
                      response age */}
                  <span>response head (first ≤47 chars)</span>
                  <span>· as of the last packet, {fmtAgo(rec.recv_ts)}</span>
                </div>
              </div>
            </div>
          ) : (
            <div style={{ textAlign: 'center', padding: 'var(--space-12) 0', color: 'var(--text-muted)' }}>
              <div style={{ font: '400 var(--text-lg)/1.4 var(--font-sans)' }}>No inference response yet</div>
              <div style={{ marginTop: 8, font: '400 var(--text-sm)/1.4 var(--font-mono)' }}>
                last_text carries the boot placeholder until the box's first inference
              </div>
            </div>
          )}

          {/* honest measurement note — v4: tokens + decode speed are REAL (RDTSC-measured in PB,
              latched from the last inference; 0/'none yet' until a boot's first inference) */}
          {rec && (
            <div style={{ marginTop: 'var(--space-8)', display: 'flex', gap: 18, flexWrap: 'wrap',
              padding: 'var(--space-4)', border: '1px solid var(--border-hairline)', borderRadius: 'var(--radius-md)',
              background: 'var(--surface-card)' }}>
              {[['per-inference tokens', (Number(rec.infer_gen_tokens) || 0) > 0
                  ? `${rec.infer_gen_tokens} — last inference, measured`
                  : 'none yet this boot (measured from the first inference)'],
                ['RAM available', (Number(rec.total_ram_mb) || 0) > 0
                  ? `${rec.total_ram_mb} MB — live (sum of non-device untypeds)`
                  : 'no source reported'],
                ['decode speed', (Number(rec.infer_last_tok_x100) || 0) > 0
                  ? `${(rec.infer_last_tok_x100 / 100).toFixed(2)} tok/s — ${Number(rec.infer_active) === 1
                      ? 'live · last inference' : 'idle (serving from cache) · last inference'} (RDTSC-measured) · 5.46 deployed benchmark (reference)`
                  : 'no inference yet this boot · 5.46 tok/s deployed benchmark (reference)']].map(([k, v]) => (
                <div key={k} style={{ minWidth: 180 }}>
                  <div style={{ font: 'var(--type-eyebrow)', letterSpacing: 'var(--tracking-wide)', textTransform: 'uppercase',
                    color: 'var(--text-muted)', marginBottom: 4 }}>{k}</div>
                  <div style={{ font: '400 var(--text-xs)/1.4 var(--font-mono)', color: 'var(--text-secondary)' }}>{v}</div>
                </div>
              ))}
            </div>
          )}
        </div>
      </div>

      {/* footer — explain why there is no composer */}
      <div style={{ flex: 'none', padding: 'var(--space-4) var(--space-6)', borderTop: '1px solid var(--border-hairline)',
        font: '400 var(--text-xs)/1.5 var(--font-mono)', color: 'var(--text-muted)', textAlign: 'center' }}>
        Read-only telemetry console (telemetry-OUT). Inbound queries are control-IN — a later, hard-gated Phase-6 goal, not live yet.
      </div>
    </div>
  );
}

window.LastResponse = LastResponse;
