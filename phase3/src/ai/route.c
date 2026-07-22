/*
 * route.c — the control-IN QUERY ROUTER classifier (Phase 6 6-6/B M0).
 *
 * See route.h for the contract + the honesty ceiling. This is a COARSE,
 * KEYWORD-based intent classifier over three DEFINED handlers, NOT a semantic
 * matcher (the embedding arc is goal doc §8).
 *
 * Engine: normalize the untrusted bytes into a fixed stack buffer (lowercase,
 * alphanumeric words, single-spaced — the qs_normalize precedent), tokenize into
 * whole words, then run ORDERED per-field predicate rules over small static word
 * tables. Order is load-bearing (first match wins):
 *
 *   (1) SYSFACTS, most-distinctive field first:
 *         MODEL  — model/llm/gguf/neural/hood nouns (unambiguous, checked first
 *                  so "which llm are you RUNNING" resolves to MODEL, not uptime).
 *         NODES  — a worker/core/thread/node/cpus COUNT question (requires a
 *                  count-context word AND no "id"/"ids", so "a process and a
 *                  THREAD" and "show your worker IDS" stay INFERENCE).
 *         HEALTH — healthy/ok/errors/faults/fine/problems/alright/smoothly.
 *         UPTIME — uptime/duration/hours or a "how long"/"since when"/"been up|
 *                  running|on" DURATION phrase (checked before BOOT_ID so
 *                  "how long since you last BOOTED" is uptime, not a boot count).
 *         BOOT_ID— boot/reboot morphology + power cycles (a COUNT of boots; the
 *                  duration-boot phrasings were already claimed by UPTIME above).
 *   (2) DECLINE — an untracked status metric (cpu/busy/load/ram/disk/storage/
 *                 temperature/wall-clock). "memory" requires a usage word so the
 *                 CONCEPT question "what is virtual MEMORY" stays INFERENCE.
 *   (3) else ROUTE_INFER — the safe default (goal doc §7 CONSERVATIVE rule).
 *
 * Every bound is a token index into `norm`; no OOB for any input (0..255,
 * embedded NUL, any length). If a rule false-positives, NARROW it, never widen
 * it (the query_shield discipline). The keyword tables were tuned against the
 * DEV split of routing_suite.h + general knowledge of how humans phrase these
 * questions — never against the HELDOUT split (the anti-circularity rule).
 */

#include "route.h"

#define RT_NORM_MAX  192u   /* > CONTROL_QUERY_MAX (172) + NUL */
#define RT_MAX_WORDS 128    /* norm has <= ~96 words; cap the token array */

/* Rule ids (route_result_t.matched_pattern) — one per fired rule, -1 on INFER. */
enum {
    RID_MODEL = 0,
    RID_NODES,
    RID_HEALTH,
    RID_UPTIME,
    RID_BOOT,
    RID_DECLINE
};

/*
 * Normalize `len` untrusted bytes of `q` into `norm` (NUL-terminated): A-Z ->
 * a-z, keep [a-z0-9], collapse EVERY other byte (space, punctuation, control,
 * non-printable, embedded NUL, high bytes) into a single-space word separator;
 * trim leading/trailing spaces. Returns the normalized length (excluding NUL).
 * Length-bounded — never reads past q[len-1], never writes past norm[cap-1].
 * (The qs_normalize precedent — deliberately NOT a NUL-terminated normalizer;
 * the input is length-carried raw bytes.)
 */
static size_t rt_normalize(const char *q, size_t len, char *norm, size_t cap)
{
    if (cap == 0) return 0;
    size_t o = 0;
    int prev_sep = 1;   /* suppress leading separators */
    for (size_t i = 0; i < len && o + 1 < cap; i++) {
        unsigned char c = (unsigned char)q[i];
        if (c >= 'A' && c <= 'Z') {
            norm[o++] = (char)(c - 'A' + 'a');
            prev_sep = 0;
        } else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            norm[o++] = (char)c;
            prev_sep = 0;
        } else if (!prev_sep) {
            norm[o++] = ' ';
            prev_sep = 1;
        }
    }
    while (o > 0 && norm[o - 1] == ' ') o--;   /* trim trailing */
    norm[o] = '\0';
    return o;
}

/* Tokenized view of `norm`: whole-word [start, end) spans. */
typedef struct {
    const char *norm;
    size_t      ws[RT_MAX_WORDS];
    size_t      we[RT_MAX_WORDS];
    int         nw;
} rt_words_t;

static void rt_tokenize(const char *norm, size_t nl, rt_words_t *W)
{
    W->norm = norm;
    W->nw = 0;
    for (size_t i = 0; i < nl && W->nw < RT_MAX_WORDS; ) {
        while (i < nl && norm[i] == ' ') i++;
        if (i >= nl) break;
        size_t s = i;
        while (i < nl && norm[i] != ' ') i++;
        W->ws[W->nw] = s;
        W->we[W->nw] = i;
        W->nw++;
    }
}

/* norm[start..end) (a whole word) equals the NUL-terminated `w`. */
static int rt_word_eq(const char *norm, size_t start, size_t end, const char *w)
{
    size_t i = 0;
    for (; w[i]; i++) {
        if (start + i >= end) return 0;         /* w longer than the word */
        if (norm[start + i] != w[i]) return 0;
    }
    return (start + i == end);                  /* exact word length */
}

/* Does any whole word of `norm` equal `w`? */
static int rt_word(const rt_words_t *W, const char *w)
{
    for (int i = 0; i < W->nw; i++)
        if (rt_word_eq(W->norm, W->ws[i], W->we[i], w))
            return 1;
    return 0;
}

/* Does any word of `norm` equal any entry of the NULL-terminated `list`? */
static int rt_any(const rt_words_t *W, const char *const *list)
{
    for (int k = 0; list[k]; k++)
        if (rt_word(W, list[k]))
            return 1;
    return 0;
}

/* Ordered 2-word phrase: word `a`, then word `b` within `gap` words after it. */
static int rt_seq2(const rt_words_t *W, const char *a, const char *b, int gap)
{
    for (int i = 0; i < W->nw; i++) {
        if (!rt_word_eq(W->norm, W->ws[i], W->we[i], a)) continue;
        int hi = i + gap;
        if (hi >= W->nw) hi = W->nw - 1;
        for (int j = i + 1; j <= hi; j++)
            if (rt_word_eq(W->norm, W->ws[j], W->we[j], b))
                return 1;
    }
    return 0;
}

/* ---- static, human-reviewed keyword tables (tuned against DEV + general knowledge) ---- */

/* MODEL — model-distinctive nouns; checked first (unambiguous, never collides). */
static const char *const RT_MODEL_WORDS[] = {
    "model", "llm", "gguf", "neural", "hood", NULL
};
/* NODES — the worker/core/thread/node/cpus nouns (a COUNT question). */
static const char *const RT_NODE_WORDS[] = {
    "worker", "workers", "core", "cores", "thread", "threads",
    "node", "nodes", "cpus", NULL
};
/* Count-context that qualifies a node-noun as a COUNT question (so "a process
 * and a thread" — a concept question with no count word — stays INFERENCE). */
static const char *const RT_COUNT_WORDS[] = {
    "many", "count", "number", "across", NULL
};
/* Identifier words that DISqualify a node-noun (worker IDS -> INFERENCE). */
static const char *const RT_ID_WORDS[] = { "id", "ids", NULL };
/* HEALTH — status/wellbeing words (plural "faults"/"errors" only, so a CS
 * "page FAULT" concept question stays INFERENCE via whole-word matching). */
static const char *const RT_HEALTH_WORDS[] = {
    "healthy", "health", "ok", "okay", "errors", "error", "faults",
    "fine", "alright", "problems", "smoothly", "wrong", NULL
};
/* UPTIME — single-word duration signals (phrase signals handled separately). */
static const char *const RT_UPTIME_WORDS[] = {
    "uptime", "duration", "hours", NULL
};
/* BOOT_ID — boot/reboot morphology + power-cycle count words. Safe as bare words
 * because the DURATION-boot phrasings ("how long since ... booted") were already
 * claimed by UPTIME, which is checked first. */
static const char *const RT_BOOT_WORDS[] = {
    "boot", "boots", "booted", "booting",
    "reboot", "reboots", "rebooted", "rebooting",
    "cycles", "powered", NULL
};
/* Usage words required to treat "memory" as a resource-usage DECLINE (so the
 * CONCEPT question "what is virtual memory" / "memory-mapped io" stays INFER). */
static const char *const RT_USE_WORDS[] = {
    "much", "usage", "used", "using", "use", "consumed", "free", NULL
};
/* DECLINE — untracked status metrics (bare "ram" is safe; "memory" is guarded
 * above). "time"/"date"/"day" are wall-clock (no RTC). Whole-word matching keeps
 * "times" (boot count) / "today" (chit-chat) out. */
static const char *const RT_DECLINE_WORDS[] = {
    "cpu", "busy", "load", "ram", "disk", "storage",
    "temperature", "temp", "hot", "warm",
    "time", "date", "day", "clock", NULL
};

static route_class_t rt_finish(route_result_t *out, route_class_t cls,
                               sysfact_field_t field, int pat)
{
    if (out) {
        out->cls = cls;
        out->field = field;
        out->matched_pattern = (int16_t)pat;
    }
    return cls;
}

route_class_t route_classify(const char *q, size_t len, route_result_t *out)
{
    if (out) { out->cls = ROUTE_INFER; out->field = SF_NONE; out->matched_pattern = -1; }
    if (!q || len == 0) return ROUTE_INFER;

    char norm[RT_NORM_MAX];
    size_t nl = rt_normalize(q, len, norm, sizeof norm);

    rt_words_t W;
    rt_tokenize(norm, nl, &W);

    /* ---- (1) SYSFACTS (most-distinctive field first; first match wins) ---- */

    /* MODEL: unambiguous nouns; checked before UPTIME so "which llm are you
     * running" / "what model r u running" resolve to MODEL. */
    if (rt_any(&W, RT_MODEL_WORDS))
        return rt_finish(out, ROUTE_SYSFACTS, SF_MODEL, RID_MODEL);

    /* NODES: a node-noun WITH count context and WITHOUT an identifier word. The
     * count-context requirement keeps concept questions ("a process and a
     * thread") in INFERENCE; the no-id guard keeps "worker ids" in INFERENCE. */
    if (!rt_any(&W, RT_ID_WORDS) && rt_any(&W, RT_COUNT_WORDS) && rt_any(&W, RT_NODE_WORDS))
        return rt_finish(out, ROUTE_SYSFACTS, SF_NODES, RID_NODES);

    /* HEALTH: wellbeing/status words. */
    if (rt_any(&W, RT_HEALTH_WORDS))
        return rt_finish(out, ROUTE_SYSFACTS, SF_HEALTH, RID_HEALTH);

    /* UPTIME: a duration signal. Checked before BOOT_ID so "how long since you
     * last booted" and "how many hours since boot" are uptime, not boot count. */
    if (rt_any(&W, RT_UPTIME_WORDS) ||
        rt_seq2(&W, "how", "long", 1) ||
        rt_seq2(&W, "since", "when", 1) ||
        rt_seq2(&W, "been", "up", 2) ||
        rt_seq2(&W, "been", "running", 2) ||
        rt_seq2(&W, "been", "on", 2))
        return rt_finish(out, ROUTE_SYSFACTS, SF_UPTIME, RID_UPTIME);

    /* BOOT_ID: boot/reboot/power-cycle morphology (a COUNT of boots). */
    if (rt_any(&W, RT_BOOT_WORDS))
        return rt_finish(out, ROUTE_SYSFACTS, SF_BOOT_ID, RID_BOOT);

    /* ---- (2) DECLINE (untracked status metric; no valid source) ---- */
    if (rt_any(&W, RT_DECLINE_WORDS) ||
        (rt_word(&W, "memory") && rt_any(&W, RT_USE_WORDS)))
        return rt_finish(out, ROUTE_DECLINE, SF_NONE, RID_DECLINE);

    /* ---- (3) INFER — the safe default (out already initialized) ---- */
    return ROUTE_INFER;
}

/* ---- SYSTEM-FACTS rendering (reads only the whitelisted `facts` fields) ---- */

/* Append the decimal of `v` to out[o..cap), bounded. Returns the new offset. */
static size_t rt_put_u32(char *out, size_t cap, size_t o, uint32_t v)
{
    char tmp[10];
    int n = 0;
    if (v == 0) tmp[n++] = '0';
    while (v) { tmp[n++] = (char)('0' + (v % 10u)); v /= 10u; }
    for (int i = n - 1; i >= 0 && o + 1 < cap; i--) out[o++] = tmp[i];
    return o;
}

/* Append the NUL-terminated `s` to out[o..cap), bounded. Returns the new offset. */
static size_t rt_put_str(char *out, size_t cap, size_t o, const char *s)
{
    for (size_t i = 0; s[i] && o + 1 < cap; i++) out[o++] = s[i];
    return o;
}

int sysfacts_answer(sysfact_field_t field, const sysfacts_t *facts, char *out, size_t cap)
{
    if (!out || cap == 0) return 0;
    out[0] = '\0';
    if (!facts) return 0;

    size_t o = 0;
    switch (field) {
    case SF_UPTIME:
        o = rt_put_str(out, cap, o, "up ");
        o = rt_put_u32(out, cap, o, facts->uptime_ms / 1000u);
        o = rt_put_str(out, cap, o, " seconds");
        break;
    case SF_BOOT_ID:
        o = rt_put_str(out, cap, o, "boot ");
        o = rt_put_u32(out, cap, o, facts->boot_id);
        break;
    case SF_MODEL: {
        /* model_name is bounded to its array size and may be non-NUL-terminated;
         * copy up to the first NUL or the array end (never strlen'd). */
        size_t mlen = 0;
        while (mlen < sizeof facts->model_name && facts->model_name[mlen] != '\0') mlen++;
        for (size_t i = 0; i < mlen && o + 1 < cap; i++) out[o++] = facts->model_name[i];
        break;
    }
    case SF_NODES:
        o = rt_put_u32(out, cap, o, (uint32_t)facts->num_nodes);
        o = rt_put_str(out, cap, o, " workers");
        break;
    case SF_HEALTH:
        o = rt_put_str(out, cap, o, facts->healthy ? "healthy, no errors" : "errors have occurred");
        break;
    case SF_NONE:
    case SF__COUNT:
    default:
        out[0] = '\0';
        return 0;   /* unmappable -> nothing rendered (caller DECLINEs/INFERs) */
    }
    out[o] = '\0';
    return (int)o;
}

const char *route_decline_text(void)
{
    return "I don't track that.";
}
