#include "sampling.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

int sample_greedy(const float *logits, int vocab_size)
{
    int best = 0;
    for (int i = 1; i < vocab_size; i++)
        if (logits[i] > logits[best]) best = i;
    return best;
}

uint64_t sampling_rng_next(uint64_t *state)
{
    uint64_t x = *state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

float sampling_rng_float(uint64_t *state)
{
    return (float)(sampling_rng_next(state) >> 11) / (float)(1ULL << 53);
}

int sample_topk(const float *logits, int vocab_size, int k, float temperature, uint64_t *seed)
{
    if (k <= 0) k = 1;
    if (k > vocab_size) k = vocab_size;

    /* Find top-k indices and values */
    typedef struct { float val; int idx; } pair_t;
    pair_t *topk = (pair_t *)malloc(sizeof(pair_t) * k);
    if (!topk) return sample_greedy(logits, vocab_size);  /* Fallback */

    /* Initialize with first k elements */
    for (int i = 0; i < k; i++) {
        topk[i].val = logits[i];
        topk[i].idx = i;
    }
    /* Find min in topk */
    int min_idx = 0;
    for (int i = 1; i < k; i++)
        if (topk[i].val < topk[min_idx].val) min_idx = i;

    /* Scan rest, replace min if larger */
    for (int i = k; i < vocab_size; i++) {
        if (logits[i] > topk[min_idx].val) {
            topk[min_idx].val = logits[i];
            topk[min_idx].idx = i;
            /* Find new min */
            min_idx = 0;
            for (int j = 1; j < k; j++)
                if (topk[j].val < topk[min_idx].val) min_idx = j;
        }
    }

    /* Apply temperature */
    if (temperature < 1e-6f) temperature = 1e-6f;  /* Prevent division by zero */
    float max_val = topk[0].val;
    for (int i = 1; i < k; i++)
        if (topk[i].val > max_val) max_val = topk[i].val;

    /* Softmax over top-k */
    float sum = 0.0f;
    for (int i = 0; i < k; i++) {
        topk[i].val = expf((topk[i].val - max_val) / temperature);
        sum += topk[i].val;
    }
    for (int i = 0; i < k; i++)
        topk[i].val /= sum;

    /* Sample from categorical */
    float r = sampling_rng_float(seed);
    float cumsum = 0.0f;
    int result = topk[k - 1].idx;  /* Default to last */
    for (int i = 0; i < k; i++) {
        cumsum += topk[i].val;
        if (r < cumsum) { result = topk[i].idx; break; }
    }

    free(topk);
    return result;
}

int sample_argmax_prob(const float *logits, int vocab_size, float *out_prob)
{
    /* Find max for numerical stability */
    float max_val = logits[0];
    int best = 0;
    for (int i = 1; i < vocab_size; i++) {
        if (logits[i] > max_val) { max_val = logits[i]; best = i; }
    }

    /* Compute softmax probability of best token */
    float sum = 0.0f;
    for (int i = 0; i < vocab_size; i++)
        sum += expf(logits[i] - max_val);

    if (out_prob) *out_prob = expf(logits[best] - max_val) / sum;
    return best;
}
