#include "rng.h"

/**
 * splitmix64 PRNG - simple, fast, deterministic
 * Reference: https://prng.di.unimi.it/splitmix64.c
 */

void rng_init(Rng *rng, uint64_t seed) {
    rng->state = seed;
}

uint64_t rng_next_u64(Rng *rng) {
    uint64_t z = (rng->state += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

uint64_t rng_range(Rng *rng, uint64_t max) {
    if (max == 0) return 0;
    return rng_next_u64(rng) % max;
}

uint64_t rng_next_bit(Rng *rng) {
    return rng_next_u64(rng) & 1;
}
