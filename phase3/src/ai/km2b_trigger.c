/* Phase 6 K/M2b-2 — keyword-clean restart trigger builder (§5-E). See km2b_trigger.h. */

#include "km2b_trigger.h"
#include <stddef.h>

/* Bounded append of a NUL-terminated literal. */
static int tb_ts(char *buf, int cap, int p, const char *s)
{
    while (*s && p < cap - 1) buf[p++] = *s++;
    return p;
}

/* Bounded append of a u32 in decimal. */
static int tb_tu(char *buf, int cap, int p, uint32_t v)
{
    char d[12]; int n = 0;
    if (!v) d[n++] = '0'; else while (v) { d[n++] = (char)('0' + v % 10); v /= 10; }
    while (n && p < cap - 1) buf[p++] = d[--n];
    return p;
}

int km2b_build_trigger(char *buf, int cap, const char *reason, uint16_t lane,
                       uint32_t missed, uint64_t age_ms, uint64_t flabel,
                       uint64_t fbadge)
{
    if (buf == NULL || cap < 1) return 0;
    int p = 0;
    p = tb_ts(buf, cap, p, "respawn ");
    p = tb_ts(buf, cap, p, reason ? reason : "?");
    p = tb_ts(buf, cap, p, " lane="); p = tb_tu(buf, cap, p, (uint32_t)lane);
    p = tb_ts(buf, cap, p, " miss="); p = tb_tu(buf, cap, p, missed);
    p = tb_ts(buf, cap, p, " age=");  p = tb_tu(buf, cap, p, (uint32_t)age_ms);
    p = tb_ts(buf, cap, p, "ms");
    p = tb_ts(buf, cap, p, " flt=");  p = tb_tu(buf, cap, p, (uint32_t)flabel);
    p = tb_ts(buf, cap, p, "/");      p = tb_tu(buf, cap, p, (uint32_t)fbadge);
    buf[p] = '\0';
    return p;
}
