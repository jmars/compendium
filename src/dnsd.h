/* dnsd.h — shared data model + DNS wire constants/API for the dnsd server.
   Links the dhall-c interpreter core (parse_source -> normalize -> walk the
   normalized Term tree into the DnsConfig structs below). Records-only,
   authoritative, UDP-only. */
#ifndef DNSD_H
#define DNSD_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define MAX_ZONES        64
#define MAX_RECORDS     256
#define MAX_NAME_LEN    255
#define MAX_TEXT_LEN   1024
#define MAX_PKT         512
#define MAX_ANSWERS     16     /* cap answer RRs per response; TC set when exceeded */

/* DNS wire constants (RFC 1035). */
enum {
    T_A = 1, T_NS = 2, T_CNAME = 5, T_SOA = 6, T_MX = 15, T_TXT = 16,
    T_AAAA = 28, T_CAA = 257, T_ANY = 255,
    C_IN = 1,
    R_NOERROR = 0, R_FORMERR = 1, R_NXDOMAIN = 3, R_NOTIMP = 4, R_REFUSED = 5,
    F_QR = 0x8000, F_AA = 0x0400, F_TC = 0x0200, F_RD = 0x0100
};

typedef enum { RT_A, RT_AAAA, RT_CNAME, RT_TXT, RT_MX, RT_NS, RT_SOA, RT_CAA } RecType;

/* One record. `name` is the owner relative to its zone ("@" / "" = apex);
   `owner` is the full owner FQDN (lowercase, trailing dot) built from the
   relative name + zone during config load. rdata target names (value for
   CNAME/NS, exchange for MX, mname/rname for SOA) are canonicalized to
   lowercase with a trailing dot. All char* are arena-allocated (dhall_arena)
   and live for the process lifetime. */
typedef struct {
    RecType type;
    char *name;            /* owner, relative to zone ("@" or "" = apex) */
    char *owner;           /* full owner FQDN, lowercase, trailing dot */
    uint32_t ttl;
    char *value;           /* A/AAAA/CNAME/TXT/NS/CAA value */
    uint16_t priority;     /* MX */
    char *exchange;        /* MX */
    uint8_t flags;         /* CAA */
    char *tag;             /* CAA */
    char *mname, *rname;   /* SOA */
    uint32_t serial, refresh, retry, expire, minimum; /* SOA */
    uint32_t addr4;        /* A rdata parsed ONCE at config load (was inet_pton
                              per query in build_answer — 10.6ns/call x answers) */
    uint8_t addr6[16];     /* AAAA rdata parsed once at config load (24.3ns/call) */
} DnsRecord;

typedef struct {
    char *name;            /* zone FQDN, lowercase, trailing dot */
    DnsRecord records[MAX_RECORDS];
    int nrecords;
} DnsZone;

typedef struct {
    DnsZone zones[MAX_ZONES];
    int nzones;
} DnsConfig;

/* config.c — evaluate a Dhall config file and walk its normal form into cfg.
   Returns 0 on success, -1 on failure (message written to errbuf/errcap). */
int config_load(DnsConfig *cfg, const char *path, char *errbuf, size_t errcap);

/* dns.c — wire format + lookup semantics. */
typedef struct {
    uint16_t id, flags, qdcount, ancount, nscount, arcount;
} DnsHeader;

uint16_t dns_rd16(const uint8_t *p);   /* big-endian 16-bit read */
int dns_parse_header(const uint8_t *pkt, size_t len, DnsHeader *h);
int dns_decode_qname(const uint8_t *pkt, size_t len, size_t *off,
                     char *out, size_t cap);
uint16_t dns_type_to_wire(RecType t);

/* Look up qname (lowercase, trailing dot) of qtype in cfg. Fills out[] with at
   most outcap matching records, sets *nout to the TOTAL matching count (may
   exceed outcap by 1 to signal that more records exist — callers must index
   out[] no further than outcap-1) and *rcode:
     NXDOMAIN no zone matched or name absent,
     NOERROR with n>0 answers, or NOERROR with n==0 (NODATA) when the name
     exists but has no record of qtype (or qtype==ANY: all records). */
int dns_lookup(const DnsConfig *cfg, const char *qname, uint16_t qtype,
               DnsRecord *out, int outcap, int *nout, int *rcode);

/* Build a full response packet (header + echoed question + answers) using
   suffix name-compression. Returns packet length, or -1 on overflow. */
int dns_build_response(uint8_t *out, size_t cap, const DnsHeader *qh,
                       const char *qname, uint16_t qtype,
                       const DnsRecord *recs, int nrecs, int rcode);

/* Handle one received UDP packet: FORMERR on malformed (or qdcount!=1, or a
   bad qname), NOTIMP on opcode!=0, REFUSED on qclass!=IN, drop (<0) on short
   packets or QR bit set; otherwise lookup + answer. Writes the response into
   resp and returns its length, or -1 to drop. EDNS0 additional section is
   ignored gracefully (responded ARCOUNT=0). */
int dns_handle_query(const uint8_t *buf, size_t len,
                     uint8_t *resp, size_t cap, const DnsConfig *cfg);

/* rl.c — per-source token-bucket rate limiter (server-level DoS hardening).
   A fixed-size table of buckets lives in main.c; the pure token decision below
   is factored out so it can be unit-tested directly (see src/rl_check.c).
   Tokens are stored as fixed-point micro-tokens (RL_SCALE == 1 whole token). */
#define RL_BUCKETS        1024          /* table size (power of two) */
#define RL_BURST_TOKENS   100           /* per-source burst capacity (tokens) */
#define RL_RATE_PER_SEC   20            /* per-source refill rate (tokens/sec) */
#define RL_SCALE          1000000ULL
#define RL_CAPACITY       ((uint64_t)RL_BURST_TOKENS * RL_SCALE)
#define RL_RATE_PER_US    (((uint64_t)RL_RATE_PER_SEC * RL_SCALE) / 1000000ULL)
#define RL_MAX_ELAPSED_US 1000000ULL    /* clamp refill window (clock wrap/suspend) */
/* Global token bucket: bounds total CPU regardless of how the traffic is
   distributed across sources. A rotating-spoofed-source flood (every distinct
   IP gets a fresh per-source burst) cannot exceed this ceiling, so the server
   always has a hard upper bound on queries it processes per unit time. */
#define RL_GLOBAL_BURST_TOKENS  500
#define RL_GLOBAL_RATE_PER_SEC  100
#define RL_GLOBAL_CAPACITY      ((uint64_t)RL_GLOBAL_BURST_TOKENS * RL_SCALE)
#define RL_GLOBAL_RATE_PER_US   (((uint64_t)RL_GLOBAL_RATE_PER_SEC * RL_SCALE) / 1000000ULL)

typedef struct {
    uint32_t ip;            /* source IP (opaque key; 0 = empty slot) */
    uint64_t tokens;        /* micro-tokens (RL_SCALE = 1 token) */
    uint64_t last_us;       /* CLOCK_MONOTONIC us of last refill */
    uint64_t created_us;    /* CLOCK_MONOTONIC us bucket was created (eviction) */
} RlBucket;

/* Refill *b up to capacity using now_us (monotonic) and consume one token if
   available. Returns true to process the packet, false to drop it (rate
   limited). Handles clock wrap / suspend by clamping the refill window. */
bool rl_allow(RlBucket *b, uint64_t now_us);
/* Capacity-aware variant: refills up to cap_microtokens instead of the default
   RL_CAPACITY. Used by the GLOBAL token bucket so it can hold a larger burst
   than a per-source bucket. Same refill-rate-per-second base. */
bool rl_allow_cap(RlBucket *b, uint64_t now_us, uint64_t cap_microtokens,
                  uint64_t rate_per_us);

#endif /* DNSD_H */
