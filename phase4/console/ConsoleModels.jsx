/* JARVIS OS — Telemetry Console · Models
 * One live "Deployed model" card + the real historical bench-off table.
 * No tiers, no swaps — there is one model.
 */

function Models({ store }) {
  const { Card, Badge } = window.JarvisOSDesignSystem_e0065d;
  const { hasFlag } = window.JConsoleHelpers;
  const rec = store.latest;
  const loaded = hasFlag(rec, 'MODEL_LOADED');

  // historical bench-off (FINAL_SCORES.txt) — top 7 of 11 models / 6 families
  const bench = [
    { m: 'gemma-4-E2B', p: '4.65B', q: 8.40, sp: 19.7, drop: 'NO', deployed: true },
    { m: 'mistral-7b-v0.2', p: '7.24B', q: 7.50, sp: 5.5, drop: 'YES' },
    { m: 'gemma-4-E4B', p: '7.52B', q: 7.33, sp: 10.6, drop: 'NO' },
    { m: 'Qwen3.5-9B', p: '8.95B', q: 7.26, sp: 6.6, drop: 'NO' },
    { m: 'Llama-3.2-3B', p: '3.21B', q: 5.76, sp: 16.7, drop: 'YES' },
    { m: 'Llama-3.2-1B', p: '1.24B', q: 5.37, sp: 40.4, drop: 'YES' },
    { m: 'Phi-3-mini', p: '3.82B', q: 5.31, sp: 14.3, drop: 'MAYBE' },
  ];
  const maxQ = 10;

  const stat = (label, value, sub) => (
    <div>
      <div style={{ font: 'var(--type-eyebrow)', letterSpacing: 'var(--tracking-wide)', textTransform: 'uppercase',
        color: 'var(--text-muted)', marginBottom: 6 }}>{label}</div>
      <div style={{ font: 'var(--weight-semibold) var(--text-lg)/1.1 var(--font-mono)', color: 'var(--text-primary)' }}>{value}</div>
      {sub && <div style={{ marginTop: 3, font: '400 var(--text-2xs)/1.3 var(--font-mono)', color: 'var(--text-muted)' }}>{sub}</div>}
    </div>
  );

  return (
    <div style={{ padding: 'var(--space-6)', overflow: 'auto', display: 'flex', flexDirection: 'column', gap: 'var(--space-5)' }}>
      <div>
        <div style={{ font: 'var(--type-eyebrow)', letterSpacing: 'var(--tracking-caps)', textTransform: 'uppercase',
          color: 'var(--text-accent)', marginBottom: 8 }}>One model · no tiers</div>
        <h1 style={{ margin: 0, font: 'var(--weight-semibold) var(--text-2xl)/1.1 var(--font-display)',
          letterSpacing: 'var(--tracking-tight)', color: 'var(--text-primary)' }}>Deployed model</h1>
      </div>

      {/* live deployed-model card */}
      <Card title={rec ? rec.model_name : 'Gemma 4 E2B'} subtitle="resident on the deployed box"
        right={loaded ? <Badge tone="ok" dot>LOADED</Badge> : <Badge tone="warn">{rec ? (rec.model_load_pct || 0) + '%' : '—'}</Badge>}
        padding="md">
        <div style={{ display: 'grid', gridTemplateColumns: 'repeat(4, 1fr)', gap: 'var(--space-5)' }}>
          {stat('Size', rec ? rec.model_size_mb + ' MB' : '2962 MB', '~2.89 GiB')}
          {stat('Compute', rec ? 'CPU · ' + rec.num_nodes + ' cores' : 'CPU · 6 cores', 'NUM_NODES')}
          {stat('Load', rec ? (rec.model_load_pct || 0) + '%' : '—', loaded ? 'MODEL_LOADED' : 'loading')}
          {stat('Framebuffer', rec ? rec.fb_w + '×' + rec.fb_h : '1024×768', rec ? rec.fb_bpp + 'bpp' : '32bpp')}
        </div>
        <div style={{ marginTop: 'var(--space-5)', paddingTop: 'var(--space-4)', borderTop: '1px solid var(--border-hairline)',
          font: '400 var(--text-sm)/1.5 var(--font-mono)', color: 'var(--text-secondary)' }}>
          5.46 tok/s — deployed CPU benchmark (NUM_NODES=6), <span style={{ color: 'var(--text-muted)' }}>not live</span>.
          Differs from the bench-off's 19.7 t/s, which is the <em>native dev engine</em>, not the deployed build.
        </div>
      </Card>

      {/* historical bench-off */}
      <Card title="Quality bench-off" subtitle="historical · Apr 9 2026 · 11 models / 6 families judged, top 7 shown" padding="none">
        <div style={{ padding: 'var(--space-2) var(--space-4) var(--space-4)' }}>
          <div style={{ display: 'grid', gridTemplateColumns: '160px 64px 1fr 110px 70px', gap: 12, alignItems: 'center',
            padding: 'var(--space-3) var(--space-2)', borderBottom: '1px solid var(--border-default)' }}>
            {['Model', 'Params', 'Quality', 'tg128 t/s · 2700X CPU', 'Drop-in'].map((h) => (
              <span key={h} style={{ font: 'var(--type-eyebrow)', letterSpacing: 'var(--tracking-wide)', textTransform: 'uppercase',
                color: 'var(--text-muted)' }}>{h}</span>
            ))}
          </div>
          {bench.map((b, i) => (
            <div key={b.m} style={{ display: 'grid', gridTemplateColumns: '160px 64px 1fr 110px 70px', gap: 12, alignItems: 'center',
              padding: 'var(--space-3) var(--space-2)', borderBottom: i < bench.length - 1 ? '1px solid var(--border-hairline)' : 'none' }}>
              <span style={{ font: 'var(--weight-medium) var(--text-sm)/1 var(--font-mono)', color: b.deployed ? 'var(--text-accent)' : 'var(--text-primary)' }}>
                {b.m}{b.deployed ? ' ·' : ''}
              </span>
              <span style={{ font: '400 var(--text-xs)/1 var(--font-mono)', color: 'var(--text-muted)' }}>{b.p}</span>
              <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
                <div style={{ flex: 1, height: 6, borderRadius: 3, background: 'var(--surface-inset)', overflow: 'hidden' }}>
                  <div style={{ width: `${(b.q / maxQ) * 100}%`, height: '100%', background: b.deployed ? 'var(--accent)' : 'var(--gray-6)', borderRadius: 3 }} />
                </div>
                <span style={{ font: '500 var(--text-xs)/1 var(--font-mono)', color: 'var(--text-secondary)', width: 26 }}>{b.q.toFixed(2)}</span>
              </div>
              <span style={{ font: '400 var(--text-xs)/1 var(--font-mono)', color: 'var(--text-secondary)' }}>{b.sp} t/s</span>
              <Badge tone={b.drop === 'YES' ? 'ok' : b.drop === 'NO' ? 'neutral' : 'warn'}>{b.drop}</Badge>
            </div>
          ))}
          <div style={{ marginTop: 10, font: '400 var(--text-2xs)/1.4 var(--font-mono)', color: 'var(--text-muted)' }}>
            · = deployed model. Bench-off ran on a Ryzen 7 2700X (CPU, native engine). Deployed speed differs — see card above.
          </div>
        </div>
      </Card>
    </div>
  );
}

window.Models = Models;
