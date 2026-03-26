/*
 * JARVIS AI-OS Phase 3 — SHIELD Safety Module
 *
 * Two-tier risk assessment:
 *   1. Keyword-based (fast, no model needed) — blocklist substring match
 *   2. Model-assisted (optional) — LLM classifies command as safe/dangerous
 *
 * Keyword check always runs first. If it blocks, we skip the model entirely.
 * If no model is available, keyword result is returned as-is (graceful fallback).
 *
 * Pure C11, no C++ dependencies.
 */

#define _POSIX_C_SOURCE 200809L
#include "shield.h"
#include "llama_quant.h"    /* qmodel_t, qmodel_forward, qmodel_generate */
#include "tokenizer.h"      /* tokenizer_t, tokenizer_encode, tokenizer_decode */
#include "sampling.h"       /* sample_greedy */

#include <stddef.h>
#include <string.h>
#include <ctype.h>

/* ---- Normalization ---- */

/*
 * Normalize query for SHIELD matching: lowercase + collapse whitespace + trim.
 * Extracted from main_x86.c normalize_for_shield() (SEC-010).
 */
static void normalize_for_shield(const char *input, char *output, size_t max_len)
{
    size_t j = 0;
    int prev_space = 0;

    /* Skip leading whitespace */
    while (*input == ' ' || *input == '\t') input++;

    for (size_t i = 0; input[i] && j < max_len - 1; i++) {
        char c = input[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c + ('a' - 'A'));
        if (c == ' ' || c == '\t') {
            if (!prev_space && j > 0) { output[j++] = ' '; }
            prev_space = 1;
        } else {
            output[j++] = c;
            prev_space = 0;
        }
    }

    /* Trim trailing space */
    if (j > 0 && output[j - 1] == ' ') j--;

    output[j] = '\0';
}

/* ---- Keyword Blocklist ---- */

static const char *blocklist[] = {
    "delete",
    "remove",
    "kill",
    "destroy",
    "format",
    "rm -rf",
    "drop table",
    "shutdown",
    "halt",
    NULL
};

shield_result_t shield_check_keywords(const char *query)
{
    shield_result_t result;
    char normalized[256];

    if (!query || !query[0]) {
        result.risk_score = 0.0f;
        result.decision = SHIELD_ALLOW;
        result.reason = "empty query";
        return result;
    }

    normalize_for_shield(query, normalized, sizeof(normalized));

    for (int i = 0; blocklist[i]; i++) {
        if (strstr(normalized, blocklist[i])) {
            result.risk_score = 0.95f;
            result.decision = SHIELD_BLOCK;
            result.reason = blocklist[i];
            return result;
        }
    }

    result.risk_score = 0.05f;
    result.decision = SHIELD_ALLOW;
    result.reason = "no blocked keywords";
    return result;
}

/* ---- Model-Assisted Check ---- */

/*
 * Run LLM to classify a command as safe or dangerous.
 * Returns SHIELD_BLOCK / SHIELD_ALLOW / SHIELD_WARN.
 *
 * The model is asked a simple classification question and we inspect
 * the first few generated tokens for "dangerous", "unsafe", or "safe".
 */
static shield_result_t model_classify(const char *query,
                                       const qmodel_t *model,
                                       llama_state_t *state,
                                       const tokenizer_t *tok)
{
    shield_result_t result;

    /* Build classification prompt */
    char prompt[512];
    int plen = snprintf(prompt, sizeof(prompt),
        "Is this command safe or dangerous? Command: '%s'. Answer:", query);
    if (plen < 0 || plen >= (int)sizeof(prompt)) {
        /* Prompt too long — warn conservatively */
        result.risk_score = 0.5f;
        result.decision = SHIELD_WARN;
        result.reason = "prompt too long for model check";
        return result;
    }

    /* Encode prompt */
    int prompt_tokens[256];
    int n_prompt = tokenizer_encode(tok, prompt, prompt_tokens, 256);
    if (n_prompt <= 0) {
        result.risk_score = 0.5f;
        result.decision = SHIELD_WARN;
        result.reason = "tokenizer encode failed";
        return result;
    }

    /* Generate up to 5 tokens (greedy, temperature=0) */
    int output_tokens[5];
    int n_gen = qmodel_generate(model, state, prompt_tokens, n_prompt,
                                 output_tokens, 5, tok->eos_id,
                                 0.0f, 1, 42);

    if (n_gen <= 0) {
        result.risk_score = 0.5f;
        result.decision = SHIELD_WARN;
        result.reason = "model generation failed";
        return result;
    }

    /* Decode generated tokens */
    char decoded[256];
    int dec_len = tokenizer_decode(tok, output_tokens, n_gen, decoded, sizeof(decoded));
    if (dec_len <= 0) {
        result.risk_score = 0.5f;
        result.decision = SHIELD_WARN;
        result.reason = "tokenizer decode failed";
        return result;
    }

    /* Normalize decoded output for matching */
    char norm_output[256];
    normalize_for_shield(decoded, norm_output, sizeof(norm_output));

    /* Check for dangerous/unsafe indicators */
    if (strstr(norm_output, "dangerous") || strstr(norm_output, "unsafe")) {
        result.risk_score = 0.9f;
        result.decision = SHIELD_BLOCK;
        result.reason = "model classified as dangerous";
        return result;
    }

    /* Check for safe indicators */
    if (strstr(norm_output, "safe")) {
        result.risk_score = 0.1f;
        result.decision = SHIELD_ALLOW;
        result.reason = "model classified as safe";
        return result;
    }

    /* Unclear — warn */
    result.risk_score = 0.5f;
    result.decision = SHIELD_WARN;
    result.reason = "model output unclear";
    return result;
}

/* ---- Combined Check ---- */

shield_result_t shield_check(const char *query,
                              const struct qmodel_t *model,
                              struct llama_state_t *state,
                              const struct tokenizer_t *tok)
{
    /* Always run keyword check first (fast path) */
    shield_result_t kw_result = shield_check_keywords(query);
    if (kw_result.decision == SHIELD_BLOCK) {
        return kw_result;  /* Keyword blocklist is authoritative */
    }

    /* If model is available, run model-assisted classification */
    if (model && state && tok) {
        return model_classify(query,
                              (const qmodel_t *)model,
                              (llama_state_t *)state,
                              (const tokenizer_t *)tok);
    }

    /* No model — return keyword result (graceful fallback) */
    return kw_result;
}
