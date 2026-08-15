#!/usr/bin/env bash
# dnsd test suite. Run from the project root (make test), or directly:
#   bash tests/run.sh
# Covers:
#   - config_check: Dhall config walked into zones/records (Stage 1)
#   - lookup_check: dns_lookup semantics, in-process (NXDOMAIN/NODATA/ANY)
#   - wire_check:   C wire encoder output decoded by the independent Python
#                   parser for all 8 record types + NXDOMAIN + NODATA (Stage 2)
#   - end-to-end:   real UDP queries against the running dnsd.com.dbg for all
#                   8 types + NXDOMAIN + NODATA + FORMERR + REFUSED (Stage 4)
set -u

cd "$(dirname "$0")/.."

# Build everything (idempotent).
make all >/dev/null 2>&1 || { echo "FAIL: make all"; exit 1; }

PY=tests/dnsproto.py
pass=0; fail=0
ok()  { pass=$((pass+1)); echo "PASS: $1"; }
bad() { fail=$((fail+1)); echo "FAIL: $1"; }

echo "== Stage 1: config_check =="
out=$(./config_check.com.dbg config.example.dhall)
[ $? -eq 0 ] && ok "config_check exit 0" || bad "config_check exit 0"
grep -q "zones=2" <<<"$out" && ok "2 zones" || bad "2 zones"
grep -q "zone=example.com. records=8" <<<"$out" && ok "example.com 8 records" || bad "example.com 8 records"
grep -q "zone=example.org. records=2" <<<"$out" && ok "example.org 2 records" || bad "example.org 2 records"
grep -q "example.com. A ttl=3600 value=192.0.2.1" <<<"$out" && ok "apex A record" || bad "apex A record"
grep -q "example.com. AAAA ttl=3600 value=2001:db8::1" <<<"$out" && ok "apex AAAA record" || bad "apex AAAA record"
grep -q "www.example.com. CNAME ttl=3600 value=example.com." <<<"$out" && ok "www CNAME" || bad "www CNAME"
grep -q "priority=10 exchange=mail.example.com." <<<"$out" && ok "MX record" || bad "MX record"
grep -q "value=ns1.example.com." <<<"$out" && ok "NS record" || bad "NS record"
grep -q "SOA ttl=3600 mname=ns1.example.com. rname=hostmaster.example.com. serial=2024010101" <<<"$out" && ok "SOA record" || bad "SOA record"
grep -q "value=v=spf1 -all" <<<"$out" && ok "TXT record" || bad "TXT record"
grep -q "example.com. CAA ttl=3600 flags=0 tag=issue value=letsencrypt.org" <<<"$out" && ok "CAA record" || bad "CAA record"
if ./config_check.com.dbg tests/bad-config.dhall >/dev/null 2>&1; then bad "type-error config rejected at load"; else ok "type-error config rejected at load"; fi

echo "== Stage 2: lookup_check =="
./lookup_check.com.dbg config.example.dhall && ok "lookup_check exit 0" || bad "lookup_check exit 0"

echo "== Stage 3: query_check =="
./query_check.com.dbg config.example.dhall && ok "query_check exit 0" || bad "query_check exit 0"

echo "== Stage 3b: rl_check (rate limiter, deterministic) =="
./rl_check.com.dbg && ok "rl_check exit 0" || bad "rl_check exit 0"

echo "== Stage 2: wire_check =="
./wire_check.com.dbg > /tmp/wire.out 2>/dev/null
echo "== Stage 2 decode via python =="
python3 - "$PY" <<'PYEOF' || bad "wire_check decode"
import subprocess, sys
sys.path.insert(0, "tests")
import dnsproto as D
# Re-run wire_check, decode each packet line, assert answer + rcode.
lines = subprocess.run(["./wire_check.com.dbg"], capture_output=True, text=True).stdout.splitlines()
assert len(lines) == 10, f"expected 10 packets, got {len(lines)}"
dec = [D.parse_response(bytes.fromhex(ln.split(" ", 1)[1])) for ln in lines]
expect_types = ["A", "AAAA", "CNAME", "TXT", "CAA", "MX", "NS", "SOA", None, None]
expect_rcodes = ["NOERROR"]*8 + ["NXDOMAIN", "NOERROR"]
for r, et, er in zip(dec, expect_types, expect_rcodes):
    assert r["rcode_name"] == er, f"rcode={r['rcode_name']} != {er}"
    if et is not None:
        assert len(r["answers"]) == 1, "expected 1 answer"
        assert r["answers"][0]["type_name"] == et, r["answers"]
print("PASS: wire_check decoded all 10 packets with correct rcode/type")
PYEOF

echo "== Stage 4: end-to-end UDP =="
# Detect whether this environment permits UDP sockets (some sandboxes block
# all socket() calls); if not, SKIP rather than report a false FAIL.
if ! python3 -c "import socket; socket.socket(socket.AF_INET, socket.SOCK_DGRAM)" 2>/dev/null; then
    echo "SKIP: end-to-end UDP (no UDP sockets permitted in this environment)"
else
PORT=$((20000 + RANDOM % 20000))
./dnsd.com.dbg -c config.example.dhall -p "$PORT" -a 127.0.0.1 >/tmp/dnsd.log 2>&1 &
DPID=$!
trap 'kill "$DPID" 2>/dev/null' EXIT
sleep 0.4

python3 - "$PORT" <<'PYEOF'
import socket, struct, sys, time
sys.path.insert(0, "tests")
import dnsproto as D

HOST, PORT = "127.0.0.1", int(sys.argv[1])

def query(name, qtype, raw=None, expect_rcode=None, expect_answers=None):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.settimeout(2)
    pkt = raw if raw is not None else D.build_query(name, qtype)
    s.sendto(pkt, (HOST, PORT))
    data, _ = s.recvfrom(512)
    s.close()
    r = D.parse_response(data)
    if expect_rcode is not None:
        assert r["rcode_name"] == expect_rcode, f"{name}: rcode={r['rcode_name']} != {expect_rcode}"
    if expect_answers is not None:
        assert len(r["answers"]) == len(expect_answers), f"{name}: answers={len(r['answers'])} != {len(expect_answers)}"
        for i, exp in enumerate(expect_answers):
            got = r["answers"][i]
            assert got["type_name"] == exp["type"], f"{name}[{i}]: type={got['type_name']} != {exp['type']}"
            if "rdata" in exp:
                assert got["rdata"] == exp["rdata"], f"{name}[{i}]: rdata={got['rdata']} != {exp['rdata']}"
    return r

# All 8 record types at the apex.
query("example.com.", D.T_A, expect_rcode="NOERROR", expect_answers=[{"type": "A", "rdata": "192.0.2.1"}])
query("example.com.", D.T_AAAA, expect_rcode="NOERROR", expect_answers=[{"type": "AAAA", "rdata": "2001:db8::1"}])
query("example.com.", D.T_NS, expect_rcode="NOERROR", expect_answers=[{"type": "NS", "rdata": "ns1.example.com."}])
query("example.com.", D.T_MX, expect_rcode="NOERROR", expect_answers=[{"type": "MX", "rdata": {"priority": 10, "exchange": "mail.example.com."}}])
query("example.com.", D.T_TXT, expect_rcode="NOERROR", expect_answers=[{"type": "TXT", "rdata": ["v=spf1 -all"]}])
query("example.com.", D.T_CAA, expect_rcode="NOERROR", expect_answers=[{"type": "CAA", "rdata": {"flags": 0, "tag": "issue", "value": "letsencrypt.org"}}])
query("example.com.", D.T_SOA, expect_rcode="NOERROR", expect_answers=[{"type": "SOA"}])
query("www.example.com.", D.T_CNAME, expect_rcode="NOERROR", expect_answers=[{"type": "CNAME", "rdata": "example.com."}])

# ANY -> all apex records.
r = query("example.com.", D.T_ANY, expect_rcode="NOERROR")
assert r["answers"], "ANY should return answers"

# Second zone.
query("example.org.", D.T_A, expect_rcode="NOERROR", expect_answers=[{"type": "A", "rdata": "203.0.113.7"}])

# NXDOMAIN: name absent within a zone.
query("nosuch.example.com.", D.T_A, expect_rcode="NXDOMAIN", expect_answers=[])
# NXDOMAIN: no zone matches.
query("other.example.net.", D.T_A, expect_rcode="NXDOMAIN", expect_answers=[])
# NODATA: name exists (www has CNAME) but no A record.
query("www.example.com.", D.T_A, expect_rcode="NOERROR", expect_answers=[])
# FORMERR: QDCOUNT != 1 (2 questions).
hdr = struct.pack(">HHHHHH", 0x2222, 0x0100, 2, 0, 0, 0)
q = hdr + b"\x01a\x00" + struct.pack(">HH", D.T_A, D.C_IN)
query(None, None, raw=q, expect_rcode="FORMERR")
# FORMERR: truncated / garbage body (compress pointer in question qname).
hdr = struct.pack(">HHHHHH", 0x3333, 0x0100, 1, 0, 0, 0)
q = hdr + b"\xc0\x0c" + struct.pack(">HH", D.T_A, D.C_IN)
query(None, None, raw=q, expect_rcode="FORMERR")
# REFUSED: qclass != IN.
hdr = struct.pack(">HHHHHH", 0x4444, 0x0100, 1, 0, 0, 0)
q = hdr + b"\x07example\x03com\x00" + struct.pack(">HH", D.T_A, 3)
query(None, None, raw=q, expect_rcode="REFUSED")

# Rate limiting: a rapid burst from ONE source is throttled. Burst capacity is
# 100 tokens (refill 20/s), so sending many more than 100 instantly must drop
# some — i.e. the server returns strictly fewer responses than queries sent.
pkt = D.build_query("example.com.", D.T_A)
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.settimeout(2)
sent = 150
for _ in range(sent):
    s.sendto(pkt, (HOST, PORT))
got = 0
for _ in range(sent):
    try:
        s.recvfrom(512)
        got += 1
    except socket.timeout:
        break
s.close()
assert 0 < got < sent, f"rate limit: got {got} responses for {sent} queries (expected throttled)"
print(f"ok   rate-limited burst: {got}/{sent} answered, rest dropped")

print("PASS: end-to-end assertions complete")
PYEOF
[ $? -eq 0 ] && ok "end-to-end UDP" || bad "end-to-end UDP"
fi

echo
echo "== results: $pass passed, $fail failed =="
[ "$fail" -eq 0 ] && echo "ALL TESTS PASSED" || echo "SOME TESTS FAILED"
exit "$fail"
