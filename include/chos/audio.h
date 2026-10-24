#ifndef CHOS_AUDIO_H
#define CHOS_AUDIO_H

typedef enum {
    CHOS_AUDIO_CHIME_IDLE = 0,
    CHOS_AUDIO_CHIME_PENDING = 1,
    CHOS_AUDIO_CHIME_PLAYED = 2,
    CHOS_AUDIO_CHIME_FAILED = 3
} chos_audio_chime_state_t;

int chos_audio_init(void);
int chos_audio_available(void);
int chos_audio_play_startup_chime(void);
chos_audio_chime_state_t chos_audio_startup_chime_state(void);
void chos_audio_pump(void);
const char *chos_audio_last_error(void);

#endif
