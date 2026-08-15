/* wire_check — Stage-2 test: build DNS responses for all 8 record types plus
   NXDOMAIN, emit each as a labeled hex line on stdout. tests/run.sh pipes each
   hex through tests/dnsproto.py to decode and assert. */
#include "dhall.h"
#include "dnsd.h"
#include <stdio.h>
#include <string.h>

static void hexline(const char *label, const uint8_t *b, int n) {
    printf("%s ", label);
    for (int i = 0; i < n; i++) printf("%02x", b[i]);
    printf("\n");
}

/* Build and print one response for a fixed question + a single answer. */
static void emit(DnsHeader *qh, const char *qname, uint16_t qtype,
                 const DnsRecord *recs, int nrecs, int rcode) {
    uint8_t out[MAX_PKT];
    int n = dns_build_response(out, sizeof out, qh, qname, qtype, recs, nrecs, rcode);
    if (n < 0) { printf("ERROR overflow\n"); return; }
    hexline("PKT", out, n);
}

int main(void) {
    DnsHeader qh;
    memset(&qh, 0, sizeof qh);
    qh.id = 0x1234;
    qh.flags = 0x0100; /* RD set, opcode=0, qdcount=1 */
    qh.qdcount = 1;

    DnsRecord r;
    memset(&r, 0, sizeof r);
    r.ttl = 3600;
    r.owner = "www.example.com.";

    r.type = RT_A;   r.value = "192.0.2.9";          emit(&qh, "www.example.com.", T_A, &r, 1, R_NOERROR);
    r.type = RT_AAAA;r.value = "2001:db8::9";        emit(&qh, "www.example.com.", T_AAAA, &r, 1, R_NOERROR);
    r.type = RT_CNAME; r.value = "example.com.";     emit(&qh, "www.example.com.", T_CNAME, &r, 1, R_NOERROR);
    r.type = RT_TXT; r.value = "hello world";        emit(&qh, "www.example.com.", T_TXT, &r, 1, R_NOERROR);
    r.type = RT_CAA; r.flags = 0; r.tag = "issue"; r.value = "letsencrypt.org";
                                                     emit(&qh, "www.example.com.", T_CAA, &r, 1, R_NOERROR);
    r.type = RT_MX;  r.priority = 10; r.exchange = "mail.example.com.";
                                                     emit(&qh, "www.example.com.", T_MX, &r, 1, R_NOERROR);
    r.type = RT_NS;  r.value = "ns1.example.com.";   emit(&qh, "www.example.com.", T_NS, &r, 1, R_NOERROR);
    r.type = RT_SOA; r.mname = "ns1.example.com."; r.rname = "hostmaster.example.com.";
    r.serial = 2024010101; r.refresh = 7200; r.retry = 3600;
    r.expire = 1209600; r.minimum = 300;             emit(&qh, "www.example.com.", T_SOA, &r, 1, R_NOERROR);
    /* NXDOMAIN: no answers. */
    emit(&qh, "nonexistent.example.com.", T_A, NULL, 0, R_NXDOMAIN);
    /* NODATA: name exists, no answer of qtype -> NOERROR empty. */
    emit(&qh, "www.example.com.", T_A, NULL, 0, R_NOERROR);
    return 0;
}
