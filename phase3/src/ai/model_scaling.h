#ifndef MODEL_SCALING_H
#define MODEL_SCALING_H

typedef enum {
    SCALING_IDLE      = 0,  /* 1B model, low resource usage */
    SCALING_ACTIVE    = 1,  /* 3B model, higher quality */
    SCALING_CRITICAL  = 2,  /* 3B + validator (dual inference) */
    SCALING_EMERGENCY = 3,  /* Rules-only, no model (failsafe) */
} scaling_state_t;

typedef struct {
    scaling_state_t current_state;
    scaling_state_t target_state;
    int transitions;          /* Count of state changes */
    int idle_counter;         /* Updates spent below idle threshold */
    /* Thresholds (configurable) */
    float idle_threshold;     /* Below this load -> can drop to IDLE (default 0.2) */
    float active_threshold;   /* Above this -> move to ACTIVE (default 0.5) */
    float critical_threshold; /* Above this -> move to CRITICAL (default 0.8) */
    int idle_hysteresis;      /* Must stay below idle_threshold for this many updates (default 10) */
} model_scaler_t;

void scaler_init(model_scaler_t *s);
scaling_state_t scaler_update(model_scaler_t *s, float system_load, int pending_queries);
void scaler_force_emergency(model_scaler_t *s);
const char *scaler_state_name(scaling_state_t state);

#endif
