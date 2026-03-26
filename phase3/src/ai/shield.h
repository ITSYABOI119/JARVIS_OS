#ifndef SHIELD_H
#define SHIELD_H

#include <stdbool.h>

/* Forward declarations — avoid pulling in heavy headers */
struct qmodel_t;
struct llama_state_t;
struct tokenizer_t;

typedef enum {
    SHIELD_ALLOW = 0,
    SHIELD_WARN  = 1,
    SHIELD_BLOCK = 2,
} shield_decision_t;

typedef struct {
    float risk_score;           /* 0.0 (safe) to 1.0 (dangerous) */
    shield_decision_t decision;
    const char *reason;         /* Static string, NOT malloc'd */
} shield_result_t;

/* Keyword-based check (fast, no model needed) */
shield_result_t shield_check_keywords(const char *query);

/* Combined: keyword first (fast reject), then model if available */
shield_result_t shield_check(const char *query,
                              const struct qmodel_t *model,
                              struct llama_state_t *state,
                              const struct tokenizer_t *tok);

#endif
