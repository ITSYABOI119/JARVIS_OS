/* @ds-bundle: {"format":3,"namespace":"JarvisOSDesignSystem_e0065d","components":[{"name":"Badge","sourcePath":"components/core/Badge.jsx"},{"name":"Button","sourcePath":"components/core/Button.jsx"},{"name":"Card","sourcePath":"components/core/Card.jsx"},{"name":"Input","sourcePath":"components/core/Input.jsx"},{"name":"Kbd","sourcePath":"components/core/Kbd.jsx"},{"name":"SegmentedTabs","sourcePath":"components/core/SegmentedTabs.jsx"},{"name":"StatusDot","sourcePath":"components/core/StatusDot.jsx"},{"name":"Switch","sourcePath":"components/core/Switch.jsx"}],"sourceHashes":{"components/core/Badge.jsx":"a682b9275bfd","components/core/Button.jsx":"c092994faa49","components/core/Card.jsx":"4ae66fd4ec67","components/core/Input.jsx":"609d0f12d286","components/core/Kbd.jsx":"abc6a0a4cbdf","components/core/SegmentedTabs.jsx":"74fca9241d35","components/core/StatusDot.jsx":"4b57d876f61d","components/core/Switch.jsx":"9f0d3f83f4a4","ui_kits/jarvis-os/Agents.jsx":"1e1153f79216","ui_kits/jarvis-os/Assistant.jsx":"0d20d677c550","ui_kits/jarvis-os/CommandCenter.jsx":"ac3870f0740e","ui_kits/jarvis-os/Models.jsx":"c37b4da8636e","ui_kits/jarvis-os/Shell.jsx":"1c32907b3c57","ui_kits/jarvis-os/Shield.jsx":"016443f8deb6","ui_kits/telemetry-console/ConsoleCommandCenter.jsx":"b9e73facae2f","ui_kits/telemetry-console/ConsoleModels.jsx":"060a07b49806","ui_kits/telemetry-console/ConsoleShell.jsx":"99fd42cfb1c2","ui_kits/telemetry-console/ConsoleShield.jsx":"825f215f58c0","ui_kits/telemetry-console/LastResponse.jsx":"d24ed52309c0","ui_kits/telemetry-console/Routing.jsx":"003b1036c7be","ui_kits/telemetry-console/telemetry.js":"35148bb88e63"},"inlinedExternals":[],"unexposedExports":[]} */

(() => {

const __ds_ns = (window.JarvisOSDesignSystem_e0065d = window.JarvisOSDesignSystem_e0065d || {});

const __ds_scope = {};

(__ds_ns.__errors = __ds_ns.__errors || []);

// components/core/Badge.jsx
try { (() => {
function _extends() { return _extends = Object.assign ? Object.assign.bind() : function (n) { for (var e = 1; e < arguments.length; e++) { var t = arguments[e]; for (var r in t) ({}).hasOwnProperty.call(t, r) && (n[r] = t[r]); } return n; }, _extends.apply(null, arguments); }
/**
 * Jarvis OS — Badge
 * Compact status/label pill. Tone sets color + tint wash. Optional leading dot.
 */
function Badge({
  tone = 'neutral',
  dot = false,
  mono = true,
  children,
  style,
  ...rest
}) {
  const tones = {
    neutral: {
      fg: 'var(--text-secondary)',
      bg: 'var(--surface-inset)',
      bd: 'var(--border-default)'
    },
    accent: {
      fg: 'var(--blue-400)',
      bg: 'var(--blue-tint)',
      bd: 'transparent'
    },
    ok: {
      fg: 'var(--green-500)',
      bg: 'var(--green-tint)',
      bd: 'transparent'
    },
    warn: {
      fg: 'var(--amber-500)',
      bg: 'var(--amber-tint)',
      bd: 'transparent'
    },
    err: {
      fg: 'var(--red-500)',
      bg: 'var(--red-tint)',
      bd: 'transparent'
    }
  }[tone];
  return /*#__PURE__*/React.createElement("span", _extends({
    style: {
      display: 'inline-flex',
      alignItems: 'center',
      gap: 6,
      height: 20,
      padding: '0 8px',
      font: mono ? 'var(--weight-medium) var(--text-2xs)/1 var(--font-mono)' : 'var(--weight-medium) var(--text-2xs)/1 var(--font-sans)',
      letterSpacing: mono ? 'var(--tracking-wide)' : 'normal',
      textTransform: mono ? 'uppercase' : 'none',
      color: tones.fg,
      background: tones.bg,
      border: '1px solid ' + tones.bd,
      borderRadius: 'var(--radius-full)',
      whiteSpace: 'nowrap',
      ...style
    }
  }, rest), dot && /*#__PURE__*/React.createElement("span", {
    style: {
      width: 6,
      height: 6,
      borderRadius: '999px',
      background: 'currentColor'
    }
  }), children);
}
Object.assign(__ds_scope, { Badge });
})(); } catch (e) { __ds_ns.__errors.push({ path: "components/core/Badge.jsx", error: String((e && e.message) || e) }); }

// components/core/Button.jsx
try { (() => {
function _extends() { return _extends = Object.assign ? Object.assign.bind() : function (n) { for (var e = 1; e < arguments.length; e++) { var t = arguments[e]; for (var r in t) ({}).hasOwnProperty.call(t, r) && (n[r] = t[r]); } return n; }, _extends.apply(null, arguments); }
/**
 * Jarvis OS — Button
 * Variants: primary | secondary | ghost | danger. Sizes: sm | md | lg.
 */
function Button({
  variant = 'primary',
  size = 'md',
  iconLeft = null,
  iconRight = null,
  loading = false,
  disabled = false,
  fullWidth = false,
  type = 'button',
  onClick,
  children,
  style,
  ...rest
}) {
  const sizes = {
    sm: {
      h: 28,
      px: 'var(--space-3)',
      fs: 'var(--text-sm)',
      gap: 6
    },
    md: {
      h: 34,
      px: 'var(--space-4)',
      fs: 'var(--text-md)',
      gap: 8
    },
    lg: {
      h: 42,
      px: 'var(--space-5)',
      fs: 'var(--text-lg)',
      gap: 8
    }
  }[size];
  const variants = {
    primary: {
      bg: 'var(--accent)',
      fg: 'var(--accent-text)',
      bd: 'transparent',
      hov: 'var(--accent-hover)'
    },
    secondary: {
      bg: 'var(--surface-inset)',
      fg: 'var(--text-primary)',
      bd: 'var(--border-strong)',
      hov: 'var(--surface-hover)'
    },
    ghost: {
      bg: 'transparent',
      fg: 'var(--text-secondary)',
      bd: 'transparent',
      hov: 'var(--surface-hover)'
    },
    danger: {
      bg: 'var(--status-err)',
      fg: '#fff',
      bd: 'transparent',
      hov: '#FF6E63'
    }
  }[variant];
  const [hover, setHover] = React.useState(false);
  const isOff = disabled || loading;
  return /*#__PURE__*/React.createElement("button", _extends({
    type: type,
    disabled: isOff,
    onClick: onClick,
    onMouseEnter: () => setHover(true),
    onMouseLeave: () => setHover(false),
    style: {
      display: 'inline-flex',
      alignItems: 'center',
      justifyContent: 'center',
      gap: sizes.gap,
      height: sizes.h,
      padding: `0 ${sizes.px}`,
      width: fullWidth ? '100%' : 'auto',
      font: 'var(--weight-medium) ' + sizes.fs + '/1 var(--font-sans)',
      color: variants.fg,
      background: hover && !isOff ? variants.hov : variants.bg,
      border: '1px solid ' + variants.bd,
      borderRadius: 'var(--radius-sm)',
      cursor: isOff ? 'not-allowed' : 'pointer',
      opacity: isOff ? 0.5 : 1,
      whiteSpace: 'nowrap',
      userSelect: 'none',
      transition: 'background var(--dur-fast) var(--ease-out), transform var(--dur-fast) var(--ease-out)',
      transform: hover && !isOff ? 'translateY(-0.5px)' : 'none',
      ...style
    }
  }, rest), loading ? /*#__PURE__*/React.createElement(Spinner, null) : iconLeft, children && /*#__PURE__*/React.createElement("span", null, children), !loading && iconRight);
}
function Spinner() {
  return /*#__PURE__*/React.createElement("svg", {
    width: "14",
    height: "14",
    viewBox: "0 0 14 14",
    style: {
      animation: 'jos-spin 0.7s linear infinite'
    }
  }, /*#__PURE__*/React.createElement("circle", {
    cx: "7",
    cy: "7",
    r: "5.5",
    fill: "none",
    stroke: "currentColor",
    strokeWidth: "2",
    strokeOpacity: "0.25"
  }), /*#__PURE__*/React.createElement("path", {
    d: "M7 1.5 a5.5 5.5 0 0 1 5.5 5.5",
    fill: "none",
    stroke: "currentColor",
    strokeWidth: "2",
    strokeLinecap: "round"
  }), /*#__PURE__*/React.createElement("style", null, '@keyframes jos-spin{to{transform:rotate(360deg)}}'));
}
Object.assign(__ds_scope, { Button });
})(); } catch (e) { __ds_ns.__errors.push({ path: "components/core/Button.jsx", error: String((e && e.message) || e) }); }

// components/core/Card.jsx
try { (() => {
function _extends() { return _extends = Object.assign ? Object.assign.bind() : function (n) { for (var e = 1; e < arguments.length; e++) { var t = arguments[e]; for (var r in t) ({}).hasOwnProperty.call(t, r) && (n[r] = t[r]); } return n; }, _extends.apply(null, arguments); }
/**
 * Jarvis OS — Card
 * Surface container with optional header (title/subtitle/right slot) and padding.
 */
function Card({
  title = null,
  subtitle = null,
  right = null,
  padding = 'md',
  interactive = false,
  children,
  style,
  ...rest
}) {
  const [hover, setHover] = React.useState(false);
  const pad = padding === 'none' ? 0 : padding === 'sm' ? 'var(--space-3)' : padding === 'lg' ? 'var(--space-6)' : 'var(--space-4)';
  const hasHeader = title || subtitle || right;
  return /*#__PURE__*/React.createElement("div", _extends({
    onMouseEnter: () => interactive && setHover(true),
    onMouseLeave: () => interactive && setHover(false),
    style: {
      background: 'var(--surface-card)',
      border: '1px solid ' + (hover ? 'var(--border-strong)' : 'var(--border-default)'),
      borderRadius: 'var(--radius-lg)',
      boxShadow: hover ? 'var(--shadow-md)' : 'var(--shadow-sm)',
      cursor: interactive ? 'pointer' : 'default',
      transition: 'border-color var(--dur-base), box-shadow var(--dur-base)',
      overflow: 'hidden',
      ...style
    }
  }, rest), hasHeader && /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      alignItems: 'flex-start',
      justifyContent: 'space-between',
      gap: 'var(--space-3)',
      padding: pad,
      borderBottom: children ? '1px solid var(--border-hairline)' : 'none'
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      flexDirection: 'column',
      gap: 2,
      minWidth: 0
    }
  }, title && /*#__PURE__*/React.createElement("span", {
    style: {
      font: 'var(--type-heading)',
      color: 'var(--text-primary)'
    }
  }, title), subtitle && /*#__PURE__*/React.createElement("span", {
    style: {
      font: 'var(--type-mono)',
      fontSize: 'var(--text-xs)',
      color: 'var(--text-muted)'
    }
  }, subtitle)), right && /*#__PURE__*/React.createElement("div", {
    style: {
      flex: 'none'
    }
  }, right)), children && /*#__PURE__*/React.createElement("div", {
    style: {
      padding: pad
    }
  }, children));
}
Object.assign(__ds_scope, { Card });
})(); } catch (e) { __ds_ns.__errors.push({ path: "components/core/Card.jsx", error: String((e && e.message) || e) }); }

// components/core/Input.jsx
try { (() => {
function _extends() { return _extends = Object.assign ? Object.assign.bind() : function (n) { for (var e = 1; e < arguments.length; e++) { var t = arguments[e]; for (var r in t) ({}).hasOwnProperty.call(t, r) && (n[r] = t[r]); } return n; }, _extends.apply(null, arguments); }
/**
 * Jarvis OS — Input
 * Text field with optional leading icon, label, and error state. Mono-friendly.
 */
function Input({
  label = null,
  hint = null,
  error = null,
  iconLeft = null,
  size = 'md',
  mono = false,
  disabled = false,
  value,
  defaultValue,
  placeholder,
  onChange,
  type = 'text',
  style,
  ...rest
}) {
  const [focus, setFocus] = React.useState(false);
  const h = size === 'sm' ? 30 : size === 'lg' ? 42 : 36;
  const fs = size === 'lg' ? 'var(--text-lg)' : 'var(--text-md)';
  const border = error ? 'var(--status-err)' : focus ? 'var(--border-focus)' : 'var(--border-strong)';
  return /*#__PURE__*/React.createElement("label", {
    style: {
      display: 'flex',
      flexDirection: 'column',
      gap: 6,
      width: '100%',
      ...style
    }
  }, label && /*#__PURE__*/React.createElement("span", {
    style: {
      font: 'var(--type-label)',
      color: 'var(--text-secondary)'
    }
  }, label), /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      alignItems: 'center',
      gap: 8,
      height: h,
      padding: '0 var(--space-3)',
      background: disabled ? 'var(--surface-raised)' : 'var(--surface-inset)',
      border: '1px solid ' + border,
      borderRadius: 'var(--radius-sm)',
      boxShadow: focus && !error ? 'var(--ring-focus)' : 'none',
      transition: 'border-color var(--dur-fast), box-shadow var(--dur-fast)',
      opacity: disabled ? 0.55 : 1
    }
  }, iconLeft && /*#__PURE__*/React.createElement("span", {
    style: {
      color: 'var(--text-muted)',
      display: 'flex'
    }
  }, iconLeft), /*#__PURE__*/React.createElement("input", _extends({
    type: type,
    value: value,
    defaultValue: defaultValue,
    placeholder: placeholder,
    disabled: disabled,
    onChange: onChange,
    onFocus: () => setFocus(true),
    onBlur: () => setFocus(false),
    style: {
      flex: 1,
      border: 'none',
      outline: 'none',
      background: 'transparent',
      color: 'var(--text-primary)',
      minWidth: 0,
      font: mono ? 'var(--weight-regular) ' + fs + '/1 var(--font-mono)' : 'var(--weight-regular) ' + fs + '/1 var(--font-sans)',
      letterSpacing: mono ? 'var(--tracking-wide)' : 'normal'
    }
  }, rest))), (hint || error) && /*#__PURE__*/React.createElement("span", {
    style: {
      font: 'var(--type-mono)',
      fontSize: 'var(--text-xs)',
      color: error ? 'var(--status-err)' : 'var(--text-muted)'
    }
  }, error || hint));
}
Object.assign(__ds_scope, { Input });
})(); } catch (e) { __ds_ns.__errors.push({ path: "components/core/Input.jsx", error: String((e && e.message) || e) }); }

// components/core/Kbd.jsx
try { (() => {
function _extends() { return _extends = Object.assign ? Object.assign.bind() : function (n) { for (var e = 1; e < arguments.length; e++) { var t = arguments[e]; for (var r in t) ({}).hasOwnProperty.call(t, r) && (n[r] = t[r]); } return n; }, _extends.apply(null, arguments); }
/**
 * Jarvis OS — Kbd
 * Keyboard key cap. Pass keys as children (e.g. "⌘", "K") or combo via `keys`.
 */
function Kbd({
  keys = null,
  children,
  style,
  ...rest
}) {
  const cap = (k, i) => /*#__PURE__*/React.createElement("kbd", {
    key: i,
    style: {
      display: 'inline-flex',
      alignItems: 'center',
      justifyContent: 'center',
      minWidth: 18,
      height: 20,
      padding: '0 5px',
      font: 'var(--weight-medium) var(--text-2xs)/1 var(--font-mono)',
      color: 'var(--text-secondary)',
      background: 'var(--surface-raised)',
      border: '1px solid var(--border-strong)',
      borderBottomWidth: 2,
      borderRadius: 'var(--radius-xs)',
      boxShadow: '0 1px 0 rgba(0,0,0,0.3)'
    }
  }, k);
  if (Array.isArray(keys)) {
    return /*#__PURE__*/React.createElement("span", _extends({
      style: {
        display: 'inline-flex',
        alignItems: 'center',
        gap: 3,
        ...style
      }
    }, rest), keys.map((k, i) => cap(k, i)));
  }
  return React.cloneElement(cap(children, 0), {
    style: {
      ...cap(children, 0).props.style,
      ...style
    },
    ...rest
  });
}
Object.assign(__ds_scope, { Kbd });
})(); } catch (e) { __ds_ns.__errors.push({ path: "components/core/Kbd.jsx", error: String((e && e.message) || e) }); }

// components/core/SegmentedTabs.jsx
try { (() => {
function _extends() { return _extends = Object.assign ? Object.assign.bind() : function (n) { for (var e = 1; e < arguments.length; e++) { var t = arguments[e]; for (var r in t) ({}).hasOwnProperty.call(t, r) && (n[r] = t[r]); } return n; }, _extends.apply(null, arguments); }
/**
 * Jarvis OS — SegmentedTabs
 * Inset segmented control. items: [{ id, label, icon? }]. Controlled or not.
 */
function SegmentedTabs({
  items = [],
  value,
  defaultValue,
  onChange,
  size = 'md',
  style,
  ...rest
}) {
  const isControlled = value !== undefined;
  const first = defaultValue ?? (items[0] && items[0].id);
  const [internal, setInternal] = React.useState(first);
  const active = isControlled ? value : internal;
  const h = size === 'sm' ? 28 : 34;
  function pick(id) {
    if (!isControlled) setInternal(id);
    onChange && onChange(id);
  }
  return /*#__PURE__*/React.createElement("div", _extends({
    role: "tablist",
    style: {
      display: 'inline-flex',
      gap: 2,
      padding: 3,
      height: h,
      boxSizing: 'border-box',
      background: 'var(--surface-inset)',
      border: '1px solid var(--border-hairline)',
      borderRadius: 'var(--radius-md)',
      ...style
    }
  }, rest), items.map(it => {
    const on = it.id === active;
    return /*#__PURE__*/React.createElement("button", {
      key: it.id,
      role: "tab",
      "aria-selected": on,
      onClick: () => pick(it.id),
      style: {
        display: 'inline-flex',
        alignItems: 'center',
        gap: 6,
        padding: '0 var(--space-3)',
        height: '100%',
        border: 'none',
        borderRadius: 'var(--radius-sm)',
        cursor: 'pointer',
        font: 'var(--weight-medium) var(--text-sm)/1 var(--font-sans)',
        color: on ? 'var(--text-primary)' : 'var(--text-muted)',
        background: on ? 'var(--surface-card)' : 'transparent',
        boxShadow: on ? 'var(--shadow-sm)' : 'none',
        transition: 'color var(--dur-fast), background var(--dur-fast)',
        whiteSpace: 'nowrap'
      }
    }, it.icon && /*#__PURE__*/React.createElement("span", {
      style: {
        display: 'flex'
      }
    }, it.icon), it.label);
  }));
}
Object.assign(__ds_scope, { SegmentedTabs });
})(); } catch (e) { __ds_ns.__errors.push({ path: "components/core/SegmentedTabs.jsx", error: String((e && e.message) || e) }); }

// components/core/StatusDot.jsx
try { (() => {
function _extends() { return _extends = Object.assign ? Object.assign.bind() : function (n) { for (var e = 1; e < arguments.length; e++) { var t = arguments[e]; for (var r in t) ({}).hasOwnProperty.call(t, r) && (n[r] = t[r]); } return n; }, _extends.apply(null, arguments); }
/**
 * Jarvis OS — StatusDot
 * Small connection/health indicator with optional pulse + label.
 */
function StatusDot({
  status = 'ok',
  pulse = false,
  label = null,
  size = 8,
  style,
  ...rest
}) {
  const colors = {
    ok: 'var(--status-ok)',
    warn: 'var(--status-warn)',
    err: 'var(--status-err)',
    live: 'var(--status-live)',
    idle: 'var(--text-muted)'
  };
  const c = colors[status] || colors.idle;
  return /*#__PURE__*/React.createElement("span", _extends({
    style: {
      display: 'inline-flex',
      alignItems: 'center',
      gap: 7,
      ...style
    }
  }, rest), /*#__PURE__*/React.createElement("span", {
    style: {
      position: 'relative',
      width: size,
      height: size,
      flex: 'none'
    }
  }, pulse && /*#__PURE__*/React.createElement("span", {
    style: {
      position: 'absolute',
      inset: 0,
      borderRadius: '999px',
      background: c,
      animation: 'jos-pulse 1.6s var(--ease-out) infinite'
    }
  }), /*#__PURE__*/React.createElement("span", {
    style: {
      position: 'absolute',
      inset: 0,
      borderRadius: '999px',
      background: c,
      boxShadow: '0 0 6px ' + c
    }
  }), /*#__PURE__*/React.createElement("style", null, '@keyframes jos-pulse{0%{transform:scale(1);opacity:0.6}70%{transform:scale(2.6);opacity:0}100%{opacity:0}}')), label && /*#__PURE__*/React.createElement("span", {
    style: {
      font: 'var(--type-mono)',
      fontSize: 'var(--text-xs)',
      color: 'var(--text-secondary)'
    }
  }, label));
}
Object.assign(__ds_scope, { StatusDot });
})(); } catch (e) { __ds_ns.__errors.push({ path: "components/core/StatusDot.jsx", error: String((e && e.message) || e) }); }

// components/core/Switch.jsx
try { (() => {
function _extends() { return _extends = Object.assign ? Object.assign.bind() : function (n) { for (var e = 1; e < arguments.length; e++) { var t = arguments[e]; for (var r in t) ({}).hasOwnProperty.call(t, r) && (n[r] = t[r]); } return n; }, _extends.apply(null, arguments); }
/**
 * Jarvis OS — Switch
 * Controlled or uncontrolled toggle. Accent fill when on.
 */
function Switch({
  checked,
  defaultChecked = false,
  onChange,
  disabled = false,
  size = 'md',
  style,
  ...rest
}) {
  const isControlled = checked !== undefined;
  const [internal, setInternal] = React.useState(defaultChecked);
  const on = isControlled ? checked : internal;
  const dims = size === 'sm' ? {
    w: 30,
    h: 18,
    k: 12
  } : {
    w: 38,
    h: 22,
    k: 16
  };
  function toggle() {
    if (disabled) return;
    if (!isControlled) setInternal(v => !v);
    onChange && onChange(!on);
  }
  return /*#__PURE__*/React.createElement("button", _extends({
    role: "switch",
    "aria-checked": on,
    disabled: disabled,
    onClick: toggle,
    style: {
      position: 'relative',
      width: dims.w,
      height: dims.h,
      flex: 'none',
      padding: 0,
      border: 'none',
      cursor: disabled ? 'not-allowed' : 'pointer',
      borderRadius: 'var(--radius-full)',
      background: on ? 'var(--accent)' : 'var(--surface-inset)',
      boxShadow: on ? 'none' : 'inset 0 0 0 1px var(--border-strong)',
      opacity: disabled ? 0.5 : 1,
      transition: 'background var(--dur-base) var(--ease-out)',
      ...style
    }
  }, rest), /*#__PURE__*/React.createElement("span", {
    style: {
      position: 'absolute',
      top: '50%',
      left: on ? dims.w - dims.k - 3 : 3,
      width: dims.k,
      height: dims.k,
      borderRadius: '999px',
      background: '#fff',
      transform: 'translateY(-50%)',
      boxShadow: '0 1px 2px rgba(0,0,0,0.4)',
      transition: 'left var(--dur-base) var(--ease-out)'
    }
  }));
}
Object.assign(__ds_scope, { Switch });
})(); } catch (e) { __ds_ns.__errors.push({ path: "components/core/Switch.jsx", error: String((e && e.message) || e) }); }

// ui_kits/jarvis-os/Agents.jsx
try { (() => {
/* JARVIS AI-OS UI kit — Agents view
 * The Agent Router + 4 specialist agents. Domains, keywords, actions, priority.
 * Data lifted directly from multi_agent_design.md.
 */

function Agents() {
  const {
    Card,
    Badge,
    StatusDot
  } = window.JarvisOSDesignSystem_e0065d;
  const {
    Icon
  } = window.JarvisShell;
  const agents = [{
    name: 'device-manager',
    icon: 'cpu',
    prio: 'P1',
    status: 'ok',
    domain: 'Hardware, devices, system resources',
    keywords: ['device', 'disk', 'memory', 'cpu', 'thermal', 'battery', 'usb'],
    actions: ['show_disk_space', 'show_memory', 'show_cpu', 'list_devices', 'show_thermal']
  }, {
    name: 'network',
    icon: 'wifi',
    prio: 'P2',
    status: 'ok',
    domain: 'Network operations, connectivity, remote access',
    keywords: ['network', 'ping', 'ssh', 'http', 'ip', 'port', 'dns', 'firewall'],
    actions: ['show_network_status', 'ping_host', 'check_connection', 'show_listening_ports']
  }, {
    name: 'filesystem',
    icon: 'folder-tree',
    prio: 'P3',
    status: 'ok',
    domain: 'File operations, directories, permissions',
    keywords: ['file', 'directory', 'ls', 'cp', 'mv', 'rm', 'find', 'chmod'],
    actions: ['list_directory', 'search_files', 'show_permissions', 'disk_usage']
  }, {
    name: 'user-interaction',
    icon: 'message-square',
    prio: 'P4',
    status: 'idle',
    domain: 'Conversation, help, clarification, general queries',
    keywords: ['help', 'what', 'how', 'explain', 'hello'],
    actions: ['answer_query', 'clarify', 'show_help', 'summarize']
  }];
  return /*#__PURE__*/React.createElement("div", {
    style: {
      padding: 'var(--space-6)',
      overflow: 'auto',
      display: 'flex',
      flexDirection: 'column',
      gap: 'var(--space-5)'
    }
  }, /*#__PURE__*/React.createElement("div", null, /*#__PURE__*/React.createElement("div", {
    style: {
      font: 'var(--type-eyebrow)',
      letterSpacing: 'var(--tracking-caps)',
      textTransform: 'uppercase',
      color: 'var(--text-accent)',
      marginBottom: 8
    }
  }, "Multi-agent architecture \xB7 Week 11"), /*#__PURE__*/React.createElement("h1", {
    style: {
      margin: 0,
      font: 'var(--weight-semibold) var(--text-2xl)/1.1 var(--font-display)',
      letterSpacing: 'var(--tracking-tight)',
      color: 'var(--text-primary)'
    }
  }, "Specialist agents")), /*#__PURE__*/React.createElement(Card, {
    padding: "md"
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      alignItems: 'center',
      gap: 16
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      width: 40,
      height: 40,
      flex: 'none',
      borderRadius: 'var(--radius-md)',
      background: 'var(--blue-tint)',
      display: 'flex',
      alignItems: 'center',
      justifyContent: 'center',
      color: 'var(--text-accent)'
    }
  }, /*#__PURE__*/React.createElement(Icon, {
    name: "git-fork",
    size: 20
  })), /*#__PURE__*/React.createElement("div", {
    style: {
      flex: 1
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      alignItems: 'center',
      gap: 10
    }
  }, /*#__PURE__*/React.createElement("span", {
    style: {
      font: 'var(--weight-semibold) var(--text-lg)/1 var(--font-mono)',
      color: 'var(--text-primary)'
    }
  }, "agent-router"), /*#__PURE__*/React.createElement(StatusDot, {
    status: "live",
    pulse: true,
    label: "routing"
  })), /*#__PURE__*/React.createElement("div", {
    style: {
      marginTop: 4,
      font: '400 var(--text-sm)/1.4 var(--font-sans)',
      color: 'var(--text-secondary)'
    }
  }, "Keyword-based routing \xB7 shared context pool \xB7 conflict resolution with wait-for deadlock detection (5s timeout).")), /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      gap: 8
    }
  }, /*#__PURE__*/React.createElement(Badge, {
    tone: "neutral"
  }, "conflicts 0"), /*#__PURE__*/React.createElement(Badge, {
    tone: "ok",
    dot: true
  }, "no deadlocks")))), /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'grid',
      gridTemplateColumns: '1fr 1fr',
      gap: 'var(--space-4)'
    }
  }, agents.map(a => /*#__PURE__*/React.createElement(Card, {
    key: a.name,
    padding: "md"
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      alignItems: 'flex-start',
      justifyContent: 'space-between',
      marginBottom: 12
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      alignItems: 'center',
      gap: 10
    }
  }, /*#__PURE__*/React.createElement("span", {
    style: {
      width: 30,
      height: 30,
      borderRadius: 'var(--radius-sm)',
      background: 'var(--surface-inset)',
      display: 'flex',
      alignItems: 'center',
      justifyContent: 'center',
      color: 'var(--text-accent)'
    }
  }, /*#__PURE__*/React.createElement(Icon, {
    name: a.icon,
    size: 16
  })), /*#__PURE__*/React.createElement("span", {
    style: {
      font: 'var(--weight-semibold) var(--text-md)/1 var(--font-mono)',
      color: 'var(--text-primary)'
    }
  }, a.name)), /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      alignItems: 'center',
      gap: 8
    }
  }, /*#__PURE__*/React.createElement(Badge, {
    tone: "neutral"
  }, a.prio), /*#__PURE__*/React.createElement(StatusDot, {
    status: a.status
  }))), /*#__PURE__*/React.createElement("div", {
    style: {
      font: '400 var(--text-sm)/1.4 var(--font-sans)',
      color: 'var(--text-secondary)',
      marginBottom: 14
    }
  }, a.domain), /*#__PURE__*/React.createElement("div", {
    style: {
      font: 'var(--type-eyebrow)',
      letterSpacing: 'var(--tracking-wide)',
      textTransform: 'uppercase',
      color: 'var(--text-muted)',
      marginBottom: 8
    }
  }, "Keywords"), /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      flexWrap: 'wrap',
      gap: 6,
      marginBottom: 14
    }
  }, a.keywords.map(k => /*#__PURE__*/React.createElement("span", {
    key: k,
    style: {
      font: '400 var(--text-2xs)/1 var(--font-mono)',
      color: 'var(--text-secondary)',
      padding: '4px 7px',
      borderRadius: 'var(--radius-xs)',
      background: 'var(--surface-inset)',
      border: '1px solid var(--border-hairline)'
    }
  }, k))), /*#__PURE__*/React.createElement("div", {
    style: {
      font: 'var(--type-eyebrow)',
      letterSpacing: 'var(--tracking-wide)',
      textTransform: 'uppercase',
      color: 'var(--text-muted)',
      marginBottom: 8
    }
  }, "Actions"), /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      flexDirection: 'column',
      gap: 4
    }
  }, a.actions.map(ac => /*#__PURE__*/React.createElement("div", {
    key: ac,
    style: {
      display: 'flex',
      alignItems: 'center',
      gap: 8
    }
  }, /*#__PURE__*/React.createElement(Icon, {
    name: "chevron-right",
    size: 11,
    style: {
      color: 'var(--text-muted)'
    }
  }), /*#__PURE__*/React.createElement("span", {
    style: {
      font: '400 var(--text-xs)/1.3 var(--font-mono)',
      color: 'var(--text-accent)'
    }
  }, ac))))))));
}
window.Agents = Agents;
})(); } catch (e) { __ds_ns.__errors.push({ path: "ui_kits/jarvis-os/Agents.jsx", error: String((e && e.message) || e) }); }

// ui_kits/jarvis-os/Assistant.jsx
try { (() => {
/* JARVIS AI-OS UI kit — Assistant (query → router → SHIELD → exec → response)
 * Reflects the real pipeline: AgentRouter routes by keyword to a specialist,
 * SHIELD scores risk + picks an execution mode, the action runs, JARVIS answers.
 */

function Assistant() {
  const {
    Badge,
    Button,
    StatusDot
  } = window.JarvisOSDesignSystem_e0065d;
  const {
    Icon
  } = window.JarvisShell;
  const [messages, setMessages] = React.useState([{
    from: 'user',
    text: 'How much disk space is left?'
  }, {
    from: 'jarvis',
    text: 'Routed to the Device Manager agent. Action cleared by SHIELD and executed — root volume is at 62% (148 GB free of 400 GB).',
    blocks: [{
      kind: 'route',
      value: 'device-manager · matched “disk” · priority P1'
    }, {
      kind: 'shield',
      tone: 'ok',
      value: 'show_disk_space · risk 0.10 SAFE · automatic'
    }, {
      kind: 'exec',
      value: 'df -h /'
    }]
  }]);
  const [draft, setDraft] = React.useState('');
  const scrollRef = React.useRef(null);
  React.useEffect(() => {
    if (scrollRef.current) scrollRef.current.scrollTop = scrollRef.current.scrollHeight;
    if (window.lucide) window.lucide.createIcons();
  }, [messages]);
  function send() {
    const t = draft.trim();
    if (!t) return;
    setMessages(m => [...m, {
      from: 'user',
      text: t
    }]);
    setDraft('');
    setTimeout(() => {
      setMessages(m => [...m, {
        from: 'jarvis',
        text: 'Query received. Routing to the appropriate specialist agent and running SHIELD risk analysis before execution.',
        blocks: [{
          kind: 'route',
          value: 'agent-router · classifying keywords'
        }, {
          kind: 'shield',
          tone: 'ok',
          value: 'pending risk score'
        }]
      }]);
    }, 500);
  }
  const blockMeta = {
    route: {
      icon: 'git-fork',
      label: 'route',
      color: 'var(--text-accent)'
    },
    exec: {
      icon: 'terminal',
      label: 'exec',
      color: 'var(--text-accent)'
    },
    shield: {
      icon: 'shield-check',
      label: 'shield',
      color: 'var(--status-ok)'
    }
  };
  return /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      flexDirection: 'column',
      height: '100%'
    }
  }, /*#__PURE__*/React.createElement("div", {
    ref: scrollRef,
    style: {
      flex: 1,
      overflow: 'auto',
      padding: 'var(--space-6) 0'
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      maxWidth: 760,
      margin: '0 auto',
      padding: '0 var(--space-6)',
      display: 'flex',
      flexDirection: 'column',
      gap: 'var(--space-6)'
    }
  }, messages.map((m, i) => /*#__PURE__*/React.createElement("div", {
    key: i,
    style: {
      display: 'flex',
      gap: 14,
      flexDirection: m.from === 'user' ? 'row-reverse' : 'row'
    }
  }, m.from === 'jarvis' && /*#__PURE__*/React.createElement("div", {
    style: {
      width: 30,
      height: 30,
      flex: 'none',
      borderRadius: 'var(--radius-sm)',
      background: 'var(--accent)',
      display: 'flex',
      alignItems: 'center',
      justifyContent: 'center',
      boxShadow: 'var(--glow-accent)'
    }
  }, /*#__PURE__*/React.createElement("span", {
    style: {
      font: 'var(--weight-bold) 13px/1 var(--font-display)',
      color: '#fff'
    }
  }, "J")), /*#__PURE__*/React.createElement("div", {
    style: {
      maxWidth: m.from === 'user' ? '78%' : '100%',
      display: 'flex',
      flexDirection: 'column',
      gap: 10
    }
  }, m.from === 'jarvis' && m.blocks && /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      flexWrap: 'wrap',
      gap: 8
    }
  }, m.blocks.map((b, j) => {
    const meta = blockMeta[b.kind] || blockMeta.exec;
    const color = b.tone === 'ok' ? 'var(--status-ok)' : meta.color;
    return /*#__PURE__*/React.createElement("div", {
      key: j,
      style: {
        display: 'inline-flex',
        alignItems: 'center',
        gap: 8,
        alignSelf: 'flex-start',
        padding: '5px 10px',
        borderRadius: 'var(--radius-sm)',
        background: 'var(--surface-raised)',
        border: '1px solid var(--border-hairline)'
      }
    }, /*#__PURE__*/React.createElement(Icon, {
      name: meta.icon,
      size: 13,
      style: {
        color
      }
    }), /*#__PURE__*/React.createElement("span", {
      style: {
        font: 'var(--weight-medium) var(--text-2xs)/1 var(--font-mono)',
        textTransform: 'uppercase',
        letterSpacing: 'var(--tracking-wide)',
        color: 'var(--text-muted)'
      }
    }, meta.label), /*#__PURE__*/React.createElement("span", {
      style: {
        font: '400 var(--text-xs)/1 var(--font-mono)',
        color: 'var(--text-secondary)'
      }
    }, b.value));
  })), /*#__PURE__*/React.createElement("div", {
    style: {
      font: '400 var(--text-md)/1.55 var(--font-sans)',
      color: 'var(--text-primary)',
      background: m.from === 'user' ? 'var(--surface-inset)' : 'transparent',
      border: m.from === 'user' ? '1px solid var(--border-default)' : 'none',
      padding: m.from === 'user' ? 'var(--space-3) var(--space-4)' : 0,
      borderRadius: 'var(--radius-lg)'
    }
  }, m.text)))))), /*#__PURE__*/React.createElement("div", {
    style: {
      padding: 'var(--space-4) var(--space-6) var(--space-6)'
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      maxWidth: 760,
      margin: '0 auto'
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      background: 'var(--surface-card)',
      border: '1px solid var(--border-strong)',
      borderRadius: 'var(--radius-lg)',
      boxShadow: 'var(--shadow-md)',
      padding: 'var(--space-3) var(--space-3) var(--space-3) var(--space-4)'
    }
  }, /*#__PURE__*/React.createElement("textarea", {
    value: draft,
    onChange: e => setDraft(e.target.value),
    onKeyDown: e => {
      if (e.key === 'Enter' && !e.shiftKey) {
        e.preventDefault();
        send();
      }
    },
    placeholder: "Ask JARVIS about devices, network, files, or system state\u2026  \u23CE to send",
    rows: 1,
    style: {
      width: '100%',
      resize: 'none',
      border: 'none',
      outline: 'none',
      background: 'transparent',
      color: 'var(--text-primary)',
      font: '400 var(--text-md)/1.5 var(--font-sans)',
      minHeight: 24
    }
  }), /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      alignItems: 'center',
      justifyContent: 'space-between',
      marginTop: 10
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      alignItems: 'center',
      gap: 8
    }
  }, /*#__PURE__*/React.createElement(Badge, {
    tone: "accent",
    dot: true
  }, "gemma-4-e2b"), /*#__PURE__*/React.createElement(StatusDot, {
    status: "ok",
    label: "SHIELD on"
  }), /*#__PURE__*/React.createElement(StatusDot, {
    status: "ok",
    label: "ctx 18%"
  })), /*#__PURE__*/React.createElement(Button, {
    variant: "primary",
    size: "sm",
    iconRight: /*#__PURE__*/React.createElement(Icon, {
      name: "arrow-up",
      size: 15
    }),
    onClick: send
  }, "Send"))))));
}
window.Assistant = Assistant;
})(); } catch (e) { __ds_ns.__errors.push({ path: "ui_kits/jarvis-os/Assistant.jsx", error: String((e && e.message) || e) }); }

// ui_kits/jarvis-os/CommandCenter.jsx
try { (() => {
/* JARVIS AI-OS UI kit — Command Center (system overview)
 * Real metrics from the project: inference latency (558ms GPU target <600),
 * Gemma 4 E2B throughput (19.7 tok/s CPU), memory (<4GB), round-trip (<3s).
 */

function CommandCenter() {
  const {
    Card,
    Badge,
    Button
  } = window.JarvisOSDesignSystem_e0065d;
  const {
    Icon
  } = window.JarvisShell;
  const metrics = [{
    label: 'Inference',
    value: '558',
    unit: 'ms',
    sub: 'target <600 (GPU)',
    tone: 'ok',
    spark: [9, 8, 10, 7, 9, 8, 8]
  }, {
    label: 'Throughput',
    value: '19.7',
    unit: 'tok/s',
    sub: 'gemma-4-e2b · CPU',
    tone: 'accent',
    spark: [12, 14, 13, 16, 18, 17, 20]
  }, {
    label: 'Memory',
    value: '3.4',
    unit: 'GB',
    sub: 'budget <4 GB',
    tone: 'ok',
    spark: [4, 5, 4, 6, 5, 6, 5]
  }, {
    label: 'Round-trip',
    value: '2.1',
    unit: 's',
    sub: 'query → response <3',
    tone: 'warn',
    spark: [6, 7, 6, 9, 12, 10, 11]
  }];
  const activity = [{
    icon: 'git-fork',
    text: 'router → device-manager · “how much disk space is left?”',
    when: '4s',
    tone: 'live'
  }, {
    icon: 'shield-check',
    text: 'SHIELD cleared show_disk_space · risk 0.10 SAFE · automatic',
    when: '4s',
    tone: 'ok'
  }, {
    icon: 'arrow-left-right',
    text: 'IPC ring buffer · 1,284 msgs · 0 drops · 18ms rtt',
    when: '38s',
    tone: 'accent'
  }, {
    icon: 'shield-alert',
    text: 'SHIELD blocked rm -rf /* · risk 0.95 CRITICAL · approval required',
    when: '3m',
    tone: 'err'
  }, {
    icon: 'refresh-cw',
    text: 'model tier swap · llama-3.2-1b (IDLE) → gemma-4-e2b (ACTIVE)',
    when: '6m',
    tone: 'accent'
  }, {
    icon: 'wifi',
    text: 'network agent · ping 8.8.8.8 · 14ms · 0% loss',
    when: '11m',
    tone: 'ok'
  }];
  const toneColor = t => ({
    ok: 'var(--status-ok)',
    warn: 'var(--status-warn)',
    err: 'var(--status-err)',
    accent: 'var(--text-accent)',
    live: 'var(--status-live)',
    idle: 'var(--text-muted)'
  })[t];
  function Spark({
    data,
    color
  }) {
    const max = Math.max(...data);
    return /*#__PURE__*/React.createElement("div", {
      style: {
        display: 'flex',
        alignItems: 'flex-end',
        gap: 3,
        height: 28
      }
    }, data.map((v, i) => /*#__PURE__*/React.createElement("span", {
      key: i,
      style: {
        width: 5,
        height: `${v / max * 100}%`,
        minHeight: 2,
        background: color,
        opacity: 0.35 + i / data.length * 0.65,
        borderRadius: 1
      }
    })));
  }
  return /*#__PURE__*/React.createElement("div", {
    style: {
      padding: 'var(--space-6)',
      overflow: 'auto',
      display: 'flex',
      flexDirection: 'column',
      gap: 'var(--space-5)'
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      alignItems: 'flex-end',
      justifyContent: 'space-between'
    }
  }, /*#__PURE__*/React.createElement("div", null, /*#__PURE__*/React.createElement("div", {
    style: {
      font: 'var(--type-eyebrow)',
      letterSpacing: 'var(--tracking-caps)',
      textTransform: 'uppercase',
      color: 'var(--text-accent)',
      marginBottom: 8
    }
  }, "seL4 \xB7 x86-64 \xB7 all systems nominal"), /*#__PURE__*/React.createElement("h1", {
    style: {
      margin: 0,
      font: 'var(--weight-semibold) var(--text-3xl)/1.1 var(--font-display)',
      letterSpacing: 'var(--tracking-tight)',
      color: 'var(--text-primary)'
    }
  }, "JARVIS online.")), /*#__PURE__*/React.createElement(Button, {
    variant: "primary",
    iconLeft: /*#__PURE__*/React.createElement(Icon, {
      name: "terminal",
      size: 16
    })
  }, "New query")), /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'grid',
      gridTemplateColumns: 'repeat(4, 1fr)',
      gap: 'var(--space-4)'
    }
  }, metrics.map(m => /*#__PURE__*/React.createElement(Card, {
    key: m.label,
    padding: "md"
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      justifyContent: 'space-between',
      alignItems: 'flex-start'
    }
  }, /*#__PURE__*/React.createElement("span", {
    style: {
      font: 'var(--type-eyebrow)',
      letterSpacing: 'var(--tracking-wide)',
      textTransform: 'uppercase',
      color: 'var(--text-muted)'
    }
  }, m.label), /*#__PURE__*/React.createElement(Spark, {
    data: m.spark,
    color: toneColor(m.tone)
  })), /*#__PURE__*/React.createElement("div", {
    style: {
      marginTop: 10,
      display: 'flex',
      alignItems: 'baseline',
      gap: 4
    }
  }, /*#__PURE__*/React.createElement("span", {
    style: {
      font: 'var(--weight-semibold) var(--text-3xl)/1 var(--font-display)',
      color: 'var(--text-primary)'
    }
  }, m.value), /*#__PURE__*/React.createElement("span", {
    style: {
      font: '400 var(--text-sm)/1 var(--font-mono)',
      color: 'var(--text-muted)'
    }
  }, m.unit)), /*#__PURE__*/React.createElement("div", {
    style: {
      marginTop: 6,
      font: '400 var(--text-2xs)/1 var(--font-mono)',
      color: 'var(--text-muted)'
    }
  }, m.sub)))), /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'grid',
      gridTemplateColumns: '1fr 1fr',
      gap: 'var(--space-4)'
    }
  }, /*#__PURE__*/React.createElement(Card, {
    title: "Agent roster",
    subtitle: "router + 4 specialists \xB7 priority device > network > fs > user",
    right: /*#__PURE__*/React.createElement(Badge, {
      tone: "accent"
    }, "5"),
    padding: "none"
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      padding: '4px var(--space-2) var(--space-2)'
    }
  }, [['agent-router', 'live', 'routing', 'ctx pool'], ['device-manager', 'ok', 'idle', 'P1'], ['network', 'ok', 'ping 8.8.8.8', 'P2'], ['filesystem', 'ok', 'idle', 'P3'], ['user-interaction', 'idle', 'paused', 'P4']].map(([n, s, state, meta]) => {
    const {
      StatusDot
    } = window.JarvisOSDesignSystem_e0065d;
    return /*#__PURE__*/React.createElement("div", {
      key: n,
      style: {
        display: 'flex',
        alignItems: 'center',
        gap: 12,
        padding: 'var(--space-3)',
        borderRadius: 'var(--radius-sm)'
      },
      onMouseEnter: e => e.currentTarget.style.background = 'var(--surface-hover)',
      onMouseLeave: e => e.currentTarget.style.background = 'transparent'
    }, /*#__PURE__*/React.createElement(StatusDot, {
      status: s,
      pulse: s === 'live'
    }), /*#__PURE__*/React.createElement("span", {
      style: {
        font: 'var(--weight-medium) var(--text-sm)/1 var(--font-mono)',
        color: 'var(--text-primary)',
        width: 130
      }
    }, n), /*#__PURE__*/React.createElement("span", {
      style: {
        font: '400 var(--text-xs)/1 var(--font-mono)',
        color: 'var(--text-secondary)',
        flex: 1
      }
    }, state), /*#__PURE__*/React.createElement("span", {
      style: {
        font: '400 var(--text-xs)/1 var(--font-mono)',
        color: 'var(--text-muted)'
      }
    }, meta));
  }))), /*#__PURE__*/React.createElement(Card, {
    title: "System activity",
    subtitle: "router \xB7 SHIELD \xB7 IPC \xB7 model tiers",
    right: (() => {
      const {
        StatusDot
      } = window.JarvisOSDesignSystem_e0065d;
      return /*#__PURE__*/React.createElement(StatusDot, {
        status: "live",
        pulse: true,
        label: "live"
      });
    })(),
    padding: "none"
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      padding: '4px var(--space-3) var(--space-3)'
    }
  }, activity.map((a, i) => /*#__PURE__*/React.createElement("div", {
    key: i,
    style: {
      display: 'flex',
      gap: 12,
      padding: 'var(--space-3) var(--space-2)',
      borderBottom: i < activity.length - 1 ? '1px solid var(--border-hairline)' : 'none'
    }
  }, /*#__PURE__*/React.createElement("span", {
    style: {
      width: 24,
      height: 24,
      flex: 'none',
      borderRadius: 'var(--radius-sm)',
      display: 'flex',
      alignItems: 'center',
      justifyContent: 'center',
      background: 'var(--surface-inset)',
      color: toneColor(a.tone)
    }
  }, /*#__PURE__*/React.createElement(Icon, {
    name: a.icon,
    size: 13
  })), /*#__PURE__*/React.createElement("span", {
    style: {
      font: '400 var(--text-sm)/1.4 var(--font-mono)',
      color: 'var(--text-secondary)',
      flex: 1
    }
  }, a.text), /*#__PURE__*/React.createElement("span", {
    style: {
      font: '400 var(--text-2xs)/1.6 var(--font-mono)',
      color: 'var(--text-muted)',
      flex: 'none'
    }
  }, a.when)))))));
}
window.CommandCenter = CommandCenter;
})(); } catch (e) { __ds_ns.__errors.push({ path: "ui_kits/jarvis-os/CommandCenter.jsx", error: String((e && e.message) || e) }); }

// ui_kits/jarvis-os/Models.jsx
try { (() => {
/* JARVIS AI-OS UI kit — Models view
 * The on-device model tier system + bench-off results (FINAL_SCORES.txt).
 * Quality from 7 blind judges; speed on Ryzen 7 2700X CPU-only.
 */

function Models() {
  const {
    Card,
    Badge
  } = window.JarvisOSDesignSystem_e0065d;
  const tiers = [{
    tier: 'IDLE',
    model: 'Llama 3.2 1B',
    q: '5.37',
    sp: '40.4',
    status: 'Running now',
    tone: 'ok'
  }, {
    tier: 'ACTIVE',
    model: 'Gemma 4 E2B',
    q: '8.40',
    sp: '19.7',
    status: 'Target · ~1000 LOC',
    tone: 'accent'
  }, {
    tier: 'CRITICAL',
    model: 'Mistral 7B Q8',
    q: '7.50',
    sp: '5.5',
    status: 'Drop-in ready',
    tone: 'warn'
  }, {
    tier: 'EMERGENCY',
    model: 'Rules (cache)',
    q: '—',
    sp: '<1ms',
    status: 'Running now',
    tone: 'err'
  }];
  const bench = [{
    m: 'gemma-4-E2B',
    p: '4.65B',
    q: 8.40,
    sp: 19.7,
    drop: 'NO'
  }, {
    m: 'mistral-7b-v0.2',
    p: '7.24B',
    q: 7.50,
    sp: 5.5,
    drop: 'YES'
  }, {
    m: 'gemma-4-E4B',
    p: '7.52B',
    q: 7.33,
    sp: 10.6,
    drop: 'NO'
  }, {
    m: 'Qwen3.5-9B',
    p: '8.95B',
    q: 7.26,
    sp: 6.6,
    drop: 'NO'
  }, {
    m: 'Llama-3.2-3B',
    p: '3.21B',
    q: 5.76,
    sp: 16.7,
    drop: 'YES'
  }, {
    m: 'Llama-3.2-1B',
    p: '1.24B',
    q: 5.37,
    sp: 40.4,
    drop: 'YES'
  }, {
    m: 'Phi-3-mini',
    p: '3.82B',
    q: 5.31,
    sp: 14.3,
    drop: 'MAYBE'
  }];
  const maxQ = 10;
  const tierTone = t => ({
    ok: 'var(--status-ok)',
    accent: 'var(--text-accent)',
    warn: 'var(--status-warn)',
    err: 'var(--status-err)'
  })[t];
  return /*#__PURE__*/React.createElement("div", {
    style: {
      padding: 'var(--space-6)',
      overflow: 'auto',
      display: 'flex',
      flexDirection: 'column',
      gap: 'var(--space-5)'
    }
  }, /*#__PURE__*/React.createElement("div", null, /*#__PURE__*/React.createElement("div", {
    style: {
      font: 'var(--type-eyebrow)',
      letterSpacing: 'var(--tracking-caps)',
      textTransform: 'uppercase',
      color: 'var(--text-accent)',
      marginBottom: 8
    }
  }, "Model bench-off \xB7 7 blind judges \xB7 Apr 2026"), /*#__PURE__*/React.createElement("h1", {
    style: {
      margin: 0,
      font: 'var(--weight-semibold) var(--text-2xl)/1.1 var(--font-display)',
      letterSpacing: 'var(--tracking-tight)',
      color: 'var(--text-primary)'
    }
  }, "On-device model tiers")), /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'grid',
      gridTemplateColumns: 'repeat(4, 1fr)',
      gap: 'var(--space-4)'
    }
  }, tiers.map(t => /*#__PURE__*/React.createElement(Card, {
    key: t.tier,
    padding: "md",
    style: {
      borderTop: `2px solid ${tierTone(t.tone)}`
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      font: 'var(--type-eyebrow)',
      letterSpacing: 'var(--tracking-caps)',
      textTransform: 'uppercase',
      color: tierTone(t.tone),
      marginBottom: 10
    }
  }, t.tier), /*#__PURE__*/React.createElement("div", {
    style: {
      font: 'var(--weight-semibold) var(--text-lg)/1.1 var(--font-sans)',
      color: 'var(--text-primary)'
    }
  }, t.model), /*#__PURE__*/React.createElement("div", {
    style: {
      marginTop: 10,
      display: 'flex',
      gap: 14
    }
  }, /*#__PURE__*/React.createElement("span", {
    style: {
      font: '400 var(--text-2xs)/1.4 var(--font-mono)',
      color: 'var(--text-muted)'
    }
  }, "Q ", t.q), /*#__PURE__*/React.createElement("span", {
    style: {
      font: '400 var(--text-2xs)/1.4 var(--font-mono)',
      color: 'var(--text-muted)'
    }
  }, t.sp, " tok/s")), /*#__PURE__*/React.createElement("div", {
    style: {
      marginTop: 8,
      font: '400 var(--text-2xs)/1.3 var(--font-mono)',
      color: 'var(--text-secondary)'
    }
  }, t.status)))), /*#__PURE__*/React.createElement(Card, {
    title: "Quality bench-off",
    subtitle: "quality (7 judges) vs speed (Ryzen 7 2700X, CPU, 8 threads)",
    padding: "none"
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      padding: 'var(--space-2) var(--space-4) var(--space-4)'
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'grid',
      gridTemplateColumns: '160px 64px 1fr 80px 70px',
      gap: 12,
      alignItems: 'center',
      padding: 'var(--space-3) var(--space-2)',
      borderBottom: '1px solid var(--border-default)'
    }
  }, ['Model', 'Params', 'Quality', 'Speed', 'Drop-in'].map(h => /*#__PURE__*/React.createElement("span", {
    key: h,
    style: {
      font: 'var(--type-eyebrow)',
      letterSpacing: 'var(--tracking-wide)',
      textTransform: 'uppercase',
      color: 'var(--text-muted)'
    }
  }, h))), bench.map((b, i) => /*#__PURE__*/React.createElement("div", {
    key: b.m,
    style: {
      display: 'grid',
      gridTemplateColumns: '160px 64px 1fr 80px 70px',
      gap: 12,
      alignItems: 'center',
      padding: 'var(--space-3) var(--space-2)',
      borderBottom: i < bench.length - 1 ? '1px solid var(--border-hairline)' : 'none'
    }
  }, /*#__PURE__*/React.createElement("span", {
    style: {
      font: 'var(--weight-medium) var(--text-sm)/1 var(--font-mono)',
      color: i === 0 ? 'var(--text-accent)' : 'var(--text-primary)'
    }
  }, b.m), /*#__PURE__*/React.createElement("span", {
    style: {
      font: '400 var(--text-xs)/1 var(--font-mono)',
      color: 'var(--text-muted)'
    }
  }, b.p), /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      alignItems: 'center',
      gap: 8
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      flex: 1,
      height: 6,
      borderRadius: 3,
      background: 'var(--surface-inset)',
      overflow: 'hidden'
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      width: `${b.q / maxQ * 100}%`,
      height: '100%',
      background: i === 0 ? 'var(--accent)' : 'var(--gray-6)',
      borderRadius: 3
    }
  })), /*#__PURE__*/React.createElement("span", {
    style: {
      font: '500 var(--text-xs)/1 var(--font-mono)',
      color: 'var(--text-secondary)',
      width: 26
    }
  }, b.q.toFixed(2))), /*#__PURE__*/React.createElement("span", {
    style: {
      font: '400 var(--text-xs)/1 var(--font-mono)',
      color: 'var(--text-secondary)'
    }
  }, b.sp, " t/s"), /*#__PURE__*/React.createElement(Badge, {
    tone: b.drop === 'YES' ? 'ok' : b.drop === 'NO' ? 'neutral' : 'warn'
  }, b.drop))))));
}
window.Models = Models;
})(); } catch (e) { __ds_ns.__errors.push({ path: "ui_kits/jarvis-os/Models.jsx", error: String((e && e.message) || e) }); }

// ui_kits/jarvis-os/Shell.jsx
try { (() => {
/* JARVIS AI-OS UI kit — Shell: app frame (icon rail + context sidebar + topbar)
 * Composes design-system primitives from window.JarvisOSDesignSystem_e0065d.
 * Icons: Lucide (CDN) — substitution; swap for the real icon set when available.
 * Content reflects the real JARVIS AI-OS: seL4 microkernel, Agent Router + 4
 * specialist agents, SHIELD safety framework, on-device model tiers, IPC.
 */

function Icon({
  name,
  size = 18,
  style
}) {
  const ref = React.useRef(null);
  React.useEffect(() => {
    const el = ref.current;
    if (!el) return;
    el.innerHTML = '';
    const i = document.createElement('i');
    i.setAttribute('data-lucide', name);
    el.appendChild(i);
    if (window.lucide) window.lucide.createIcons();
    const svg = el.querySelector('svg');
    if (svg) {
      svg.setAttribute('width', size);
      svg.setAttribute('height', size);
      svg.style.strokeWidth = '1.75';
    }
  }, [name, size]);
  return /*#__PURE__*/React.createElement("span", {
    ref: ref,
    style: {
      width: size,
      height: size,
      display: 'inline-flex',
      flex: 'none',
      ...style
    }
  });
}
function Logo({
  size = 26
}) {
  return /*#__PURE__*/React.createElement("div", {
    style: {
      width: size,
      height: size,
      borderRadius: 'var(--radius-sm)',
      background: 'var(--accent)',
      display: 'flex',
      alignItems: 'center',
      justifyContent: 'center',
      boxShadow: 'var(--glow-accent)',
      flex: 'none'
    }
  }, /*#__PURE__*/React.createElement("span", {
    style: {
      font: 'var(--weight-bold) 15px/1 var(--font-display)',
      color: '#fff'
    }
  }, "J"));
}
const RAIL_ITEMS = [{
  id: 'command',
  icon: 'layout-dashboard',
  label: 'Command Center'
}, {
  id: 'assistant',
  icon: 'sparkles',
  label: 'Assistant'
}, {
  id: 'agents',
  icon: 'git-fork',
  label: 'Agents'
}, {
  id: 'shield',
  icon: 'shield-check',
  label: 'SHIELD'
}, {
  id: 'models',
  icon: 'cpu',
  label: 'Models'
}];
function Rail({
  view,
  setView
}) {
  return /*#__PURE__*/React.createElement("nav", {
    style: {
      width: 'var(--rail-w)',
      flex: 'none',
      background: 'var(--bg-canvas)',
      borderRight: '1px solid var(--border-hairline)',
      display: 'flex',
      flexDirection: 'column',
      alignItems: 'center',
      padding: 'var(--space-3) 0',
      gap: 'var(--space-4)'
    }
  }, /*#__PURE__*/React.createElement(Logo, null), /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      flexDirection: 'column',
      gap: 4,
      marginTop: 4
    }
  }, RAIL_ITEMS.map(it => {
    const on = it.id === view;
    return /*#__PURE__*/React.createElement("button", {
      key: it.id,
      title: it.label,
      onClick: () => setView(it.id),
      style: {
        position: 'relative',
        width: 38,
        height: 38,
        border: 'none',
        cursor: 'pointer',
        borderRadius: 'var(--radius-md)',
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'center',
        background: on ? 'var(--surface-inset)' : 'transparent',
        color: on ? 'var(--text-accent)' : 'var(--text-muted)',
        transition: 'color var(--dur-fast), background var(--dur-fast)'
      }
    }, on && /*#__PURE__*/React.createElement("span", {
      style: {
        position: 'absolute',
        left: -10,
        top: 9,
        width: 3,
        height: 20,
        borderRadius: '0 3px 3px 0',
        background: 'var(--accent)'
      }
    }), /*#__PURE__*/React.createElement(Icon, {
      name: it.icon
    }));
  })), /*#__PURE__*/React.createElement("div", {
    style: {
      marginTop: 'auto',
      display: 'flex',
      flexDirection: 'column',
      gap: 10,
      alignItems: 'center'
    }
  }, /*#__PURE__*/React.createElement("button", {
    title: "Settings",
    style: {
      width: 38,
      height: 38,
      border: 'none',
      background: 'transparent',
      color: 'var(--text-muted)',
      cursor: 'pointer',
      display: 'flex',
      alignItems: 'center',
      justifyContent: 'center'
    }
  }, /*#__PURE__*/React.createElement(Icon, {
    name: "settings"
  })), /*#__PURE__*/React.createElement("div", {
    style: {
      width: 30,
      height: 30,
      borderRadius: '999px',
      background: 'var(--surface-inset)',
      border: '1px solid var(--border-strong)',
      display: 'flex',
      alignItems: 'center',
      justifyContent: 'center',
      font: 'var(--weight-semibold) 12px/1 var(--font-mono)',
      color: 'var(--text-secondary)'
    }
  }, "OP")));
}

// Real agent roster — router + 4 specialists, in priority order (device > network > filesystem > user)
const AGENT_ROSTER = [{
  name: 'agent-router',
  status: 'live',
  meta: 'routing · ctx pool',
  prio: '—'
}, {
  name: 'device-manager',
  status: 'ok',
  meta: 'idle · ready',
  prio: 'P1'
}, {
  name: 'network',
  status: 'ok',
  meta: 'ping 8.8.8.8 · 14ms',
  prio: 'P2'
}, {
  name: 'filesystem',
  status: 'ok',
  meta: 'idle · ready',
  prio: 'P3'
}, {
  name: 'user-interaction',
  status: 'idle',
  meta: 'paused',
  prio: 'P4'
}];

// Recent assistant queries (real example queries from the multi-agent design doc)
const RECENT_QUERIES = [{
  t: 'How much disk space is left?',
  when: 'now',
  active: true
}, {
  t: 'Ping google.com',
  when: '2m'
}, {
  t: 'Show me memory usage',
  when: '14m'
}, {
  t: 'What ports are listening?',
  when: '1h'
}, {
  t: 'List USB devices',
  when: '3h'
}];
function Sidebar({
  view
}) {
  const {
    StatusDot,
    Badge
  } = window.JarvisOSDesignSystem_e0065d;
  const isAssistant = view === 'assistant';
  return /*#__PURE__*/React.createElement("aside", {
    style: {
      width: 'var(--sidebar-w)',
      flex: 'none',
      background: 'var(--bg-app)',
      borderRight: '1px solid var(--border-hairline)',
      display: 'flex',
      flexDirection: 'column'
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      padding: 'var(--space-4) var(--space-4) var(--space-3)',
      display: 'flex',
      alignItems: 'center',
      justifyContent: 'space-between'
    }
  }, /*#__PURE__*/React.createElement("span", {
    style: {
      font: 'var(--type-eyebrow)',
      letterSpacing: 'var(--tracking-caps)',
      textTransform: 'uppercase',
      color: 'var(--text-muted)'
    }
  }, isAssistant ? 'Recent queries' : 'Agent roster'), /*#__PURE__*/React.createElement(Icon, {
    name: isAssistant ? 'plus' : 'git-fork',
    size: 14,
    style: {
      color: 'var(--text-muted)'
    }
  })), /*#__PURE__*/React.createElement("div", {
    style: {
      flex: 1,
      overflow: 'auto',
      padding: '0 var(--space-2) var(--space-3)'
    }
  }, !isAssistant && AGENT_ROSTER.map(a => /*#__PURE__*/React.createElement("div", {
    key: a.name,
    style: {
      display: 'flex',
      alignItems: 'center',
      gap: 10,
      padding: 'var(--space-2) var(--space-3)',
      borderRadius: 'var(--radius-sm)',
      cursor: 'pointer'
    },
    onMouseEnter: e => e.currentTarget.style.background = 'var(--surface-hover)',
    onMouseLeave: e => e.currentTarget.style.background = 'transparent'
  }, /*#__PURE__*/React.createElement(StatusDot, {
    status: a.status,
    pulse: a.status === 'live'
  }), /*#__PURE__*/React.createElement("div", {
    style: {
      minWidth: 0,
      flex: 1
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      font: 'var(--weight-medium) var(--text-sm)/1.2 var(--font-mono)',
      color: 'var(--text-primary)'
    }
  }, a.name), /*#__PURE__*/React.createElement("div", {
    style: {
      font: '400 var(--text-2xs)/1.4 var(--font-mono)',
      color: 'var(--text-muted)'
    }
  }, a.meta)), /*#__PURE__*/React.createElement("span", {
    style: {
      font: '500 var(--text-2xs)/1 var(--font-mono)',
      color: 'var(--text-muted)',
      padding: '2px 5px',
      borderRadius: 'var(--radius-xs)',
      background: 'var(--surface-inset)'
    }
  }, a.prio))), isAssistant && RECENT_QUERIES.map((th, i) => /*#__PURE__*/React.createElement("div", {
    key: i,
    style: {
      padding: 'var(--space-3)',
      borderRadius: 'var(--radius-sm)',
      cursor: 'pointer',
      background: th.active ? 'var(--surface-inset)' : 'transparent'
    },
    onMouseEnter: e => {
      if (!th.active) e.currentTarget.style.background = 'var(--surface-hover)';
    },
    onMouseLeave: e => {
      if (!th.active) e.currentTarget.style.background = 'transparent';
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      justifyContent: 'space-between',
      gap: 8
    }
  }, /*#__PURE__*/React.createElement("span", {
    style: {
      font: 'var(--weight-medium) var(--text-sm)/1.3 var(--font-sans)',
      color: th.active ? 'var(--text-primary)' : 'var(--text-secondary)',
      overflow: 'hidden',
      textOverflow: 'ellipsis',
      whiteSpace: 'nowrap'
    }
  }, th.t), /*#__PURE__*/React.createElement("span", {
    style: {
      font: '400 var(--text-2xs)/1.6 var(--font-mono)',
      color: 'var(--text-muted)',
      flex: 'none'
    }
  }, th.when))))), /*#__PURE__*/React.createElement("div", {
    style: {
      padding: 'var(--space-3) var(--space-4)',
      borderTop: '1px solid var(--border-hairline)',
      display: 'flex',
      alignItems: 'center',
      justifyContent: 'space-between'
    }
  }, /*#__PURE__*/React.createElement("span", {
    style: {
      font: '400 var(--text-2xs)/1 var(--font-mono)',
      color: 'var(--text-muted)'
    }
  }, "seL4 \xB7 Phase 1"), /*#__PURE__*/React.createElement(Badge, {
    tone: "ok",
    dot: true
  }, "verified")));
}
function Topbar({
  title,
  crumb
}) {
  const {
    Kbd,
    Badge
  } = window.JarvisOSDesignSystem_e0065d;
  return /*#__PURE__*/React.createElement("header", {
    style: {
      height: 'var(--topbar-h)',
      flex: 'none',
      display: 'flex',
      alignItems: 'center',
      gap: 'var(--space-4)',
      padding: '0 var(--space-5)',
      borderBottom: '1px solid var(--border-hairline)',
      background: 'var(--bg-app)'
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      alignItems: 'center',
      gap: 8
    }
  }, /*#__PURE__*/React.createElement("span", {
    style: {
      font: '400 var(--text-xs)/1 var(--font-mono)',
      color: 'var(--text-muted)'
    }
  }, crumb), /*#__PURE__*/React.createElement(Icon, {
    name: "chevron-right",
    size: 13,
    style: {
      color: 'var(--text-muted)'
    }
  }), /*#__PURE__*/React.createElement("span", {
    style: {
      font: 'var(--weight-semibold) var(--text-md)/1 var(--font-sans)',
      color: 'var(--text-primary)'
    }
  }, title)), /*#__PURE__*/React.createElement("div", {
    style: {
      flex: 1
    }
  }), /*#__PURE__*/React.createElement(Badge, {
    tone: "accent",
    dot: true
  }, "gemma-4-e2b \xB7 ACTIVE"), /*#__PURE__*/React.createElement("button", {
    style: {
      display: 'flex',
      alignItems: 'center',
      gap: 8,
      height: 30,
      padding: '0 10px',
      background: 'var(--surface-inset)',
      border: '1px solid var(--border-default)',
      borderRadius: 'var(--radius-sm)',
      color: 'var(--text-muted)',
      cursor: 'pointer',
      width: 200
    }
  }, /*#__PURE__*/React.createElement(Icon, {
    name: "search",
    size: 15
  }), /*#__PURE__*/React.createElement("span", {
    style: {
      font: '400 var(--text-sm)/1 var(--font-sans)',
      flex: 1,
      textAlign: 'left'
    }
  }, "Ask JARVIS\u2026"), /*#__PURE__*/React.createElement(Kbd, {
    keys: ['⌘', 'K']
  })));
}
window.JarvisShell = {
  Icon,
  Logo,
  Rail,
  Sidebar,
  Topbar
};
})(); } catch (e) { __ds_ns.__errors.push({ path: "ui_kits/jarvis-os/Shell.jsx", error: String((e && e.message) || e) }); }

// ui_kits/jarvis-os/Shield.jsx
try { (() => {
/* JARVIS AI-OS UI kit — SHIELD view
 * Safety framework: S.H.I.E.L.D. pillars, continuous risk scoring (0.0–1.0),
 * execution modes, recent gated actions. From shield_framework.py.
 */

function Shield() {
  const {
    Card,
    Badge
  } = window.JarvisOSDesignSystem_e0065d;
  const {
    Icon
  } = window.JarvisShell;
  const pillars = [{
    k: 'S',
    name: 'Staged execution sandbox'
  }, {
    k: 'H',
    name: 'Hardware impact analysis'
  }, {
    k: 'I',
    name: 'Irreversibility detection'
  }, {
    k: 'E',
    name: 'Escalation intelligence'
  }, {
    k: 'L',
    name: 'Learning from failures'
  }, {
    k: 'D',
    name: 'Deterministic rollback'
  }];
  const risk = [{
    lvl: 'SAFE',
    range: '0.0 – 0.2',
    tone: 'ok',
    mode: 'automatic'
  }, {
    lvl: 'LOW',
    range: '0.2 – 0.4',
    tone: 'ok',
    mode: 'automatic'
  }, {
    lvl: 'MEDIUM',
    range: '0.4 – 0.6',
    tone: 'warn',
    mode: 'shadow'
  }, {
    lvl: 'HIGH',
    range: '0.6 – 0.8',
    tone: 'warn',
    mode: 'approval required'
  }, {
    lvl: 'CRITICAL',
    range: '0.8 – 1.0',
    tone: 'err',
    mode: 'blocked'
  }];
  const recent = [{
    action: 'show_disk_space',
    score: '0.10',
    lvl: 'SAFE',
    tone: 'ok',
    mode: 'automatic'
  }, {
    action: 'ping_host 8.8.8.8',
    score: '0.15',
    lvl: 'SAFE',
    tone: 'ok',
    mode: 'automatic'
  }, {
    action: 'chmod 600 ~/.ssh/id_rsa',
    score: '0.45',
    lvl: 'MEDIUM',
    tone: 'warn',
    mode: 'shadow'
  }, {
    action: 'kill -9 1422',
    score: '0.68',
    lvl: 'HIGH',
    tone: 'warn',
    mode: 'approval'
  }, {
    action: 'rm -rf /*',
    score: '0.95',
    lvl: 'CRITICAL',
    tone: 'err',
    mode: 'blocked'
  }];
  const toneColor = t => ({
    ok: 'var(--status-ok)',
    warn: 'var(--status-warn)',
    err: 'var(--status-err)'
  })[t];
  return /*#__PURE__*/React.createElement("div", {
    style: {
      padding: 'var(--space-6)',
      overflow: 'auto',
      display: 'flex',
      flexDirection: 'column',
      gap: 'var(--space-5)'
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      alignItems: 'flex-end',
      justifyContent: 'space-between'
    }
  }, /*#__PURE__*/React.createElement("div", null, /*#__PURE__*/React.createElement("div", {
    style: {
      font: 'var(--type-eyebrow)',
      letterSpacing: 'var(--tracking-caps)',
      textTransform: 'uppercase',
      color: 'var(--text-accent)',
      marginBottom: 8
    }
  }, "Safety framework \xB7 100 action types \xB7 Week 16\u201317"), /*#__PURE__*/React.createElement("h1", {
    style: {
      margin: 0,
      font: 'var(--weight-semibold) var(--text-2xl)/1.1 var(--font-display)',
      letterSpacing: 'var(--tracking-tight)',
      color: 'var(--text-primary)'
    }
  }, "SHIELD")), /*#__PURE__*/React.createElement(Badge, {
    tone: "ok",
    dot: true
  }, "enabled")), /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'grid',
      gridTemplateColumns: 'repeat(6, 1fr)',
      gap: 'var(--space-3)'
    }
  }, pillars.map(p => /*#__PURE__*/React.createElement(Card, {
    key: p.k,
    padding: "md"
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      font: 'var(--weight-bold) var(--text-2xl)/1 var(--font-display)',
      color: 'var(--text-accent)'
    }
  }, p.k), /*#__PURE__*/React.createElement("div", {
    style: {
      marginTop: 8,
      font: '400 var(--text-2xs)/1.35 var(--font-sans)',
      color: 'var(--text-secondary)'
    }
  }, p.name)))), /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'grid',
      gridTemplateColumns: '1fr 1.3fr',
      gap: 'var(--space-4)'
    }
  }, /*#__PURE__*/React.createElement(Card, {
    title: "Risk scoring",
    subtitle: "continuous 0.0\u20131.0 \u2192 execution mode",
    padding: "none"
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      padding: '4px var(--space-3) var(--space-3)'
    }
  }, risk.map((r, i) => /*#__PURE__*/React.createElement("div", {
    key: r.lvl,
    style: {
      display: 'flex',
      alignItems: 'center',
      gap: 12,
      padding: 'var(--space-3) var(--space-2)',
      borderBottom: i < risk.length - 1 ? '1px solid var(--border-hairline)' : 'none'
    }
  }, /*#__PURE__*/React.createElement("span", {
    style: {
      width: 8,
      height: 8,
      borderRadius: 999,
      background: toneColor(r.tone),
      flex: 'none',
      boxShadow: `0 0 6px ${toneColor(r.tone)}`
    }
  }), /*#__PURE__*/React.createElement("span", {
    style: {
      font: 'var(--weight-semibold) var(--text-xs)/1 var(--font-mono)',
      color: 'var(--text-primary)',
      width: 72
    }
  }, r.lvl), /*#__PURE__*/React.createElement("span", {
    style: {
      font: '400 var(--text-xs)/1 var(--font-mono)',
      color: 'var(--text-muted)',
      flex: 1
    }
  }, r.range), /*#__PURE__*/React.createElement("span", {
    style: {
      font: '400 var(--text-2xs)/1 var(--font-mono)',
      textTransform: 'uppercase',
      letterSpacing: 'var(--tracking-wide)',
      color: 'var(--text-secondary)'
    }
  }, r.mode))))), /*#__PURE__*/React.createElement(Card, {
    title: "Recent actions",
    subtitle: "scored + gated by SHIELD before execution",
    right: /*#__PURE__*/React.createElement(Badge, {
      tone: "neutral"
    }, "last 5"),
    padding: "none"
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      padding: '4px var(--space-3) var(--space-3)'
    }
  }, recent.map((a, i) => /*#__PURE__*/React.createElement("div", {
    key: i,
    style: {
      display: 'flex',
      alignItems: 'center',
      gap: 12,
      padding: 'var(--space-3) var(--space-2)',
      borderBottom: i < recent.length - 1 ? '1px solid var(--border-hairline)' : 'none'
    }
  }, /*#__PURE__*/React.createElement(Icon, {
    name: a.tone === 'err' ? 'shield-x' : a.tone === 'warn' ? 'shield-alert' : 'shield-check',
    size: 15,
    style: {
      color: toneColor(a.tone),
      flex: 'none'
    }
  }), /*#__PURE__*/React.createElement("span", {
    style: {
      font: '400 var(--text-sm)/1 var(--font-mono)',
      color: 'var(--text-primary)',
      flex: 1,
      overflow: 'hidden',
      textOverflow: 'ellipsis',
      whiteSpace: 'nowrap'
    }
  }, a.action), /*#__PURE__*/React.createElement("span", {
    style: {
      font: '500 var(--text-xs)/1 var(--font-mono)',
      color: toneColor(a.tone),
      width: 34
    }
  }, a.score), /*#__PURE__*/React.createElement(Badge, {
    tone: a.tone
  }, a.mode)))))));
}
window.Shield = Shield;
})(); } catch (e) { __ds_ns.__errors.push({ path: "ui_kits/jarvis-os/Shield.jsx", error: String((e && e.message) || e) }); }

// ui_kits/telemetry-console/ConsoleCommandCenter.jsx
try { (() => {
/* JARVIS OS — Telemetry Console · Command Center (live overview) */

function CommandCenter({
  store
}) {
  const {
    Card,
    Badge
  } = window.JarvisOSDesignSystem_e0065d;
  const {
    Icon
  } = window.JarvisShell;
  const {
    num,
    pct,
    fmtClock,
    hasFlag
  } = window.JConsoleHelpers;
  const rec = store.latest;

  // state eyebrow — never "nominal"/"verified"
  let stateWord = 'DISCONNECTED',
    stateColor = 'var(--status-err)';
  if (rec) {
    if (store.connState === 'live') {
      const err = hasFlag(rec, 'HAS_ERROR') || (Number(rec.q_errors) || 0) > 0;
      stateWord = err ? 'ERROR' : 'READY';
      stateColor = err ? 'var(--status-err)' : 'var(--status-ok)';
    } else if (store.connState === 'stale') {
      stateWord = 'STALE';
      stateColor = 'var(--status-warn)';
    } else if (store.connState === 'crcfail') {
      stateWord = 'CRC FAIL';
      stateColor = 'var(--status-err)';
    }
  }
  const qTotal = rec ? Number(rec.q_total) || 0 : 0;
  const cacheP = rec ? pct(rec.q_hits, rec.q_total) : null;
  const llmP = rec ? pct(rec.q_infer, rec.q_total) : null;
  const qErr = rec ? Number(rec.q_errors) || 0 : 0;
  const qps = window.JarvisTelemetry.queriesPerSec();
  function Tile({
    label,
    children,
    foot
  }) {
    return /*#__PURE__*/React.createElement(Card, {
      padding: "md"
    }, /*#__PURE__*/React.createElement("span", {
      style: {
        font: 'var(--type-eyebrow)',
        letterSpacing: 'var(--tracking-wide)',
        textTransform: 'uppercase',
        color: 'var(--text-muted)'
      }
    }, label), /*#__PURE__*/React.createElement("div", {
      style: {
        marginTop: 10
      }
    }, children), foot && /*#__PURE__*/React.createElement("div", {
      style: {
        marginTop: 6,
        font: '400 var(--text-2xs)/1.3 var(--font-mono)',
        color: 'var(--text-muted)'
      }
    }, foot));
  }
  const big = {
    font: 'var(--weight-semibold) var(--text-3xl)/1 var(--font-display)',
    color: 'var(--text-primary)'
  };
  const unit = {
    font: '400 var(--text-sm)/1 var(--font-mono)',
    color: 'var(--text-muted)'
  };

  // real rolling-buffer sparkline (queries/sec) — collecting state until enough samples
  function RateSpark() {
    const s = store.rateSamples || [];
    if (s.length < 4) {
      return /*#__PURE__*/React.createElement("div", {
        style: {
          height: 28,
          display: 'flex',
          alignItems: 'center',
          font: '400 var(--text-2xs)/1 var(--font-mono)',
          color: 'var(--text-muted)'
        }
      }, "collecting\u2026");
    }
    const deltas = [];
    for (let i = 1; i < s.length; i++) {
      const dt = s[i].t - s[i - 1].t;
      deltas.push(dt > 0 ? Math.max(0, (s[i].q - s[i - 1].q) / dt) : 0);
    }
    const max = Math.max(...deltas, 1);
    return /*#__PURE__*/React.createElement("div", {
      style: {
        display: 'flex',
        alignItems: 'flex-end',
        gap: 2,
        height: 28
      }
    }, deltas.map((v, i) => /*#__PURE__*/React.createElement("span", {
      key: i,
      style: {
        width: 4,
        height: `${v / max * 100}%`,
        minHeight: 2,
        background: 'var(--accent)',
        opacity: 0.4 + i / deltas.length * 0.6,
        borderRadius: 1
      }
    })));
  }

  // activity feed from live SSE records, keyed on kind_name
  const kindMeta = {
    STATS: {
      icon: 'activity',
      color: 'var(--text-accent)'
    },
    INFER: {
      icon: 'message-square',
      color: 'var(--status-live)'
    },
    STATE: {
      icon: 'git-commit-horizontal',
      color: 'var(--text-secondary)'
    }
  };
  return /*#__PURE__*/React.createElement("div", {
    style: {
      padding: 'var(--space-6)',
      overflow: 'auto',
      display: 'flex',
      flexDirection: 'column',
      gap: 'var(--space-5)'
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      alignItems: 'flex-end',
      justifyContent: 'space-between'
    }
  }, /*#__PURE__*/React.createElement("div", null, /*#__PURE__*/React.createElement("div", {
    style: {
      font: 'var(--type-eyebrow)',
      letterSpacing: 'var(--tracking-caps)',
      textTransform: 'uppercase',
      color: stateColor,
      marginBottom: 8
    }
  }, "seL4 \xB7 x86-64 (functional-but-unverified \xB7 perf config) \xB7 ", stateWord), /*#__PURE__*/React.createElement("h1", {
    style: {
      margin: 0,
      font: 'var(--weight-semibold) var(--text-3xl)/1.1 var(--font-display)',
      letterSpacing: 'var(--tracking-tight)',
      color: 'var(--text-primary)'
    }
  }, rec ? 'JARVIS telemetry' : 'Awaiting telemetry')), /*#__PURE__*/React.createElement("a", {
    href: "#",
    onClick: e => {
      e.preventDefault();
      store._setView && store._setView('last');
    },
    style: {
      display: 'inline-flex',
      alignItems: 'center',
      gap: 8,
      height: 34,
      padding: '0 14px',
      borderRadius: 'var(--radius-sm)',
      border: '1px solid var(--border-strong)',
      background: 'var(--surface-inset)',
      color: 'var(--text-primary)',
      font: 'var(--weight-medium) var(--text-sm)/1 var(--font-sans)',
      textDecoration: 'none'
    }
  }, /*#__PURE__*/React.createElement(Icon, {
    name: "eye",
    size: 15
  }), " View last response")), /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'grid',
      gridTemplateColumns: 'repeat(4, 1fr)',
      gap: 'var(--space-4)'
    }
  }, /*#__PURE__*/React.createElement(Tile, {
    label: "Queries",
    foot: qps == null ? 'rate: collecting…' : qps.toFixed(1) + ' q/s (rolling)'
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      alignItems: 'flex-end',
      justifyContent: 'space-between'
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      alignItems: 'baseline',
      gap: 4
    }
  }, /*#__PURE__*/React.createElement("span", {
    style: big
  }, rec ? num(qTotal) : '—'), /*#__PURE__*/React.createElement("span", {
    style: unit
  }, "q_total")), /*#__PURE__*/React.createElement(RateSpark, null))), /*#__PURE__*/React.createElement(Tile, {
    label: "Route",
    foot: qTotal === 0 ? null : 'cache vs → LLM (aggregate)'
  }, qTotal === 0 ? /*#__PURE__*/React.createElement("div", {
    style: {
      font: '400 var(--text-sm)/1.3 var(--font-mono)',
      color: 'var(--text-muted)',
      paddingTop: 6
    }
  }, "no queries yet") : /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      alignItems: 'baseline',
      gap: 10
    }
  }, /*#__PURE__*/React.createElement("span", {
    style: {
      ...big,
      fontSize: 'var(--text-2xl)',
      color: 'var(--status-ok)'
    }
  }, cacheP.toFixed(0), "%"), /*#__PURE__*/React.createElement("span", {
    style: unit
  }, "cache"), /*#__PURE__*/React.createElement("span", {
    style: {
      ...big,
      fontSize: 'var(--text-2xl)',
      color: 'var(--text-accent)'
    }
  }, llmP.toFixed(0), "%"), /*#__PURE__*/React.createElement("span", {
    style: unit
  }, "\u2192 LLM"))), /*#__PURE__*/React.createElement(Tile, {
    label: "Errors",
    foot: "q_errors"
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      alignItems: 'baseline',
      gap: 4
    }
  }, /*#__PURE__*/React.createElement("span", {
    style: {
      ...big,
      color: qErr > 0 ? 'var(--status-err)' : 'var(--text-primary)'
    }
  }, rec ? num(qErr) : '—'), qErr === 0 && rec && /*#__PURE__*/React.createElement(Badge, {
    tone: "ok"
  }, "clean"))), /*#__PURE__*/React.createElement(Tile, {
    label: "Throughput",
    foot: "deployed benchmark \xB7 not live"
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      alignItems: 'baseline',
      gap: 4
    }
  }, /*#__PURE__*/React.createElement("span", {
    style: big
  }, "5.46"), /*#__PURE__*/React.createElement("span", {
    style: unit
  }, "tok/s")), /*#__PURE__*/React.createElement("div", {
    style: {
      marginTop: 4,
      font: '400 var(--text-2xs)/1.3 var(--font-mono)',
      color: 'var(--text-muted)'
    }
  }, "Gemma 4 E2B \xB7 CPU \xB7 6 cores"))), /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'grid',
      gridTemplateColumns: '0.85fr 1.15fr',
      gap: 'var(--space-4)'
    }
  }, /*#__PURE__*/React.createElement(ProcessesCard, {
    store: store
  }), /*#__PURE__*/React.createElement(Card, {
    title: "System activity",
    subtitle: "live SSE records \xB7 keyed on kind_name",
    right: store.droppedPackets > 0 ? /*#__PURE__*/React.createElement(Badge, {
      tone: "warn"
    }, store.droppedPackets, " dropped") : /*#__PURE__*/React.createElement(Badge, {
      tone: "neutral"
    }, num(store.records.length)),
    padding: "none"
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      padding: '4px var(--space-3) var(--space-3)',
      maxHeight: 320,
      overflow: 'auto'
    }
  }, !rec && /*#__PURE__*/React.createElement("div", {
    style: {
      padding: 'var(--space-4)',
      font: '400 var(--text-sm)/1 var(--font-mono)',
      color: 'var(--text-muted)'
    }
  }, "waiting for first packet\u2026"), store.records.map((r, i) => {
    const meta = kindMeta[r.kind_name] || kindMeta.STATS;
    return /*#__PURE__*/React.createElement("div", {
      key: r.seq + '-' + i,
      style: {
        display: 'flex',
        gap: 12,
        alignItems: 'center',
        padding: 'var(--space-2) var(--space-2)',
        borderBottom: i < store.records.length - 1 ? '1px solid var(--border-hairline)' : 'none'
      }
    }, /*#__PURE__*/React.createElement("span", {
      style: {
        width: 22,
        height: 22,
        flex: 'none',
        borderRadius: 'var(--radius-sm)',
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'center',
        background: 'var(--surface-inset)',
        color: meta.color
      }
    }, /*#__PURE__*/React.createElement(Icon, {
      name: meta.icon,
      size: 12
    })), /*#__PURE__*/React.createElement("span", {
      style: {
        font: '500 var(--text-2xs)/1 var(--font-mono)',
        color: 'var(--text-secondary)',
        width: 46,
        textTransform: 'uppercase'
      }
    }, r.kind_name), /*#__PURE__*/React.createElement("span", {
      style: {
        font: '400 var(--text-xs)/1.3 var(--font-mono)',
        color: 'var(--text-muted)',
        flex: 1,
        overflow: 'hidden',
        textOverflow: 'ellipsis',
        whiteSpace: 'nowrap'
      }
    }, "seq ", r.seq, " \xB7 q=", num(r.q_total), r.kind_name === 'INFER' && r.last_text ? ' · “…' + r.last_text + '”' : ''), /*#__PURE__*/React.createElement("span", {
      style: {
        font: '400 var(--text-2xs)/1.6 var(--font-mono)',
        color: 'var(--text-muted)',
        flex: 'none'
      }
    }, fmtClock(r.recv_ts)));
  })))));
}

/* shared Processes card (used by Command Center + Routing) */
function ProcessesCard({
  store
}) {
  const {
    Card,
    StatusDot
  } = window.JarvisOSDesignSystem_e0065d;
  const {
    processStatuses
  } = window.JarvisShell;
  const rec = store.latest;
  const {
    procA,
    procB
  } = processStatuses(rec, store.live);
  const row = (name, sub, st) => /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      alignItems: 'center',
      gap: 12,
      padding: 'var(--space-3)',
      borderRadius: 'var(--radius-sm)'
    }
  }, /*#__PURE__*/React.createElement(StatusDot, {
    status: st.dot,
    pulse: st.pulse
  }), /*#__PURE__*/React.createElement("span", {
    style: {
      font: 'var(--weight-medium) var(--text-sm)/1 var(--font-mono)',
      color: 'var(--text-primary)',
      width: 96
    }
  }, name), /*#__PURE__*/React.createElement("span", {
    style: {
      font: '400 var(--text-xs)/1 var(--font-mono)',
      color: 'var(--text-secondary)',
      flex: 1
    }
  }, sub), /*#__PURE__*/React.createElement("span", {
    style: {
      font: '500 var(--text-2xs)/1 var(--font-mono)',
      textTransform: 'uppercase',
      color: st.color
    }
  }, st.label));
  return /*#__PURE__*/React.createElement(Card, {
    title: "Processes",
    subtitle: "single-process box \xB7 A rootserver + B inference",
    padding: "none"
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      padding: '4px var(--space-2) var(--space-2)'
    }
  }, row('Process A', 'seL4 rootserver', procA), row('Process B', rec ? rec.model_name + ' inference' : 'inference', procB), /*#__PURE__*/React.createElement("div", {
    style: {
      padding: '6px var(--space-3) var(--space-2)',
      font: '400 var(--text-2xs)/1.5 var(--font-mono)',
      color: 'var(--text-muted)'
    }
  }, "self-test 5/5 \u2014 2 of 5 are vacuous")));
}
window.CommandCenter = CommandCenter;
window.ProcessesCard = ProcessesCard;
})(); } catch (e) { __ds_ns.__errors.push({ path: "ui_kits/telemetry-console/ConsoleCommandCenter.jsx", error: String((e && e.message) || e) }); }

// ui_kits/telemetry-console/ConsoleModels.jsx
try { (() => {
/* JARVIS OS — Telemetry Console · Models
 * One live "Deployed model" card + the real historical bench-off table.
 * No tiers, no swaps — there is one model.
 */

function Models({
  store
}) {
  const {
    Card,
    Badge
  } = window.JarvisOSDesignSystem_e0065d;
  const {
    hasFlag
  } = window.JConsoleHelpers;
  const rec = store.latest;
  const loaded = hasFlag(rec, 'MODEL_LOADED');

  // historical bench-off (FINAL_SCORES.txt) — top 7 of 11 models / 6 families
  const bench = [{
    m: 'gemma-4-E2B',
    p: '4.65B',
    q: 8.40,
    sp: 19.7,
    drop: 'NO',
    deployed: true
  }, {
    m: 'mistral-7b-v0.2',
    p: '7.24B',
    q: 7.50,
    sp: 5.5,
    drop: 'YES'
  }, {
    m: 'gemma-4-E4B',
    p: '7.52B',
    q: 7.33,
    sp: 10.6,
    drop: 'NO'
  }, {
    m: 'Qwen3.5-9B',
    p: '8.95B',
    q: 7.26,
    sp: 6.6,
    drop: 'NO'
  }, {
    m: 'Llama-3.2-3B',
    p: '3.21B',
    q: 5.76,
    sp: 16.7,
    drop: 'YES'
  }, {
    m: 'Llama-3.2-1B',
    p: '1.24B',
    q: 5.37,
    sp: 40.4,
    drop: 'YES'
  }, {
    m: 'Phi-3-mini',
    p: '3.82B',
    q: 5.31,
    sp: 14.3,
    drop: 'MAYBE'
  }];
  const maxQ = 10;
  const stat = (label, value, sub) => /*#__PURE__*/React.createElement("div", null, /*#__PURE__*/React.createElement("div", {
    style: {
      font: 'var(--type-eyebrow)',
      letterSpacing: 'var(--tracking-wide)',
      textTransform: 'uppercase',
      color: 'var(--text-muted)',
      marginBottom: 6
    }
  }, label), /*#__PURE__*/React.createElement("div", {
    style: {
      font: 'var(--weight-semibold) var(--text-lg)/1.1 var(--font-mono)',
      color: 'var(--text-primary)'
    }
  }, value), sub && /*#__PURE__*/React.createElement("div", {
    style: {
      marginTop: 3,
      font: '400 var(--text-2xs)/1.3 var(--font-mono)',
      color: 'var(--text-muted)'
    }
  }, sub));
  return /*#__PURE__*/React.createElement("div", {
    style: {
      padding: 'var(--space-6)',
      overflow: 'auto',
      display: 'flex',
      flexDirection: 'column',
      gap: 'var(--space-5)'
    }
  }, /*#__PURE__*/React.createElement("div", null, /*#__PURE__*/React.createElement("div", {
    style: {
      font: 'var(--type-eyebrow)',
      letterSpacing: 'var(--tracking-caps)',
      textTransform: 'uppercase',
      color: 'var(--text-accent)',
      marginBottom: 8
    }
  }, "One model \xB7 no tiers"), /*#__PURE__*/React.createElement("h1", {
    style: {
      margin: 0,
      font: 'var(--weight-semibold) var(--text-2xl)/1.1 var(--font-display)',
      letterSpacing: 'var(--tracking-tight)',
      color: 'var(--text-primary)'
    }
  }, "Deployed model")), /*#__PURE__*/React.createElement(Card, {
    title: rec ? rec.model_name : 'Gemma 4 E2B',
    subtitle: "resident on the deployed box",
    right: loaded ? /*#__PURE__*/React.createElement(Badge, {
      tone: "ok",
      dot: true
    }, "LOADED") : /*#__PURE__*/React.createElement(Badge, {
      tone: "warn"
    }, rec ? (rec.model_load_pct || 0) + '%' : '—'),
    padding: "md"
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'grid',
      gridTemplateColumns: 'repeat(4, 1fr)',
      gap: 'var(--space-5)'
    }
  }, stat('Size', rec ? rec.model_size_mb + ' MB' : '2962 MB', '~2.89 GiB'), stat('Compute', rec ? 'CPU · ' + rec.num_nodes + ' cores' : 'CPU · 6 cores', 'NUM_NODES'), stat('Load', rec ? (rec.model_load_pct || 0) + '%' : '—', loaded ? 'MODEL_LOADED' : 'loading'), stat('Framebuffer', rec ? rec.fb_w + '×' + rec.fb_h : '1024×768', rec ? rec.fb_bpp + 'bpp' : '32bpp')), /*#__PURE__*/React.createElement("div", {
    style: {
      marginTop: 'var(--space-5)',
      paddingTop: 'var(--space-4)',
      borderTop: '1px solid var(--border-hairline)',
      font: '400 var(--text-sm)/1.5 var(--font-mono)',
      color: 'var(--text-secondary)'
    }
  }, "5.46 tok/s \u2014 deployed CPU benchmark (NUM_NODES=6), ", /*#__PURE__*/React.createElement("span", {
    style: {
      color: 'var(--text-muted)'
    }
  }, "not live"), ". Differs from the bench-off's 19.7 t/s, which is the ", /*#__PURE__*/React.createElement("em", null, "native dev engine"), ", not the deployed build.")), /*#__PURE__*/React.createElement(Card, {
    title: "Quality bench-off",
    subtitle: "historical \xB7 Apr 9 2026 \xB7 11 models / 6 families judged, top 7 shown",
    padding: "none"
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      padding: 'var(--space-2) var(--space-4) var(--space-4)'
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'grid',
      gridTemplateColumns: '160px 64px 1fr 110px 70px',
      gap: 12,
      alignItems: 'center',
      padding: 'var(--space-3) var(--space-2)',
      borderBottom: '1px solid var(--border-default)'
    }
  }, ['Model', 'Params', 'Quality', 'tg128 t/s · 2700X CPU', 'Drop-in'].map(h => /*#__PURE__*/React.createElement("span", {
    key: h,
    style: {
      font: 'var(--type-eyebrow)',
      letterSpacing: 'var(--tracking-wide)',
      textTransform: 'uppercase',
      color: 'var(--text-muted)'
    }
  }, h))), bench.map((b, i) => /*#__PURE__*/React.createElement("div", {
    key: b.m,
    style: {
      display: 'grid',
      gridTemplateColumns: '160px 64px 1fr 110px 70px',
      gap: 12,
      alignItems: 'center',
      padding: 'var(--space-3) var(--space-2)',
      borderBottom: i < bench.length - 1 ? '1px solid var(--border-hairline)' : 'none'
    }
  }, /*#__PURE__*/React.createElement("span", {
    style: {
      font: 'var(--weight-medium) var(--text-sm)/1 var(--font-mono)',
      color: b.deployed ? 'var(--text-accent)' : 'var(--text-primary)'
    }
  }, b.m, b.deployed ? ' ·' : ''), /*#__PURE__*/React.createElement("span", {
    style: {
      font: '400 var(--text-xs)/1 var(--font-mono)',
      color: 'var(--text-muted)'
    }
  }, b.p), /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      alignItems: 'center',
      gap: 8
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      flex: 1,
      height: 6,
      borderRadius: 3,
      background: 'var(--surface-inset)',
      overflow: 'hidden'
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      width: `${b.q / maxQ * 100}%`,
      height: '100%',
      background: b.deployed ? 'var(--accent)' : 'var(--gray-6)',
      borderRadius: 3
    }
  })), /*#__PURE__*/React.createElement("span", {
    style: {
      font: '500 var(--text-xs)/1 var(--font-mono)',
      color: 'var(--text-secondary)',
      width: 26
    }
  }, b.q.toFixed(2))), /*#__PURE__*/React.createElement("span", {
    style: {
      font: '400 var(--text-xs)/1 var(--font-mono)',
      color: 'var(--text-secondary)'
    }
  }, b.sp, " t/s"), /*#__PURE__*/React.createElement(Badge, {
    tone: b.drop === 'YES' ? 'ok' : b.drop === 'NO' ? 'neutral' : 'warn'
  }, b.drop))), /*#__PURE__*/React.createElement("div", {
    style: {
      marginTop: 10,
      font: '400 var(--text-2xs)/1.4 var(--font-mono)',
      color: 'var(--text-muted)'
    }
  }, "\xB7 = deployed model. Bench-off ran on a Ryzen 7 2700X (CPU, native engine). Deployed speed differs \u2014 see card above."))));
}
window.Models = Models;
})(); } catch (e) { __ds_ns.__errors.push({ path: "ui_kits/telemetry-console/ConsoleModels.jsx", error: String((e && e.message) || e) }); }

// ui_kits/telemetry-console/ConsoleShell.jsx
try { (() => {
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
  return m + 'm ' + s % 60 + 's ago';
}
function fmtClock(recvTs) {
  if (!recvTs) return '--:--:--';
  const d = new Date(recvTs * 1000);
  const p = n => String(n).padStart(2, '0');
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
    case 'live':
      return {
        label: 'link OK · live',
        tone: 'ok',
        dot: 'ok',
        pulse: true
      };
    case 'stale':
      return {
        label: 'stale · no packet',
        tone: 'warn',
        dot: 'warn',
        pulse: false
      };
    case 'crcfail':
      return {
        label: 'CRC FAIL',
        tone: 'err',
        dot: 'err',
        pulse: false
      };
    case 'disconnected':
      return {
        label: 'disconnected',
        tone: 'err',
        dot: 'idle',
        pulse: false
      };
    default:
      return {
        label: 'connecting…',
        tone: 'neutral',
        dot: 'idle',
        pulse: false
      };
  }
}
function hasFlag(rec, f) {
  return rec && rec.flags_list && rec.flags_list.indexOf(f) >= 0;
}
window.JConsoleHelpers = {
  fmtAgo,
  fmtClock,
  num,
  pct,
  connPill,
  hasFlag
};

/* ---- Icon (Lucide, isolated from React reconciliation) ------------------ */
function Icon({
  name,
  size = 18,
  style
}) {
  const ref = React.useRef(null);
  React.useEffect(() => {
    const el = ref.current;
    if (!el) return;
    el.innerHTML = '';
    const i = document.createElement('i');
    i.setAttribute('data-lucide', name);
    el.appendChild(i);
    if (window.lucide) window.lucide.createIcons();
    const svg = el.querySelector('svg');
    if (svg) {
      svg.setAttribute('width', size);
      svg.setAttribute('height', size);
      svg.style.strokeWidth = '1.75';
    }
  }, [name, size]);
  return /*#__PURE__*/React.createElement("span", {
    ref: ref,
    style: {
      width: size,
      height: size,
      display: 'inline-flex',
      flex: 'none',
      ...style
    }
  });
}
function Logo({
  size = 26
}) {
  return /*#__PURE__*/React.createElement("div", {
    style: {
      width: size,
      height: size,
      borderRadius: 'var(--radius-sm)',
      background: 'var(--accent)',
      display: 'flex',
      alignItems: 'center',
      justifyContent: 'center',
      boxShadow: 'var(--glow-accent)',
      flex: 'none'
    }
  }, /*#__PURE__*/React.createElement("span", {
    style: {
      font: 'var(--weight-bold) 15px/1 var(--font-display)',
      color: '#fff'
    }
  }, "J"));
}
const RAIL_ITEMS = [{
  id: 'command',
  icon: 'layout-dashboard',
  label: 'Command Center'
}, {
  id: 'last',
  icon: 'message-square',
  label: 'Last response'
}, {
  id: 'routing',
  icon: 'split',
  label: 'Routing'
}, {
  id: 'shield',
  icon: 'shield',
  label: 'SHIELD'
},
// neutral icon — passive
{
  id: 'models',
  icon: 'cpu',
  label: 'Models'
}];
function Rail({
  view,
  setView
}) {
  return /*#__PURE__*/React.createElement("nav", {
    style: {
      width: 'var(--rail-w)',
      flex: 'none',
      background: 'var(--bg-canvas)',
      borderRight: '1px solid var(--border-hairline)',
      display: 'flex',
      flexDirection: 'column',
      alignItems: 'center',
      padding: 'var(--space-3) 0',
      gap: 'var(--space-4)'
    }
  }, /*#__PURE__*/React.createElement(Logo, null), /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      flexDirection: 'column',
      gap: 4,
      marginTop: 4
    }
  }, RAIL_ITEMS.map(it => {
    const on = it.id === view;
    return /*#__PURE__*/React.createElement("button", {
      key: it.id,
      title: it.label,
      onClick: () => setView(it.id),
      style: {
        position: 'relative',
        width: 38,
        height: 38,
        border: 'none',
        cursor: 'pointer',
        borderRadius: 'var(--radius-md)',
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'center',
        background: on ? 'var(--surface-inset)' : 'transparent',
        color: on ? 'var(--text-accent)' : 'var(--text-muted)',
        transition: 'color var(--dur-fast), background var(--dur-fast)'
      }
    }, on && /*#__PURE__*/React.createElement("span", {
      style: {
        position: 'absolute',
        left: -10,
        top: 9,
        width: 3,
        height: 20,
        borderRadius: '0 3px 3px 0',
        background: 'var(--accent)'
      }
    }), /*#__PURE__*/React.createElement(Icon, {
      name: it.icon
    }));
  })), /*#__PURE__*/React.createElement("div", {
    style: {
      marginTop: 'auto',
      display: 'flex',
      flexDirection: 'column',
      gap: 10,
      alignItems: 'center'
    }
  }, /*#__PURE__*/React.createElement("div", {
    title: "Read-only telemetry console",
    style: {
      width: 38,
      height: 38,
      display: 'flex',
      alignItems: 'center',
      justifyContent: 'center',
      color: 'var(--text-muted)'
    }
  }, /*#__PURE__*/React.createElement(Icon, {
    name: "eye"
  })), /*#__PURE__*/React.createElement("div", {
    style: {
      width: 30,
      height: 30,
      borderRadius: '999px',
      background: 'var(--surface-inset)',
      border: '1px solid var(--border-strong)',
      display: 'flex',
      alignItems: 'center',
      justifyContent: 'center',
      font: 'var(--weight-semibold) 12px/1 var(--font-mono)',
      color: 'var(--text-secondary)'
    }
  }, "RO")));
}

/* ---- Processes sidebar (the TWO real processes) ------------------------- */
function ProcessRow({
  name,
  sub,
  status
}) {
  const {
    StatusDot
  } = window.JarvisOSDesignSystem_e0065d;
  return /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      alignItems: 'center',
      gap: 10,
      padding: 'var(--space-2) var(--space-3)',
      borderRadius: 'var(--radius-sm)'
    }
  }, /*#__PURE__*/React.createElement(StatusDot, {
    status: status.dot,
    pulse: status.pulse
  }), /*#__PURE__*/React.createElement("div", {
    style: {
      minWidth: 0,
      flex: 1
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      font: 'var(--weight-medium) var(--text-sm)/1.2 var(--font-mono)',
      color: 'var(--text-primary)'
    }
  }, name), /*#__PURE__*/React.createElement("div", {
    style: {
      font: '400 var(--text-2xs)/1.4 var(--font-mono)',
      color: 'var(--text-muted)'
    }
  }, sub)), /*#__PURE__*/React.createElement("span", {
    style: {
      font: '500 var(--text-2xs)/1 var(--font-mono)',
      textTransform: 'uppercase',
      letterSpacing: '0.06em',
      color: status.color
    }
  }, status.label));
}
function processStatuses(rec, live) {
  const {
    hasFlag
  } = window.JConsoleHelpers;
  const C = {
    ok: 'var(--status-ok)',
    warn: 'var(--status-warn)',
    err: 'var(--status-err)',
    muted: 'var(--text-muted)'
  };
  if (!rec) {
    const idle = {
      dot: 'idle',
      pulse: false,
      label: '—',
      color: C.muted
    };
    return {
      procA: idle,
      procB: idle
    };
  }
  const err = hasFlag(rec, 'HAS_ERROR') || (Number(rec.q_errors) || 0) > 0;
  const selftest = hasFlag(rec, 'SELFTEST_PASS');
  const loaded = hasFlag(rec, 'MODEL_LOADED');
  const procA = err ? {
    dot: 'err',
    pulse: false,
    label: 'error',
    color: C.err
  } : {
    dot: 'ok',
    pulse: live,
    label: selftest ? 'self-test 5/5*' : 'running',
    color: C.ok
  };
  const procB = loaded ? {
    dot: 'ok',
    pulse: live,
    label: 'loaded',
    color: C.ok
  } : {
    dot: 'warn',
    pulse: false,
    label: (rec.model_load_pct || 0) + '%',
    color: C.warn
  };
  return {
    procA,
    procB
  };
}
function Sidebar({
  view,
  store
}) {
  const {
    num
  } = window.JConsoleHelpers;
  const rec = store.latest;
  const {
    procA,
    procB
  } = processStatuses(rec, store.live);
  const isLast = view === 'last';
  return /*#__PURE__*/React.createElement("aside", {
    style: {
      width: 'var(--sidebar-w)',
      flex: 'none',
      background: 'var(--bg-app)',
      borderRight: '1px solid var(--border-hairline)',
      display: 'flex',
      flexDirection: 'column'
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      padding: 'var(--space-4) var(--space-4) var(--space-3)',
      display: 'flex',
      alignItems: 'center',
      justifyContent: 'space-between'
    }
  }, /*#__PURE__*/React.createElement("span", {
    style: {
      font: 'var(--type-eyebrow)',
      letterSpacing: 'var(--tracking-caps)',
      textTransform: 'uppercase',
      color: 'var(--text-muted)'
    }
  }, isLast ? 'Last response' : 'Processes')), /*#__PURE__*/React.createElement("div", {
    style: {
      flex: 1,
      overflow: 'auto',
      padding: '0 var(--space-2) var(--space-3)'
    }
  }, !isLast && /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      flexDirection: 'column',
      gap: 2
    }
  }, /*#__PURE__*/React.createElement(ProcessRow, {
    name: "Process A",
    sub: "rootserver \xB7 seL4",
    status: procA
  }), /*#__PURE__*/React.createElement(ProcessRow, {
    name: "Process B",
    sub: rec ? rec.model_name + ' inference' : 'inference',
    status: procB
  }), /*#__PURE__*/React.createElement("div", {
    style: {
      padding: '6px var(--space-3) 0',
      font: '400 var(--text-2xs)/1.5 var(--font-mono)',
      color: 'var(--text-muted)'
    }
  }, "*2 of 5 self-tests are vacuous")), isLast && /*#__PURE__*/React.createElement("div", {
    style: {
      padding: 'var(--space-2) var(--space-3)',
      display: 'flex',
      flexDirection: 'column',
      gap: 14
    }
  }, /*#__PURE__*/React.createElement("div", null, /*#__PURE__*/React.createElement("div", {
    style: {
      font: '400 var(--text-2xs)/1.5 var(--font-mono)',
      color: 'var(--text-muted)',
      marginBottom: 6
    }
  }, "truncated tail (\u226455 chars)"), /*#__PURE__*/React.createElement("div", {
    style: {
      font: '400 var(--text-sm)/1.45 var(--font-sans)',
      color: rec && rec.last_text ? 'var(--text-secondary)' : 'var(--text-muted)'
    }
  }, rec && rec.last_text ? '“…' + rec.last_text + '”' : 'No inference response yet')), /*#__PURE__*/React.createElement("div", {
    style: {
      borderTop: '1px solid var(--border-hairline)',
      paddingTop: 12,
      display: 'flex',
      flexDirection: 'column',
      gap: 6
    }
  }, [['q_total', rec && rec.q_total], ['q_infer', rec && rec.q_infer], ['q_hits', rec && rec.q_hits], ['q_errors', rec && rec.q_errors]].map(([k, v]) => /*#__PURE__*/React.createElement("div", {
    key: k,
    style: {
      display: 'flex',
      justifyContent: 'space-between',
      font: '400 var(--text-xs)/1 var(--font-mono)'
    }
  }, /*#__PURE__*/React.createElement("span", {
    style: {
      color: 'var(--text-muted)'
    }
  }, k), /*#__PURE__*/React.createElement("span", {
    style: {
      color: k === 'q_errors' && (Number(v) || 0) > 0 ? 'var(--status-err)' : 'var(--text-primary)'
    }
  }, rec ? num(v) : '—')))))), /*#__PURE__*/React.createElement("div", {
    style: {
      padding: 'var(--space-3) var(--space-4)',
      borderTop: '1px solid var(--border-hairline)',
      display: 'flex',
      alignItems: 'center',
      justifyContent: 'space-between'
    }
  }, /*#__PURE__*/React.createElement("span", {
    style: {
      font: '400 var(--text-2xs)/1 var(--font-mono)',
      color: 'var(--text-muted)'
    }
  }, "seL4 x86 \xB7 Phase 4 \xB7 boot ", rec ? rec.boot_id : '—')));
}

/* ---- Topbar: honest model badge + connection pill ----------------------- */
function Topbar({
  title,
  store
}) {
  const {
    Badge,
    StatusDot
  } = window.JarvisOSDesignSystem_e0065d;
  const {
    connPill,
    hasFlag
  } = window.JConsoleHelpers;
  const rec = store.latest;
  const pill = connPill(store);
  const loaded = hasFlag(rec, 'MODEL_LOADED');
  const modelLabel = !rec ? 'no telemetry' : loaded ? rec.model_name + ' · LOADED' : 'loading ' + (rec.model_load_pct || 0) + '%';
  return /*#__PURE__*/React.createElement("header", {
    style: {
      height: 'var(--topbar-h)',
      flex: 'none',
      display: 'flex',
      alignItems: 'center',
      gap: 'var(--space-4)',
      padding: '0 var(--space-5)',
      borderBottom: '1px solid var(--border-hairline)',
      background: 'var(--bg-app)'
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      alignItems: 'center',
      gap: 8
    }
  }, /*#__PURE__*/React.createElement("span", {
    style: {
      font: '400 var(--text-xs)/1 var(--font-mono)',
      color: 'var(--text-muted)'
    }
  }, "telemetry"), /*#__PURE__*/React.createElement(Icon, {
    name: "chevron-right",
    size: 13,
    style: {
      color: 'var(--text-muted)'
    }
  }), /*#__PURE__*/React.createElement("span", {
    style: {
      font: 'var(--weight-semibold) var(--text-md)/1 var(--font-sans)',
      color: 'var(--text-primary)'
    }
  }, title)), /*#__PURE__*/React.createElement("div", {
    style: {
      flex: 1
    }
  }), store.simulated && /*#__PURE__*/React.createElement(Badge, {
    tone: "warn"
  }, "SIMULATED \xB7 replay"), /*#__PURE__*/React.createElement(Badge, {
    tone: loaded ? 'accent' : 'neutral',
    dot: loaded
  }, modelLabel), /*#__PURE__*/React.createElement(Badge, {
    tone: "neutral"
  }, "functional-unverified"), /*#__PURE__*/React.createElement("span", {
    style: {
      display: 'inline-flex',
      alignItems: 'center',
      gap: 7,
      height: 24,
      padding: '0 10px',
      borderRadius: 'var(--radius-full)',
      border: '1px solid var(--border-default)',
      background: 'var(--surface-inset)'
    }
  }, /*#__PURE__*/React.createElement(StatusDot, {
    status: pill.dot,
    pulse: pill.pulse
  }), /*#__PURE__*/React.createElement("span", {
    style: {
      font: '500 var(--text-2xs)/1 var(--font-mono)',
      textTransform: 'uppercase',
      letterSpacing: '0.06em',
      color: pill.tone === 'ok' ? 'var(--status-ok)' : pill.tone === 'warn' ? 'var(--status-warn)' : pill.tone === 'err' ? 'var(--status-err)' : 'var(--text-muted)'
    }
  }, pill.label)));
}
window.JarvisShell = {
  Icon,
  Logo,
  Rail,
  Sidebar,
  Topbar,
  processStatuses
};
})(); } catch (e) { __ds_ns.__errors.push({ path: "ui_kits/telemetry-console/ConsoleShell.jsx", error: String((e && e.message) || e) }); }

// ui_kits/telemetry-console/ConsoleShield.jsx
try { (() => {
/* JARVIS OS — Telemetry Console · SHIELD (passive monitoring)
 * The ONLY live SHIELD datum is q_shield = a passive CHECK COUNT.
 * No risk ladder, no recent-actions feed, no blocked-action icons.
 */

function Shield({
  store
}) {
  const {
    Card,
    Badge
  } = window.JarvisOSDesignSystem_e0065d;
  const {
    num,
    fmtAgo
  } = window.JConsoleHelpers;
  const rec = store.latest;
  const qShield = rec ? Number(rec.q_shield) || 0 : 0;
  const qErr = rec ? Number(rec.q_errors) || 0 : 0;
  const pillars = [['S', 'Staged execution sandbox'], ['H', 'Hardware impact analysis'], ['I', 'Irreversibility detection'], ['E', 'Escalation intelligence'], ['L', 'Learning from failures'], ['D', 'Deterministic rollback']];
  return /*#__PURE__*/React.createElement("div", {
    style: {
      padding: 'var(--space-6)',
      overflow: 'auto',
      display: 'flex',
      flexDirection: 'column',
      gap: 'var(--space-5)'
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      alignItems: 'flex-end',
      justifyContent: 'space-between'
    }
  }, /*#__PURE__*/React.createElement("div", null, /*#__PURE__*/React.createElement("div", {
    style: {
      font: 'var(--type-eyebrow)',
      letterSpacing: 'var(--tracking-caps)',
      textTransform: 'uppercase',
      color: 'var(--text-muted)',
      marginBottom: 8
    }
  }, "Safety framework \xB7 passive monitoring (SEC-039)"), /*#__PURE__*/React.createElement("h1", {
    style: {
      margin: 0,
      font: 'var(--weight-semibold) var(--text-2xl)/1.1 var(--font-display)',
      letterSpacing: 'var(--tracking-tight)',
      color: 'var(--text-primary)'
    }
  }, "SHIELD")), /*#__PURE__*/React.createElement(Badge, {
    tone: "neutral"
  }, "passive \xB7 not a blocker")), /*#__PURE__*/React.createElement(Card, {
    title: "SHIELD checks",
    subtitle: "passive monitoring count \u2014 the box always ALLOWs",
    padding: "md"
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      alignItems: 'flex-end',
      gap: 'var(--space-8)'
    }
  }, /*#__PURE__*/React.createElement("div", null, /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      alignItems: 'baseline',
      gap: 6
    }
  }, /*#__PURE__*/React.createElement("span", {
    style: {
      font: 'var(--weight-semibold) var(--text-4xl)/1 var(--font-display)',
      color: 'var(--text-primary)'
    }
  }, rec ? num(qShield) : '—'), /*#__PURE__*/React.createElement("span", {
    style: {
      font: '400 var(--text-sm)/1 var(--font-mono)',
      color: 'var(--text-muted)'
    }
  }, "q_shield")), /*#__PURE__*/React.createElement("div", {
    style: {
      marginTop: 6,
      font: '400 var(--text-2xs)/1.3 var(--font-mono)',
      color: 'var(--text-muted)'
    }
  }, "standalone check count \xB7 not a fraction of q_total")), /*#__PURE__*/React.createElement("div", {
    style: {
      borderLeft: '1px solid var(--border-hairline)',
      paddingLeft: 'var(--space-6)',
      display: 'flex',
      gap: 'var(--space-8)'
    }
  }, /*#__PURE__*/React.createElement("div", null, /*#__PURE__*/React.createElement("div", {
    style: {
      font: 'var(--type-eyebrow)',
      letterSpacing: 'var(--tracking-wide)',
      textTransform: 'uppercase',
      color: 'var(--text-muted)',
      marginBottom: 6
    }
  }, "Errors"), /*#__PURE__*/React.createElement("div", {
    style: {
      font: 'var(--weight-semibold) var(--text-xl)/1 var(--font-mono)',
      color: qErr > 0 ? 'var(--status-err)' : 'var(--text-primary)'
    }
  }, rec ? num(qErr) : '—')), /*#__PURE__*/React.createElement("div", null, /*#__PURE__*/React.createElement("div", {
    style: {
      font: 'var(--type-eyebrow)',
      letterSpacing: 'var(--tracking-wide)',
      textTransform: 'uppercase',
      color: 'var(--text-muted)',
      marginBottom: 6
    }
  }, "Last packet"), /*#__PURE__*/React.createElement("div", {
    style: {
      font: 'var(--weight-semibold) var(--text-xl)/1 var(--font-mono)',
      color: 'var(--text-secondary)'
    }
  }, rec ? fmtAgo(rec.recv_ts) : '—'))))), /*#__PURE__*/React.createElement(Card, {
    title: "Framework (designed, not enforced)",
    padding: "md"
  }, /*#__PURE__*/React.createElement("p", {
    style: {
      margin: '0 0 16px',
      font: '400 var(--text-sm)/1.55 var(--font-sans)',
      color: 'var(--text-secondary)',
      maxWidth: 760
    }
  }, "The full S.H.I.E.L.D. framework is designed in ", /*#__PURE__*/React.createElement("code", {
    style: {
      font: '400 var(--text-xs) var(--font-mono)',
      color: 'var(--text-accent)'
    }
  }, "shield_framework.py"), ", but it is ", /*#__PURE__*/React.createElement("strong", null, "not linked into the deployed build"), " (", /*#__PURE__*/React.createElement("code", {
    style: {
      font: '400 var(--text-xs) var(--font-mono)',
      color: 'var(--text-accent)'
    }
  }, "shield.c"), " absent). The running box performs passive checks and always returns ALLOW \u2014 there is no risk scoring, no gating, and no blocked actions on this system."), /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      flexWrap: 'wrap',
      gap: 10
    }
  }, pillars.map(([k, name]) => /*#__PURE__*/React.createElement("div", {
    key: k,
    style: {
      display: 'flex',
      alignItems: 'center',
      gap: 10,
      padding: '8px 12px',
      border: '1px dashed var(--border-strong)',
      borderRadius: 'var(--radius-md)',
      opacity: 0.6
    }
  }, /*#__PURE__*/React.createElement("span", {
    style: {
      font: 'var(--weight-bold) var(--text-lg)/1 var(--font-display)',
      color: 'var(--text-muted)'
    }
  }, k), /*#__PURE__*/React.createElement("span", {
    style: {
      font: '400 var(--text-xs)/1.3 var(--font-sans)',
      color: 'var(--text-muted)'
    }
  }, name))))));
}
window.Shield = Shield;
})(); } catch (e) { __ds_ns.__errors.push({ path: "ui_kits/telemetry-console/ConsoleShield.jsx", error: String((e && e.message) || e) }); }

// ui_kits/telemetry-console/LastResponse.jsx
try { (() => {
/* JARVIS OS — Telemetry Console · Last Response (read-only) */

function LastResponse({
  store
}) {
  const {
    Badge,
    StatusDot
  } = window.JarvisOSDesignSystem_e0065d;
  const {
    num,
    fmtAgo
  } = window.JConsoleHelpers;
  const rec = store.latest;
  const hasText = rec && rec.last_text;
  return /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      flexDirection: 'column',
      height: '100%'
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      flex: 'none',
      padding: 'var(--space-4) var(--space-6)',
      borderBottom: '1px solid var(--border-hairline)',
      display: 'flex',
      alignItems: 'center',
      gap: 14
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      width: 30,
      height: 30,
      flex: 'none',
      borderRadius: 'var(--radius-sm)',
      background: 'var(--accent)',
      display: 'flex',
      alignItems: 'center',
      justifyContent: 'center',
      boxShadow: 'var(--glow-accent)'
    }
  }, /*#__PURE__*/React.createElement("span", {
    style: {
      font: 'var(--weight-bold) 13px/1 var(--font-display)',
      color: '#fff'
    }
  }, "J")), /*#__PURE__*/React.createElement("div", {
    style: {
      flex: 1
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      font: 'var(--weight-semibold) var(--text-md)/1.1 var(--font-sans)',
      color: 'var(--text-primary)'
    }
  }, rec ? rec.model_name : 'JARVIS'), /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      alignItems: 'center',
      gap: 8,
      marginTop: 3
    }
  }, /*#__PURE__*/React.createElement(StatusDot, {
    status: store.live ? 'live' : 'idle',
    pulse: store.live,
    label: store.live ? 'live' : store.connState
  }))), /*#__PURE__*/React.createElement(Badge, {
    tone: "neutral"
  }, "SHIELD: passive \xB7 ", rec ? num(rec.q_shield) : '—', " checks")), /*#__PURE__*/React.createElement("div", {
    style: {
      flex: 1,
      overflow: 'auto',
      padding: 'var(--space-8) 0'
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      maxWidth: 720,
      margin: '0 auto',
      padding: '0 var(--space-6)'
    }
  }, hasText ? /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      gap: 14
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      width: 30,
      height: 30,
      flex: 'none',
      borderRadius: 'var(--radius-sm)',
      background: 'var(--accent)',
      display: 'flex',
      alignItems: 'center',
      justifyContent: 'center',
      boxShadow: 'var(--glow-accent)'
    }
  }, /*#__PURE__*/React.createElement("span", {
    style: {
      font: 'var(--weight-bold) 13px/1 var(--font-display)',
      color: '#fff'
    }
  }, "J")), /*#__PURE__*/React.createElement("div", {
    style: {
      flex: 1
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      font: '400 var(--text-lg)/1.55 var(--font-sans)',
      color: 'var(--text-primary)'
    }
  }, "\u2026", rec.last_text), /*#__PURE__*/React.createElement("div", {
    style: {
      marginTop: 10,
      display: 'flex',
      gap: 14,
      font: '400 var(--text-2xs)/1 var(--font-mono)',
      color: 'var(--text-muted)'
    }
  }, /*#__PURE__*/React.createElement("span", null, "truncated last-response tail, \u226455 chars"), /*#__PURE__*/React.createElement("span", null, "\xB7 updated ", fmtAgo(rec.recv_ts))))) : /*#__PURE__*/React.createElement("div", {
    style: {
      textAlign: 'center',
      padding: 'var(--space-12) 0',
      color: 'var(--text-muted)'
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      font: '400 var(--text-lg)/1.4 var(--font-sans)'
    }
  }, "No inference response yet"), /*#__PURE__*/React.createElement("div", {
    style: {
      marginTop: 8,
      font: '400 var(--text-sm)/1.4 var(--font-mono)'
    }
  }, "last_text is empty until the box reports an INFER packet")), rec && /*#__PURE__*/React.createElement("div", {
    style: {
      marginTop: 'var(--space-8)',
      display: 'flex',
      gap: 18,
      flexWrap: 'wrap',
      padding: 'var(--space-4)',
      border: '1px solid var(--border-hairline)',
      borderRadius: 'var(--radius-md)',
      background: 'var(--surface-card)'
    }
  }, [['per-inference tokens', 'not measured in deploy (infer_gen_tokens=0)'], ['RAM usage', 'no source (total_ram_mb=0)'], ['decode speed', '5.46 tok/s — deployed benchmark, not live']].map(([k, v]) => /*#__PURE__*/React.createElement("div", {
    key: k,
    style: {
      minWidth: 180
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      font: 'var(--type-eyebrow)',
      letterSpacing: 'var(--tracking-wide)',
      textTransform: 'uppercase',
      color: 'var(--text-muted)',
      marginBottom: 4
    }
  }, k), /*#__PURE__*/React.createElement("div", {
    style: {
      font: '400 var(--text-xs)/1.4 var(--font-mono)',
      color: 'var(--text-secondary)'
    }
  }, v)))))), /*#__PURE__*/React.createElement("div", {
    style: {
      flex: 'none',
      padding: 'var(--space-4) var(--space-6)',
      borderTop: '1px solid var(--border-hairline)',
      font: '400 var(--text-xs)/1.5 var(--font-mono)',
      color: 'var(--text-muted)',
      textAlign: 'center'
    }
  }, "Read-only telemetry console (telemetry-OUT). Inbound queries are control-IN, deferred to ~Phase 6."));
}
window.LastResponse = LastResponse;
})(); } catch (e) { __ds_ns.__errors.push({ path: "ui_kits/telemetry-console/LastResponse.jsx", error: String((e && e.message) || e) }); }

// ui_kits/telemetry-console/Routing.jsx
try { (() => {
/* JARVIS OS — Telemetry Console · Routing view
 * Live route split = aggregate ratio only (no per-query routing).
 * The 4 specialists appear ONLY as a static roadmap diagram — not live in v1.0.
 */

function Routing({
  store
}) {
  const {
    Card,
    Badge
  } = window.JarvisOSDesignSystem_e0065d;
  const {
    Icon
  } = window.JarvisShell;
  const {
    num,
    pct
  } = window.JConsoleHelpers;
  const rec = store.latest;
  const qTotal = rec ? Number(rec.q_total) || 0 : 0;
  const splits = qTotal === 0 ? null : [{
    k: 'q_hits',
    label: 'Cache hit',
    v: rec.q_hits,
    p: pct(rec.q_hits, rec.q_total),
    color: 'var(--status-ok)'
  }, {
    k: 'q_infer',
    label: '→ LLM (inference)',
    v: rec.q_infer,
    p: pct(rec.q_infer, rec.q_total),
    color: 'var(--text-accent)'
  }, {
    k: 'q_heartbeat',
    label: 'Heartbeat',
    v: rec.q_heartbeat,
    p: pct(rec.q_heartbeat, rec.q_total),
    color: 'var(--text-secondary)'
  }, {
    k: 'q_shield',
    label: 'SHIELD check (passive)',
    v: rec.q_shield,
    p: pct(rec.q_shield, rec.q_total),
    color: 'var(--status-warn)'
  }];
  const specialists = [{
    name: 'device-manager',
    icon: 'cpu',
    domain: 'Hardware, disk, memory, thermal'
  }, {
    name: 'network',
    icon: 'wifi',
    domain: 'Connectivity, ping, ports, routes'
  }, {
    name: 'filesystem',
    icon: 'folder-tree',
    domain: 'Files, directories, permissions'
  }, {
    name: 'user-interaction',
    icon: 'message-square',
    domain: 'Help, clarify, general queries'
  }];
  return /*#__PURE__*/React.createElement("div", {
    style: {
      padding: 'var(--space-6)',
      overflow: 'auto',
      display: 'flex',
      flexDirection: 'column',
      gap: 'var(--space-5)'
    }
  }, /*#__PURE__*/React.createElement("div", null, /*#__PURE__*/React.createElement("div", {
    style: {
      font: 'var(--type-eyebrow)',
      letterSpacing: 'var(--tracking-caps)',
      textTransform: 'uppercase',
      color: 'var(--text-accent)',
      marginBottom: 8
    }
  }, "Aggregate routing \xB7 live"), /*#__PURE__*/React.createElement("h1", {
    style: {
      margin: 0,
      font: 'var(--weight-semibold) var(--text-2xl)/1.1 var(--font-display)',
      letterSpacing: 'var(--tracking-tight)',
      color: 'var(--text-primary)'
    }
  }, "Routing")), /*#__PURE__*/React.createElement(Card, {
    title: "Workload split",
    subtitle: "aggregate counters \u2014 not per-query routing",
    right: /*#__PURE__*/React.createElement(Badge, {
      tone: "neutral"
    }, rec ? num(qTotal) + ' total' : '—'),
    padding: "md"
  }, qTotal === 0 ? /*#__PURE__*/React.createElement("div", {
    style: {
      font: '400 var(--text-sm)/1.4 var(--font-mono)',
      color: 'var(--text-muted)'
    }
  }, "no queries yet") : /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      flexDirection: 'column',
      gap: 14
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      height: 14,
      borderRadius: 'var(--radius-xs)',
      overflow: 'hidden',
      border: '1px solid var(--border-hairline)'
    }
  }, splits.map(s => /*#__PURE__*/React.createElement("span", {
    key: s.k,
    style: {
      width: s.p + '%',
      background: s.color
    },
    title: s.label
  }))), /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'grid',
      gridTemplateColumns: 'repeat(2, 1fr)',
      gap: 10
    }
  }, splits.map(s => /*#__PURE__*/React.createElement("div", {
    key: s.k,
    style: {
      display: 'flex',
      alignItems: 'center',
      gap: 10
    }
  }, /*#__PURE__*/React.createElement("span", {
    style: {
      width: 9,
      height: 9,
      borderRadius: 9,
      background: s.color,
      flex: 'none'
    }
  }), /*#__PURE__*/React.createElement("span", {
    style: {
      font: '400 var(--text-sm)/1 var(--font-sans)',
      color: 'var(--text-secondary)',
      flex: 1
    }
  }, s.label), /*#__PURE__*/React.createElement("span", {
    style: {
      font: '500 var(--text-sm)/1 var(--font-mono)',
      color: 'var(--text-primary)'
    }
  }, s.p.toFixed(1), "%"), /*#__PURE__*/React.createElement("span", {
    style: {
      font: '400 var(--text-xs)/1 var(--font-mono)',
      color: 'var(--text-muted)',
      width: 64,
      textAlign: 'right'
    }
  }, num(s.v))))))), /*#__PURE__*/React.createElement(ProcessesCard, {
    store: store
  }), /*#__PURE__*/React.createElement("div", {
    style: {
      marginTop: 'var(--space-2)'
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      alignItems: 'center',
      gap: 10,
      marginBottom: 12
    }
  }, /*#__PURE__*/React.createElement("span", {
    style: {
      font: 'var(--type-eyebrow)',
      letterSpacing: 'var(--tracking-caps)',
      textTransform: 'uppercase',
      color: 'var(--text-muted)'
    }
  }, "Architecture (roadmap)"), /*#__PURE__*/React.createElement(Badge, {
    tone: "neutral"
  }, "not live in v1.0")), /*#__PURE__*/React.createElement("p", {
    style: {
      margin: '0 0 14px',
      font: '400 var(--text-sm)/1.5 var(--font-sans)',
      color: 'var(--text-muted)',
      maxWidth: 720
    }
  }, "The deployed box is single-process (A rootserver + B inference). The four specialist agents below are a design from the Phase-1 multi-agent work \u2014 they are ", /*#__PURE__*/React.createElement("strong", null, "not running"), " in the deployed build and emit no telemetry. Shown for context only."), /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'grid',
      gridTemplateColumns: 'repeat(4, 1fr)',
      gap: 'var(--space-3)',
      opacity: 0.5
    }
  }, specialists.map(s => /*#__PURE__*/React.createElement("div", {
    key: s.name,
    style: {
      border: '1px dashed var(--border-strong)',
      borderRadius: 'var(--radius-lg)',
      padding: 'var(--space-4)',
      background: 'transparent'
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      alignItems: 'center',
      gap: 8,
      marginBottom: 8
    }
  }, /*#__PURE__*/React.createElement("span", {
    style: {
      color: 'var(--text-muted)'
    }
  }, /*#__PURE__*/React.createElement(Icon, {
    name: s.icon,
    size: 15
  })), /*#__PURE__*/React.createElement("span", {
    style: {
      font: 'var(--weight-medium) var(--text-sm)/1 var(--font-mono)',
      color: 'var(--text-secondary)'
    }
  }, s.name)), /*#__PURE__*/React.createElement("div", {
    style: {
      font: '400 var(--text-xs)/1.4 var(--font-sans)',
      color: 'var(--text-muted)'
    }
  }, s.domain))))));
}
window.Routing = Routing;
})(); } catch (e) { __ds_ns.__errors.push({ path: "ui_kits/telemetry-console/Routing.jsx", error: String((e && e.message) || e) }); }

// ui_kits/telemetry-console/telemetry.js
try { (() => {
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
  const STALE_MS = 2800; // no fresh CRC-valid packet within this => stale
  const MAX_RECORDS = 60; // activity feed ring
  const RATE_BUF = 30; // rolling buffer length for the q/s sparkline

  const listeners = new Set();
  const state = {
    connState: 'connecting',
    // connecting | live | stale | crcfail | disconnected
    simulated: false,
    // true when the built-in replay sim is the source
    latest: null,
    // last CRC-valid record (for display)
    lastAny: null,
    // last record regardless of crc (to surface CRC FAIL)
    records: [],
    // recent records (newest first) for the activity feed
    droppedPackets: 0,
    // cumulative seq-gap total
    lastSeq: null,
    lastArrival: 0,
    // client-clock ms of last fresh CRC-valid arrival
    rateBuf: [] // [{t, q_total}] rolling buffer for queries/sec
  };
  function emit() {
    listeners.forEach(fn => fn(snapshot()));
  }
  function snapshot() {
    return {
      connState: state.connState,
      simulated: state.simulated,
      latest: state.latest,
      lastAny: state.lastAny,
      records: state.records.slice(),
      droppedPackets: state.droppedPackets,
      rateSamples: state.rateBuf.slice(),
      live: state.connState === 'live'
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
    state.rateBuf.push({
      t: rec.recv_ts || Date.now() / 1000,
      q: Number(rec.q_total) || 0
    });
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
    const first = b[0],
      last = b[b.length - 1];
    const dt = last.t - first.t;
    if (dt <= 0) return null;
    return Math.max(0, (last.q - first.q) / dt);
  }

  // ---- public API ----------------------------------------------------------
  const API = {
    subscribe(fn) {
      listeners.add(fn);
      fn(snapshot());
      return () => listeners.delete(fn);
    },
    getState: snapshot,
    queriesPerSec,
    connect(url) {
      startEventSource(url || '/events');
    },
    startSimulator
  };
  function startEventSource(url) {
    let opened = false;
    let es;
    try {
      es = new EventSource(url);
    } catch (e) {
      startSimulator();
      return;
    }
    const failTimer = setTimeout(() => {
      if (!opened) {
        es.close();
        startSimulator();
      }
    }, 1500);
    es.onopen = () => {
      opened = true;
      clearTimeout(failTimer);
      state.simulated = false;
    };
    es.onmessage = ev => {
      try {
        ingest(JSON.parse(ev.data));
      } catch (_) {/* ignore non-JSON keepalive */}
    };
    es.onerror = () => {
      clearTimeout(failTimer);
      if (!opened) {
        es.close();
        startSimulator();
      } // no server => preview sim
      else {
        state.connState = 'disconnected';
        emit();
      } // stream dropped
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
    const responses = ['the capital of France is Paris.', 'free space on the root volume is 148 GB.', 'memory in use is approximately 3 of 32 GB.', 'the current kernel is seL4 on x86-64.', 'no errors recorded in the last interval.'];
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
      let kind = 1,
        kindName = 'STATS';
      if (!loading) {
        const roll = seq % 7;
        if (roll === 0) {
          kind = 2;
          kindName = 'INFER';
          ri = (ri + 1) % responses.length;
          lastText = responses[ri];
        } else if (roll === 3) {
          kind = 3;
          kindName = 'STATE';
        }
      }

      // grow counters honestly once loaded
      if (!loading) {
        const burst = 5 + seq % 4;
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
        kind,
        kind_name: kindName,
        flags: 0,
        flags_list: flags,
        boot_id: BOOT_ID,
        seq,
        uptime_ms: uptimeMs,
        q_total: qTotal,
        q_hits: qHits,
        q_infer: qInfer,
        q_heartbeat: qHb,
        q_shield: qShield,
        q_errors: qErrors,
        num_nodes: 6,
        model_load_pct: loadPct,
        fb_w: 1024,
        fb_h: 768,
        fb_bpp: 32,
        selftest_score: 5,
        model_size_mb: 2962,
        total_ram_mb: 0,
        // honest: no source
        infer_gen_tokens: 0,
        // honest: not measured in deploy
        model_name: 'Gemma 4 E2B',
        last_text: lastText,
        crc_ok: true
      };
    }

    // first record promptly, then ~1 Hz
    ingest(makeRecord());
    setInterval(() => ingest(makeRecord()), 1000);
  }
  window.JarvisTelemetry = API;
})();
})(); } catch (e) { __ds_ns.__errors.push({ path: "ui_kits/telemetry-console/telemetry.js", error: String((e && e.message) || e) }); }

__ds_ns.Badge = __ds_scope.Badge;

__ds_ns.Button = __ds_scope.Button;

__ds_ns.Card = __ds_scope.Card;

__ds_ns.Input = __ds_scope.Input;

__ds_ns.Kbd = __ds_scope.Kbd;

__ds_ns.SegmentedTabs = __ds_scope.SegmentedTabs;

__ds_ns.StatusDot = __ds_scope.StatusDot;

__ds_ns.Switch = __ds_scope.Switch;

})();
