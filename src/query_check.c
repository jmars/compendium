/* query_check — exercise dns_handle_query() dispatch in-process (no sockets):
   valid answer, NXDOMAIN, NODATA, FORMERR (short / qdcount!=1 / bad qname),
   NOTIMP (opcode!=0), REFUSED (qclass!=IN), and QR-bit drop.
   Usage: query_check.com.dbg [config.dhall] */
#include "dhall.h"
#include "dnsd.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;
static DnsConfig gcfg;

/* Build a query packet: qname labels + qtype/qclass + optional header flags. */
static size_t make_query(uint8_t *out, uint16_t id, uint16_t flags,
                         uint16_t qdcount, const char *qname, uint16_t qtype,
                         uint16_t qclass) {
    size_t o = 12;
    out[0] = (uint8_t)(id >> 8); out[1] = (uint8_t)id;
    out[2] = (uint8_t)(flags >> 8); out[3] = (uint8_t)flags;
    out[4] = (uint8_t)(qdcount >> 8); out[5] = (uint8_t)qdcount;
    memset(out + 6, 0, 6); /* an/ns/ar = 0 */
    if (qname) {
        const char *p = qname;
        while (*p) {
            const char *q = p; size_t l = 0;
            while (*q && *q != '.') { q++; l++; }
            out[o++] = (uint8_t)l;
            memcpy(out + o, p, l); o += l;
            p = (*q == '.') ? q + 1 : q;
        }
    }
    out[o++] = 0;
    out[o++] = (uint8_t)(qtype >> 8); out[o++] = (uint8_t)qtype;
    out[o++] = (uint8_t)(qclass >> 8); out[o++] = (uint8_t)qclass;
    return o;
}

/* Run dns_handle_query and assert response rcode (rd16 of byte 2) or drop. */
static void run(const char *label, const uint8_t *pkt, size_t n, int want_rcode) {
    uint8_t resp[MAX_PKT];
    int r = dns_handle_query(pkt, n, resp, sizeof resp, &gcfg);
    if (want_rcode < 0) {
        if (r < 0) { printf("ok   %s -> drop\n", label); return; }
        fprintf(stderr, "FAIL %s: expected drop, got len %d\n", label, r);
        failures++; return;
    }
    if (r < 0) { fprintf(stderr, "FAIL %s: dropped unexpectedly\n", label); failures++; return; }
    uint16_t rc = dns_rd16(resp + 2) & 0xF;
    if (rc != (uint16_t)want_rcode) {
        fprintf(stderr, "FAIL %s: rcode=%d (want %d)\n", label, rc, want_rcode);
        failures++;
    } else {
        printf("ok   %s -> rcode=%d\n", label, rc);
    }
}

int main(int argc, char **argv) {
    const char *path = (argc > 1) ? argv[1] : "config.example.dhall";
    char errbuf[512];
    if (config_load(&gcfg, path, errbuf, sizeof errbuf) != 0) {
        fprintf(stderr, "config error: %s\n", errbuf);
        return 1;
    }
    uint8_t pkt[MAX_PKT];
    size_t n;

    /* Valid apex A query -> NOERROR. */
    n = make_query(pkt, 0x1111, 0x0100, 1, "example.com.", T_A, C_IN);
    run("valid A", pkt, n, R_NOERROR);
    /* NXDOMAIN. */
    n = make_query(pkt, 0x1111, 0x0100, 1, "nosuch.example.com.", T_A, C_IN);
    run("NXDOMAIN", pkt, n, R_NXDOMAIN);
    /* NODATA: www exists but no A. */
    n = make_query(pkt, 0x1111, 0x0100, 1, "www.example.com.", T_A, C_IN);
    run("NODATA (NOERROR empty)", pkt, n, R_NOERROR);
    /* Short packet (<12 bytes) -> drop. */
    memcpy(pkt, "\x12\x34\x01", 3);
    run("short packet drop", pkt, 3, -1);
    /* QR bit set -> drop. */
    n = make_query(pkt, 0x1111, 0x8100, 1, "example.com.", T_A, C_IN);
    run("QR-bit drop", pkt, n, -1);
    /* QDCOUNT != 1 -> FORMERR. */
    n = make_query(pkt, 0x1111, 0x0100, 2, "example.com.", T_A, C_IN);
    run("qdcount=2 FORMERR", pkt, n, R_FORMERR);
    /* opcode != 0 -> NOTIMP. */
    n = make_query(pkt, 0x1111, 0x1100, 1, "example.com.", T_A, C_IN);
    run("opcode=1 NOTIMP", pkt, n, R_NOTIMP);
    /* qclass != IN -> REFUSED. */
    n = make_query(pkt, 0x1111, 0x0100, 1, "example.com.", T_A, 3);
    run("qclass=3 REFUSED", pkt, n, R_REFUSED);
    /* EDNS0: valid question + an additional OPT RR (arcount=1) -> still
       NOERROR (additional ignored). */
    n = make_query(pkt, 0x1111, 0x0100, 1, "example.com.", T_A, C_IN);
    /* append an OPT record body (name=0, type=41, class=1232, ttl=0, rdlen=0) */
    size_t o = n;
    pkt[10] = 0; pkt[11] = 1;                       /* arcount = 1 */
    pkt[o++] = 0;                                    /* OPT owner (root) */
    pkt[o++] = 0; pkt[o++] = 41;                     /* TYPE = OPT */
    pkt[o++] = 0x04; pkt[o++] = 0xD0;                /* CLASS = UDP payload 1232 */
    memset(pkt + o, 0, 6); o += 6;                   /* TTL + RDLEN = 0 */
    run("EDNS0 ignored", pkt, o, R_NOERROR);
    /* Bad qname (compression pointer in question) -> FORMERR. */
    n = 12;
    pkt[0]=0x11;pkt[1]=0x11; pkt[2]=0x01;pkt[3]=0x00;
    pkt[4]=0;pkt[5]=1; memset(pkt+6,0,6);
    pkt[n++] = 0xC0; pkt[n++] = 0x0C;                /* compression pointer */
    pkt[n++] = 0; pkt[n++] = 1; pkt[n++] = 0; pkt[n++] = 1;
    run("bad qname FORMERR", pkt, n, R_FORMERR);

    /* Answer cap: dns_build_response with >MAX_ANSWERS records must write only
       MAX_ANSWERS answers and set the TC bit (valid truncated response). */
    {
        DnsHeader qh; memset(&qh, 0, sizeof qh);
        qh.id = 0x6666; qh.flags = 0x0100; qh.qdcount = 1;
        DnsRecord recs[MAX_ANSWERS + 4];
        for (int i = 0; i < MAX_ANSWERS + 4; i++) {
            memset(&recs[i], 0, sizeof recs[i]);
            recs[i].type = RT_A; recs[i].owner = "many.example.com.";
            recs[i].ttl = 60; recs[i].value = "192.0.2.1";
        }
        uint8_t resp2[MAX_PKT];
        int r = dns_build_response(resp2, sizeof resp2, &qh, "many.example.com.",
                                   T_A, recs, MAX_ANSWERS + 4, R_NOERROR);
        if (r < 0) { fprintf(stderr, "FAIL answer cap: build error\n"); failures++; }
        else {
            uint16_t flags = dns_rd16(resp2 + 2);
            uint16_t ancount = dns_rd16(resp2 + 6);
            if (!(flags & F_TC)) { fprintf(stderr, "FAIL answer cap: TC not set\n"); failures++; }
            if (ancount != MAX_ANSWERS) {
                fprintf(stderr, "FAIL answer cap: ancount=%u want %d\n", ancount, MAX_ANSWERS);
                failures++;
            }
            if (failures == 0) printf("ok   answer cap -> TC set, ancount=%d\n", ancount);
        }
    }

    /* ANY flood cap through the full dns_handle_query path: a zone with one
       owner holding >MAX_ANSWERS A records, queried via ANY, must yield an
       NOERROR response with TC set and exactly MAX_ANSWERS answers. */
    {
        DnsConfig big; memset(&big, 0, sizeof big);
        big.nzones = 1;
        DnsZone *z = &big.zones[0];
        z->name = "big.example.com.";
        z->nrecords = 0;
        for (int i = 0; i < MAX_ANSWERS + 4; i++) {
            DnsRecord *r = &z->records[z->nrecords++];
            memset(r, 0, sizeof *r);
            r->type = RT_A; r->owner = "big.example.com.";
            r->ttl = 60; r->value = "192.0.2.1";
        }
        n = make_query(pkt, 0x7777, 0x0100, 1, "big.example.com.", T_ANY, C_IN);
        uint8_t resp3[MAX_PKT];
        int r = dns_handle_query(pkt, n, resp3, sizeof resp3, &big);
        if (r < 0) { fprintf(stderr, "FAIL ANY cap: dropped\n"); failures++; }
        else {
            uint16_t flags = dns_rd16(resp3 + 2);
            uint16_t ancount = dns_rd16(resp3 + 6);
            if ((flags & 0xF) != R_NOERROR) { fprintf(stderr, "FAIL ANY cap: rcode!=NOERROR\n"); failures++; }
            if (!(flags & F_TC)) { fprintf(stderr, "FAIL ANY cap: TC not set\n"); failures++; }
            if (ancount != MAX_ANSWERS) {
                fprintf(stderr, "FAIL ANY cap: ancount=%u want %d\n", ancount, MAX_ANSWERS);
                failures++;
            }
            if (failures == 0) printf("ok   ANY flood cap -> TC set, ancount=%d\n", ancount);
        }
    }

    if (failures) { fprintf(stderr, "%d query checks failed\n", failures); return 1; }
    printf("all query checks passed\n");
    return 0;
}
