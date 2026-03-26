/*
 * JARVIS AI-OS Phase 3 — SHIELD Safety Module Tests
 *
 * Tests keyword-based blocking and NULL-model fallback only.
 * Model-assisted path is tested manually in QEMU with a loaded model.
 *
 * Compile:
 *   gcc -Wall -Werror -O2 -std=c11 -D_POSIX_C_SOURCE=200809L \
 *     -I phase3/src/ai \
 *     phase3/src/ai/test_shield.c phase3/src/ai/shield.c \
 *     phase3/src/ai/tokenizer.c \
 *     -lm -o /tmp/test_shield
 *   /tmp/test_shield
 */

#include "shield.h"
#include "llama_quant.h"   /* qmodel_t for stub signature */
#include "tokenizer.h"     /* tokenizer_t for stub signature */
#include <stdio.h>
#include <string.h>
#include <math.h>

/*
 * Stub for qmodel_generate — satisfies linker without pulling in
 * llama_quant.c and its heavy dependencies (dequant, tensor_ops, etc.).
 * The model-assisted path is tested manually in QEMU with a real model.
 */
int qmodel_generate(const qmodel_t *qm, llama_state_t *state,
                    const int *prompt_tokens, int n_prompt,
                    int *output_tokens, int max_output,
                    int eos_token, float temperature, int top_k,
                    uint64_t seed)
{
    (void)qm; (void)state; (void)prompt_tokens; (void)n_prompt;
    (void)output_tokens; (void)max_output; (void)eos_token;
    (void)temperature; (void)top_k; (void)seed;
    return 0;  /* Generate nothing — triggers "model generation failed" WARN */
}

static int pass_count = 0;
static int fail_count = 0;

#define TEST(name) \
    do { printf("  TEST: %-50s ", name); } while (0)

#define PASS() \
    do { printf("PASS\n"); pass_count++; } while (0)

#define FAIL(msg) \
    do { printf("FAIL (%s)\n", msg); fail_count++; } while (0)

/* ---- Test Cases ---- */

static void test_keyword_block_delete(void)
{
    TEST("keyword block: 'delete all files'");
    shield_result_t r = shield_check_keywords("delete all files");
    if (r.decision == SHIELD_BLOCK && r.risk_score > 0.9f)
        PASS();
    else
        FAIL("expected BLOCK with risk > 0.9");
}

static void test_keyword_block_rm_rf(void)
{
    TEST("keyword block: 'rm -rf /'");
    shield_result_t r = shield_check_keywords("rm -rf /");
    if (r.decision == SHIELD_BLOCK && r.risk_score > 0.9f)
        PASS();
    else
        FAIL("expected BLOCK with risk > 0.9");
}

static void test_keyword_block_drop_table(void)
{
    TEST("keyword block: 'drop table users'");
    shield_result_t r = shield_check_keywords("drop table users");
    if (r.decision == SHIELD_BLOCK && r.risk_score > 0.9f)
        PASS();
    else
        FAIL("expected BLOCK with risk > 0.9");
}

static void test_keyword_allow_disk(void)
{
    TEST("keyword allow: 'check disk space'");
    shield_result_t r = shield_check_keywords("check disk space");
    if (r.decision == SHIELD_ALLOW && r.risk_score < 0.1f)
        PASS();
    else
        FAIL("expected ALLOW with risk < 0.1");
}

static void test_keyword_allow_network(void)
{
    TEST("keyword allow: 'show network status'");
    shield_result_t r = shield_check_keywords("show network status");
    if (r.decision == SHIELD_ALLOW && r.risk_score < 0.1f)
        PASS();
    else
        FAIL("expected ALLOW with risk < 0.1");
}

static void test_case_insensitivity(void)
{
    TEST("case insensitivity: 'DELETE ALL'");
    shield_result_t r = shield_check_keywords("DELETE ALL");
    if (r.decision == SHIELD_BLOCK && r.risk_score > 0.9f)
        PASS();
    else
        FAIL("expected BLOCK (case insensitive)");
}

static void test_whitespace_normalization(void)
{
    TEST("whitespace normalization: '  rm   -rf  / '");
    shield_result_t r = shield_check_keywords("  rm   -rf  / ");
    if (r.decision == SHIELD_BLOCK && r.risk_score > 0.9f)
        PASS();
    else
        FAIL("expected BLOCK after whitespace normalization");
}

static void test_combined_null_model(void)
{
    TEST("combined with NULL model -> keyword fallback");
    /* Blocked query with no model should still block */
    shield_result_t r1 = shield_check("destroy everything", NULL, NULL, NULL);
    if (r1.decision != SHIELD_BLOCK) {
        FAIL("expected BLOCK for 'destroy everything'");
        return;
    }
    /* Safe query with no model should still allow */
    shield_result_t r2 = shield_check("list files", NULL, NULL, NULL);
    if (r2.decision != SHIELD_ALLOW) {
        FAIL("expected ALLOW for 'list files'");
        return;
    }
    PASS();
}

/* ---- Main ---- */

int main(void)
{
    printf("=== SHIELD Safety Module Tests ===\n\n");

    test_keyword_block_delete();
    test_keyword_block_rm_rf();
    test_keyword_block_drop_table();
    test_keyword_allow_disk();
    test_keyword_allow_network();
    test_case_insensitivity();
    test_whitespace_normalization();
    test_combined_null_model();

    printf("\n  Results: %d PASS, %d FAIL (of %d)\n",
           pass_count, fail_count, pass_count + fail_count);

    if (fail_count > 0) {
        printf("  *** SOME TESTS FAILED ***\n");
        return 1;
    }
    printf("  All tests passed.\n");
    return 0;
}
