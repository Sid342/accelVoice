#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    APP_MODE_OFF    = 0,
    APP_MODE_NORMAL = 1,
    APP_MODE_TURBO  = 2,
} app_mode_t;

void        mode_set(app_mode_t m);
app_mode_t  mode_get(void);
const char *mode_str(app_mode_t m);
app_mode_t  mode_from_str(const char *s);

/* Derived ionizer-would-be state given mode + wear + battery override.
 * Off-body, OFF mode, or battery override → false. Else true.              */
bool ionizer_state(void);
