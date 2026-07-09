/* JARVIS OS — Telemetry Console · System
 * Real box system telemetry only. Every value here has a live /events source:
 *   - Memory:  RAM available to JARVIS (total_ram_mb), model resident (model_size_mb —
 *              the real lower bound), and the episodic record count (episodic_count,
 *              shown only when the box reports TLM_F_MEMORY). Live heap is NOT
 *              tracked, so there is no used/free figure.
 *   - Inference: a real ACTIVE / IDLE state (infer_active) + a WORKLOAD duty cycle
 *              (infer_duty_pct = inference time / uptime). This is NOT a CPU-load gauge —
 *              the rootserver busy-polls, so a literal load would read ~100% and mean nothing.
 *              Plus the box uptime (uptime_ms — boot-relative, uncalibrated TSC, shown ≈).
 *   - Storage: NVMe namespace size (nvme_total_mb), the model file, and telemetry-log
 *              fullness (log_cursor / cap). No used/free, throughput, or drive-health —
 *              the box does not report them, so they are not shown.
 */

function SystemView({ store }) {
  const { Card, Badge } = window.JarvisOSDesignSystem_e0065d;
  const { num, fmtUptime } = window.JConsoleHelpers;
  const rec = store.latest;

  const has = (v) => rec && v != null;
  const gib = (mb) => (Number(mb) / 1024).toFixed(2) + ' GiB';
  const LOG_CAP = 2700;        // NVME_LOG_MAX_ENTRIES (rolling durable telemetry log)

  const totalRam = rec ? Number(rec.total_ram_mb) || 0 : null;
  const modelMb = rec ? Number(rec.model_size_mb) || 0 : null;
  const nvmeMb = rec ? Number(rec.nvme_total_mb) || 0 : null;
  const inferActive = rec ? !!Number(rec.infer_active) : false;
  const duty = rec ? Math.max(0, Math.min(100, Number(rec.infer_duty_pct) || 0)) : 0;
  const cursor = rec ? Number(rec.log_cursor) || 0 : 0;
  const cores = rec ? Number(rec.num_nodes) || 0 : 0;
  // Episodic store: show the count ONLY when the box reports TLM_F_MEMORY (store up),
  // so a not-ready 0 never reads as "0 records". Real live field, flag-gated.
  const epiReported = !!(rec && rec.flags_list && rec.flags_list.indexOf('MEMORY') >= 0);
  const epiCount = rec ? Number(rec.episodic_count) || 0 : null;
  // Shared context pool (G2): live working-memory counts, flag-gated on TLM_F_CONTEXT.
  const ctxReported = !!(rec && rec.flags_list && rec.flags_list.indexOf('CONTEXT') >= 0);
  const poolEvents = rec ? Number(rec.pool_events) || 0 : null;
  const poolDecisions = rec ? Number(rec.pool_decisions) || 0 : null;
  // Retrieval before inference (G3): hit count + last in-RAM latency (µs), flag-gated on
  // TLM_F_RETRIEVAL (set only once retrieval has fired). Real fields — hit/latency only, never a
  // "memory-helped" claim.
  const retrReported = !!(rec && rec.flags_list && rec.flags_list.indexOf('RETRIEVAL') >= 0);
  const retrHits = rec ? Number(rec.retrieval_hits) || 0 : null;
  const retrLatencyUs = rec ? Number(rec.retrieval_latency_us) || 0 : null;
  // Cache growth (#6): promoted-pattern count, flag-gated on TLM_F_CACHE_GROWTH (set only once a
  // promotion has occurred). Frequency-based + deterministic — the cache learns FREQUENTLY-ASKED
  // queries and serves them fast; it never "understands" them.
  const cgReported = !!(rec && rec.flags_list && rec.flags_list.indexOf('CACHE_GROWTH') >= 0);
  const cgCount = rec ? Number(rec.cache_growth_count) || 0 : null;
  // Semantic memory (#4): distilled-fact count, flag-gated on TLM_F_SEMANTIC. The deterministic
  // distill compacts recurring Q&A into durable facts — observable patterns, NOT stated
  // preferences; it never "knows" anything.
  const semReported = !!(rec && rec.flags_list && rec.flags_list.indexOf('SEMANTIC') >= 0);
  const semCount = rec ? Number(rec.semantic_fact_count) || 0 : null;
  // Self-healing / autonomous actions (Phase 6 K/M3): flag-gated on TLM_F_ACTIONS. restart_count =
  // lifetime PB self-heal restarts; actions_fired = allowlisted actions EXECUTED (SHIELD-gated);
  // actions_blocked = actions REFUSED by the SEPARATE action gate — NOT the passive query-SHIELD path.
  const actReported = !!(rec && rec.flags_list && rec.flags_list.indexOf('ACTIONS') >= 0);
  const restartCount = rec ? Number(rec.restart_count) || 0 : null;
  const actionsFired = rec ? Number(rec.actions_fired) || 0 : null;
  const actionsBlocked = rec ? Number(rec.actions_blocked) || 0 : null;
  // Always-on monitors (Phase 6 6-1/M3): flag-gated on TLM_F_MONITORS. monitors_fired is a
  // NEUTRAL count of debounced monitor NOTIFY events — a MIX of degradation signals (error-rate,
  // self-heal-rate) and benign liveness events (uptime milestones, store wraps). Never a health
  // verdict; every event is SHIELD-assessed and JACT-audited.
  const monReported = !!(rec && rec.flags_list && rec.flags_list.indexOf('MONITORS') >= 0);
  const monFired = rec ? Number(rec.monitors_fired) || 0 : null;
  const MON_EVENT_LABELS = ['none', 'error-rate', 'self-heal-rate', 'store-wrap', 'heartbeat-age', 'uptime-milestone'];
  const monLastEvent = rec ? (MON_EVENT_LABELS[Number(rec.last_monitor_event) || 0] || 'none') : null;

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
          {stat('RAM available to JARVIS', has(totalRam) ? num(totalRam) + ' MB' : '—',
            has(totalRam) ? gib(totalRam) + ' · sum of non-device untypeds' : 'no telemetry yet')}
          {stat('Model resident', has(modelMb) ? num(modelMb) + ' MB' : '—',
            has(modelMb) ? gib(modelMb) + ' in RAM — the real lower bound (live heap not tracked)' : null)}
          {stat('Episodic records', epiReported ? num(epiCount) : '—',
            epiReported ? 'persisted to the NVMe memory region' : 'store not reported')}
          {stat('Context pool', ctxReported ? num(poolDecisions) : '—',
            ctxReported ? num(poolEvents) + ' events · live working memory (decisions tracked)' : 'pool not reported')}
          {stat('Preambles packed', retrReported ? num(retrHits) : '—',
            retrReported ? 'retrieval before inference (non-empty preambles)' : 'retrieval not reported')}
          {stat('Last retrieval', retrReported ? (num(retrLatencyUs) + ' µs') : '—',
            retrReported ? 'in-RAM select + assemble + pack' : 'retrieval not reported')}
          {stat('Patterns promoted', cgReported ? num(cgCount) : '—',
            cgReported ? 'frequent queries promoted into the decision cache' : 'cache growth not reported')}
          {stat('Distilled facts', semReported ? num(semCount) : '—',
            semReported ? 'compacts recurring Q&A into durable facts — observable patterns, not stated preferences'
                        : 'semantic memory not reported')}
          {/* Self-healing / autonomous actions (Phase 6 K/M3) — flag-gated on TLM_F_ACTIONS. */}
          {stat('PB restarts (self-heal)', actReported ? num(restartCount) : '—',
            actReported ? 'lifetime Process-B respawns by the self-heal action' : 'self-healing not reported')}
          {stat('Actions executed', actReported ? num(actionsFired) : '—',
            actReported ? 'allowlisted actions executed through the SHIELD action gate' : 'self-healing not reported')}
          {stat('Actions blocked (gate)', actReported ? num(actionsBlocked) : '—',
            actReported ? 'actions refused by the action gate — not the passive query-SHIELD path' : 'self-healing not reported')}
          {/* Always-on monitors (Phase 6 6-1/M3) — flag-gated on TLM_F_MONITORS; a neutral event count. */}
          {stat('Monitor notifications', monReported ? num(monFired) : '—',
            monReported ? 'events the always-on monitors flagged and audited — a mix of degradation and benign liveness events' : 'monitors not reported')}
          {stat('Last monitor event', monReported ? monLastEvent : '—',
            monReported ? 'the most recent monitor event type' : 'monitors not reported')}
        </div>
        {note('Live heap used/free is not tracked on the box, so it is not shown. The resident model size above is the only real lower bound.')}
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
          {/* Box uptime — the REAL boot-relative uptime_ms wire field. The box has no RTC; the
              clock is an uncalibrated TSC, so the value is approximate (≈) and never wall-clock. */}
          {stat('Uptime', rec ? '≈ ' + fmtUptime(rec.uptime_ms) : '—',
            rec ? 'boot ' + (Number(rec.boot_id) || 0) + ' · boot-relative, TSC-derived (approximate)'
                : 'no telemetry yet')}
        </div>
        {note('The duty cycle is the share of uptime spent inferring — not a CPU-load gauge. The rootserver busy-polls, so a literal load reading would sit near 100% and tell you nothing.')}
      </Card>

      {/* Storage — totals + telemetry-log fullness; no fabricated used/free */}
      <Card title="Storage" subtitle="NVMe totals + durable telemetry log" padding="md">
        <div style={{ display: 'grid', gridTemplateColumns: 'repeat(2, 1fr)', gap: 'var(--space-5)' }}>
          {stat('NVMe namespace', has(nvmeMb) ? gib(nvmeMb) : '—',
            has(nvmeMb) ? num(nvmeMb) + ' MB · total device' : 'no telemetry yet')}
          {stat('Model file', has(modelMb) ? num(modelMb) + ' MB' : '—',
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
