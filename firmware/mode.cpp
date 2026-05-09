#include "mode.h"
#include "wear.h"
#include "battsim.h"
#include <string.h>

static app_mode_t s_mode = APP_MODE_NORMAL;

void       mode_set(app_mode_t m) { s_mode = m; }
app_mode_t mode_get(void)         { return s_mode; }

const char *mode_str(app_mode_t m)
{
    switch (m) {
        case APP_MODE_OFF:    return "off";
        case APP_MODE_NORMAL: return "normal";
        case APP_MODE_TURBO:  return "turbo";
    }
    return "?";
}

app_mode_t mode_from_str(const char *s)
{
    if (s == nullptr) return APP_MODE_NORMAL;
    if (!strcmp(s, "off"))    return APP_MODE_OFF;
    if (!strcmp(s, "turbo"))  return APP_MODE_TURBO;
    return APP_MODE_NORMAL;
}

bool ionizer_state(void)
{
    if (s_mode == APP_MODE_OFF)              return false;
    if (battsim_overrides_ionizer())         return false;
    if (wear_get_state() != WEAR_STATE_ON_BODY) return false;
    return true;
}
