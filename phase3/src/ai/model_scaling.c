#include "model_scaling.h"
#include <string.h>

void scaler_init(model_scaler_t *s) {
    memset(s, 0, sizeof(*s));
    s->current_state = SCALING_IDLE;
    s->target_state = SCALING_IDLE;
    s->idle_threshold = 0.2f;
    s->active_threshold = 0.5f;
    s->critical_threshold = 0.8f;
    s->idle_hysteresis = 10;
}

scaling_state_t scaler_update(model_scaler_t *s, float system_load, int pending_queries) {
    scaling_state_t prev = s->current_state;
    (void)prev;

    /* Determine target based on load and pending queries */
    if (system_load >= s->critical_threshold || pending_queries > 20) {
        s->target_state = SCALING_CRITICAL;
        s->idle_counter = 0;
    } else if (system_load >= s->active_threshold || pending_queries > 5) {
        s->target_state = SCALING_ACTIVE;
        s->idle_counter = 0;
    } else if (system_load < s->idle_threshold && pending_queries == 0) {
        s->idle_counter++;
        if (s->idle_counter >= s->idle_hysteresis)
            s->target_state = SCALING_IDLE;
    } else {
        s->idle_counter = 0;
        /* Keep current state if in ambiguous zone */
    }

    /* Don't downgrade from EMERGENCY without explicit reset */
    if (s->current_state == SCALING_EMERGENCY)
        return s->current_state;

    /* Apply transition (only escalate instantly, de-escalate with hysteresis) */
    if (s->target_state > s->current_state) {
        s->current_state = s->target_state;
        s->transitions++;
    } else if (s->target_state < s->current_state && s->idle_counter >= s->idle_hysteresis) {
        s->current_state = s->target_state;
        s->transitions++;
    }

    return s->current_state;
}

void scaler_force_emergency(model_scaler_t *s) {
    if (s->current_state != SCALING_EMERGENCY) {
        s->current_state = SCALING_EMERGENCY;
        s->target_state = SCALING_EMERGENCY;
        s->transitions++;
    }
}

const char *scaler_state_name(scaling_state_t state) {
    switch (state) {
    case SCALING_IDLE:      return "IDLE";
    case SCALING_ACTIVE:    return "ACTIVE";
    case SCALING_CRITICAL:  return "CRITICAL";
    case SCALING_EMERGENCY: return "EMERGENCY";
    default:                return "UNKNOWN";
    }
}

const char *scaler_model_file(scaling_state_t state)
{
    switch (state) {
    case SCALING_IDLE:      return "LLAMA1B GUF";
    case SCALING_ACTIVE:    return "GEMMA2B GUF";
    case SCALING_CRITICAL:  return "MISTR7B GUF";
    case SCALING_EMERGENCY: return NULL;
    default:                return NULL;
    }
}
