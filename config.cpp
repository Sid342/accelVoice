#include "config.h"
#include "app_config.h"
#include "wear.h"
#include "accel.h"
#include "steps.h"
#include <Preferences.h>
#include <Arduino.h>

static cfg_t s_cfg;
static Preferences s_prefs;
static const char *NS = "atovio";

static void load_defaults(void)
{
    s_cfg.motion_thresh_mg = ACCEL_MOTION_THRESH_MG;
    s_cfg.still_sec        = ACCEL_STILL_SEC;
    s_cfg.offbody_sec      = ACCEL_OFFBODY_SEC;
    s_cfg.step_thresh_mg   = STEP_PEAK_THRESH_MG;
    s_cfg.cal_x_mg         = 0;
    s_cfg.cal_y_mg         = 0;
    s_cfg.cal_z_mg         = 0;
}

void cfg_load(void)
{
    load_defaults();
    s_prefs.begin(NS, true /* read-only */);
    if (s_prefs.isKey("mot_thr_mg"))  s_cfg.motion_thresh_mg = s_prefs.getUShort("mot_thr_mg", s_cfg.motion_thresh_mg);
    if (s_prefs.isKey("still_sec"))   s_cfg.still_sec        = s_prefs.getUShort("still_sec",  s_cfg.still_sec);
    if (s_prefs.isKey("offbody_sec")) s_cfg.offbody_sec      = s_prefs.getUShort("offbody_sec", s_cfg.offbody_sec);
    if (s_prefs.isKey("step_thr_mg")) s_cfg.step_thresh_mg   = s_prefs.getUShort("step_thr_mg", s_cfg.step_thresh_mg);
    if (s_prefs.isKey("cal_x"))       s_cfg.cal_x_mg         = s_prefs.getShort("cal_x", 0);
    if (s_prefs.isKey("cal_y"))       s_cfg.cal_y_mg         = s_prefs.getShort("cal_y", 0);
    if (s_prefs.isKey("cal_z"))       s_cfg.cal_z_mg         = s_prefs.getShort("cal_z", 0);
    s_prefs.end();
}

void cfg_save(void)
{
    s_prefs.begin(NS, false /* read-write */);
    s_prefs.putUShort("mot_thr_mg",  s_cfg.motion_thresh_mg);
    s_prefs.putUShort("still_sec",   s_cfg.still_sec);
    s_prefs.putUShort("offbody_sec", s_cfg.offbody_sec);
    s_prefs.putUShort("step_thr_mg", s_cfg.step_thresh_mg);
    s_prefs.putShort("cal_x",        s_cfg.cal_x_mg);
    s_prefs.putShort("cal_y",        s_cfg.cal_y_mg);
    s_prefs.putShort("cal_z",        s_cfg.cal_z_mg);
    s_prefs.end();
}

cfg_t *cfg_get(void) { return &s_cfg; }

void cfg_apply(void)
{
    accel_set_motion_thresh_mg(s_cfg.motion_thresh_mg);
    accel_set_offsets_mg(s_cfg.cal_x_mg, s_cfg.cal_y_mg, s_cfg.cal_z_mg);
    wear_set_timings(s_cfg.still_sec, s_cfg.offbody_sec);
    steps_set_thresh_mg(s_cfg.step_thresh_mg);
}
