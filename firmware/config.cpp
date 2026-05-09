#include "config.h"
#include "app_config.h"
#include "wear.h"
#include "accel.h"
#include "steps.h"
#include "respiration.h"
#include <Preferences.h>
#include <Arduino.h>
#include <string.h>

static cfg_t s_cfg;
static Preferences s_prefs;
static const char *NS = "atovio";

static void load_defaults(void)
{
    s_cfg.motion_thresh_mg          = ACCEL_MOTION_THRESH_MG;
    s_cfg.still_sec                 = ACCEL_STILL_SEC;
    s_cfg.offbody_sec               = ACCEL_OFFBODY_SEC;
    s_cfg.wear_var_thresh_mg        = WEAR_VAR_THRESH_MG;
    s_cfg.wear_grav_diff_thresh_mg  = WEAR_GRAV_DIFF_THRESH_MG;
    s_cfg.step_thresh_mg   = STEP_PEAK_THRESH_MG;
    s_cfg.step_min_ms      = STEP_MIN_INTERVAL_MS;
    s_cfg.step_max_ms      = STEP_MAX_INTERVAL_MS;
    s_cfg.bpm_min          = RESP_BPM_MIN;
    s_cfg.bpm_max          = RESP_BPM_MAX;
    s_cfg.resp_window_sec  = RESP_WINDOW_SEC;
    s_cfg.cal_x_mg         = 0;
    s_cfg.cal_y_mg         = 0;
    s_cfg.cal_z_mg         = 0;
    memset(s_cfg.wifi_ssid, 0, CFG_WIFI_FIELD_LEN);
    memset(s_cfg.wifi_pass, 0, CFG_WIFI_FIELD_LEN);
    s_cfg.wifi_sta_enabled = false;
}

void cfg_load(void)
{
    load_defaults();
    s_prefs.begin(NS, true);
    if (s_prefs.isKey("mot_thr_mg"))   s_cfg.motion_thresh_mg = s_prefs.getUShort("mot_thr_mg",   s_cfg.motion_thresh_mg);
    if (s_prefs.isKey("still_sec"))    s_cfg.still_sec        = s_prefs.getUShort("still_sec",    s_cfg.still_sec);
    if (s_prefs.isKey("offbody_sec"))  s_cfg.offbody_sec      = s_prefs.getUShort("offbody_sec",  s_cfg.offbody_sec);
    if (s_prefs.isKey("wvar_thr"))     s_cfg.wear_var_thresh_mg       = s_prefs.getUShort("wvar_thr",  s_cfg.wear_var_thresh_mg);
    if (s_prefs.isKey("wgrav_thr"))    s_cfg.wear_grav_diff_thresh_mg = s_prefs.getUShort("wgrav_thr", s_cfg.wear_grav_diff_thresh_mg);
    if (s_prefs.isKey("step_thr_mg"))  s_cfg.step_thresh_mg   = s_prefs.getUShort("step_thr_mg",  s_cfg.step_thresh_mg);
    if (s_prefs.isKey("step_min"))     s_cfg.step_min_ms      = s_prefs.getUShort("step_min",     s_cfg.step_min_ms);
    if (s_prefs.isKey("step_max"))     s_cfg.step_max_ms      = s_prefs.getUShort("step_max",     s_cfg.step_max_ms);
    if (s_prefs.isKey("bpm_min"))      s_cfg.bpm_min          = s_prefs.getUChar("bpm_min",       s_cfg.bpm_min);
    if (s_prefs.isKey("bpm_max"))      s_cfg.bpm_max          = s_prefs.getUChar("bpm_max",       s_cfg.bpm_max);
    if (s_prefs.isKey("resp_win"))     s_cfg.resp_window_sec  = s_prefs.getUChar("resp_win",      s_cfg.resp_window_sec);
    if (s_prefs.isKey("cal_x"))        s_cfg.cal_x_mg         = s_prefs.getShort("cal_x", 0);
    if (s_prefs.isKey("cal_y"))        s_cfg.cal_y_mg         = s_prefs.getShort("cal_y", 0);
    if (s_prefs.isKey("cal_z"))        s_cfg.cal_z_mg         = s_prefs.getShort("cal_z", 0);
    if (s_prefs.isKey("wssid"))        s_prefs.getString("wssid", s_cfg.wifi_ssid, CFG_WIFI_FIELD_LEN);
    if (s_prefs.isKey("wpass"))        s_prefs.getString("wpass", s_cfg.wifi_pass, CFG_WIFI_FIELD_LEN);
    if (s_prefs.isKey("wsta_en"))      s_cfg.wifi_sta_enabled = s_prefs.getBool("wsta_en", false);
    s_prefs.end();
}

void cfg_save(void)
{
    s_prefs.begin(NS, false);
    s_prefs.putUShort("mot_thr_mg",   s_cfg.motion_thresh_mg);
    s_prefs.putUShort("still_sec",    s_cfg.still_sec);
    s_prefs.putUShort("offbody_sec",  s_cfg.offbody_sec);
    s_prefs.putUShort("wvar_thr",     s_cfg.wear_var_thresh_mg);
    s_prefs.putUShort("wgrav_thr",    s_cfg.wear_grav_diff_thresh_mg);
    s_prefs.putUShort("step_thr_mg",  s_cfg.step_thresh_mg);
    s_prefs.putUShort("step_min",     s_cfg.step_min_ms);
    s_prefs.putUShort("step_max",     s_cfg.step_max_ms);
    s_prefs.putUChar("bpm_min",       s_cfg.bpm_min);
    s_prefs.putUChar("bpm_max",       s_cfg.bpm_max);
    s_prefs.putUChar("resp_win",      s_cfg.resp_window_sec);
    s_prefs.putShort("cal_x",         s_cfg.cal_x_mg);
    s_prefs.putShort("cal_y",         s_cfg.cal_y_mg);
    s_prefs.putShort("cal_z",         s_cfg.cal_z_mg);
    s_prefs.putString("wssid",        s_cfg.wifi_ssid);
    s_prefs.putString("wpass",        s_cfg.wifi_pass);
    s_prefs.putBool("wsta_en",        s_cfg.wifi_sta_enabled);
    s_prefs.end();
}

cfg_t *cfg_get(void) { return &s_cfg; }

void cfg_apply(void)
{
    accel_set_motion_thresh_mg(s_cfg.motion_thresh_mg);
    accel_set_offsets_mg(s_cfg.cal_x_mg, s_cfg.cal_y_mg, s_cfg.cal_z_mg);
    wear_set_timings(s_cfg.still_sec, s_cfg.offbody_sec);
    wear_set_var_thresh_mg(s_cfg.wear_var_thresh_mg);
    wear_set_grav_diff_thresh_mg(s_cfg.wear_grav_diff_thresh_mg);
    steps_set_thresh_mg(s_cfg.step_thresh_mg);
    steps_set_intervals_ms(s_cfg.step_min_ms, s_cfg.step_max_ms);
    resp_set_filter(s_cfg.bpm_min, s_cfg.bpm_max);
    resp_set_window_sec(s_cfg.resp_window_sec);
}
