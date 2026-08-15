/* lookup_check — Stage-2/4 test: exercise dns_lookup() directly (no sockets)
   against config.example.dhall, asserting the plan's lookup semantics:
   zone match (label-boundary), longest match, NXDOMAIN/NODATA/NOERROR/ANY.
   Usage: lookup_check.com.dbg [config.dhall] */
#include "dhall.h"
#include "dnsd.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;
static void check(const char *name, uint16_t qtype,
                  const DnsConfig *cfg, int want_rcode, int want_n) {
    DnsRecord out[MAX_RECORDS];
    int n = 0, rcode = -1;
    dns_lookup(cfg, name, qtype, out, MAX_RECORDS, &n, &rcode);
    if (rcode != want_rcode || n != want_n) {
        fprintf(stderr, "FAIL %s/%d: rcode=%d n=%d (want rcode=%d n=%d)\n",
                name, qtype, rcode, n, want_rcode, want_n);
        failures++;
    } else {
        printf("ok   %s type=%d -> rcode=%d answers=%d\n", name, qtype, rcode, n);
    }
}

int main(int argc, char **argv) {
    const char *path = (argc > 1) ? argv[1] : "config.example.dhall";
    DnsConfig cfg;
    char errbuf[512];
    if (config_load(&cfg, path, errbuf, sizeof errbuf) != 0) {
        fprintf(stderr, "config error: %s\n", errbuf);
        return 1;
    }
    /* apex A: exists -> NOERROR 1 answer */
    check("example.com.", T_A, &cfg, R_NOERROR, 1);
    /* apex AAAA: exists -> NOERROR 1 */
    check("example.com.", T_AAAA, &cfg, R_NOERROR, 1);
    /* www CNAME: exists -> NOERROR 1 */
    check("www.example.com.", T_CNAME, &cfg, R_NOERROR, 1);
    /* www A: name exists (www) but no A -> NODATA (NOERROR 0) */
    check("www.example.com.", T_A, &cfg, R_NOERROR, 0);
    /* ANY at apex -> all 7 apex records (SOA,NS,A,AAAA,MX,TXT,CAA) */
    check("example.com.", T_ANY, &cfg, R_NOERROR, 7);
    /* name absent within zone -> NXDOMAIN */
    check("nosuch.example.com.", T_A, &cfg, R_NXDOMAIN, 0);
    /* no zone matches -> NXDOMAIN (label-boundary: badexample.com != example.com) */
    check("badexample.com.", T_A, &cfg, R_NXDOMAIN, 0);
    /* no zone at all -> NXDOMAIN */
    check("example.net.", T_A, &cfg, R_NXDOMAIN, 0);
    /* second zone apex A */
    check("example.org.", T_A, &cfg, R_NOERROR, 1);
    /* subdomain within a zone that has no records at that owner -> NXDOMAIN
       (MVP: empty non-terminals treated as NXDOMAIN) */
    check("a.b.example.com.", T_A, &cfg, R_NXDOMAIN, 0);

    if (failures) { fprintf(stderr, "%d lookup checks failed\n", failures); return 1; }
    printf("all lookup checks passed\n");
    return 0;
}
