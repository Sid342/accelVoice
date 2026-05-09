#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    APP_MODE_OFF    = 0,
    APP_MODE_NORMAL = 1,
    APP_MODE_TURBO  = 2,
    APP_MODE_AUTO   = 3,
} app_mode_t;

void        mode_set(app_mode_t m);
app_mode_t  mode_get(void);
const char *mode_str(app_mode_t m);
app_mode_t  mode_from_str(const char *s);

/* Derived ionizer-would-be state.
 *   OFF    → false
 *   batt override → false
 *   AUTO   → ionizer follows wear (on-body=true / off-body=false)
 *   NORMAL → always true (bench/lab mode — respects OFF + battery only)
 *   TURBO  → always true (same as NORMAL; flagged as boosted upstream)
 */
bool ionizer_state(void);
