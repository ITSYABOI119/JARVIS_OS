/*
 * cm4_veto_driver.c — stdin -> route_veto_should -> stdout, one verdict per line.
 *
 * WHY THIS EXISTS
 * ---------------
 * The veto's remaining open question is not its logic — test_route_veto.c pins that
 * — but whether the BOX reproduces the decision the measurement made. The box embeds
 * with Q8_0 weights where cm4_routing_measure.py used F32, and a veto is decided by
 * the SIGN OF A DIFFERENCE between two similarities, so a shared bias only partially
 * cancels. That is a measurement, and this is the C half of it: an off-box harness
 * feeds projected vectors to the REAL route_veto.c and prints what it decides, so
 * Python can compare verdict-for-verdict rather than reimplementing the rule.
 *
 * It shells out to the shipped module for the same reason cm4_route_driver.c shells
 * out to route.c: a Python mirror of the thing under measurement is a
 * reimplementation, and a reimplementation can drift from the original silently.
 *
 * WHY IT IS NOT A CI TEST
 * -----------------------
 * The parity gate needs Qwen3-Embedding-0.6B (609 MB) plus sentence-transformers to
 * produce the input vectors, so it cannot run in CI — the model-gated-local
 * precedent (cm1a_vector_parity.py, cm2_floor.py). What CI CAN do, and should, is
 * compile this file and feed it empty stdin: that costs nothing, exits 0, and means
 * the harness cannot rot into un-compilable while the parity gate sits unused
 * between measurements. A tool that only runs when something is wrong is a tool that
 * is broken when something is wrong.
 *
 * PROTOCOL
 * --------
 *   stdin : one case per line — <kw_cls_int> then 128 whitespace-separated floats.
 *           kw_cls_int is route_class_t (0 INFER / 1 SYSFACTS / 2 DECLINE).
 *   stdout: "0" or "1" per case, one per line, flushed per line so a Python parent
 *           reading incrementally cannot deadlock on a full pipe buffer.
 *
 * Tokens are whitespace-delimited, so the line structure is for human readability;
 * the parser does not depend on it. Any malformed or short input is a hard error to
 * stderr with a non-zero exit, never a silently-emitted verdict — a parity gate that
 * mis-parses and answers anyway produces a false alarm or, worse, a false pass.
 *
 * FLOAT FORMAT: C99 HEX FLOAT (%a / strtof), STRONGLY PREFERRED
 * ------------------------------------------------------------
 * Decimal round-tripping through printf/strtof is exact only at 9 significant digits
 * (FLT_DECIMAL_DIG) and only if BOTH sides implement correct rounding. Hex assumes
 * nothing about either: the digits ARE the bits. That matters more here than usual
 * because this harness exists to detect small numerical disagreements — a 1-ULP
 * difference introduced by the TRANSPORT could flip a near-tie and be reported as a
 * box-vs-host parity failure that never existed. Decimal input is still accepted
 * (strtof takes both) so a hand-written probe case is convenient; the Python side of
 * the gate should emit %a.
 *
 * Build:
 *   gcc -Wall -Werror -O2 -std=c11 -I phase3/src/ai \
 *       phase3/scripts/embed/cm4_veto_driver.c phase3/src/ai/route_veto.c \
 *       -o /tmp/cm4_veto -lm
 */

#include <stdio.h>
#include <stdlib.h>
#include "route_veto.h"
#include "route_centroids.h"   /* ROUTE_CENTROIDS_DIM — the vector length to read */

#define DIM ROUTE_CENTROIDS_DIM

/* Longest plausible token: a hex float like "-0x1.fffffep-126" is ~17 chars. 63
 * leaves room for decimal forms with a long exponent, and the width is bounded in
 * the fscanf format so an adversarially long token truncates rather than overflows
 * (it then fails to parse, which is the intended hard error). */
#define TOKMAX 63

/* The fscanf width must equal TOKMAX. Stringified rather than written twice, so
 * raising the buffer cannot leave the width behind — which would be a buffer
 * overflow, not a parse bug. */
#define STR_(x) #x
#define STR(x)  STR_(x)
#define TOKFMT  "%" STR(TOKMAX) "s"

static int read_token(char *tok)
{
    return fscanf(stdin, TOKFMT, tok) == 1;
}

int main(void)
{
    char tok[TOKMAX + 1];
    long lineno = 0;

    /*
     * The caller contract, exercised rather than described: route_veto.h requires a
     * caller to REFUSE to veto when the centroids do not verify. A harness that
     * skipped this could report a whole run of verdicts computed against corrupted
     * constants and call it a parity failure.
     */
    if (route_veto_centroids_verify() != 0) {
        fprintf(stderr, "cm4_veto_driver: ROUTE_CENTROIDS failed XOR verification "
                        "- refusing to emit verdicts\n");
        return 2;
    }

    while (read_token(tok)) {
        lineno++;

        char *end = NULL;
        const long cls = strtol(tok, &end, 10);
        if (end == tok || *end != '\0') {
            fprintf(stderr, "cm4_veto_driver: case %ld: bad class token '%s'\n", lineno, tok);
            return 3;
        }

        /* Out-of-range class values are passed THROUGH to route_veto_should rather
         * than rejected here: refusing them would hide the module's own fail-closed
         * guard from the gate, and that guard is part of what is being measured. */
        float v[DIM];
        for (int i = 0; i < DIM; i++) {
            if (!read_token(tok)) {
                fprintf(stderr, "cm4_veto_driver: case %ld: short vector, got %d of %d floats\n",
                        lineno, i, DIM);
                return 3;
            }
            v[i] = strtof(tok, &end);
            if (end == tok || *end != '\0') {
                fprintf(stderr, "cm4_veto_driver: case %ld: bad float token '%s' at index %d\n",
                        lineno, tok, i);
                return 3;
            }
            /* strtof's ERANGE (underflow/overflow) is deliberately NOT checked. It
             * still yields a usable value (0 / HUGE_VALF), and a degenerate vector
             * is a legitimate thing for the gate to feed in — route_veto_should is
             * required to survive one (test_route_veto.c T4) and rejecting it here
             * would hide that from the measurement. The parse is validated for
             * SHAPE (a whole token consumed), not for magnitude. */
        }

        printf("%d\n", route_veto_should(v, (route_class_t)cls));
        fflush(stdout);
    }

    /* Distinguish clean EOF from a read error, so a truncated pipe is not silently
     * reported as a complete run. */
    if (ferror(stdin)) {
        fprintf(stderr, "cm4_veto_driver: read error on stdin after %ld case(s)\n", lineno);
        return 4;
    }

    return 0;
}
