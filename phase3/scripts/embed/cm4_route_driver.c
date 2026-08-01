/**
 * cm4_route_driver.c — stdin -> route_classify -> stdout, one line per query.
 *
 * ARM A of the C/M4 routing measurement. This SHELLS OUT TO THE REAL CLASSIFIER
 * rather than mirroring route.c's rules in Python, deliberately: a Python mirror is a
 * reimplementation, and a reimplementation of the thing under measurement can drift from
 * it silently. The whole value of the baseline arm is that it IS the deployed behaviour,
 * so the only honest way to get it is to link route.c and call it.
 *
 * It lives in phase3/scripts/embed/ and not phase3/src/ because the C/M4 task is
 * measure-only: no phase3/src file may be added or changed. route.c is READ (linked),
 * never modified.
 *
 * Protocol: one query per input line (trailing \n stripped, nothing else touched — the
 * corpus is fed verbatim so punctuation and case reach route_classify exactly as a real
 * control-IN query would). Output per line: "<cls> <field>" as integers, matching
 * route_class_t (0 INFER / 1 SYSFACTS / 2 DECLINE) and sysfact_field_t.
 *
 * Build: gcc -Wall -Werror -O2 -std=c11 -I phase3/src/ai \
 *          phase3/scripts/embed/cm4_route_driver.c phase3/src/ai/route.c -o /tmp/cm4_route
 */

#include <stdio.h>
#include <string.h>
#include "route.h"

int main(void)
{
    char line[1024];

    while (fgets(line, sizeof line, stdin)) {
        size_t n = strlen(line);
        /* Strip ONLY the line terminator. Everything else — case, punctuation, interior
         * spacing — is passed through untouched, because route_classify does its own
         * normalisation and pre-trimming here would measure a different function. */
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            line[--n] = '\0';

        route_result_t rr;
        memset(&rr, 0, sizeof rr);
        route_class_t cls = route_classify(line, n, &rr);
        printf("%d %d\n", (int)cls, (int)rr.field);
    }
    return 0;
}
