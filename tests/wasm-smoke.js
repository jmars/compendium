/* wasm-smoke.js — headless smoke test for the compendium DNS wasm module.
 * Run from the repo root via `node tests/wasm-smoke.js` (after `make wasm`).
 */
const fs = require('fs');
const path = require('path');
const createDnsd = require(path.join(__dirname, '..', 'docs', 'dnsd.js'));

const config = fs.readFileSync(path.join(__dirname, '..', 'config.example.dhall'), 'utf8');

createDnsd().then((M) => {
  const bytes = M.lengthBytesUTF8(config);
  const ptr = M._malloc(bytes + 1);
  M.stringToUTF8(config, ptr, bytes + 1);
  const nzones = M._dnsd_config(ptr);
  M._free(ptr);
  if (nzones < 0) {
    console.error('CONFIG ERROR:', M.UTF8ToString(M._dnsd_err()));
    process.exit(1);
  }
  if (nzones !== 2) { console.error('expected 2 zones, got', nzones); process.exit(1); }
  console.log('loaded zones:', nzones);

  function query(name, type) {
    const b = M.lengthBytesUTF8(name);
    const p = M._malloc(b + 1);
    M.stringToUTF8(name, p, b + 1);
    const rlen = M._dnsd_query(p, type);
    M._free(p);
    if (rlen < 0) throw new Error('query error: ' + M.UTF8ToString(M._dnsd_err()));
    const jlen = M._dnsd_json_len();
    const json = JSON.parse(M.UTF8ToString(M._dnsd_json(), jlen));
    const rptr = M._dnsd_resp();
    const hex = Buffer.from(M.HEAPU8.subarray(rptr, rptr + rlen)).toString('hex');
    return { json, wireLen: rlen, hex };
  }

  function expect(label, name, type, status, count) {
    const r = query(name, type);
    if (r.json.status !== status) throw new Error(label + ': status ' + r.json.status + ' != ' + status);
    if (r.json.answers.length !== count) throw new Error(label + ': ' + r.json.answers.length + ' answers != ' + count);
    if (r.wireLen < 12) throw new Error(label + ': wire too short');
    console.log('  ok', label, '->', status, '(' + count + ' answer(s), ' + r.wireLen + 'B)');
  }

  expect('A apex', 'example.com', 1, 'NOERROR', 1);
  expect('AAAA apex', 'example.com', 28, 'NOERROR', 1);
  expect('TXT apex', 'example.com', 16, 'NOERROR', 1);
  expect('MX apex', 'example.com', 15, 'NOERROR', 1);
  expect('CNAME www', 'www.example.com', 5, 'NOERROR', 1);
  expect('CAA apex', 'example.com', 257, 'NOERROR', 1);
  expect('NS apex', 'example.com', 2, 'NOERROR', 1);
  expect('SOA apex', 'example.com', 6, 'NOERROR', 1);
  expect('ANY apex', 'example.com', 255, 'NOERROR', 7);
  expect('NXDOMAIN', 'nope.example.com', 1, 'NXDOMAIN', 0);
  expect('NODATA', 'www.example.com', 28, 'NODATA', 0);

  const a = query('example.com', 1);
  if (!a.hex.includes('c0000201')) throw new Error('A rdata 192.0.2.1 not found in wire hex: ' + a.hex);

  console.log('SMOKE OK');
});
