// shell/mfe/compendium-playground.js — the <compendium-playground> custom element.
//
// Defines a custom element that encapsulates the WASM + CodeMirror DNS demo.
// The element builds its editor DOM on connectedCallback and loads the
// required scripts (dnsd.js, CodeMirror, dhall-mode.js, playground-ui.js).
//
// DOM contract (must be satisfied for playground-ui.js, ported from the old
// docs/app.js):
//   #source   — textarea (converted to CodeMirror, mode:'dhall') — the Dhall config
//   #status   — status line
//   #runBtn   — the Run button
//   #qname    — text input for the name to query
//   #qtype    — select of query types (value is the numeric RFC 1035 type)
//   #answers  — container for the decoded answer rows
//   #hex      — <pre> for the raw response wire bytes
//
// The element uses light DOM (NOT shadow DOM) because playground-ui.js uses
// getElementById-style lookups scoped to the element's root.

const BASE = '/compendium';

// Once-per-element boot guard.
const BOOTED = Symbol('booted');

// Reusable script loading (cached per URL).
const scriptCache = new Map();

function loadScript(src, opts) {
  const cache = !opts || opts.cache !== false;
  if (cache && scriptCache.has(src)) {
    return scriptCache.get(src);
  }
  const promise = new Promise((resolve, reject) => {
    const s = document.createElement('script');
    s.src = src;
    s.async = false; // preserve ordering
    s.onload = () => { s.remove(); resolve(); };
    s.onerror = () => { s.remove(); reject(new Error(`compendium-playground: failed to load ${src}`)); };
    (document.head || document.documentElement).appendChild(s);
  });
  if (cache) scriptCache.set(src, promise);
  return promise;
}

function loadFactories() {
  // NOTE: no codemirror-lint / dhall-lsp — compendium's demo has no LSP, just
  // the config editor (dhall mode) + the wasm server (dnsd.js).
  return Promise.all([
    loadScript(`${BASE}/dnsd.js`),
    loadScript(`${BASE}/vendor/codemirror.min.js`),
    loadScript(`${BASE}/vendor/codemirror-simple.js`),
    loadScript(`${BASE}/vendor/dhall-mode.js`),
  ]);
}

// Minimal DOM builder helper.
function el(tag, attrs, children) {
  const node = document.createElement(tag);
  if (attrs) {
    for (const [k, v] of Object.entries(attrs)) {
      if (k === 'class') node.className = v;
      else if (k === 'html') node.innerHTML = v;
      else node.setAttribute(k, v);
    }
  }
  for (const c of children || []) {
    node.appendChild(typeof c === 'string' ? document.createTextNode(c) : c);
  }
  return node;
}

// Consolidated playground CSS: the fixpoint palette + the old docs demo's
// editor/query/answer rules (CodeMirror Tokyo-Night theme, answer rows, hex).
const PLAYGROUND_CSS = `
:root{--bg:#0b0e11;--bg2:#10141a;--fg:#d8dee6;--dim:#7d8794;--accent:#6ad6a1;--accent2:#8ab4f8;--line:#1e2730;--mono:"SFMono-Regular","Cascadia Code","JetBrains Mono","Fira Code",Menlo,Consolas,monospace;--sans:-apple-system,"Segoe UI",Roboto,Helvetica,Arial,sans-serif}
.playground .demo-actions{display:flex;gap:8px;align-items:center;margin:0.6em 0;flex-wrap:wrap}
.playground .btn{border:none;border-radius:6px;font-weight:600;font-size:14px;padding:8px 16px;cursor:pointer}
.playground .btn-primary{background:var(--accent);color:var(--bg)}
.playground .btn-primary:hover{background:var(--accent2)}
.playground .query-bar{display:flex;gap:10px;align-items:center;flex-wrap:wrap;margin:0.6em 0 0.8em}
.playground .query-bar input,.playground .query-bar select{font-family:var(--mono);font-size:14px;padding:8px 10px;background:var(--bg2);border:1px solid var(--line);border-radius:6px;color:var(--fg)}
.playground .query-bar input{flex:1;min-width:180px}
.playground .query-bar select{cursor:pointer}
.playground .pane label{font-family:var(--mono);font-size:12px;color:var(--dim);display:block;margin-bottom:6px}
.playground textarea{width:100%;font-family:var(--mono);font-size:0.9em;line-height:1.5;background:var(--bg2);border:1px solid var(--line);border-radius:8px;padding:12px;color:var(--fg);resize:vertical;tab-size:2}
.playground .status{font-family:var(--mono);font-size:12px;color:var(--dim)}
.playground .status.error{color:#ff7b72}
.playground .answers{margin-top:10px;font-family:var(--mono);font-size:13px;display:flex;flex-direction:column;gap:6px}
.playground .ans-row{display:grid;grid-template-columns:80px 1fr auto 1fr;gap:12px;align-items:baseline;background:var(--bg2);border:1px solid var(--line);border-radius:6px;padding:8px 12px}
.playground .ans-type{color:var(--accent2);font-weight:600}
.playground .ans-owner{color:var(--fg);word-break:break-all}
.playground .ans-rdata{color:var(--accent);word-break:break-all}
.playground .ans-ttl{color:var(--dim)}
.playground .ans-empty{font-family:var(--mono);font-size:13px;color:var(--dim);background:var(--bg2);border:1px dashed var(--line);border-radius:6px;padding:8px 12px}
.playground .hex{font-family:var(--mono);font-size:12px;line-height:1.6;color:var(--dim);background:var(--bg2);border:1px solid var(--line);border-radius:8px;padding:12px;white-space:pre-wrap;word-break:break-all;margin-top:12px;min-height:60px}
.CodeMirror{border:1px solid #414868;height:360px;font-size:14px}
.CodeMirror,.CodeMirror-scroll{background:#1a1b26;color:#c0caf5}
.CodeMirror-gutters{background:#16161e;border-right:1px solid #292e42}
.CodeMirror-linenumber{color:#3b4261}
.CodeMirror-cursor{border-left:2px solid #c0caf5}
.CodeMirror-selected{background:#33467c}
.cm-s-tokyonight .cm-comment{color:#565f89;font-style:italic;}
.cm-s-tokyonight .cm-string{color:#9ece6a;}
.cm-s-tokyonight .cm-number{color:#ff9e64;}
.cm-s-tokyonight .cm-keyword{color:#bb9af7;font-weight:600;}
.cm-s-tokyonight .cm-atom{color:#e0af68;}
.cm-s-tokyonight .cm-operator{color:#89ddff;}
.cm-s-tokyonight .cm-bracket{color:#c0caf5;}
.cm-s-tokyonight .cm-def{color:#73daca;font-weight:600;}
`;

/**
 * Wait until `element` is attached to the live document.
 */
function waitConnected(element) {
  if (element.isConnected) return Promise.resolve();
  return new Promise((resolve) => {
    const check = () => {
      if (element.isConnected) resolve();
      else requestAnimationFrame(check);
    };
    requestAnimationFrame(check);
  });
}

/**
 * Boot the playground: load factories then playground-ui.js against the element's DOM.
 */
async function bootPlayground(element) {
  await waitConnected(element);
  await loadFactories();
  // Tell playground-ui.js which element to scope its DOM lookups to. Because
  // the SSR pre-render and the client MFE each create a <compendium-playground>,
  // this root scoping keeps each boot's glue on its own subtree — a removed
  // pre-rendered element's late boot won't double-create the editor on the
  // live element's shared textarea (see the comment in playground-ui.js).
  window.__compendiumPlaygroundRoot = element;
  // playground-ui.js is NOT cached: the @mfe shell unmounts/remounts the slot on
  // cross-page SPA nav, so a fresh <compendium-playground> must re-run the UI glue
  // against its own (only) DOM. The libs (dnsd.js, CodeMirror, dhall-mode.js)
  // stay cached; only one playground exists at a time.
  await loadScript(`${BASE}/playground-ui.js`, { cache: false });
}

const QTYPE_OPTIONS = [
  ['A', 1], ['AAAA', 28], ['CNAME', 5], ['TXT', 16], ['MX', 15],
  ['NS', 2], ['SOA', 6], ['CAA', 257], ['ANY', 255],
];

/**
 * The <compendium-playground> custom element.
 */
class CompendiumPlayground extends HTMLElement {
  constructor() {
    super();
    this[BOOTED] = false;
    this._styleEl = null;
    this._linkEl = null;
  }

  connectedCallback() {
    if (this[BOOTED]) return;
    this[BOOTED] = true;

    const wrap = el('div', { class: 'playground' });
    const status = el('span', { id: 'status', class: 'status' });

    // Dhall config editor pane.
    const srcPane = el('div', { class: 'pane' }, [
      el('label', { for: 'source' }, ['Dhall config (zones + records)']),
      el('textarea', { id: 'source', spellcheck: 'false', autocomplete: 'off', autocapitalize: 'off' }),
    ]);

    // Query bar: name input + type select + Run button.
    const qname = el('input', { id: 'qname', type: 'text', value: 'example.com.', spellcheck: 'false', autocomplete: 'off', placeholder: 'example.com.' });
    const qtype = el('select', { id: 'qtype', class: 'qtype' });
    QTYPE_OPTIONS.forEach(([label, value]) => {
      qtype.appendChild(el('option', { value: String(value) }, [label]));
    });
    const runBtn = el('button', { id: 'runBtn', class: 'btn btn-primary', type: 'button' }, ['Query']);
    const queryBar = el('div', { class: 'query-bar' }, [qname, qtype, runBtn]);

    const answers = el('div', { id: 'answers', class: 'answers' });
    const hex = el('pre', { id: 'hex', class: 'hex' });

    wrap.appendChild(srcPane);
    wrap.appendChild(queryBar);
    wrap.appendChild(answers);
    wrap.appendChild(hex);
    wrap.appendChild(status);

    // Clear any existing content and append the demo DOM.
    this.textContent = '';
    this.appendChild(wrap);

    // Inject CSS: codemirror.css link + inline PLAYGROUND_CSS.
    this._linkEl = document.createElement('link');
    this._linkEl.rel = 'stylesheet';
    this._linkEl.href = `${BASE}/vendor/codemirror.css`;
    document.head.appendChild(this._linkEl);

    this._styleEl = document.createElement('style');
    this._styleEl.textContent = PLAYGROUND_CSS;
    document.head.appendChild(this._styleEl);

    // Boot the playground scripts non-blocking.
    void bootPlayground(this);
  }

  disconnectedCallback() {
    // Clean up injected styles if this element is removed.
    for (const ref of ['_styleEl', '_linkEl']) {
      if (this[ref]) {
        this[ref].remove();
        this[ref] = null;
      }
    }
  }
}

// Register the custom element.
customElements.define('compendium-playground', CompendiumPlayground);
