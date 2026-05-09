#include "config.h"
#include "app_config.h"
#include "wear.h"
#include "accel.h"
#include "steps.h"
#include "respiration.h"
#include "mode.h"
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
    s_cfg.step_thresh_mg       = STEP_PEAK_THRESH_MG;
    s_cfg.step_min_ms          = STEP_MIN_INTERVAL_MS;
    s_cfg.step_max_ms          = STEP_MAX_INTERVAL_MS;
    s_cfg.step_adaptive        = false;
    s_cfg.step_bandpass        = false;
    s_cfg.step_axis            = STEP_AXIS_DEFAULT;
    s_cfg.step_amp_window_ms   = STEP_AMP_WINDOW_MS;
    s_cfg.bpm_min                = RESP_BPM_MIN;
    s_cfg.bpm_max                = RESP_BPM_MAX;
    s_cfg.resp_window_sec        = RESP_WINDOW_SEC;
    s_cfg.resp_min_interval_ms   = RESP_MIN_INTERVAL_MS;
    s_cfg.resp_iir_alpha_q15     = RESP_IIR_ALPHA_Q15;
    s_cfg.resp_min_amplitude_mg  = RESP_MIN_AMPLITUDE_MG;
    s_cfg.resp_axis              = RESP_AXIS_DEFAULT;
    s_cfg.resp_median_len           = RESP_MEDIAN_LEN_DEFAULT;
    s_cfg.resp_bp_low_hz_x10        = RESP_BP_LOW_HZ_X10_DEFAULT;
    s_cfg.resp_bp_high_hz_x10       = RESP_BP_HIGH_HZ_X10_DEFAULT;
    s_cfg.resp_hysteresis_mg        = RESP_HYSTERESIS_MG_DEFAULT;
    s_cfg.resp_adaptive_bp          = RESP_ADAPTIVE_BP_DEFAULT;
    s_cfg.resp_autocorr_window_sec  = RESP_AUTOCORR_WIN_SEC_DEF;
    s_cfg.cal_x_mg         = 0;
    s_cfg.cal_y_mg         = 0;
    s_cfg.cal_z_mg         = 0;
    memset(s_cfg.wifi_ssid, 0, CFG_WIFI_FIELD_LEN);
    memset(s_cfg.wifi_pass, 0, CFG_WIFI_FIELD_LEN);
    s_cfg.wifi_sta_enabled = false;
    s_cfg.mode             = APP_MODE_AUTO;
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
    if (s_prefs.isKey("s_adapt"))      s_cfg.step_adaptive       = s_prefs.getBool("s_adapt",   s_cfg.step_adaptive);
    if (s_prefs.isKey("s_bp"))         s_cfg.step_bandpass       = s_prefs.getBool("s_bp",      s_cfg.step_bandpass);
    if (s_prefs.isKey("s_axis"))       s_cfg.step_axis           = s_prefs.getUChar("s_axis",   s_cfg.step_axis);
    if (s_prefs.isKey("s_amp_ms"))     s_cfg.step_amp_window_ms  = s_prefs.getUShort("s_amp_ms", s_cfg.step_amp_window_ms);
    if (s_prefs.isKey("bpm_min"))      s_cfg.bpm_min          = s_prefs.getUChar("bpm_min",       s_cfg.bpm_min);
    if (s_prefs.isKey("bpm_max"))      s_cfg.bpm_max          = s_prefs.getUChar("bpm_max",       s_cfg.bpm_max);
    if (s_prefs.isKey("resp_win"))     s_cfg.resp_window_sec  = s_prefs.getUChar("resp_win",      s_cfg.resp_window_sec);
    if (s_prefs.isKey("rmin_ms"))      s_cfg.resp_min_interval_ms = s_prefs.getUShort("rmin_ms",  s_cfg.resp_min_interval_ms);
    if (s_prefs.isKey("riir_a"))       s_cfg.resp_iir_alpha_q15   = s_prefs.getUShort("riir_a",   s_cfg.resp_iir_alpha_q15);
    if (s_prefs.isKey("rminamp"))      s_cfg.resp_min_amplitude_mg = s_prefs.getShort("rminamp",  s_cfg.resp_min_amplitude_mg);
    if (s_prefs.isKey("raxis"))        s_cfg.resp_axis            = s_prefs.getUChar("raxis",     s_cfg.resp_axis);
    if (s_prefs.isKey("r_med"))        s_cfg.resp_median_len      = s_prefs.getUChar("r_med",     s_cfg.resp_median_len);
    if (s_prefs.isKey("r_bp_lo"))      s_cfg.resp_bp_low_hz_x10   = s_prefs.getUChar("r_bp_lo",   s_cfg.resp_bp_low_hz_x10);
    if (s_prefs.isKey("r_bp_hi"))      s_cfg.resp_bp_high_hz_x10  = s_prefs.getUChar("r_bp_hi",   s_cfg.resp_bp_high_hz_x10);
    if (s_prefs.isKey("r_hyst"))       s_cfg.resp_hysteresis_mg   = s_prefs.getUChar("r_hyst",    s_cfg.resp_hysteresis_mg);
    if (s_prefs.isKey("r_adp_bp"))     s_cfg.resp_adaptive_bp     = s_prefs.getBool("r_adp_bp",   s_cfg.resp_adaptive_bp);
    if (s_prefs.isKey("r_ac_win"))     s_cfg.resp_autocorr_window_sec = s_prefs.getUChar("r_ac_win", s_cfg.resp_autocorr_window_sec);
    if (s_prefs.isKey("cal_x"))        s_cfg.cal_x_mg         = s_prefs.getShort("cal_x", 0);
    if (s_prefs.isKey("cal_y"))        s_cfg.cal_y_mg         = s_prefs.getShort("cal_y", 0);
    if (s_prefs.isKey("cal_z"))        s_cfg.cal_z_mg         = s_prefs.getShort("cal_z", 0);
    if (s_prefs.isKey("wssid"))        s_prefs.getString("wssid", s_cfg.wifi_ssid, CFG_WIFI_FIELD_LEN);
    if (s_prefs.isKey("wpass"))        s_prefs.getString("wpass", s_cfg.wifi_pass, CFG_WIFI_FIELD_LEN);
    if (s_prefs.isKey("wsta_en"))      s_cfg.wifi_sta_enabled = s_prefs.getBool("wsta_en", false);
    if (s_prefs.isKey("app_mode"))     s_cfg.mode             = s_prefs.getUChar("app_mode", APP_MODE_AUTO);
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
    s_prefs.putBool("s_adapt",        s_cfg.step_adaptive);
    s_prefs.putBool("s_bp",           s_cfg.step_bandpass);
    s_prefs.putUChar("s_axis",        s_cfg.step_axis);
    s_prefs.putUShort("s_amp_ms",     s_cfg.step_amp_window_ms);
    s_prefs.putUChar("bpm_min",       s_cfg.bpm_min);
    s_prefs.putUChar("bpm_max",       s_cfg.bpm_max);
    s_prefs.putUChar("resp_win",      s_cfg.resp_window_sec);
    s_prefs.putUShort("rmin_ms",      s_cfg.resp_min_interval_ms);
    s_prefs.putUShort("riir_a",       s_cfg.resp_iir_alpha_q15);
    s_prefs.putShort("rminamp",       s_cfg.resp_min_amplitude_mg);
    s_prefs.putUChar("raxis",         s_cfg.resp_axis);
    s_prefs.putUChar("r_med",         s_cfg.resp_median_len);
    s_prefs.putUChar("r_bp_lo",       s_cfg.resp_bp_low_hz_x10);
    s_prefs.putUChar("r_bp_hi",       s_cfg.resp_bp_high_hz_x10);
    s_prefs.putUChar("r_hyst",        s_cfg.resp_hysteresis_mg);
    s_prefs.putBool("r_adp_bp",       s_cfg.resp_adaptive_bp);
    s_prefs.putUChar("r_ac_win",      s_cfg.resp_autocorr_window_sec);
    s_prefs.putShort("cal_x",         s_cfg.cal_x_mg);
    s_prefs.putShort("cal_y",         s_cfg.cal_y_mg);
    s_prefs.putShort("cal_z",         s_cfg.cal_z_mg);
    s_prefs.putString("wssid",        s_cfg.wifi_ssid);
    s_prefs.putString("wpass",        s_cfg.wifi_pass);
    s_prefs.putBool("wsta_en",        s_cfg.wifi_sta_enabled);
    s_prefs.putUChar("app_mode",      s_cfg.mode);
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
    steps_set_adaptive(s_cfg.step_adaptive);
    steps_set_bandpass(s_cfg.step_bandpass);
    steps_set_axis((step_axis_t)s_cfg.step_axis);
    steps_set_amp_window_ms(s_cfg.step_amp_window_ms);
    resp_set_filter(s_cfg.bpm_min, s_cfg.bpm_max);
    resp_set_window_sec(s_cfg.resp_window_sec);
    resp_set_min_interval_ms(s_cfg.resp_min_interval_ms);
    resp_set_iir_alpha_q15(s_cfg.resp_iir_alpha_q15);
    resp_set_min_amplitude_mg(s_cfg.resp_min_amplitude_mg);
    resp_set_axis((resp_axis_t)s_cfg.resp_axis);
    resp_set_median_len(s_cfg.resp_median_len);
    resp_set_bp_cutoffs(s_cfg.resp_bp_low_hz_x10, s_cfg.resp_bp_high_hz_x10);
    resp_set_hysteresis_mg(s_cfg.resp_hysteresis_mg);
    resp_set_adaptive_bp(s_cfg.resp_adaptive_bp);
    resp_set_autocorr_window_sec(s_cfg.resp_autocorr_window_sec);
    if (s_cfg.mode > APP_MODE_AUTO) s_cfg.mode = APP_MODE_AUTO;
    mode_set((app_mode_t)s_cfg.mode);
}
