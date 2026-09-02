/*
 * soak_prov_driver.c — run ONE control-IN recall turn through the DEPLOYED selector and
 * preamble builder, from a binary blob of candidates and vectors.
 *
 * WHY A C DRIVER AND NOT A PYTHON MIRROR
 * --------------------------------------
 * The question this answers — "what bytes did the box actually inject as the preamble for
 * soak turn [23:00110]?" — is decided by g3_select_semantic() and
 * g3_build_preamble_answer_only(). A Python reimplementation of those two would be a
 * DIFFERENT program that happens to agree today: the argmax tie rule (strict '>', first of
 * equal wins), the >= floor, the unit-vector fail-closed, the thought-marker exclusion, the
 * tag-4 usable filter, and g3_clean_answer_len's complete-sentence rule would each have to
 * be re-derived correctly, and any one of them silently drifting would produce a plausible
 * wrong answer. So this links the REAL phase3/src/ai/g3_retrieval.c and passes the DEPLOYED
 * constants from the headers — never literals. (The cm4_routing_measure.py precedent: a
 * Python measurement drives a C driver linked against the module under measurement.)
 *
 * The vectors are the BOX'S OWN, read out of the JVEC store at LBA 21,150,000, including the
 * query's own (embed-on-write). Nothing is embedded on the host, so the measured ~0.0094
 * box-vs-host cosine delta — which would sit right on top of a 0.55-floor decision — does
 * not apply at all.
 *
 * HOST-ONLY. Never compiled into the seL4 image; no model, no box, no network.
 *
 * Build:
 *   gcc -Wall -Werror -O2 -std=c11 -I phase3/src/ai \
 *       phase3/scripts/embed/soak_prov_driver.c phase3/src/ai/g3_retrieval.c -o soak_prov_driver
 * Run:
 *   ./soak_prov_driver <blob>
 *
 * Blob layout (little-endian; produced by soak_23_00110_reconstruct.py, and by the CI smoke):
 *   u32   n                       candidate count, 1..32
 *   f32   qvec[128]               the query's stored vector
 *   n x {
 *     u32 seq
 *     u16 action
 *     u8  outcome
 *     u8  pad
 *     u16 resp_len
 *     u16 pad2
 *     u8  resp[256]               length-carried; bytes past resp_len are ignored
 *     f32 vec[128]
 *   }
 *
 * NOTE on g3_candidate_t.query_key: the blob carries no key, so every candidate gets 0. That
 * is provably behaviour-neutral here and is not a shortcut. The only place the selector reads
 * query_key is its already-taken test, which requires BOTH key and seq to match; with all keys
 * equal the test degenerates to seq equality, and seq is unique per episodic record. The
 * driver ASSERTS that uniqueness rather than assuming it, so the neutrality is enforced.
 * query/query_len are left NULL/0: verified by reading both functions — g3_select_semantic
 * reads query_key/seq/action/outcome/resp/resp_len only, and the ANSWER-ONLY builder emits
 * each record's response and never its query.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* The preamble is written to stdout VERBATIM and its '\n' fact separators are evidence. On
 * Windows the CRT opens stdout in TEXT mode and would rewrite every '\n' as '\r\n', silently
 * corrupting the bytes this program exists to report — the project's recorded "never send
 * binary through a text channel" failure, which has already bitten three times. Inert on
 * Linux/CI, where _WIN32 is not defined and this file compiles exactly as if it were absent. */
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

#include "g3_retrieval.h"      /* G3_MAX_FACTS, G3_R_MAX_CONTROL_IN, G3_SEM_DIM_DEFAULT,
                                  G3_SEM_FLOOR_DEFAULT, g3_candidate_t, the two functions */
#include "episodic_store.h"    /* EPI_ACT_CONTROL_IN, EPI_OUT_OK, EPI_RESP_MAX */
#include "shared_context.h"    /* SCTX_PREAMBLE_MAX — the box's ctrl_pre size */

#define MAX_CANDS 32           /* CTRL_SEM_MAX_CANDS in main_x86.c */
#define VEC_DIM   G3_SEM_DIM_DEFAULT

/* One candidate as it sits in the blob. Read field-by-field rather than as a struct so the
 * layout cannot drift with compiler padding. */
#define CAND_BYTES (4 + 2 + 1 + 1 + 2 + 2 + EPI_RESP_MAX + (VEC_DIM * 4))

static uint32_t rd_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint16_t rd_u16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
static float rd_f32(const uint8_t *p) {
    uint32_t u = rd_u32(p);
    float f;
    memcpy(&f, &u, sizeof f);
    return f;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s <blob>\n", argv[0]);
        return 2;
    }

#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);   /* see the note beside the includes */
#endif

    FILE *fh = fopen(argv[1], "rb");
    if (!fh) { fprintf(stderr, "cannot open %s\n", argv[1]); return 2; }
    if (fseek(fh, 0, SEEK_END) != 0) { fclose(fh); return 2; }
    long fsz = ftell(fh);
    if (fsz < 0) { fclose(fh); return 2; }
    rewind(fh);
    uint8_t *buf = malloc((size_t)fsz);
    if (!buf) { fclose(fh); return 2; }
    if (fread(buf, 1, (size_t)fsz, fh) != (size_t)fsz) {
        fprintf(stderr, "short read\n"); free(buf); fclose(fh); return 2;
    }
    fclose(fh);

    const long HDR_BYTES = 4 + (long)VEC_DIM * 4;
    if (fsz < HDR_BYTES) { fprintf(stderr, "blob too small\n"); free(buf); return 2; }

    uint32_t n = rd_u32(buf);
    if (n == 0 || n > MAX_CANDS) {
        fprintf(stderr, "bad candidate count %u (want 1..%d)\n", n, MAX_CANDS);
        free(buf); return 2;
    }
    if (fsz != HDR_BYTES + (long)n * CAND_BYTES) {
        fprintf(stderr, "blob size %ld != expected %ld for n=%u\n",
                fsz, HDR_BYTES + (long)n * CAND_BYTES, n);
        free(buf); return 2;
    }

    static float qvec[VEC_DIM];
    for (int d = 0; d < VEC_DIM; d++)
        qvec[d] = rd_f32(buf + 4 + (long)d * 4);

    static g3_candidate_t cands[MAX_CANDS];
    static float vecs[MAX_CANDS * VEC_DIM];
    static uint8_t resps[MAX_CANDS][EPI_RESP_MAX];

    const uint8_t *p = buf + HDR_BYTES;
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t *c = p + (long)i * CAND_BYTES;
        cands[i].query_key = 0;                 /* see the header note — neutral, asserted below */
        cands[i].seq       = rd_u32(c + 0);
        cands[i].action    = rd_u16(c + 4);
        cands[i].outcome   = c[6];
        cands[i].query     = NULL;
        cands[i].query_len = 0;
        cands[i].resp_len  = rd_u16(c + 8);
        memcpy(resps[i], c + 12, EPI_RESP_MAX);
        cands[i].resp      = (const char *)resps[i];
        for (int d = 0; d < VEC_DIM; d++)
            vecs[(long)i * VEC_DIM + d] = rd_f32(c + 12 + EPI_RESP_MAX + (long)d * 4);
    }

    /* The query_key=0 shortcut is only neutral while seq discriminates. Enforce it. */
    for (uint32_t i = 0; i < n; i++) {
        for (uint32_t j = i + 1; j < n; j++) {
            if (cands[i].seq == cands[j].seq) {
                fprintf(stderr, "duplicate seq %u at %u and %u — the query_key=0 shortcut is "
                                "NOT neutral for this input\n", cands[i].seq, i, j);
                free(buf); return 2;
            }
        }
    }

    /* Every candidate gets a DOT line, including ones the selector will skip (below floor,
     * non-unit vector, thought marker, not usable) — the ranking is evidence in its own right.
     * double accumulation over the 128 floats, exactly as g3_select_semantic does. */
    for (uint32_t i = 0; i < n; i++) {
        double dot = 0.0;
        const float *cv = vecs + (long)i * VEC_DIM;
        for (int d = 0; d < VEC_DIM; d++)
            dot += (double)qvec[d] * (double)cv[d];
        /* TRUNCATION, matching main_x86.c's crk_cos = (int)(best * 1000.0) — not rounding. */
        printf("DOT %u %d\n", cands[i].seq, (int)(dot * 1000.0));
    }

    static g3_candidate_t sel[G3_MAX_FACTS];
    int ss = g3_select_semantic(cands, (int)n, qvec, vecs,
                                G3_SEM_DIM_DEFAULT, G3_SEM_FLOOR_DEFAULT, G3_MAX_FACTS,
                                EPI_ACT_CONTROL_IN, EPI_OUT_OK, sel);

    for (int k = 0; k < ss; k++) {
        double dot = 0.0;
        /* Recompute against the SELECTED record's own vector (found by seq — unique, asserted). */
        for (uint32_t i = 0; i < n; i++) {
            if (cands[i].seq != sel[k].seq)
                continue;
            const float *cv = vecs + (long)i * VEC_DIM;
            for (int d = 0; d < VEC_DIM; d++)
                dot += (double)qvec[d] * (double)cv[d];
            break;
        }
        printf("SEL %d %u %d\n", k + 1, sel[k].seq, (int)(dot * 1000.0));
    }

    static char out[SCTX_PREAMBLE_MAX];
    int cplen = g3_build_preamble_answer_only(sel, ss, out, (int)sizeof out, G3_R_MAX_CONTROL_IN);

    printf("PRE_LEN %d\n", cplen);
    printf("PRE_BEGIN\n");
    if (cplen > 0)
        fwrite(out, 1, (size_t)cplen, stdout);
    /* The separator newline belongs to the MARKER, not to the preamble. The preamble's own
     * last byte is normally '\n' (the builder terminates every fact with one), so without a
     * separator of its own the closing marker is ambiguous and a reader searching backwards
     * for "\nPRE_END" silently eats the preamble's final byte. Read exactly PRE_LEN bytes
     * after "PRE_BEGIN\n" and expect "\nPRE_END\n" immediately after them. */
    printf("\nPRE_END\n");

    free(buf);
    return 0;
}
