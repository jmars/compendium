/* rl.c — per-source token-bucket rate limiter (server-level DoS hardening).
   Pure decision logic, factored out of main.c's UDP loop so it is directly
   unit-testable (see src/rl_check.c). The fixed bucket table + hash/probe/
   eviction live in main.c; this file only decides allow/drop for one bucket.

   Tokens are stored as fixed-point micro-tokens: RL_SCALE micro-tokens == 1
   whole token. A source may burst up to RL_BURST_TOKENS immediately and refills
   at RL_RATE_PER_SEC tokens/second. When a source exceeds its allowance the
   packet is DROPPED silently (no response) — we never reflect the rate-limit
   as amplification. Uses a monotonic clock (no wall-clock jumps); the refill
   window is clamped so a suspend or clock wrap cannot dump a huge refill. */
#include "dnsd.h"

bool rl_allow_cap(RlBucket *b, uint64_t now_us, uint64_t cap_microtokens,
                  uint64_t rate_per_us) {
    /* Refill: tokens += elapsed * rate, clamped to capacity. */
    uint64_t elapsed = (now_us > b->last_us) ? (now_us - b->last_us) : 0;
    if (elapsed > RL_MAX_ELAPSED_US) elapsed = RL_MAX_ELAPSED_US;  /* wrap/suspend */
    b->tokens += elapsed * rate_per_us;
    if (b->tokens > cap_microtokens) b->tokens = cap_microtokens;
    b->last_us = now_us;
    /* Consume one whole token if available; otherwise drop. */
    if (b->tokens >= RL_SCALE) {
        b->tokens -= RL_SCALE;
        return true;
    }
    return false;
}

bool rl_allow(RlBucket *b, uint64_t now_us) {
    return rl_allow_cap(b, now_us, RL_CAPACITY, RL_RATE_PER_US);
}
