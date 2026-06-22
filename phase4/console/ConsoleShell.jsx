/* JARVIS OS — Telemetry Console · shared helpers + Shell chrome
 * Reuses the design system verbatim (window.JarvisOSDesignSystem_e0065d).
 * Content is rewired to the honest telemetry record shape. Read-only — no
 * composer, no control-in, no fabricated metrics.
 */

/* ---- liveness / formatting helpers -------------------------------------- */
function fmtAgo(recvTs) {
  if (!recvTs) return '—';
  const s = Math.max(0, Math.round(Date.now() / 1000 - recvTs));
  if (s < 1) return 'now';
  if (s < 60) return s + 's ago';
  const m = Math.floor(s / 60);
  return m + 'm ' + (s % 60) + 's ago';
}
function fmtClock(recvTs) {
  if (!recvTs) return '--:--:--';
  const d = new Date(recvTs * 1000);
  const p = (n) => String(n).padStart(2, '0');
  return p(d.getHours()) + ':' + p(d.getMinutes()) + ':' + p(d.getSeconds());
}
function num(n) {
  const v = Number(n) || 0;
  return v.toLocaleString('en-US');
}
function pct(part, total) {
  const t = Number(total) || 0;
  if (t === 0) return null; // cold-start guard — caller shows "no queries yet"
  return (Number(part) || 0) / t * 100;
}
// connection pill descriptor from store state
function connPill(s) {
  switch (s.connState) {
    case 'live':         return { label: 'link OK · live', tone: 'ok', dot: 'ok', pulse: true };
    case 'stale':        return { label: 'stale · no packet', tone: 'warn', dot: 'warn', pulse: false };
    case 'crcfail':      return { label: 'CRC FAIL', tone: 'err', dot: 'err', pulse: false };
    case 'disconnected': return { label: 'disconnected', tone: 'err', dot: 'idle', pulse: false };
    default:             return { label: 'connecting…', tone: 'neutral', dot: 'idle', pulse: false };
  }
}
function hasFlag(rec, f) { return rec && rec.flags_list && rec.flags_list.indexOf(f) >= 0; }

window.JConsoleHelpers = { fmtAgo, fmtClock, num, pct, connPill, hasFlag };

/* ---- Icon (Lucide, isolated from React reconciliation) ------------------ */
function Icon({ name, size = 18, style }) {
  const ref = React.useRef(null);
  React.useEffect(() => {
    const el = ref.current; if (!el) return;
    el.innerHTML = '';
    const i = document.createElement('i');
    i.setAttribute('data-lucide', name);
    el.appendChild(i);
    if (window.lucide) window.lucide.createIcons();
    const svg = el.querySelector('svg');
    if (svg) { svg.setAttribute('width', size); svg.setAttribute('height', size); svg.style.strokeWidth = '1.75'; }
  }, [name, size]);
  return <span ref={ref} style={{ width: size, height: size, display: 'inline-flex', flex: 'none', ...style }} />;
}

function Logo({ size = 26 }) {
  return (
    <div style={{ width: size, height: size, borderRadius: 'var(--radius-sm)', background: 'var(--accent)',
      display: 'flex', alignItems: 'center', justifyContent: 'center', boxShadow: 'var(--glow-accent)', flex: 'none' }}>
      <span style={{ font: 'var(--weight-bold) 15px/1 var(--font-display)', color: '#fff' }}>J</span>
    </div>
  );
}

const RAIL_ITEMS = [
  { id: 'command', icon: 'layout-dashboard', label: 'Command Center' },
  { id: 'last',    icon: 'message-square',   label: 'Last response' },
  { id: 'routing', icon: 'split',            label: 'Routing' },
  { id: 'shield',  icon: 'shield',           label: 'SHIELD' },   // neutral icon — passive
  { id: 'models',  icon: 'cpu',              label: 'Models' },
  { id: 'capabilities', icon: 'list-checks', label: 'Capabilities' }, // auto-populated from flags_list
];

function Rail({ view, setView }) {
  return (
    <nav style={{ width: 'var(--rail-w)', flex: 'none', background: 'var(--bg-canvas)',
      borderRight: '1px solid var(--border-hairline)', display: 'flex', flexDirection: 'column',
      alignItems: 'center', padding: 'var(--space-3) 0', gap: 'var(--space-4)' }}>
      <Logo />
      <div style={{ display: 'flex', flexDirection: 'column', gap: 4, marginTop: 4 }}>
        {RAIL_ITEMS.map((it) => {
          const on = it.id === view;
          return (
            <button key={it.id} title={it.label} onClick={() => setView(it.id)}
              style={{ position: 'relative', width: 38, height: 38, border: 'none', cursor: 'pointer',
                borderRadius: 'var(--radius-md)', display: 'flex', alignItems: 'center', justifyContent: 'center',
                background: on ? 'var(--surface-inset)' : 'transparent',
                color: on ? 'var(--text-accent)' : 'var(--text-muted)',
                transition: 'color var(--dur-fast), background var(--dur-fast)' }}>
              {on && <span style={{ position: 'absolute', left: -10, top: 9, width: 3, height: 20,
                borderRadius: '0 3px 3px 0', background: 'var(--accent)' }} />}
              <Icon name={it.icon} />
            </button>
          );
        })}
      </div>
      <div style={{ marginTop: 'auto', display: 'flex', flexDirection: 'column', gap: 10, alignItems: 'center' }}>
        <div title="Read-only telemetry console" style={{ width: 38, height: 38, display: 'flex',
          alignItems: 'center', justifyContent: 'center', color: 'var(--text-muted)' }}>
          <Icon name="eye" />
        </div>
        <div style={{ width: 30, height: 30, borderRadius: '999px', background: 'var(--surface-inset)',
          border: '1px solid var(--border-strong)', display: 'flex', alignItems: 'center', justifyContent: 'center',
          font: 'var(--weight-semibold) 12px/1 var(--font-mono)', color: 'var(--text-secondary)' }}>RO</div>
      </div>
    </nav>
  );
}

/* ---- Processes sidebar (the TWO real processes) ------------------------- */
function ProcessRow({ name, sub, status }) {
  const { StatusDot } = window.JarvisOSDesignSystem_e0065d;
  return (
    <div style={{ display: 'flex', alignItems: 'center', gap: 10, padding: 'var(--space-2) var(--space-3)',
      borderRadius: 'var(--radius-sm)' }}>
      <StatusDot status={status.dot} pulse={status.pulse} />
      <div style={{ minWidth: 0, flex: 1 }}>
        <div style={{ font: 'var(--weight-medium) var(--text-sm)/1.2 var(--font-mono)', color: 'var(--text-primary)' }}>{name}</div>
        <div style={{ font: '400 var(--text-2xs)/1.4 var(--font-mono)', color: 'var(--text-muted)' }}>{sub}</div>
      </div>
      <span style={{ font: '500 var(--text-2xs)/1 var(--font-mono)', textTransform: 'uppercase', letterSpacing: '0.06em',
        color: status.color }}>{status.label}</span>
    </div>
  );
}

function processStatuses(rec, live) {
  const { hasFlag } = window.JConsoleHelpers;
  const C = { ok: 'var(--status-ok)', warn: 'var(--status-warn)', err: 'var(--status-err)', muted: 'var(--text-muted)' };
  if (!rec) {
    const idle = { dot: 'idle', pulse: false, label: '—', color: C.muted };
    return { procA: idle, procB: idle };
  }
  const err = hasFlag(rec, 'HAS_ERROR') || (Number(rec.q_errors) || 0) > 0;
  const selftest = hasFlag(rec, 'SELFTEST_PASS');
  const loaded = hasFlag(rec, 'MODEL_LOADED');
  const procA = err
    ? { dot: 'err', pulse: false, label: 'error', color: C.err }
    : { dot: 'ok', pulse: live, label: selftest ? 'self-test 5/5*' : 'running', color: C.ok };
  const procB = loaded
    ? { dot: 'ok', pulse: live, label: 'loaded', color: C.ok }
    : { dot: 'warn', pulse: false, label: (rec.model_load_pct || 0) + '%', color: C.warn };
  return { procA, procB };
}

function Sidebar({ view, store }) {
  const { num } = window.JConsoleHelpers;
  const rec = store.latest;
  const { procA, procB } = processStatuses(rec, store.live);
  const isLast = view === 'last';

  return (
    <aside style={{ width: 'var(--sidebar-w)', flex: 'none', background: 'var(--bg-app)',
      borderRight: '1px solid var(--border-hairline)', display: 'flex', flexDirection: 'column' }}>
      <div style={{ padding: 'var(--space-4) var(--space-4) var(--space-3)', display: 'flex',
        alignItems: 'center', justifyContent: 'space-between' }}>
        <span style={{ font: 'var(--type-eyebrow)', letterSpacing: 'var(--tracking-caps)', textTransform: 'uppercase',
          color: 'var(--text-muted)' }}>{isLast ? 'Last response' : 'Processes'}</span>
      </div>

      <div style={{ flex: 1, overflow: 'auto', padding: '0 var(--space-2) var(--space-3)' }}>
        {!isLast && (
          <div style={{ display: 'flex', flexDirection: 'column', gap: 2 }}>
            <ProcessRow name="Process A" sub="rootserver · seL4" status={procA} />
            <ProcessRow name="Process B" sub={rec ? rec.model_name + ' inference' : 'inference'} status={procB} />
            <div style={{ padding: '6px var(--space-3) 0', font: '400 var(--text-2xs)/1.5 var(--font-mono)',
              color: 'var(--text-muted)' }}>*2 of 5 self-tests are vacuous</div>
          </div>
        )}
        {isLast && (
          <div style={{ padding: 'var(--space-2) var(--space-3)', display: 'flex', flexDirection: 'column', gap: 14 }}>
            <div>
              <div style={{ font: '400 var(--text-2xs)/1.5 var(--font-mono)', color: 'var(--text-muted)', marginBottom: 6 }}>
                truncated tail (≤55 chars)
              </div>
              <div style={{ font: '400 var(--text-sm)/1.45 var(--font-sans)', color: rec && rec.last_text ? 'var(--text-secondary)' : 'var(--text-muted)' }}>
                {rec && rec.last_text ? '“…' + rec.last_text + '”' : 'No inference response yet'}
              </div>
            </div>
            <div style={{ borderTop: '1px solid var(--border-hairline)', paddingTop: 12, display: 'flex', flexDirection: 'column', gap: 6 }}>
              {[['q_total', rec && rec.q_total], ['q_infer', rec && rec.q_infer], ['q_hits', rec && rec.q_hits], ['q_errors', rec && rec.q_errors]].map(([k, v]) => (
                <div key={k} style={{ display: 'flex', justifyContent: 'space-between',
                  font: '400 var(--text-xs)/1 var(--font-mono)' }}>
                  <span style={{ color: 'var(--text-muted)' }}>{k}</span>
                  <span style={{ color: k === 'q_errors' && (Number(v) || 0) > 0 ? 'var(--status-err)' : 'var(--text-primary)' }}>{rec ? num(v) : '—'}</span>
                </div>
              ))}
            </div>
          </div>
        )}
      </div>

      <div style={{ padding: 'var(--space-3) var(--space-4)', borderTop: '1px solid var(--border-hairline)',
        display: 'flex', alignItems: 'center', justifyContent: 'space-between' }}>
        <span style={{ font: '400 var(--text-2xs)/1 var(--font-mono)', color: 'var(--text-muted)' }}>
          seL4 x86 · Phase 4 · boot {rec ? rec.boot_id : '—'}
        </span>
      </div>
    </aside>
  );
}

/* ---- Topbar: honest model badge + connection pill ----------------------- */
function Topbar({ title, store }) {
  const { Badge, StatusDot } = window.JarvisOSDesignSystem_e0065d;
  const { connPill, hasFlag } = window.JConsoleHelpers;
  const rec = store.latest;
  const pill = connPill(store);
  const loaded = hasFlag(rec, 'MODEL_LOADED');
  const modelLabel = !rec ? 'no telemetry'
    : loaded ? rec.model_name + ' · LOADED'
    : 'loading ' + (rec.model_load_pct || 0) + '%';

  return (
    <header style={{ height: 'var(--topbar-h)', flex: 'none', display: 'flex', alignItems: 'center',
      gap: 'var(--space-4)', padding: '0 var(--space-5)', borderBottom: '1px solid var(--border-hairline)',
      background: 'var(--bg-app)' }}>
      <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
        <span style={{ font: '400 var(--text-xs)/1 var(--font-mono)', color: 'var(--text-muted)' }}>telemetry</span>
        <Icon name="chevron-right" size={13} style={{ color: 'var(--text-muted)' }} />
        <span style={{ font: 'var(--weight-semibold) var(--text-md)/1 var(--font-sans)', color: 'var(--text-primary)' }}>{title}</span>
      </div>
      <div style={{ flex: 1 }} />
      {store.simulated && (
        <Badge tone="warn">SIMULATED · replay</Badge>
      )}
      <Badge tone={loaded ? 'accent' : 'neutral'} dot={loaded}>{modelLabel}</Badge>
      <Badge tone="neutral">functional-unverified</Badge>
      <span style={{ display: 'inline-flex', alignItems: 'center', gap: 7, height: 24, padding: '0 10px',
        borderRadius: 'var(--radius-full)', border: '1px solid var(--border-default)', background: 'var(--surface-inset)' }}>
        <StatusDot status={pill.dot} pulse={pill.pulse} />
        <span style={{ font: '500 var(--text-2xs)/1 var(--font-mono)', textTransform: 'uppercase', letterSpacing: '0.06em',
          color: pill.tone === 'ok' ? 'var(--status-ok)' : pill.tone === 'warn' ? 'var(--status-warn)' : pill.tone === 'err' ? 'var(--status-err)' : 'var(--text-muted)' }}>
          {pill.label}
        </span>
      </span>
    </header>
  );
}

window.JarvisShell = { Icon, Logo, Rail, Sidebar, Topbar, processStatuses };
