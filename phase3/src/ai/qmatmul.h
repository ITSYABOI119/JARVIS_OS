#ifndef QMATMUL_H
#define QMATMUL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "llama_quant.h"

void qmatmul_vec(const qtensor_t *W, const float *x, float *out, int M, int K);
void qembed_lookup(const qtensor_t *embed, int token, float *out, int dim);

#ifdef __cplusplus
}
#endif

#endif /* QMATMUL_H */
