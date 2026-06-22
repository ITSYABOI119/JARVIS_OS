/* JARVIS OS — Remote Telemetry Console · telemetry layer
 * ---------------------------------------------------------------------------
 * Read-only. Connects to the receiver's /events SSE stream. Each message is one
 * JSON record per ~1 Hz packet, shape = telemetry_receiver.py packet_to_record:
 *   recv_ts, version, kind, kind_name, flags, flags_list, boot_id, seq,
 *   uptime_ms, q_total, q_hits, q_infer, q_heartbeat, q_shield, q_errors,
 *   num_nodes, model_load_pct, fb_w, fb_h, fb_bpp, selftest_score,
 *   model_size_mb, total_ram_mb, infer_gen_tokens, model_name, last_text, crc_ok
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
    rateBuf: [],                    // [{t, q_total}] rolling buffer for queries/sec
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
      live: state.connState === 'live',
    };
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
  const API = {
    subscribe(fn) { listeners.add(fn); fn(snapshot()); return () => listeners.delete(fn); },
    getState: snapshot,
    queriesPerSec,
    connect(url) { startEventSource(url || '/events'); },
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
      try { ingest(JSON.parse(ev.data)); } catch (_) { /* ignore non-JSON keepalive */ }
    };
    es.onerror = () => {
      clearTimeout(failTimer);
      if (!opened) { es.close(); startSimulator(); }      // no server => preview sim
      else { state.connState = 'disconnected'; emit(); }   // stream dropped
    };
  }

  // ---- box-free replay simulator (SAME record shape; clearly labelled) -----
  function startSimulator() {
    state.simulated = true;
    state.connState = 'connecting';
    emit();

    const BOOT_ID = 0x4A37;
    let seq = 0;
    let qTotal = 0;
    const t0 = Date.now();
    // honest split that matches the synthetic workload weighting (70/15/10/5)
    const responses = [
      'the capital of France is Paris.',
      'free space on the root volume is 148 GB.',
      'memory in use is approximately 3 of 32 GB.',
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
      flags.push('SELFTEST_PASS', 'FB_DRAWABLE', 'FB_MAPPED');

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
        version: 1,
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
        total_ram_mb: 0,             // honest: no source
        infer_gen_tokens: 0,         // honest: not measured in deploy
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
