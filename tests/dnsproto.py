#!/usr/bin/env python3
"""DNS wire-format test helper (pure stdlib, no dnspython).

  build_query(name, qtype, qid)  -> bytes to send over UDP
  parse_response(pkt)            -> dict to assert (fully decodes rdata incl.
                                    name-compression pointers)

Standalone: python3 dnsproto.py <hex>  decodes a packet from hex.
"""
import socket, struct, sys

T_A, T_NS, T_CNAME, T_SOA, T_MX, T_TXT, T_AAAA, T_ANY, T_CAA = 1, 2, 5, 6, 15, 16, 28, 255, 257
C_IN = 1
RCODE = {0: "NOERROR", 1: "FORMERR", 3: "NXDOMAIN", 4: "NOTIMP", 5: "REFUSED"}
TYPE_NAME = {1: "A", 2: "NS", 5: "CNAME", 6: "SOA", 15: "MX", 16: "TXT", 28: "AAAA", 255: "ANY", 257: "CAA"}


def build_query(name, qtype, qid=0x1234):
    name = name.rstrip(".")
    if not name:
        qname = b"\x00"
    else:
        qname = b"".join(bytes([len(l)]) + l.encode() for l in name.split(".")) + b"\x00"
    header = struct.pack(">HHHHHH", qid, 0x0100, 1, 0, 0, 0)
    return header + qname + struct.pack(">HH", qtype, C_IN)


def _read_name(pkt, off):
    labels = []; jumped = False; end = off
    while True:
        b = pkt[off]
        if b == 0:
            off += 1
            if not jumped: end = off
            break
        if b & 0xC0 == 0xC0:
            ptr = ((b & 0x3F) << 8) | pkt[off + 1]
            if not jumped: end = off + 2
            off = ptr; jumped = True; continue
        off += 1
        labels.append(pkt[off:off + b].decode("ascii", "replace"))
        off += b
    return ".".join(labels) + ".", end


def _decode_rdata(pkt, rtype, rdata_off, rdlen):
    o = rdata_off
    if rtype == T_A:
        return socket.inet_ntop(socket.AF_INET, pkt[o:o + 4]), o + 4
    if rtype == T_AAAA:
        return socket.inet_ntop(socket.AF_INET6, pkt[o:o + 16]), o + 16
    if rtype in (T_CNAME, T_NS):
        name, end = _read_name(pkt, o); return name, end
    if rtype == T_MX:
        prio = struct.unpack(">H", pkt[o:o + 2])[0]
        name, end = _read_name(pkt, o + 2)
        return {"priority": prio, "exchange": name}, end
    if rtype == T_TXT:
        strings = []; end = o + rdlen
        while o < end:
            ln = pkt[o]; o += 1
            strings.append(pkt[o:o + ln].decode("utf-8", "replace")); o += ln
        return strings, end
    if rtype == T_SOA:
        mname, o = _read_name(pkt, o); rname, o = _read_name(pkt, o)
        serial, refresh, retry, expire, minimum = struct.unpack(">IIIII", pkt[o:o + 20]); o += 20
        return {"mname": mname, "rname": rname, "serial": serial, "refresh": refresh,
                "retry": retry, "expire": expire, "minimum": minimum}, o
    if rtype == T_CAA:
        end = rdata_off + rdlen
        flags = pkt[o]; o += 1
        tlen = pkt[o]; o += 1
        tag = pkt[o:o + tlen].decode("ascii", "replace"); o += tlen
        value = pkt[o:end].decode("utf-8", "replace")
        return {"flags": flags, "tag": tag, "value": value}, end
    return pkt[o:o + rdlen].hex(), o + rdlen


def parse_response(pkt):
    hid, flags, qd, an, ns, ar = struct.unpack(">HHHHHH", pkt[:12])
    off = 12; questions = []
    for _ in range(qd):
        name, off = _read_name(pkt, off)
        qtype, qclass = struct.unpack(">HH", pkt[off:off + 4]); off += 4
        questions.append({"name": name, "type": qtype, "class": qclass})
    answers = []
    for _ in range(an):
        name, off = _read_name(pkt, off)
        rtype, rclass, ttl, rdlen = struct.unpack(">HHIH", pkt[off:off + 10]); off += 10
        val, off = _decode_rdata(pkt, rtype, off, rdlen)
        answers.append({"name": name, "type": rtype, "type_name": TYPE_NAME.get(rtype, "?"),
                        "class": rclass, "ttl": ttl, "rdata": val})
    return {"id": hid, "qr": bool(flags & 0x8000), "aa": bool(flags & 0x0400),
            "tc": bool(flags & 0x0200), "rd": bool(flags & 0x0100),
            "rcode": flags & 0x0F, "rcode_name": RCODE.get(flags & 0x0F, "?"),
            "questions": questions, "answers": answers}


if __name__ == "__main__":
    pkt = bytes.fromhex(sys.argv[1].replace(" ", ""))
    r = parse_response(pkt)
    print(f"id=0x{r['id']:04x} qr={r['qr']} aa={r['aa']} rcode={r['rcode_name']} "
          f"questions={r['questions']}")
    for a in r["answers"]:
        print(f"  {a['name']:<24} {a['type_name']:<5} ttl={a['ttl']} rdata={a['rdata']}")
