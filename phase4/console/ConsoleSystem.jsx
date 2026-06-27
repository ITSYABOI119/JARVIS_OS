/* JARVIS OS — Telemetry Console · System
 * Real box system telemetry only. Every value here has a live /events source:
 *   - Memory:  RAM available to JARVIS (total_ram_mb), model resident (model_size_mb),
 *              a fixed-floor estimate (model + static pool), and the episodic record
 *              count (episodic_count, shown only when the box reports TLM_F_MEMORY).
 *              Live heap is NOT tracked, so there is no used/free figure — only the
 *              floor, which is real.
 *   - Inference: a real ACTIVE / IDLE state (infer_active) + a WORKLOAD duty cycle
 *              (infer_duty_pct = inference time / uptime). This is NOT a CPU-load gauge —
 *              the rootserver busy-polls, so a literal load would read ~100% and mean nothing.
 *   - Storage: NVMe namespace size (nvme_total_mb), the model file, and telemetry-log
 *              fullness (log_cursor / cap). No used/free, throughput, or drive-health —
 *              the box does not report them, so they are not shown.
 */

function SystemView({ store }) {
  const { Card, Badge } = window.JarvisOSDesignSystem_e0065d;
  const rec = store.latest;

  const has = (v) => rec && v != null;
  const numMb = (v) => (Number(v) || 0).toLocaleString('en-US');
  const gib = (mb) => (Number(mb) / 1024).toFixed(2) + ' GiB';
  const FIXED_POOL_MB = 2;     // static rootserver pool (real, fixed) — part of the floor
  const LOG_CAP = 2700;        // NVME_LOG_MAX_ENTRIES (no-wrap durable telemetry log)

  const totalRam = rec ? Number(rec.total_ram_mb) || 0 : null;
  const modelMb = rec ? Number(rec.model_size_mb) || 0 : null;
  const floorMb = modelMb != null ? modelMb + FIXED_POOL_MB : null;
  const nvmeMb = rec ? Number(rec.nvme_total_mb) || 0 : null;
  const inferActive = rec ? !!Number(rec.infer_active) : false;
  const duty = rec ? Math.max(0, Math.min(100, Number(rec.infer_duty_pct) || 0)) : 0;
  const cursor = rec ? Number(rec.log_cursor) || 0 : 0;
  const cores = rec ? Number(rec.num_nodes) || 0 : 0;
  // Episodic store: show the count ONLY when the box reports TLM_F_MEMORY (store up),
  // so a not-ready 0 never reads as "0 records". Real live field, flag-gated.
  const epiReported = !!(rec && rec.flags_list && rec.flags_list.indexOf('MEMORY') >= 0);
  const epiCount = rec ? Number(rec.episodic_count) || 0 : null;

  const stat = (label, value, sub) => (
    <div>
      <div style={{ font: 'var(--type-eyebrow)', letterSpacing: 'var(--tracking-wide)', textTransform: 'uppercase',
        color: 'var(--text-muted)', marginBottom: 6 }}>{label}</div>
      <div style={{ font: 'var(--weight-semibold) var(--text-lg)/1.1 var(--font-mono)', color: 'var(--text-primary)' }}>{value}</div>
      {sub && <div style={{ marginTop: 3, font: '400 var(--text-2xs)/1.3 var(--font-mono)', color: 'var(--text-muted)' }}>{sub}</div>}
    </div>
  );

  const note = (text) => (
    <div style={{ marginTop: 'var(--space-5)', paddingTop: 'var(--space-4)', borderTop: '1px solid var(--border-hairline)',
      font: '400 var(--text-2xs)/1.5 var(--font-mono)', color: 'var(--text-muted)' }}>{text}</div>
  );

  return (
    <div style={{ padding: 'var(--space-6)', overflow: 'auto', display: 'flex', flexDirection: 'column', gap: 'var(--space-5)' }}>
      <div>
        <div style={{ font: 'var(--type-eyebrow)', letterSpacing: 'var(--tracking-caps)', textTransform: 'uppercase',
          color: 'var(--text-accent)', marginBottom: 8 }}>Real box state · no fabricated values</div>
        <h1 style={{ margin: 0, font: 'var(--weight-semibold) var(--text-2xl)/1.1 var(--font-display)',
          letterSpacing: 'var(--tracking-tight)', color: 'var(--text-primary)' }}>System</h1>
      </div>

      {!rec && (
        <div style={{ font: '400 var(--text-sm)/1.4 var(--font-mono)', color: 'var(--text-muted)' }}>
          Awaiting telemetry…
        </div>
      )}

      {/* Memory — only what the box actually reports */}
      <Card title="Memory" subtitle="from the untyped-memory total + model size" padding="md">
        <div style={{ display: 'grid', gridTemplateColumns: 'repeat(3, 1fr)', gap: 'var(--space-5)' }}>
          {stat('RAM available to JARVIS', has(totalRam) ? numMb(totalRam) + ' MB' : '—',
            has(totalRam) ? gib(totalRam) + ' · sum of non-device untypeds' : 'no telemetry yet')}
          {stat('Model resident', has(modelMb) ? numMb(modelMb) + ' MB' : '—',
            has(modelMb) ? '~2.89 GiB in RAM' : null)}
          {stat('Model + fixed (floor)', has(floorMb) ? numMb(floorMb) + ' MB' : '—',
            'floor — excludes live heap')}
          {stat('Episodic records', epiReported ? numMb(epiCount) : '—',
            epiReported ? 'persisted to the NVMe memory region' : 'store not reported')}
        </div>
        {note('Live heap used/free is not tracked on the box, so it is not shown. The floor above (model + a fixed static pool) is the only real lower bound.')}
      </Card>

      {/* Inference — real state + honest workload duty cycle */}
      <Card title="Inference" subtitle="rootserver workload state, sampled per packet"
        right={<Badge tone={inferActive ? 'accent' : 'neutral'} dot={inferActive}>{inferActive ? 'ACTIVE' : 'IDLE'}</Badge>}
        padding="md">
        <div style={{ display: 'grid', gridTemplateColumns: 'repeat(3, 1fr)', gap: 'var(--space-5)' }}>
          {stat('State', inferActive ? 'inferring' : 'idle / polling',
            rec ? 'infer_active' : 'no telemetry yet')}
          {stat('Workload duty cycle', rec ? duty + '%' : '—', 'inference time ÷ uptime')}
          {stat('Compute', cores ? cores + ' cores' : '—', 'CPU · NUM_NODES')}
        </div>
        {note('The duty cycle is the share of uptime spent inferring — not a CPU-load gauge. The rootserver busy-polls, so a literal load reading would sit near 100% and tell you nothing.')}
      </Card>

      {/* Storage — totals + telemetry-log fullness; no fabricated used/free */}
      <Card title="Storage" subtitle="NVMe totals + durable telemetry log" padding="md">
        <div style={{ display: 'grid', gridTemplateColumns: 'repeat(2, 1fr)', gap: 'var(--space-5)' }}>
          {stat('NVMe namespace', has(nvmeMb) ? gib(nvmeMb) : '—',
            has(nvmeMb) ? numMb(nvmeMb) + ' MB · total device' : 'no telemetry yet')}
          {stat('Model file', has(modelMb) ? numMb(modelMb) + ' MB' : '—',
            'GEMMA2B.GUF on JARVIS_DATA @ LBA 32768')}
        </div>
        <div style={{ marginTop: 'var(--space-5)' }}>
          <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', marginBottom: 6 }}>
            <span style={{ font: 'var(--type-eyebrow)', letterSpacing: 'var(--tracking-wide)', textTransform: 'uppercase',
              color: 'var(--text-muted)' }}>Telemetry log</span>
            <span style={{ font: '400 var(--text-xs)/1 var(--font-mono)', color: 'var(--text-secondary)' }}>
              {rec ? cursor.toLocaleString('en-US') + ' / ' + LOG_CAP.toLocaleString('en-US') + ' entries · rolling (keeps latest)' : '—'}
            </span>
          </div>
          <div style={{ height: 8, background: 'var(--surface-inset)', border: '1px solid var(--border-strong)',
            borderRadius: 'var(--radius-full)', overflow: 'hidden' }}>
            <div style={{ width: Math.max(0, Math.min(100, (cursor / LOG_CAP) * 100)) + '%', height: '100%',
              background: 'var(--accent)' }} />
          </div>
        </div>
        {note('Used/free, throughput, and drive-health are not reported by the box, so they are not shown. The telemetry log is a rolling 2700-entry buffer — it keeps the most recent entries (no longer stops at the cap).')}
      </Card>
    </div>
  );
}

window.SystemView = SystemView;
