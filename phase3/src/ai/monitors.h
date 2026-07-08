/*
 * JARVIS AI-OS — Phase 6 goal 6-1 M0: the pure monitor framework.
 *
 * Lightweight threshold watchers over REAL observable box state. A monitor_t is a
 * debounced, fire-once-per-crossing threshold comparator; monitor_delta_step turns a
 * per-boot cumulative counter into a window delta; monitor_build_snapshot renders the
 * keyword-clean SYSTEM-FACTS-ONLY event snapshot a NOTIFY carries through the K action
 * spine (ACTION_NOTIFY_ANOMALY -> shield_assess -> JACT). Fixed literals + decimal
 * counters ONLY — free text or query text would let a blocklist keyword make
 * shield_assess BLOCK the monitor's own NOTIFY (the km2b_build_trigger discipline).
 *
 * FIRE-ONCE SEMANTICS (the anti-spam crux): a crossing = the compare holding true for
 * `debounce` CONSECUTIVE samples while armed; the debounce-th sample returns 1 exactly
 * once and disarms. Re-arm happens only when a sample lands on the NON-triggering side
 * (hysteresis) — a sustained condition fires once, not per sample.
 *
 * Host-pure: NO <sel4/sel4.h>; stdint types only (the km2b_miss.h / km2b_fault.h
 * precedent). Wiring into Process A is M1 (gated JARVIS_MONITORS, default 0).
 * Design: phase6/docs/PHASE_6_GOAL_6-1_MONITORS.md.
 */

#ifndef MONITORS_H
#define MONITORS_H

#include <stdint.h>

#define MON_SNAP_MAX 96

/* Event types — the 6-2/6-3 seam: a wake/briefing consumer reads the same event the
 * NOTIFY path emits. Extend by appending (never renumber — JACT snapshots cite these). */
typedef enum {
    MON_EV_NONE = 0,
    MON_EV_ERROR_RATE,        /* q_errors window delta crossed the threshold        */
    MON_EV_SELF_HEAL_RATE,    /* restart_count window delta crossed the threshold   */
    MON_EV_STORE_WRAP,        /* a circular store wrapped (fires once per store)    */
    MON_EV_HEARTBEAT_AGE,     /* PB heartbeat age exceeded the calibrated threshold */
    MON_EV_UPTIME_MILESTONE,  /* boot-relative uptime crossed a milestone mark      */
    MON_EV__COUNT
} monitor_event_type_t;

typedef struct {
    monitor_event_type_t type;
    uint8_t              severity;              /* 0=info .. 2=warn (advisory only)  */
    uint16_t             snap_len;              /* bytes in snap, excl. the NUL      */
    char                 snap[MON_SNAP_MAX];    /* keyword-clean system-facts string */
} monitor_event_t;

typedef enum { MON_CMP_GE = 0, MON_CMP_LE = 1 } monitor_cmp_t;

typedef struct {
    int64_t       threshold;
    monitor_cmp_t cmp;
    uint16_t      debounce;   /* consecutive triggering samples required; 0 acts as 1 */
    /* state */
    uint16_t      consec;     /* consecutive triggering samples so far                */
    uint8_t       armed;      /* 1 = may fire; cleared on fire, re-set on hysteresis  */
} monitor_t;

/* Initialize (armed, consec 0). debounce 0 is treated as 1. NULL-safe (no-op). */
void monitor_init(monitor_t *m, int64_t threshold, monitor_cmp_t cmp, uint16_t debounce);

/* Feed one sample. Returns 1 exactly ONCE per debounced crossing (see header note);
 * 0 otherwise. NULL-safe (returns 0). */
int monitor_sample(monitor_t *m, int64_t value);

/* Per-boot cumulative-counter delta helper. */
typedef struct {
    uint64_t prev;
    uint32_t boot_id;
    uint8_t  have_prev;
} monitor_delta_t;

/* Returns cumulative - prev for the window, then advances prev. On the FIRST call or a
 * boot_id change, stores the baseline and returns 0 (per-boot counters restart across a
 * boot — the episodic/JACT boot_id precedent). NULL-safe (returns 0). */
uint64_t monitor_delta_step(monitor_delta_t *d, uint64_t cumulative, uint32_t boot_id);

/* Render the keyword-clean snapshot for an event: fixed literals + decimals only,
 * cap-bounded to MON_SNAP_MAX-1, NUL-terminated, snap_len set. v1/v2 are the two
 * event-specific facts (delta+window, ms+threshold, ...). Returns snap_len (> 0), or
 * -1 on NULL event / MON_EV_NONE / out-of-range type. */
int monitor_build_snapshot(monitor_event_t *ev, monitor_event_type_t type,
                           uint8_t severity, int64_t v1, int64_t v2);

#endif /* MONITORS_H */
