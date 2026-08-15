/* dns.c — DNS wire format encode/decode (RFC 1035, UDP) + lookup semantics.
   Ported from the verified dns_wire.c artifact (incl. the CORRECTED suffix
   name-compressor that writes leading labels before a compression pointer).
   Split into: header/qname decode, NameWriter, build_answer, dns_build_response,
   plus dns_lookup implementing the plan's zone-match + longest-match + 
   NXDOMAIN/NODATA/NOERROR/ANY semantics. */
#include "dhall.h"
#include "dnsd.h"
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>

static uint16_t rd16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void wr32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}

uint16_t dns_rd16(const uint8_t *p) { return rd16(p); }

int dns_parse_header(const uint8_t *pkt, size_t len, DnsHeader *h) {
    if (len < 12) return -1;
    h->id = rd16(pkt + 0); h->flags = rd16(pkt + 2);
    h->qdcount = rd16(pkt + 4); h->ancount = rd16(pkt + 6);
    h->nscount = rd16(pkt + 8); h->arcount = rd16(pkt + 10);
    return 0;
}

int dns_decode_qname(const uint8_t *pkt, size_t len, size_t *off, char *out, size_t cap) {
    size_t o = *off; size_t n = 0; bool first = true;
    for (;;) {
        if (o >= len) return -1;
        uint8_t l = pkt[o];
        if (l == 0) { o++; break; }
        if ((l & 0xC0) == 0xC0) return -1;   /* no compression pointers in questions */
        if (l > 63) return -1;
        if (o + 1 + l > len) return -1;
        if (!first) { if (n + 1 >= cap) return -1; out[n++] = '.'; }
        for (size_t i = 0; i < l; i++) {
            uint8_t c = pkt[o + 1 + i];
            if (c >= 'A' && c <= 'Z') c = (uint8_t)(c - 'A' + 'a');
            if (n + 1 >= cap) return -1;
            out[n++] = (char)c;
        }
        o += 1 + l; first = false;
    }
    if (n + 1 >= cap) return -1;
    out[n++] = '.'; out[n] = '\0'; *off = o;
    return 0;
}

uint16_t dns_type_to_wire(RecType t) {
    switch (t) {
    case RT_A: return T_A;
    case RT_AAAA: return T_AAAA;
    case RT_CNAME: return T_CNAME;
    case RT_TXT: return T_TXT;
    case RT_MX: return T_MX;
    case RT_NS: return T_NS;
    case RT_SOA: return T_SOA;
    case RT_CAA: return T_CAA;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* NameWriter — suffix name compression                               */
/* ------------------------------------------------------------------ */

typedef struct {
    uint8_t *buf; size_t len, cap;
    int offsets[256]; char names[256][256]; int n;
} NameWriter;

static void nw_init(NameWriter *w, uint8_t *buf, size_t cap) { w->buf = buf; w->cap = cap; w->len = 0; w->n = 0; }
static void nw_add(NameWriter *w, const char *name, size_t off) {
    if (w->n < 256 && off < 0x4000) {
        w->offsets[w->n] = (int)off;
        snprintf(w->names[w->n], sizeof w->names[w->n], "%s", name);
        w->n++;
    }
}
static int nw_find(NameWriter *w, const char *name) {
    for (int i = 0; i < w->n; i++) if (!strcmp(w->names[i], name)) return w->offsets[i];
    return -1;
}
static int split_name(const char *name, int idx[128], int len[128]) {
    int n = 0; const char *p = name;
    while (*p && *p != '.') {
        if (n >= 128) return -1;   /* bound: max 128 labels */
        idx[n] = (int)(p - name); const char *q = p;
        while (*q && *q != '.') q++;
        len[n] = (int)(q - p); n++;
        p = (*q == '.') ? q + 1 : q;
        if (*q == '\0') break;
    }
    return n;
}

static int nw_put_name(NameWriter *w, const char *name) {
    int idx[128], len[128]; int n = split_name(name, idx, len);
    if (n < 0) return -1;          /* too many labels -> drop this RR */
    if (n == 0) { if (w->len + 1 > w->cap) return -1; w->buf[w->len++] = 0; return 0; }
    int match = -1, match_off = -1;
    for (int s = 0; s < n; s++) {
        char suffix[256]; size_t k = 0;
        for (int i = s; i < n; i++) {
            if (k + (size_t)len[i] + 1 >= sizeof suffix) return -1;
            memcpy(suffix + k, name + idx[i], (size_t)len[i]); k += (size_t)len[i]; suffix[k++] = '.';
        }
        suffix[k] = '\0';
        int off = nw_find(w, suffix);
        if (off >= 0) { match = s; match_off = off; break; }
    }
    int stop = (match >= 0) ? match : n;
    for (int i = 0; i < stop; i++) {
        if (len[i] > 63) return -1;
        if (w->len + 1 + (size_t)len[i] > w->cap) return -1;
        size_t soff = w->len;
        w->buf[w->len++] = (uint8_t)len[i];
        memcpy(w->buf + w->len, name + idx[i], (size_t)len[i]); w->len += (size_t)len[i];
        char suffix[256]; size_t k = 0;
        for (int j = i; j < n; j++) {
            memcpy(suffix + k, name + idx[j], (size_t)len[j]); k += (size_t)len[j]; suffix[k++] = '.';
        }
        suffix[k] = '\0'; nw_add(w, suffix, soff);
    }
    if (match >= 0) {
        if (w->len + 2 > w->cap) return -1;
        w->buf[w->len++] = 0xC0 | (uint8_t)(match_off >> 8);
        w->buf[w->len++] = (uint8_t)match_off;
    } else {
        if (w->len + 1 > w->cap) return -1;
        w->buf[w->len++] = 0;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* build_answer — one answer RR (owner + type/class/ttl + rdata)       */
/* ------------------------------------------------------------------ */

static int build_answer(NameWriter *w, const DnsRecord *r) {
    uint8_t *b = w->buf; size_t *len = &w->len;
    if (nw_put_name(w, r->owner)) return -1;
    if (*len + 10 > w->cap) return -1;
    uint16_t type = dns_type_to_wire(r->type);
    wr16(b + *len, type); *len += 2; wr16(b + *len, C_IN); *len += 2;
    wr32(b + *len, r->ttl); *len += 4;
    size_t rdlen_at = *len; *len += 2; size_t rdata_start = *len;
    int ok = 0;
    switch (r->type) {
    case RT_A: {
        /* rdata parsed once at config load (r->addr4); no per-query inet_pton. */
        if (*len + 4 > w->cap) return -1;
        memcpy(b + *len, &r->addr4, 4); *len += 4; ok = 1; break;
    }
    case RT_AAAA: {
        if (*len + 16 > w->cap) return -1;
        memcpy(b + *len, r->addr6, 16); *len += 16; ok = 1; break;
    }
    case RT_CNAME: case RT_NS: ok = !nw_put_name(w, r->value); break;
    case RT_TXT: {
        const char *s = r->value; size_t slen = strlen(s);
        /* RFC 1035: a TXT rdata is one or more <character-string>s. For an
           empty value, emit a single zero-length character-string (one 0x00
           byte) rather than an empty rdata. */
        if (slen == 0) {
            if (*len + 1 > w->cap) return -1;
            b[(*len)++] = 0;
        }
        while (slen > 0) {
            size_t chunk = slen > 255 ? 255 : slen;
            if (*len + 1 + chunk > w->cap) return -1;
            b[(*len)++] = (uint8_t)chunk;
            memcpy(b + *len, s, chunk); *len += chunk;
            s += chunk; slen -= chunk;
        }
        ok = 1; break;
    }
    case RT_MX: {
        if (*len + 2 > w->cap) return -1;
        wr16(b + *len, r->priority); *len += 2;
        ok = !nw_put_name(w, r->exchange); break;
    }
    case RT_CAA: {
        size_t tlen = strlen(r->tag), vlen = strlen(r->value);
        if (tlen < 1 || tlen > 15) return -1;   /* tag length 1..15 (RFC 8659) */
        if (*len + 2 + tlen + vlen > w->cap) return -1;
        b[(*len)++] = r->flags;                  /* flags (1 octet) */
        b[(*len)++] = (uint8_t)tlen;             /* tag-length (1 octet) */
        memcpy(b + *len, r->tag, tlen);  *len += tlen;
        memcpy(b + *len, r->value, vlen); *len += vlen;
        ok = 1; break;
    }
    case RT_SOA: {
        if (nw_put_name(w, r->mname)) return -1;
        if (nw_put_name(w, r->rname)) return -1;
        if (*len + 20 > w->cap) return -1;
        wr32(b + *len, r->serial); *len += 4;
        wr32(b + *len, r->refresh); *len += 4;
        wr32(b + *len, r->retry); *len += 4;
        wr32(b + *len, r->expire); *len += 4;
        wr32(b + *len, r->minimum); *len += 4;
        ok = 1; break;
    }
    }
    if (!ok) return -1;
    wr16(b + rdlen_at, (uint16_t)(*len - rdata_start));
    return 0;
}

int dns_build_response(uint8_t *out, size_t cap, const DnsHeader *qh,
                       const char *qname, uint16_t qtype,
                       const DnsRecord *recs, int nrecs, int rcode) {
    if (cap < 12) return -1;          /* guard: DNS header is 12 bytes */
    NameWriter w; nw_init(&w, out, cap);
    wr16(out + 0, qh->id);
    uint16_t flags = F_QR | F_AA | (qh->flags & F_RD) | (uint16_t)(rcode & 0xF);
    wr16(out + 2, flags);
    wr16(out + 4, 1); wr16(out + 6, (uint16_t)nrecs);
    wr16(out + 8, 0); wr16(out + 10, 0);
    w.len = 12;
    int idx[128], len[128]; int nl = split_name(qname, idx, len);
    if (nl < 0) return -1;  /* qname >128 labels: cannot happen from decode, but guard */
    for (int i = 0; i < nl; i++) {
        if (w.len + 1 + (size_t)len[i] > w.cap) return -1;
        out[w.len++] = (uint8_t)len[i];
        memcpy(out + w.len, qname + idx[i], (size_t)len[i]); w.len += (size_t)len[i];
    }
    if (w.len + 5 > w.cap) return -1;
    out[w.len++] = 0;
    wr16(out + w.len, qtype); w.len += 2;
    wr16(out + w.len, C_IN); w.len += 2;
    nw_add(&w, qname, 12);
    /* Bound the answer count: never write more than MAX_ANSWERS RRs, so a
       large zone (e.g. 200 A records on one owner, or ANY floods) cannot burn
       unbounded CPU in name-compression or produce an oversized response.
       When more answers exist than the cap, write MAX_ANSWERS and set TC so
       the truncated response is still valid and signals truncation. */
    bool truncated = false;
    int added = 0;
    int want = nrecs;
    if (want > MAX_ANSWERS) { want = MAX_ANSWERS; truncated = true; }
    for (int i = 0; i < want; i++) {
        size_t saved = w.len;
        if (build_answer(&w, &recs[i])) { w.len = saved; truncated = true; break; }
        added++;
    }
    if (truncated) {
        /* Set TC and rewrite ANCOUNT to only the answers actually written. */
        flags |= F_TC;
        wr16(out + 2, flags);
        wr16(out + 6, (uint16_t)added);
    }
    return (int)w.len;
}

/* ------------------------------------------------------------------ */
/* Lookup semantics (plan obs "LOOKUP SEMANTICS")                      */
/* ------------------------------------------------------------------ */

/* Split a lowercase-with-trailing-dot name into its labels. Returns count. */
static int label_split(const char *name, const char **labels, int max) {
    int n = 0;
    const char *p = name;
    while (*p && n < max) {
        const char *start = p;
        while (*p && *p != '.') p++;
        labels[n++] = start;
        if (*p == '.') p++;
    }
    return n;
}
static bool label_eq(const char *a, const char *b) {
    while (*a && *a != '.' && *b && *b != '.') {
        if (*a != *b) return false;
        a++; b++;
    }
    return (*a == '.' || *a == '\0') && (*b == '.' || *b == '\0');
}
/* qname matches zone iff qname has >= zone's label count and its last N
   labels equal the zone's labels (label-boundary suffix match). Takes
   PRE-SPLIT qname labels so dns_lookup doesn't re-split the qname once per
   zone (64-zone config: 64 redundant label_split passes over the qname). */
static bool zone_matches_split(const char *ql[], int qn, const char *zone) {
    const char *zl[64];
    int zn = label_split(zone, zl, 64);
    if (zn > qn) return false;
    for (int i = 0; i < zn; i++)
        if (!label_eq(ql[qn - 1 - i], zl[zn - 1 - i])) return false;
    return true;
}
static int label_count(const char *name) {
    int n = 0; const char *p = name;
    while (*p) { if (*p == '.') n++; p++; }
    return n;
}

int dns_lookup(const DnsConfig *cfg, const char *qname, uint16_t qtype,
               DnsRecord *out, int outcap, int *nout, int *rcode) {
    *nout = 0;
    /* (a) choose longest (most specific) matching zone by label-boundary suffix.
       Split the qname ONCE here (label_split) instead of once per zone inside
       zone_matches — with many zones the per-zone qname re-split dominates. */
    const char *ql[128];
    int qn = label_split(qname, ql, 128);
    const DnsZone *best = NULL; int best_labels = -1;
    for (int i = 0; i < cfg->nzones; i++) {
        if (!zone_matches_split(ql, qn, cfg->zones[i].name)) continue;
        int c = label_count(cfg->zones[i].name);
        if (c > best_labels) { best_labels = c; best = &cfg->zones[i]; }
    }
    /* (b) no zone matches -> NXDOMAIN. */
    if (!best) { *rcode = R_NXDOMAIN; return 0; }
    /* (c-f) Single pass: track name existence (NXDOMAIN vs NODATA) and collect
       matching records (ANY -> all). Collection is CAPPED at outcap copies: the
       caller (dns_handle_query) passes outcap = MAX_ANSWERS because the response
       builder never writes more than MAX_ANSWERS answers — copying 256 records
       just to cap away 240 of them is pure waste (and 28KB of stack churn).
       *nout reports the TOTAL matching count (one past outcap when more exist,
       i.e. nout = outcap + 1): dns_build_response uses nrecs > MAX_ANSWERS to
       set TC, so truncation semantics are preserved exactly. out[] holds
       min(nout, outcap) valid entries; callers must index at most outcap-1. */
    bool name_exists = false;
    int n = 0; bool more = false;
    for (int i = 0; i < best->nrecords; i++) {
        const DnsRecord *r = &best->records[i];
        if (strcmp(r->owner, qname) != 0) continue;
        name_exists = true;
        if (qtype != T_ANY && dns_type_to_wire(r->type) != qtype) continue;
        if (n < outcap) {
            out[n++] = *r;
        } else {
            more = true;               /* keep scanning only to count the total */
            break;
        }
    }
    /* (d) name absent -> NXDOMAIN. */
    if (!name_exists) { *rcode = R_NXDOMAIN; return 0; }
    *nout = (more) ? n + 1 : n;        /* n+1 signals "more than outcap exist" */
    *rcode = R_NOERROR;
    return 0;
}

/* Handle one received UDP packet. See dnsd.h for the full semantics. */
int dns_handle_query(const uint8_t *buf, size_t len,
                     uint8_t *resp, size_t cap, const DnsConfig *cfg) {
    /* Defensive: never dereference a null config. */
    if (!cfg) return -1;
    /* <12 bytes: drop silently. */
    if (len < 12) return -1;
    DnsHeader h;
    if (dns_parse_header(buf, len, &h) != 0) return -1;
    /* QR bit set: we are authoritative, never receive responses -> drop. */
    if (h.flags & F_QR) return -1;
    /* QDCOUNT != 1 -> FORMERR (echo id, empty question). */
    if (h.qdcount != 1)
        return dns_build_response(resp, cap, &h, "", 0, NULL, 0, R_FORMERR);
    /* opcode != 0 -> NOTIMP. */
    uint16_t opcode = (uint16_t)((h.flags >> 11) & 0xF);
    if (opcode != 0)
        return dns_build_response(resp, cap, &h, "", 0, NULL, 0, R_NOTIMP);
    /* Decode the question qname (no compression pointers allowed). */
    size_t off = 12;
    char qname[MAX_NAME_LEN + 1];
    if (dns_decode_qname(buf, len, &off, qname, sizeof qname) != 0)
        return dns_build_response(resp, cap, &h, "", 0, NULL, 0, R_FORMERR);
    /* qtype + qclass. */
    if (off + 4 > len)
        return dns_build_response(resp, cap, &h, qname, 0, NULL, 0, R_FORMERR);
    uint16_t qtype = dns_rd16(buf + off);
    uint16_t qclass = dns_rd16(buf + off + 2);
    /* qclass != IN -> REFUSED. (EDNS0 OPT records live in the additional
       section, which we never parse; we simply respond ARCOUNT=0.) */
    if (qclass != C_IN)
        return dns_build_response(resp, cap, &h, qname, qtype, NULL, 0, R_REFUSED);
    /* Lookup + answer. Collect at most MAX_ANSWERS records (the response
       builder caps at MAX_ANSWERS anyway; dns_lookup signals "more exist"
       via nout so the TC bit is still set). Keeps the stack array at
       16 records (~1.8KB) instead of MAX_RECORDS (~28KB). */
    DnsRecord ans[MAX_ANSWERS];
    int nans = 0, rcode = R_NOERROR;
    dns_lookup(cfg, qname, qtype, ans, MAX_ANSWERS, &nans, &rcode);
    return dns_build_response(resp, cap, &h, qname, qtype, ans, nans, rcode);
}
