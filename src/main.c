/* main.c — dnsd CLI + UDP serve loop.
     dnsd --config|-c <file> [--port|-p <n>] [--address|-a <ip>]
   Reads the Dhall config (config_load), binds a UDP socket, and serves
   authoritative records: recvfrom -> dns_handle_query -> sendto. */
#include "dhall.h"
#include "dnsd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <time.h>

static void usage(const char *prog) {
    fprintf(stderr, "usage: %s --config <file> [--port <n>] [--address <ip>]\n", prog);
    fprintf(stderr, "  -c, --config <file>  Dhall config (required)\n");
    fprintf(stderr, "  -p, --port <n>       listen port (default 5353)\n");
    fprintf(stderr, "  -a, --address <ip>   listen address (default 127.0.0.1)\n");
}

/* ---------------------------------------------------------------- */
/* Per-source rate limiting (server-level DoS hardening).            */
/* A fixed-size open-addressed table keyed by source IP, with lazy   */
/* token-bucket refill (see rl.c). Bounded: never grows, evicts the  */
/* oldest-created entry when the probed region is full.              */
/* ---------------------------------------------------------------- */
static RlBucket rl_table[RL_BUCKETS];

/* Global token bucket: bounds total queries processed per unit time regardless
   of how many distinct source IPs are flooding. Checked BEFORE the per-source
   bucket so a rotating-spoofed-source flood cannot bypass the ceiling. */
static RlBucket rl_global = { .tokens = RL_GLOBAL_CAPACITY, .last_us = 0, .created_us = 0 };

/* Bound on linear-probe depth. Without this, an attacker who pre-fills
   the probe chain for a target hash slot (using spoofed source IPs that
   collide on that slot) forces every subsequent packet to scan up to
   RL_BUCKETS entries — a ~1000x per-packet CPU amplification. Capping
   the probe at 16 keeps the worst case at 16 probes/packet (~80 ns)
   regardless of table fill ratio. The tradeoff: a legit source whose
   16-slot probe window is full of colliding entries gets evicted and
   recreated at full RL_CAPACITY — which is a *grant* of a fresh burst,
   not a denial, so legit sources are not penalized. */
#define RL_MAX_PROBE 16

static uint64_t now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

/* Simple avalanche mix so table slots are well spread across source IPs. */
static uint32_t hash_ip(uint32_t ip) {
    ip ^= ip >> 16; ip *= 0x45d9f3bU;
    ip ^= ip >> 16; ip *= 0x45d9f3bU;
    ip ^= ip >> 16;
    return ip;
}

/* Find the bucket for `ip`, creating it on a miss (fresh buckets start full
   so a legitimate new source gets its burst). On a miss with no empty slot in
   the probed region, evict the oldest-created entry — memory stays bounded. */
static RlBucket *rl_get(uint32_t ip, uint64_t now) {
    uint32_t h = hash_ip(ip);
    RlBucket *evict = NULL;
    for (int i = 0; i < RL_MAX_PROBE; i++) {
        RlBucket *b = &rl_table[(h + (uint32_t)i) & (RL_BUCKETS - 1)];
        if (b->ip == ip) return b;
        if (b->ip == 0) {                       /* empty slot -> create */
            b->ip = ip; b->tokens = RL_CAPACITY;
            b->last_us = now; b->created_us = now;
            return b;
        }
        if (evict == NULL || b->created_us < evict->created_us) evict = b;
    }
    /* Table saturated: reuse the oldest-created bucket. */
    evict->ip = ip; evict->tokens = RL_CAPACITY;
    evict->last_us = now; evict->created_us = now;
    return evict;
}

int main(int argc, char **argv) {
    const char *config_path = NULL;
    const char *addr = "127.0.0.1";
    int port = 5353;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "-c") || !strcmp(a, "--config")) {
            if (i + 1 >= argc) { fprintf(stderr, "%s requires an argument\n", a); usage(argv[0]); return 2; }
            config_path = argv[++i];
        } else if (!strcmp(a, "-p") || !strcmp(a, "--port")) {
            if (i + 1 >= argc) { fprintf(stderr, "%s requires an argument\n", a); usage(argv[0]); return 2; }
            char *end = NULL;
            long v = strtol(argv[++i], &end, 10);
            if (!end || *end != '\0' || v < 1 || v > 65535) {
                fprintf(stderr, "invalid port '%s' (expected 1..65535)\n", argv[i]);
                return 2;
            }
            port = (int)v;
        } else if (!strcmp(a, "-a") || !strcmp(a, "--address")) {
            if (i + 1 >= argc) { fprintf(stderr, "%s requires an argument\n", a); usage(argv[0]); return 2; }
            addr = argv[++i];
        } else if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
            usage(argv[0]); return 0;
        } else {
            fprintf(stderr, "unknown argument '%s'\n", a); usage(argv[0]); return 2;
        }
    }
    if (!config_path) { fprintf(stderr, "--config is required\n"); usage(argv[0]); return 2; }

    /* Static: DnsConfig is ~1.84MB (64 zones x 256 records x 112B) and would
       be fragile on small embedded stacks. Startup-only, no hot-path impact. */
    static DnsConfig cfg;
    char errbuf[512];
    if (config_load(&cfg, config_path, errbuf, sizeof errbuf) != 0) {
        fprintf(stderr, "dnsd: %s\n", errbuf);
        return 1;
    }
    fprintf(stderr, "dnsd: loaded %d zone(s)\n", cfg.nzones);

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { fprintf(stderr, "dnsd: socket: %s\n", strerror(errno)); return 1; }

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, addr, &sa.sin_addr) != 1) {
        fprintf(stderr, "dnsd: invalid address '%s'\n", addr);
        return 1;
    }
    if (bind(fd, (struct sockaddr *)&sa, sizeof sa) < 0) {
        fprintf(stderr, "dnsd: bind %s:%d: %s\n", addr, port, strerror(errno));
        return 1;
    }
    fprintf(stderr, "dnsd: listening on %s:%d\n", addr, port);

    uint8_t buf[MAX_PKT], resp[MAX_PKT];
    for (;;) {
        struct sockaddr_in from;
        socklen_t fromlen = sizeof from;
        ssize_t n = recvfrom(fd, buf, sizeof buf, 0, (struct sockaddr *)&from, &fromlen);
        if (n < 0) { fprintf(stderr, "dnsd: recvfrom: %s\n", strerror(errno)); continue; }
        /* Per-source rate limit: drop silently when the source has exhausted
           its token bucket. Done before any query processing so a flood cannot
           burn CPU here, and dropped packets get NO response (no reflection). */
        uint64_t now = now_us();
        /* Global ceiling first: a distributed/rotating-source flood cannot
           exceed the global token budget, bounding total CPU no matter how the
           traffic is split across source IPs. */
        if (!rl_allow_cap(&rl_global, now, RL_GLOBAL_CAPACITY, RL_GLOBAL_RATE_PER_US)) continue;
        RlBucket *bucket = rl_get(from.sin_addr.s_addr, now);
        if (!rl_allow(bucket, now)) continue;
        int rlen = dns_handle_query(buf, (size_t)n, resp, sizeof resp, &cfg);
        if (rlen < 0) continue; /* dropped (short packet, QR bit, parse fail) */
        if (sendto(fd, resp, (size_t)rlen, 0, (struct sockaddr *)&from, fromlen) < 0)
            fprintf(stderr, "dnsd: sendto: %s\n", strerror(errno));
    }
}
