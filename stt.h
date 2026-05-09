#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef enum {
    STT_IDLE    = 0,
    STT_RUNNING = 1,
    STT_DONE    = 2,
    STT_ERROR   = 3,
} stt_state_t;

void          stt_init(void);

bool          stt_set_key(const char *key);     /* empty/NULL clears */
bool          stt_has_key(void);
const char   *stt_key_hint(void);                /* "set (••••XXXX)" or "not set" */

bool          stt_set_model(const char *model);
const char   *stt_get_model(void);

/* Blocking POST of /last.wav to Deepgram. Returns 0 on success.
 * Result reachable via stt_last_*() accessors. */
int           stt_transcribe_last_wav(void);

stt_state_t   stt_state(void);
const char   *stt_last_transcript(void);
const char   *stt_last_err(void);
int           stt_last_http_status(void);
int           stt_last_rc(void);
uint32_t      stt_last_duration_ms(void);
size_t        stt_last_wav_bytes(void);
