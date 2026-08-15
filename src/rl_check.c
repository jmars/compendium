/* rl_check — unit-test the token-bucket rate limiter (rl_allow) in-process.
   Deterministic (uses synthetic now_us, no wall-clock timing), so it is safe
   for CI. Verifies: burst allowance, drop after exhaustion, steady refill,
   clock-wrap (no refill on back-in-time), and suspend clamp (no huge refill).
   Usage: rl_check.com.dbg */
#include "dnsd.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;

int main(void) {
    /* --- burst: a fresh full bucket allows RL_BURST_TOKENS immediate queries */
    RlBucket b; memset(&b, 0, sizeof b);
    b.ip = 0x12345678; b.tokens = RL_CAPACITY;
    int allowed = 0;
    for (int i = 0; i < RL_BURST_TOKENS; i++)
        if (rl_allow(&b, 1000000ULL)) allowed++;
    if (allowed != (int)RL_BURST_TOKENS) {
        fprintf(stderr, "FAIL burst: allowed=%d want %d\n", allowed, (int)RL_BURST_TOKENS);
        failures++;
    } else printf("ok   burst allows %d consecutive queries\n", allowed);

    /* --- exhaustion: no tokens left -> dropped */
    if (rl_allow(&b, 1000000ULL)) { fprintf(stderr, "FAIL: allowed past burst\n"); failures++; }
    else printf("ok   dropped after burst exhausted\n");

    /* --- refill: +1s accumulates RL_RATE_PER_SEC tokens */
    int got = 0;
    for (int i = 0; i < RL_RATE_PER_SEC; i++)
        if (rl_allow(&b, 2000000ULL)) got++;
    if (got != (int)RL_RATE_PER_SEC) {
        fprintf(stderr, "FAIL refill: got=%d want %d\n", got, (int)RL_RATE_PER_SEC);
        failures++;
    } else printf("ok   refilled %d tokens after 1s\n", got);

    /* --- clock wrap: now < last_us must NOT refill (drop stays a drop) */
    RlBucket w; memset(&w, 0, sizeof w);
    w.ip = 1; w.tokens = 0; w.last_us = 5000000ULL;
    if (rl_allow(&w, 1000000ULL)) { fprintf(stderr, "FAIL: refilled on clock wrap\n"); failures++; }
    else printf("ok   clock wrap yields no refill\n");

    /* --- suspend clamp: a 1-day gap must refill only 1s worth (RL_RATE_PER_SEC),
           not dump a huge burst */
    RlBucket s; memset(&s, 0, sizeof s);
    s.ip = 2; s.tokens = 0; s.last_us = 0;
    int sg = 0;
    for (int i = 0; i <= RL_RATE_PER_SEC; i++)   /* try one more than available */
        if (rl_allow(&s, 86400000000ULL)) sg++;
    if (sg != (int)RL_RATE_PER_SEC) {
        fprintf(stderr, "FAIL suspend clamp: got=%d want %d\n", sg, (int)RL_RATE_PER_SEC);
        failures++;
    } else printf("ok   suspend clamps refill to %d tokens\n", sg);

    /* --- global cap (S1 fix): a rotating-source flood cannot exceed the global
           token budget even though each distinct source gets a fresh per-source
           burst. Simulate 1000 distinct sources hitting a shared global bucket
           at the same instant: only RL_GLOBAL_BURST_TOKENS may pass. */
    {
        RlBucket g; memset(&g, 0, sizeof g);
        g.tokens = RL_GLOBAL_CAPACITY; g.last_us = 1000000ULL;
        int gallowed = 0;
        for (int i = 0; i < 1000; i++)
            if (rl_allow_cap(&g, 1000001ULL, RL_GLOBAL_CAPACITY, RL_GLOBAL_RATE_PER_US)) gallowed++;
        if (gallowed != (int)RL_GLOBAL_BURST_TOKENS) {
            fprintf(stderr, "FAIL global cap: allowed=%d want %d\n", gallowed, (int)RL_GLOBAL_BURST_TOKENS);
            failures++;
        } else printf("ok   global cap bounds a 1000-source flood to %d tokens\n", gallowed);
    }

    if (failures) { fprintf(stderr, "%d rate-limit checks failed\n", failures); return 1; }
    printf("all rate-limit checks passed\n");
    return 0;
}
