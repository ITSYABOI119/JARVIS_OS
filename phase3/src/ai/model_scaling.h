#ifndef MODEL_SCALING_H
#define MODEL_SCALING_H

typedef enum {
    SCALING_IDLE      = 0,  /* Llama 3.2 1B Q4_K_M (19.79 tok/s, fast) */
    SCALING_ACTIVE    = 1,  /* Gemma 4 E2B Q4_K_M (8.63 tok/s, #1 quality) */
    SCALING_CRITICAL  = 2,  /* Mistral 7B Q8_0 (4.16 tok/s, #2 quality) */
    SCALING_EMERGENCY = 3,  /* Cache + SHIELD rules only (no model) */
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
const char *scaler_model_file(scaling_state_t state);

#endif
