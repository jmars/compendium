/* config_check — Stage-1 test: load a Dhall config and print the walked
   zones/records to stdout for assertion by tests/run.sh.
   Usage: config_check.com.dbg [config.dhall]   (default config.example.dhall) */
#include "dhall.h"
#include "dnsd.h"
#include <stdio.h>

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

int main(int argc, char **argv) {
    const char *path = (argc > 1) ? argv[1] : "config.example.dhall";
    DnsConfig cfg;
    char errbuf[512];
    if (config_load(&cfg, path, errbuf, sizeof errbuf) != 0) {
        fprintf(stderr, "config error: %s\n", errbuf);
        return 1;
    }
    printf("zones=%d\n", cfg.nzones);
    for (int i = 0; i < cfg.nzones; i++) {
        DnsZone *z = &cfg.zones[i];
        printf("zone=%s records=%d\n", z->name, z->nrecords);
        for (int j = 0; j < z->nrecords; j++) {
            DnsRecord *r = &z->records[j];
            printf("  %s %s ttl=%u", r->owner, type_name(r->type), r->ttl);
            switch (r->type) {
            case RT_A: case RT_AAAA: case RT_CNAME: case RT_TXT: case RT_NS:
                printf(" value=%s", r->value); break;
            case RT_MX:
                printf(" priority=%u exchange=%s", r->priority, r->exchange); break;
            case RT_SOA:
                printf(" mname=%s rname=%s serial=%u refresh=%u retry=%u expire=%u minimum=%u",
                       r->mname, r->rname, r->serial, r->refresh, r->retry, r->expire, r->minimum);
                break;
            case RT_CAA:
                printf(" flags=%u tag=%s value=%s", r->flags, r->tag, r->value); break;
            }
            printf("\n");
        }
    }
    return 0;
}
