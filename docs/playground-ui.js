/* playground-ui.js — wires the emscripten WASM module (window.createDnsd from
 * dnsd.js) to the <compendium-playground> custom element's DOM. Ported from the
 * old docs/app.js, re-scoped to the booting element via
 * window.__compendiumPlaygroundRoot (set by compendium-playground.js before this
 * script loads).
 *
 * DOM contract (built by compendium-playground.js):
 *   #source   — textarea (CodeMirror, mode 'dhall')
 *   #status   — status line
 *   #runBtn   — Run button
 *   #qname    — name input
 *   #qtype    — query-type select (numeric value)
 *   #answers  — decoded answer container
 *   #hex      — raw wire bytes <pre>
 */
(function () {
  'use strict';

  var root = window.__compendiumPlaygroundRoot;
  if (!root) {
    // Nothing to boot against (e.g. a late SSR element with no client mount).
    return;
  }

  var Module = null;

  var statusEl = root.querySelector('#status');
  var runBtn = root.querySelector('#runBtn');
  var qnameEl = root.querySelector('#qname');
  var qtypeEl = root.querySelector('#qtype');
  var answersEl = root.querySelector('#answers');
  var hexEl = root.querySelector('#hex');

  var editor = CodeMirror.fromTextArea(root.querySelector('#source'), {
    mode: 'dhall',
    theme: 'dhall',
    lineNumbers: true,
    indentUnit: 2,
    tabSize: 2,
    lineWrapping: true
  });

  var DEFAULT_SRC =
    'let Record =\n' +
    '      < A     : { name : Text, ttl : Natural, value : Text }\n' +
    '      | AAAA  : { name : Text, ttl : Natural, value : Text }\n' +
    '      | CNAME : { name : Text, ttl : Natural, value : Text }\n' +
    '      | TXT   : { name : Text, ttl : Natural, value : Text }\n' +
    '      | MX    : { name : Text, ttl : Natural, priority : Natural, exchange : Text }\n' +
    '      | NS    : { name : Text, ttl : Natural, value : Text }\n' +
    '      | SOA   : { name : Text, ttl : Natural, mname : Text, rname : Text, serial : Natural\n' +
    '                , refresh : Natural, retry : Natural, expire : Natural, minimum : Natural }\n' +
    '      | CAA   : { name : Text, ttl : Natural, flags : Natural, tag : Text, value : Text }\n' +
    '      >\n' +
    'in  let Zone   = { name : Text, records : List Record }\n' +
    'in  let Config = { zones : List Zone }\n' +
    'in  { zones =\n' +
    '      [ { name = "example.com."\n' +
    '        , records =\n' +
    '          [ < SOA   = { name = "@", ttl = 3600, mname = "ns1.example.com.", rname = "hostmaster.example.com."\n' +
    '                      , serial = 2024010101, refresh = 7200, retry = 3600, expire = 1209600, minimum = 300 } >\n' +
    '          , < NS    = { name = "@", ttl = 3600, value = "ns1.example.com." } >\n' +
    '          , < A     = { name = "@", ttl = 3600, value = "192.0.2.1" } >\n' +
    '          , < AAAA  = { name = "@", ttl = 3600, value = "2001:db8::1" } >\n' +
    '          , < MX    = { name = "@", ttl = 3600, priority = 10, exchange = "mail.example.com." } >\n' +
    '          , < CNAME = { name = "www", ttl = 3600, value = "example.com." } >\n' +
    '          , < TXT   = { name = "@", ttl = 3600, value = "v=spf1 -all" } >\n' +
    '          , < CAA   = { name = "@", ttl = 3600, flags = 0, tag = "issue", value = "letsencrypt.org" } >\n' +
    '          ]\n' +
    '        }\n' +
    '      , { name = "example.org."\n' +
    '        , records =\n' +
    '          [ < SOA = { name = "@", ttl = 3600, mname = "ns1.example.org.", rname = "admin.example.org."\n' +
    '                    , serial = 1, refresh = 7200, retry = 3600, expire = 1209600, minimum = 300 } >\n' +
    '          , < A   = { name = "@", ttl = 300, value = "203.0.113.7" } >\n' +
    '          ]\n' +
    '        }\n' +
    '      ]\n' +
    '    } : Config';

  function setStatus(text, isError) {
    statusEl.textContent = text;
    statusEl.classList.toggle('error', !!isError);
  }

  function allocUTF8(s) {
    var bytes = Module.lengthBytesUTF8(s);
    var p = Module._malloc(bytes + 1);
    Module.stringToUTF8(s, p, bytes + 1);
    return p;
  }

  function loadConfig() {
    var src = editor.getValue();
    var p = allocUTF8(src);
    var nzones = Module._dnsd_config(p);
    Module._free(p);
    if (nzones < 0) {
      setStatus('config error: ' + Module.UTF8ToString(Module._dnsd_err()), true);
      return false;
    }
    setStatus('loaded ' + nzones + ' zone(s)');
    return true;
  }

  function hexBytes(bytes) {
    var out = '';
    for (var i = 0; i < bytes.length; i++) {
      out += ('0' + bytes[i].toString(16)).slice(-2);
      out += (i % 2 === 1) ? ' ' : '';
    }
    return out.trim();
  }

  function render(json, bytes) {
    setStatus(json.status + ' · ' + json.answers.length + ' answer(s) · ' + bytes.length + ' wire bytes',
      json.status !== 'NOERROR');

    answersEl.innerHTML = '';
    if (!json.answers.length) {
      var empty = document.createElement('div');
      empty.className = 'ans-empty';
      empty.textContent = json.status === 'NODATA'
        ? 'Name exists, but no ' + json.qtypeName + ' record (NODATA).'
        : 'No such name (NXDOMAIN).';
      answersEl.appendChild(empty);
    }
    json.answers.forEach(function (a) {
      var row = document.createElement('div');
      row.className = 'ans-row';
      var t = document.createElement('span'); t.className = 'ans-type'; t.textContent = a.type;
      var o = document.createElement('span'); o.className = 'ans-owner'; o.textContent = a.owner;
      var r = document.createElement('span'); r.className = 'ans-rdata'; r.textContent = a.rdata;
      var tl = document.createElement('span'); tl.className = 'ans-ttl'; tl.textContent = 'TTL ' + a.ttl;
      row.appendChild(t); row.appendChild(o); row.appendChild(r); row.appendChild(tl);
      answersEl.appendChild(row);
    });

    hexEl.textContent = hexBytes(bytes);
  }

  function doQuery() {
    if (!Module) { setStatus('WASM still loading…', true); return; }
    if (!loadConfig()) return;
    var name = qnameEl.value.trim();
    var type = Number(qtypeEl.value);
    if (!name) { setStatus('enter a domain to query', true); return; }
    var p = allocUTF8(name);
    var rlen = Module._dnsd_query(p, type);
    Module._free(p);
    if (rlen < 0) { setStatus(Module.UTF8ToString(Module._dnsd_err()), true); return; }
    var jlen = Module._dnsd_json_len();
    var json = JSON.parse(Module.UTF8ToString(Module._dnsd_json(), jlen));
    var rptr = Module._dnsd_resp();
    var bytes = Module.HEAPU8.subarray(rptr, rptr + rlen);
    render(json, bytes);
  }

  runBtn.addEventListener('click', doQuery);
  qnameEl.addEventListener('keydown', function (e) { if (e.key === 'Enter') doQuery(); });
  root.addEventListener('keydown', function (e) {
    if ((e.ctrlKey || e.metaKey) && e.key === 'Enter') { e.preventDefault(); doQuery(); }
  });

  editor.setValue(DEFAULT_SRC);
  setStatus('loading WASM…');
  createDnsd()
    .then(function (M) {
      Module = M;
      setStatus('WASM ready');
      doQuery();
    })
    .catch(function (e) {
      setStatus('WASM failed to load: ' + e.message, true);
    });
})();
