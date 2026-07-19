/* JARVIS OS — Remote Telemetry Console · telemetry layer
 * ---------------------------------------------------------------------------
 * Read-only. Connects to the receiver's /events SSE stream. Each message is one
 * JSON record per ~1 Hz packet, shape = telemetry_receiver.py packet_to_record:
 *   recv_ts, version, kind, kind_name, flags, flags_list, boot_id, seq,
 *   uptime_ms, infer_active, infer_duty_pct, q_total, q_hits, q_infer,
 *   q_heartbeat, q_shield, q_errors, num_nodes, model_load_pct, fb_w, fb_h,
 *   fb_bpp, selftest_score, model_size_mb, total_ram_mb, nvme_total_mb,
 *   episodic_count, pool_events, pool_decisions, retrieval_hits, retrieval_latency_us,
 *   cache_growth_count, infer_last_tok_x100,
 *   shield_learn_keys, shield_learn_max_risk_x100 (v5 — monitor-only learned-risk fields),
 *   semantic_fact_count (v6 — distilled observable-pattern facts, never "knows preferences"),
 *   restart_count / actions_fired / actions_blocked (v7 — self-heal/action-gate activity),
 *   monitors_fired / last_monitor_event (v8 — always-on-monitor NOTIFY activity, neutral),
 *   wakes_fired / last_wake_event (v9 — event-triggered CONSULT activity, never cognition),
 *   behaviors_fired / behaviors_mask / last_behavior (v10 — registry INFORM activity; a consult
 *   bumps both wakes_fired and behaviors_fired by design — two views of one event, never summed),
 *   log_cursor, infer_gen_tokens, model_name, last_text, crc_ok
 *
 * The SAME stream also carries control-IN replies (6-5/M3-4b), tagged with the
 * STRING kind 'control_reply' — {verdict, verdict_name, seq, text, crc_ok,
 * recv_ts}. They are routed to a separate bounded ring (state.replies) and never
 * through ingest(): a reply is not a telemetry packet and must not become
 * state.latest or disturb connState / seq-gap accounting.
 *
 * Liveness is genuine: a record is "live" only while a fresh CRC-valid packet
 * arrived within STALE_MS. seq gaps => dropped packets. No fabricated fields.
 *
 * Box-free preview: if /events can't be reached, a clearly-labelled SIMULATED
 * producer emits records in the SAME shape (never passed off as live box data).
 */
(function () {
  const STALE_MS = 2800;            // no fresh CRC-valid packet within this => stale
  const MAX_RECORDS = 60;           // activity feed ring
  const RATE_BUF = 30;              // rolling buffer length for the q/s sparkline
  const MAX_REPLIES = 30;           // bounded control-IN reply ring

  const listeners = new Set();
  const state = {
    connState: 'connecting',        // connecting | live | stale | crcfail | disconnected
    simulated: false,               // true when the built-in replay sim is the source
    latest: null,                   // last CRC-valid record (for display)
    lastAny: null,                  // last record regardless of crc (to surface CRC FAIL)
    records: [],                    // recent records (newest first) for the activity feed
    droppedPackets: 0,              // cumulative seq-gap total
    lastSeq: null,
    lastArrival: 0,                 // client-clock ms of last fresh CRC-valid arrival
    rateBuf: [],                    // [{t, q}] rolling buffer for queries/sec
    replies: [],                    // bounded control-IN reply ring (oldest -> newest)
  };

  function emit() { listeners.forEach((fn) => fn(snapshot())); }
  function snapshot() {
    return {
      connState: state.connState,
      simulated: state.simulated,
      latest: state.latest,
      lastAny: state.lastAny,
      records: state.records.slice(),
      droppedPackets: state.droppedPackets,
      rateSamples: state.rateBuf.slice(),
      replies: state.replies.slice(),
      live: state.connState === 'live',
    };
  }

  /* ---- control-IN replies (6-5/M3-4b) -------------------------------------
   * A box->console JRPL reply arrives on the SAME SSE stream, tagged with the
   * STRING kind 'control_reply' (telemetry packets carry an INTEGER kind). It is
   * NOT a telemetry packet: it must never become state.latest, never touch
   * connState/lastArrival, and never enter the seq-gap accounting — its seq is
   * the control-IN request sequence, an unrelated counter from the packet seq.
   * So it lands here, in its own bounded ring, and nowhere else.
   */
  function ingestReply(rec) {
    state.replies.push(rec);
    if (state.replies.length > MAX_REPLIES) state.replies.shift();
    emit();
  }

  function ingest(rec) {
    state.lastAny = rec;

    // CRC gate — a corrupt packet must never look healthy.
    if (!rec.crc_ok) {
      state.connState = 'crcfail';
      pushRecord(rec);
      emit();
      return;
    }

    // seq monotonicity => dropped-packet accounting (same boot only).
    if (state.latest && rec.boot_id === state.latest.boot_id && state.lastSeq != null) {
      const gap = rec.seq - state.lastSeq - 1;
      if (gap > 0) state.droppedPackets += gap;
    } else if (state.latest && rec.boot_id !== state.latest.boot_id) {
      state.droppedPackets = 0; // new boot — reset
    }
    state.lastSeq = rec.seq;
    state.latest = rec;
    state.lastArrival = Date.now();
    state.connState = 'live';

    // rolling buffer for the ONE allowed sparkline (queries/sec from q_total delta)
    state.rateBuf.push({ t: rec.recv_ts || Date.now() / 1000, q: Number(rec.q_total) || 0 });
    if (state.rateBuf.length > RATE_BUF) state.rateBuf.shift();

    pushRecord(rec);
    emit();
  }

  function pushRecord(rec) {
    state.records.unshift(rec);
    if (state.records.length > MAX_RECORDS) state.records.pop();
  }

  // freshness watchdog — independent of any packet arriving
  setInterval(() => {
    if (state.connState === 'live' && Date.now() - state.lastArrival > STALE_MS) {
      state.connState = 'stale';
      emit();
    }
  }, 500);

  // ---- queries/sec from the rolling buffer (real samples only) -------------
  function queriesPerSec() {
    const b = state.rateBuf;
    if (b.length < 2) return null; // "collecting…"
    const first = b[0], last = b[b.length - 1];
    const dt = last.t - first.t;
    if (dt <= 0) return null;
    return Math.max(0, (last.q - first.q) / dt);
  }

  // ---- public API ----------------------------------------------------------
  let connectedOnce = false;        // connect()/startSimulator() are one-shot — a double
  let simStarted = false;           // call must never stack a second stream/1 Hz loop
  const API = {
    subscribe(fn) { listeners.add(fn); fn(snapshot()); return () => listeners.delete(fn); },
    getState: snapshot,
    queriesPerSec,
    connect(url) { if (connectedOnce) return; connectedOnce = true; startEventSource(url || '/events'); },
    startSimulator,
  };

  function startEventSource(url) {
    let opened = false;
    let es;
    try { es = new EventSource(url); }
    catch (e) { startSimulator(); return; }

    const failTimer = setTimeout(() => { if (!opened) { es.close(); startSimulator(); } }, 1500);

    es.onopen = () => { opened = true; clearTimeout(failTimer); state.simulated = false; };
    es.onmessage = (ev) => {
      try {
        const rec = JSON.parse(ev.data);
        // Discriminator: the reply record's kind is the STRING 'control_reply';
        // every telemetry packet's kind is an integer. Route, never merge.
        if (rec && rec.kind === 'control_reply') ingestReply(rec);
        else ingest(rec);
      } catch (_) { /* ignore non-JSON keepalive */ }
    };
    es.onerror = () => {
      clearTimeout(failTimer);
      if (!opened) { es.close(); startSimulator(); }      // no server => preview sim
      else { state.connState = 'disconnected'; emit(); }   // stream dropped
    };
  }

  // ---- box-free replay simulator (SAME record shape; clearly labelled) -----
  function startSimulator() {
    if (simStarted) return;          // guard: overlapping fallback paths must not double-tick
    simStarted = true;
    state.simulated = true;
    state.connState = 'connecting';
    emit();

    const BOOT_ID = 0x4A37;
    let seq = 0;
    let qTotal = 0;
    const t0 = Date.now();
    // preview split = the box's 70/15/10/5 dispatch weighting (badged SIMULATED);
    // the LIVE box is far more cache-heavy — promoted patterns serve most repeats.
    // Canned texts are model-utterance-shaped and deliberately NOT system-stat-shaped
    // (a fake "free space is N GB" line could be mistaken for storage telemetry).
    const responses = [
      'the capital of France is Paris.',
      'a microkernel keeps drivers out of the kernel.',
      'seL4 IPC uses capability-addressed endpoints.',
      'the current kernel is seL4 on x86-64.',
      'no errors recorded in the last interval.',
    ];
    let ri = 0;
    let lastText = '';

    function makeRecord() {
      seq += 1;
      const uptimeMs = Date.now() - t0;
      // first 4 packets: model loading (no MODEL_LOADED flag) to exercise the gated badge
      const loading = seq <= 4;
      const loadPct = loading ? Math.min(100, 35 + seq * 18) : 100;
      const flags = [];
      if (!loading) flags.push('MODEL_LOADED');
      flags.push('SELFTEST_PASS', 'FB_DRAWABLE', 'FB_MAPPED', 'MEMORY', 'CONTEXT');
      if (!loading) flags.push('RETRIEVAL');  // retrieval fires once the box is serving queries
      if (!loading) flags.push('CACHE_GROWTH');  // preview: promotions occur once queries repeat (badged SIMULATED)
      // ACTIONS + MONITORS + WAKE are default-ON in the deploy (Phase 6 K/M4 + 6-1 + 6-2) — the preview
      // mirrors a healthy live box: flags set, counters honest-0 (no faults, no crossings yet).
      if (!loading) flags.push('ACTIONS', 'MONITORS', 'WAKE');
      // CONTROL_IN is deliberately NOT pushed: the deployed box runs the channel gated
      // off, so the preview must show the Control-IN composer DISABLED, like the box.

      let kind = 1, kindName = 'STATS';
      if (!loading) {
        const roll = seq % 7;
        if (roll === 0) { kind = 2; kindName = 'INFER'; ri = (ri + 1) % responses.length; lastText = responses[ri]; }
        else if (roll === 3) { kind = 3; kindName = 'STATE'; }
      }

      // grow counters honestly once loaded
      if (!loading) {
        const burst = 5 + (seq % 4);
        qTotal += burst;
      }
      const qHits = Math.round(qTotal * 0.70);
      const qInfer = Math.round(qTotal * 0.15);
      const qHb = Math.round(qTotal * 0.10);
      const qShield = Math.round(qTotal * 0.05);
      const qErrors = 0; // healthy box; UI handles >0 from real boxes

      return {
        recv_ts: Date.now() / 1000,
        version: 10,
        kind, kind_name: kindName,
        flags: 0, flags_list: flags,
        boot_id: BOOT_ID,
        seq,
        uptime_ms: uptimeMs,
        q_total: qTotal, q_hits: qHits, q_infer: qInfer,
        q_heartbeat: qHb, q_shield: qShield, q_errors: qErrors,
        num_nodes: 6,
        model_load_pct: loadPct,
        fb_w: 1024, fb_h: 768, fb_bpp: 32,
        selftest_score: 5,
        model_size_mb: 2962,
        total_ram_mb: 30000,         // preview value (badged SIMULATED) — real source on the box
        nvme_total_mb: 1953892,      // preview value (badged SIMULATED)
        log_cursor: seq,             // preview value (badged SIMULATED)
        episodic_count: loading ? 0 : Math.max(0, seq - 4),  // preview value (badged SIMULATED)
        pool_events: loading ? 0 : (qHits + qInfer),         // preview value (badged SIMULATED)
        pool_decisions: loading ? 0 : (qHits + qInfer),      // preview value (badged SIMULATED)
        retrieval_hits: loading ? 0 : Math.max(0, qInfer),   // preview value (badged SIMULATED)
        cache_growth_count: loading ? 0 : 6,                 // preview value (badged SIMULATED)
        retrieval_latency_us: loading ? 0 : 35,              // preview value (badged SIMULATED)
        shield_learn_keys: 0,                // honest 0: no failures in the preview (no SHIELD_LEARN flag -> row shows '—')
        shield_learn_max_risk_x100: 0,
        semantic_fact_count: 0,              // honest 0: gated-off in deploy (no SEMANTIC flag -> stat shows '—')
        restart_count: 0,                    // ACTIONS is default-ON in deploy — healthy preview box: 0 restarts/blocks
        actions_fired: 0,
        actions_blocked: 0,
        monitors_fired: 0,                   // MONITORS is default-ON in deploy — healthy preview box: no crossings yet
        last_monitor_event: 0,
        wakes_fired: 0,                      // WAKE is default-ON in deploy (6-2 flip) — healthy preview box: no degradation crossings, 0 consults
        last_wake_event: 0,
        behaviors_fired: 0,                  // PROACTIVE is gated OFF pre-flip — no PROACTIVE flag in the sim, so the card previews '—'
        behaviors_mask: 0,
        last_behavior: 0,
        infer_active: kind === 2 ? 1 : 0,
        infer_duty_pct: loading ? 0 : 12,  // preview workload duty cycle (badged SIMULATED)
        infer_gen_tokens: loading ? 0 : 50,                  // preview value (badged SIMULATED) — REAL on the box since v4
        infer_last_tok_x100: loading ? 0 : 512 + (seq % 40),  // preview value (badged SIMULATED) — REAL RDTSC-measured on the box
        model_name: 'Gemma 4 E2B',
        last_text: lastText,
        crc_ok: true,
      };
    }

    // first record promptly, then ~1 Hz
    ingest(makeRecord());
    setInterval(() => ingest(makeRecord()), 1000);
  }

  window.JarvisTelemetry = API;
})();
