/*
 * routing_suite.h — the control-IN QUERY ROUTER accuracy corpus (6-6/B M0).
 *
 * The router (route_classify, authored SEPARATELY) sees a validated control-IN
 * query and picks ONE handler: ROUTE_SYSFACTS (answer from a real, whitelisted
 * box-state field), ROUTE_DECLINE (a status-shaped question the box has NO
 * source for -> honest "I don't track that", NEVER fabricated), or ROUTE_INFER
 * (everything else -> the model; the safe default). This file is the ORACLE the
 * router's >=95% accuracy is measured against.
 *
 * ANTI-CIRCULARITY DISCIPLINE (goal doc §6f honesty): labels are author-assigned
 * (goal doc §6f honesty); the HELDOUT split is the honest number; NEVER edit
 * HELDOUT to make route.c pass — fix route.c against the DEV split instead. This
 * suite was authored WITHOUT reference to route.c's keyword tables (route.c did
 * not exist at authoring).
 *
 * STANDALONE-COMPILE RATIONALE: routing_case_t's label fields are plain `int`,
 * not the route.h enum types, so this header defines NO enums and pulls in NO
 * route.h dependency. The array initialisers name the enum CONSTANTS
 * (ROUTE_INFER, SF_UPTIME, ...) that route.h WILL define; test_route.c includes
 * route.h *and* this header, so the constants resolve there and compare
 * int-compatibly against expect_cls/expect_field. (To syntax-check this header
 * in isolation, a throwaway shim that defines those constants includes it — the
 * shim is never committed.) We deliberately do NOT #define the constants here:
 * that would collide with route.h's enum constants inside test_route.c.
 *
 * LABELING NOTES (label by what a HUMAN would MEAN, not by keyword presence):
 *   - The synthetic load-generator counters (q_total / q_hits / q_errors) are
 *     NOT a SYSFACTS field. "how many queries/questions have you answered" is
 *     ambiguous -> ROUTE_INFER (the conservative default), never SYSFACTS.
 *   - "what time is it" / "what's the date" -> DECLINE (no RTC). "how long have
 *     you been up" -> SF_UPTIME. Distinct by meaning (time-of-day vs duration).
 *   - "worker IDS" asks for identifiers, not the worker COUNT; the box exposes a
 *     count (SF_NODES), not per-worker ids, so worker-id queries -> ROUTE_INFER
 *     (labeled consistently across both splits).
 *
 * Whitelisted SYSFACTS fields (and ONLY these are truthfully answerable):
 *   SF_UPTIME  — how long running / up / since boot
 *   SF_BOOT_ID — how many boots / which boot / boot count
 *   SF_MODEL   — what model / which LLM
 *   SF_NODES   — how many workers / cores / threads
 *   SF_HEALTH  — healthy? / any errors? / everything ok? / are you ok?
 * Known-UNTRACKED status shapes -> DECLINE: cpu / load / how-busy, memory-used /
 * ram-usage, disk-free / storage-left, temperature / how-hot, wall-clock / date.
 */
#ifndef ROUTING_SUITE_H
#define ROUTING_SUITE_H

#include <stddef.h>

/* Label fields are int so this header needs no enum definitions (see rationale
 * above). test_route.c supplies the ROUTE and SF constants via route.h; they
 * are int-compatible with expect_cls/expect_field. */
typedef struct {
    const char *query;       /* a realistic human-phrased control-IN query   */
    int         expect_cls;  /* ROUTE_INFER | ROUTE_SYSFACTS | ROUTE_DECLINE */
    int         expect_field;/* SF_NONE, or the SF_* field for a SYSFACTS row */
} routing_case_t;

/* ======================================================================== *
 *  DEV split — Pass 2 MAY tune route.c against this (visible while tuning).
 * ======================================================================== */
static const routing_case_t ROUTING_DEV[] = {
    /* --- SYSFACTS / SF_UPTIME (6; incl. casual/typo noise) --- */
    { "how long have you been up?",          ROUTE_SYSFACTS, SF_UPTIME },
    { "since when have you been running?",   ROUTE_SYSFACTS, SF_UPTIME },
    { "what's your uptime looking like",     ROUTE_SYSFACTS, SF_UPTIME },
    { "how long since you last booted",      ROUTE_SYSFACTS, SF_UPTIME },
    { "been running for how long now?",      ROUTE_SYSFACTS, SF_UPTIME },
    { "how long you been runnin?",           ROUTE_SYSFACTS, SF_UPTIME },   /* casual/typo */

    /* --- SYSFACTS / SF_BOOT_ID (6; count-of-boots, not duration) --- */
    { "how many times have you booted?",     ROUTE_SYSFACTS, SF_BOOT_ID },
    { "what boot is this?",                  ROUTE_SYSFACTS, SF_BOOT_ID },
    { "which boot number are we on?",        ROUTE_SYSFACTS, SF_BOOT_ID },
    { "how many reboots so far?",            ROUTE_SYSFACTS, SF_BOOT_ID },
    { "whats your boot count",               ROUTE_SYSFACTS, SF_BOOT_ID },  /* missing apostrophe */
    { "how many power cycles have you had?", ROUTE_SYSFACTS, SF_BOOT_ID },

    /* --- SYSFACTS / SF_MODEL (6) --- */
    { "what model are you?",                 ROUTE_SYSFACTS, SF_MODEL },
    { "which LLM are you running?",          ROUTE_SYSFACTS, SF_MODEL },
    { "what are you running under the hood?",ROUTE_SYSFACTS, SF_MODEL },
    { "what's the model powering you?",      ROUTE_SYSFACTS, SF_MODEL },
    { "which language model do you use?",    ROUTE_SYSFACTS, SF_MODEL },
    { "what model r u running",              ROUTE_SYSFACTS, SF_MODEL },    /* chatspeak */

    /* --- SYSFACTS / SF_NODES (6; workers/cores/threads count) --- */
    { "how many workers do you have?",       ROUTE_SYSFACTS, SF_NODES },
    { "how many cores are you using?",       ROUTE_SYSFACTS, SF_NODES },
    { "how many threads run inference?",     ROUTE_SYSFACTS, SF_NODES },
    { "what's your worker count?",           ROUTE_SYSFACTS, SF_NODES },
    { "how many compute nodes?",             ROUTE_SYSFACTS, SF_NODES },
    { "number of parallel workers ?",        ROUTE_SYSFACTS, SF_NODES },    /* stray space */

    /* --- SYSFACTS / SF_HEALTH (6) --- */
    { "are you healthy?",                    ROUTE_SYSFACTS, SF_HEALTH },
    { "is everything ok?",                   ROUTE_SYSFACTS, SF_HEALTH },
    { "are you ok?",                         ROUTE_SYSFACTS, SF_HEALTH },
    { "any errors right now?",               ROUTE_SYSFACTS, SF_HEALTH },
    { "is everything running fine?",         ROUTE_SYSFACTS, SF_HEALTH },
    { "u ok??",                              ROUTE_SYSFACTS, SF_HEALTH },    /* chatspeak */

    /* --- DECLINE (10; untracked status metrics — no real source) --- */
    { "what's your cpu usage?",              ROUTE_DECLINE, SF_NONE },      /* cpu */
    { "how busy are you right now?",         ROUTE_DECLINE, SF_NONE },      /* load */
    { "what's the load average?",            ROUTE_DECLINE, SF_NONE },      /* load */
    { "how much memory are you using?",      ROUTE_DECLINE, SF_NONE },      /* memory-used */
    { "how much ram is in use?",             ROUTE_DECLINE, SF_NONE },      /* memory-used */
    { "how much disk space is left?",        ROUTE_DECLINE, SF_NONE },      /* disk-free */
    { "how much storage do you have free?",  ROUTE_DECLINE, SF_NONE },      /* disk-free */
    { "how hot are you running?",            ROUTE_DECLINE, SF_NONE },      /* temperature */
    { "what's your temperature?",            ROUTE_DECLINE, SF_NONE },      /* temperature */
    { "what time is it?",                    ROUTE_DECLINE, SF_NONE },      /* wall-clock */

    /* --- INFER: the UNTRACKED-NOUN FALSE-POSITIVE family (6; DEV-side, for tuning) ---
     * A conceptual question that merely CONTAINS an untracked-metric noun (cpu / disk /
     * ram / load / temperature / time) is NOT a status request and MUST reach the model.
     * Added 2026-07-23 after the supervised B-flip validation caught the live defect: the
     * bare-noun DECLINE rule declined "why doesn't adding more CPU cores speed up a
     * single-threaded program?", violating route.h's stated ambiguity->INFER rule and
     * REGRESSING capability vs the ROUTING=0 box (which answers these via Gemma).
     * DECLINE must therefore require a STATUS/POSSESSIVE anchor, not the bare noun. */
    { "what does cpu cache coherency mean?",                      ROUTE_INFER, SF_NONE },
    { "explain how disk scheduling works",                        ROUTE_INFER, SF_NONE },
    { "what is time complexity?",                                 ROUTE_INFER, SF_NONE },
    { "how do temperature sensors work?",                         ROUTE_INFER, SF_NONE },
    { "why is ram faster than disk?",                             ROUTE_INFER, SF_NONE },
    { "what does load balancing mean?",                           ROUTE_INFER, SF_NONE },

    /* --- INFER (18; general knowledge / chit-chat / ambiguous counters) --- */
    { "what is a page fault?",                                    ROUTE_INFER, SF_NONE }, /* real 6-5 */
    { "explain paging in one line",                               ROUTE_INFER, SF_NONE }, /* real 6-5 */
    { "what is virtual memory?",                                  ROUTE_INFER, SF_NONE }, /* real 6-5 */
    { "what does the seL4 capability system provide",             ROUTE_INFER, SF_NONE }, /* real 6-5 */
    { "what is a mutex?",                                         ROUTE_INFER, SF_NONE },
    { "explain how a microkernel differs from a monolithic kernel",ROUTE_INFER, SF_NONE },
    { "what is a context switch?",                                ROUTE_INFER, SF_NONE },
    { "how does an mmu work?",                                    ROUTE_INFER, SF_NONE },
    { "what is a semaphore?",                                     ROUTE_INFER, SF_NONE },
    { "tell me a joke",                                           ROUTE_INFER, SF_NONE }, /* chit-chat */
    { "what's the capital of France?",                            ROUTE_INFER, SF_NONE }, /* general */
    { "how many queries have you answered?",                      ROUTE_INFER, SF_NONE }, /* ambiguous counter */
    { "how many questions have you handled so far?",              ROUTE_INFER, SF_NONE }, /* ambiguous counter */
    { "what is a system call?",                                   ROUTE_INFER, SF_NONE },
    { "explain deadlock in simple terms",                         ROUTE_INFER, SF_NONE },
    { "what does DMA stand for?",                                 ROUTE_INFER, SF_NONE },
    { "can you show your worker ids?",                            ROUTE_INFER, SF_NONE }, /* identifiers, not count */
    { "what is a b-tree?",                                        ROUTE_INFER, SF_NONE },
};

/* ======================================================================== *
 *  HELDOUT split — the >=95% is measured HERE. Disjoint from DEV.
 *  DO NOT edit to make route.c pass (fix route.c against DEV instead).
 * ======================================================================== */
static const routing_case_t ROUTING_HELDOUT[] = {
    /* --- SYSFACTS / SF_UPTIME (7; incl. typo) --- */
    { "how long have you been up and running?", ROUTE_SYSFACTS, SF_UPTIME },
    { "what is your current uptime",            ROUTE_SYSFACTS, SF_UPTIME },
    { "how many hours since boot?",             ROUTE_SYSFACTS, SF_UPTIME },
    { "how long ago did you start?",            ROUTE_SYSFACTS, SF_UPTIME },
    { "whats your uptiem",                      ROUTE_SYSFACTS, SF_UPTIME },  /* typo */
    { "how long have you been on for",          ROUTE_SYSFACTS, SF_UPTIME },
    { "duration since power-on?",               ROUTE_SYSFACTS, SF_UPTIME },

    /* --- SYSFACTS / SF_BOOT_ID (7; incl. typo) --- */
    { "what boot id is this?",                  ROUTE_SYSFACTS, SF_BOOT_ID },
    { "how many boots have there been?",        ROUTE_SYSFACTS, SF_BOOT_ID },
    { "which boot are we currently on",         ROUTE_SYSFACTS, SF_BOOT_ID },
    { "count of reboots since install?",        ROUTE_SYSFACTS, SF_BOOT_ID },
    { "what number boot is this session",       ROUTE_SYSFACTS, SF_BOOT_ID },
    { "how meny times have you rebooted",       ROUTE_SYSFACTS, SF_BOOT_ID }, /* typo */
    { "total times powered on?",                ROUTE_SYSFACTS, SF_BOOT_ID },

    /* --- SYSFACTS / SF_MODEL (7; incl. typo/chatspeak) --- */
    { "what model do you run on?",              ROUTE_SYSFACTS, SF_MODEL },
    { "which model is loaded right now?",       ROUTE_SYSFACTS, SF_MODEL },
    { "what llm sits behind you?",              ROUTE_SYSFACTS, SF_MODEL },
    { "name the model you use",                 ROUTE_SYSFACTS, SF_MODEL },
    { "what neural net are you built on?",       ROUTE_SYSFACTS, SF_MODEL },
    { "waht model r u",                         ROUTE_SYSFACTS, SF_MODEL },   /* typo/chatspeak */
    { "which gguf model is deployed?",          ROUTE_SYSFACTS, SF_MODEL },

    /* --- SYSFACTS / SF_NODES (7; incl. typo) --- */
    { "how many worker threads are there?",     ROUTE_SYSFACTS, SF_NODES },
    { "how many cores do you run across?",      ROUTE_SYSFACTS, SF_NODES },
    { "what is the node count?",                ROUTE_SYSFACTS, SF_NODES },
    { "how many parallel workers are active?",  ROUTE_SYSFACTS, SF_NODES },
    { "count of inference threads?",            ROUTE_SYSFACTS, SF_NODES },
    { "how many wokers do you have",            ROUTE_SYSFACTS, SF_NODES },   /* typo */
    { "across how many cpus do you run?",       ROUTE_SYSFACTS, SF_NODES },

    /* --- SYSFACTS / SF_HEALTH (7; incl. chatspeak) --- */
    { "how's your health?",                     ROUTE_SYSFACTS, SF_HEALTH },
    { "everything running smoothly?",           ROUTE_SYSFACTS, SF_HEALTH },
    { "any problems on your end?",              ROUTE_SYSFACTS, SF_HEALTH },
    { "are you doing alright?",                 ROUTE_SYSFACTS, SF_HEALTH },
    { "is your status healthy?",                ROUTE_SYSFACTS, SF_HEALTH },
    { "r u ok?",                                ROUTE_SYSFACTS, SF_HEALTH },  /* chatspeak */
    { "any faults or errors currently?",        ROUTE_SYSFACTS, SF_HEALTH },

    /* --- DECLINE (12; untracked status metrics; incl. typo) --- */
    { "what percent cpu are you at?",           ROUTE_DECLINE, SF_NONE },     /* cpu */
    { "how much cpu load do you have?",         ROUTE_DECLINE, SF_NONE },     /* cpu/load */
    { "whats the current load average",         ROUTE_DECLINE, SF_NONE },     /* load */
    { "how much memry are you using?",          ROUTE_DECLINE, SF_NONE },     /* typo memory-used */
    { "how much ram is consumed?",              ROUTE_DECLINE, SF_NONE },     /* memory-used */
    { "how many gigs of disk are free?",        ROUTE_DECLINE, SF_NONE },     /* disk-free */
    { "how much space is left on disk?",        ROUTE_DECLINE, SF_NONE },     /* disk-free */
    { "what's the cpu temperature?",            ROUTE_DECLINE, SF_NONE },     /* temperature */
    { "how warm is the chip?",                  ROUTE_DECLINE, SF_NONE },     /* temperature */
    { "what's the date today?",                 ROUTE_DECLINE, SF_NONE },     /* date */
    { "what's the current time of day?",        ROUTE_DECLINE, SF_NONE },     /* wall-clock */
    { "what day is it?",                        ROUTE_DECLINE, SF_NONE },     /* date */

    /* --- INFER: the UNTRACKED-NOUN FALSE-POSITIVE family (6; the HELDOUT measurement) ---
     * The boundary the ORIGINAL suite never exercised: every DECLINE case in both splits was
     * a genuine STATUS query, so a conceptual question containing an untracked-metric noun
     * was never tested and the 95.52% passed WITH the defect present. That is the honest
     * reason the held-out number is a POINT ESTIMATE, not a production-accuracy guarantee.
     * The first entry is the operator's live-validation query, verbatim.
     * NOTE "how much time does quicksort take?" is deliberately adversarial: it carries the
     * "how much" quantity-anchor AND an untracked noun, yet is plainly conceptual — so the
     * anchor set must be SPECIFIC (what time is it / the date), never a generic "how much". */
    { "why doesn't adding more CPU cores speed up a single-threaded program?", ROUTE_INFER, SF_NONE },
    { "how does a CPU pipeline work?",                           ROUTE_INFER, SF_NONE },
    { "what is disk fragmentation?",                             ROUTE_INFER, SF_NONE },
    { "how does virtual memory work?",                           ROUTE_INFER, SF_NONE },
    { "why does high temperature slow a processor down?",        ROUTE_INFER, SF_NONE },
    { "how much time does quicksort take?",                      ROUTE_INFER, SF_NONE },

    /* --- INFER (20; general knowledge / chit-chat / ambiguous counters) --- */
    { "what is a tlb?",                                          ROUTE_INFER, SF_NONE },
    { "explain copy-on-write",                                  ROUTE_INFER, SF_NONE },
    { "what is a spinlock?",                                    ROUTE_INFER, SF_NONE },
    { "how does virtual memory paging work in detail",          ROUTE_INFER, SF_NONE },
    { "what is an interrupt?",                                  ROUTE_INFER, SF_NONE },
    { "what does RAII mean?",                                   ROUTE_INFER, SF_NONE },
    { "explain the difference between a process and a thread",  ROUTE_INFER, SF_NONE },
    { "what is a race condition?",                              ROUTE_INFER, SF_NONE },
    { "tell me something interesting about seL4",               ROUTE_INFER, SF_NONE }, /* chit-chat */
    { "what is the capital of Japan?",                          ROUTE_INFER, SF_NONE }, /* general */
    { "how many questions have you answered today?",            ROUTE_INFER, SF_NONE }, /* ambiguous counter */
    { "how many queries did you process this hour?",            ROUTE_INFER, SF_NONE }, /* ambiguous counter */
    { "what is a kernel panic?",                                ROUTE_INFER, SF_NONE },
    { "explain what a syscall trap does",                       ROUTE_INFER, SF_NONE },
    { "what's 12 times 7?",                                     ROUTE_INFER, SF_NONE }, /* arithmetic */
    { "what is memory-mapped io?",                              ROUTE_INFER, SF_NONE },
    { "give me a fun fact",                                     ROUTE_INFER, SF_NONE }, /* chit-chat */
    { "what are your worker ids?",                              ROUTE_INFER, SF_NONE }, /* identifiers, not count */
    { "explain how tcp handshake works",                        ROUTE_INFER, SF_NONE },
    { "what is a hash table?",                                  ROUTE_INFER, SF_NONE },
};

#define ROUTING_DEV_N     (sizeof(ROUTING_DEV)     / sizeof(ROUTING_DEV[0]))
#define ROUTING_HELDOUT_N (sizeof(ROUTING_HELDOUT) / sizeof(ROUTING_HELDOUT[0]))

#endif /* ROUTING_SUITE_H */
