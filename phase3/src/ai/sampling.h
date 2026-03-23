#ifndef SAMPLING_H
#define SAMPLING_H

#include <stdint.h>

int sample_greedy(const float *logits, int vocab_size);
int sample_topk(const float *logits, int vocab_size, int k,
                float temperature, uint64_t *seed);
int sample_argmax_prob(const float *logits, int vocab_size, float *out_prob);
uint64_t sampling_rng_next(uint64_t *state);
float    sampling_rng_float(uint64_t *state);

#endif
