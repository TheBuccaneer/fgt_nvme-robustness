#ifndef RNG_H
#define RNG_H

#include <stdint.h>

/**
 * Deterministic PRNG using splitmix64 algorithm.
 * Same seed always produces same sequence.
 */

typedef struct {
    uint64_t state;
} Rng;

/** Initialize RNG with seed */
void rng_init(Rng *rng, uint64_t seed);

/** Get next 64-bit random value */
uint64_t rng_next_u64(Rng *rng);

/** Get random value in range [0, max) */
uint64_t rng_range(Rng *rng, uint64_t max);

/** Get next random bit (0 or 1) */
uint64_t rng_next_bit(Rng *rng);

#endif /* RNG_H */
