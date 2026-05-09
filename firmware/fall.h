#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    FALL_IDLE        = 0,
    FALL_FREEFALL    = 1,
    FALL_IMPACT      = 2,
    FALL_STILL_PEND  = 3,
    FALL_CONFIRMED   = 4,
} fall_state_t;

typedef struct {
    uint32_t t_ms;
    uint16_t freefall_min_mg;     /* min mag during free-fall window           */
    uint16_t impact_max_mg;       /* peak mag during impact                    */
    uint16_t still_mad_mg;        /* MAD that triggered confirmation           */
    bool     simulated;
} fall_event_t;

void          fall_init(void);
/* Feed at 100 Hz (every fast-loop tick). mag_mg is √(x²+y²+z²) precomputed
 * by the caller (firmware.ino already has it). */
void          fall_feed_sample(int16_t x_mg, int16_t y_mg, int16_t z_mg,
                               int16_t mag_mg);

fall_state_t  fall_get_state(void);
const char   *fall_state_str(fall_state_t s);
uint32_t      fall_total_falls(void);
uint32_t      fall_last_fall_ms(void);

size_t        fall_get_events_since(uint32_t since_ms,
                                    fall_event_t *out, size_t max);

/* Manual exercise — fires a synthetic FALL_CONFIRMED event. */
void          fall_simulate(void);

/* Tunables. */
void          fall_set_freefall_mg(uint16_t mg);
uint16_t      fall_get_freefall_mg(void);
void          fall_set_impact_mg(uint16_t mg);
uint16_t      fall_get_impact_mg(void);
