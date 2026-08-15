/* dnsd-wasm.c — browser-callable entry point for the compendium DNS server.
 *
 * Built to wasm via emscripten INSTEAD OF main.c (this file defines the entry
 * surface; main.c and rl.c are NOT linked — the UDP serve loop and its
 * per-source rate limiter are irrelevant to a single-query browser demo, and
 * rl.c is self-contained so omitting it is a plain no-op at link time).
 *
 * Exports (all EMSCRIPTEN_KEEPALIVE):
 *   int           dnsd_config(const char *src)     — load a Dhall config string
 *   int           dnsd_query(const char *qname, int qtype) — run one lookup
 *   const char   *dnsd_err(void)                   — last error message
 *   const uint8_t*dnsd_resp(void)                  — raw response wire bytes
 *   int           dnsd_resp_len(void)              — response length
 *   const char   *dnsd_json(void)                  — decoded answer as JSON
 *   int           dnsd_json_len(void)
 *
 * The Dhall config arrives as a JS string. We write it into emscripten's MEMFS
 * (via plain fopen/fwrite — no stdio shim needed, MEMFS backs the libc stdio)
 * and call the UNCHANGED config_load() from src/config.c. The full parse →
 * typecheck → normalize → walk pipeline runs exactly as in the native binary.
 *
 * A query runs the real src/dns.c dns_handle_query() on a raw query packet we
 * build here (header + label-encoded qname + qtype + qclass=IN), producing the
 * raw response bytes for hex display. We ALSO call dns_lookup() directly to get
 * the decoded DnsRecord[] and render a compact JSON summary (records / NXDOMAIN
 * / NODATA) — no wire re-parsing in JS, and no name-decompression in JS.
 */
#include "dhall.h"
#include "dnsd.h"
#include <emscripten.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static DnsConfig g_cfg;
static int       g_loaded = 0;
static char      g_err[512];
static uint8_t   g_resp[MAX_PKT];
static int       g_resp_len = 0;
static char     *g_json = NULL;
static size_t    g_json_len = 0;

EMSCRIPTEN_KEEPALIVE
int dnsd_config(const char *src) {
    g_loaded = 0;
    g_err[0] = '\0';
    if (!src) { snprintf(g_err, sizeof g_err, "null config source"); return -1; }

    FILE *f = fopen("/config.dhall", "wb");
    if (!f) { snprintf(g_err, sizeof g_err, "MEMFS open failed"); return -1; }
    size_t len = strlen(src);
    if (len > 0 && fwrite(src, 1, len, f) != len) {
        fclose(f);
        snprintf(g_err, sizeof g_err, "MEMFS write failed");
        return -1;
    }
    fclose(f);

    int rc = config_load(&g_cfg, "/config.dhall", g_err, sizeof g_err);
    if (rc != 0) return -1;
    g_loaded = 1;
    return g_cfg.nzones;
}

static int canon_qname(const char *in, char *out, size_t cap) {
    size_t n = strlen(in);
    if (n == 0) return -1;
    if (in[n - 1] == '.') n--;
    if (n + 2 > cap) return -1;
    for (size_t i = 0; i < n; i++) {
        char c = in[i];
        out[i] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
    }
    out[n] = '.'; out[n + 1] = '\0';
    return 0;
}

static int build_query_packet(uint8_t *pkt, size_t cap, const char *qname, uint16_t qtype) {
    if (cap < 12 + 1 + 4) return -1;
    memset(pkt, 0, cap);
    pkt[0] = 0x12; pkt[1] = 0x34;
    pkt[2] = 0x01; pkt[3] = 0x00;
    pkt[4] = 0x00; pkt[5] = 0x01;
    size_t o = 12;
    const char *p = qname;
    while (*p) {
        const char *dot = strchr(p, '.');
        size_t lablen = dot ? (size_t)(dot - p) : strlen(p);
        if (lablen == 0) { p++; continue; }
        if (lablen > 63 || o + 1 + lablen + 1 + 4 > cap) return -1;
        pkt[o++] = (uint8_t)lablen;
        memcpy(pkt + o, p, lablen); o += lablen;
        p = dot ? dot + 1 : p + lablen;
    }
    pkt[o++] = 0;
    pkt[o++] = (uint8_t)(qtype >> 8); pkt[o++] = (uint8_t)(qtype & 0xFF);
    pkt[o++] = 0x00; pkt[o++] = 0x01;
    return (int)o;
}

static void json_str(FILE *f, const char *s) {
    fputc('"', f);
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        unsigned char c = *p;
        switch (c) {
        case '"':  fputs("\\\"", f); break;
        case '\\': fputs("\\\\", f); break;
        case '\n': fputs("\\n",  f); break;
        case '\r': fputs("\\r",  f); break;
        case '\t': fputs("\\t",  f); break;
        default:
            if (c < 0x20) fprintf(f, "\\u%04x", c);
            else fputc(c, f);
        }
    }
    fputc('"', f);
}

static const char *type_name(RecType t) {
    switch (t) {
    case RT_A: return "A";
    case RT_AAAA: return "AAAA";
    case RT_CNAME: return "CNAME";
    case RT_TXT: return "TXT";
    case RT_MX: return "MX";
    case RT_NS: return "NS";
    case RT_SOA: return "SOA";
    case RT_CAA: return "CAA";
    }
    return "?";
}

static void rdata_str(const DnsRecord *r, char *out, size_t cap) {
    switch (r->type) {
    case RT_A:     snprintf(out, cap, "%s", r->value); break;
    case RT_AAAA:  snprintf(out, cap, "%s", r->value); break;
    case RT_CNAME: snprintf(out, cap, "%s", r->value); break;
    case RT_NS:    snprintf(out, cap, "%s", r->value); break;
    case RT_TXT:   snprintf(out, cap, "\"%s\"", r->value); break;
    case RT_CAA:   snprintf(out, cap, "%u %s \"%s\"", r->flags, r->tag, r->value); break;
    case RT_MX:    snprintf(out, cap, "%u %s", r->priority, r->exchange); break;
    case RT_SOA:   snprintf(out, cap, "%s %s %u %u %u %u %u",
                            r->mname, r->rname, r->serial, r->refresh, r->retry,
                            r->expire, r->minimum); break;
    default:       snprintf(out, cap, ""); break;
    }
}

static const char *rcode_name(int rcode) {
    switch (rcode) {
    case R_NOERROR: return "NOERROR";
    case R_FORMERR: return "FORMERR";
    case R_NXDOMAIN: return "NXDOMAIN";
    case R_NOTIMP: return "NOTIMP";
    case R_REFUSED: return "REFUSED";
    }
    return "UNKNOWN";
}

static void render_json(const char *qname, uint16_t qtype,
                        const DnsRecord *recs, int nrecs, int rcode) {
    free(g_json);
    g_json = NULL;
    g_json_len = 0;
    FILE *f = open_memstream(&g_json, &g_json_len);
    if (!f) return;

    const char *status = (rcode == R_NOERROR && nrecs == 0) ? "NODATA" : rcode_name(rcode);
    fprintf(f, "{\"status\":");
    json_str(f, status);
    fprintf(f, ",\"rcode\":%d,\"rcodeName\":", rcode);
    json_str(f, rcode_name(rcode));
    fprintf(f, ",\"qname\":");
    json_str(f, qname);
    fprintf(f, ",\"qtype\":%u,\"qtypeName\":", qtype);
    switch (qtype) {
    case T_A: json_str(f, "A"); break;
    case T_AAAA: json_str(f, "AAAA"); break;
    case T_CNAME: json_str(f, "CNAME"); break;
    case T_TXT: json_str(f, "TXT"); break;
    case T_MX: json_str(f, "MX"); break;
    case T_NS: json_str(f, "NS"); break;
    case T_SOA: json_str(f, "SOA"); break;
    case T_CAA: json_str(f, "CAA"); break;
    case T_ANY: json_str(f, "ANY"); break;
    default: fprintf(f, "\"type%u\"", qtype); break;
    }
    fprintf(f, ",\"answers\":[");
    for (int i = 0; i < nrecs; i++) {
        if (i) fputc(',', f);
        char rd[512];
        rdata_str(&recs[i], rd, sizeof rd);
        fprintf(f, "{\"type\":");
        json_str(f, type_name(recs[i].type));
        fprintf(f, ",\"owner\":");
        json_str(f, recs[i].owner);
        fprintf(f, ",\"ttl\":%u,\"rdata\":", recs[i].ttl);
        json_str(f, rd);
        fputc('}', f);
    }
    fprintf(f, "]}");
    fclose(f);
}

EMSCRIPTEN_KEEPALIVE
int dnsd_query(const char *qname, int qtype) {
    g_resp_len = 0;
    if (!g_loaded) { snprintf(g_err, sizeof g_err, "load a config first"); return -1; }
    if (!qname) { snprintf(g_err, sizeof g_err, "null qname"); return -1; }

    char qn[MAX_NAME_LEN + 1];
    if (canon_qname(qname, qn, sizeof qn) != 0) {
        snprintf(g_err, sizeof g_err, "invalid qname '%s'", qname);
        return -1;
    }

    uint8_t pkt[MAX_PKT];
    int plen = build_query_packet(pkt, sizeof pkt, qn, (uint16_t)qtype);
    if (plen < 0) { snprintf(g_err, sizeof g_err, "query build failed"); return -1; }

    int rlen = dns_handle_query(pkt, (size_t)plen, g_resp, sizeof g_resp, &g_cfg);
    if (rlen < 0) { snprintf(g_err, sizeof g_err, "dns_handle_query dropped the packet"); return -1; }
    g_resp_len = rlen;

    DnsRecord ans[MAX_ANSWERS];
    int nans = 0, rcode = R_NOERROR;
    dns_lookup(&g_cfg, qn, (uint16_t)qtype, ans, MAX_ANSWERS, &nans, &rcode);
    render_json(qn, (uint16_t)qtype, ans, nans, rcode);
    return rlen;
}

EMSCRIPTEN_KEEPALIVE
const char *dnsd_err(void) { return g_err; }

EMSCRIPTEN_KEEPALIVE
const uint8_t *dnsd_resp(void) { return g_resp; }

EMSCRIPTEN_KEEPALIVE
int dnsd_resp_len(void) { return g_resp_len; }

EMSCRIPTEN_KEEPALIVE
const char *dnsd_json(void) { return g_json ? g_json : ""; }

EMSCRIPTEN_KEEPALIVE
int dnsd_json_len(void) { return (int)g_json_len; }
