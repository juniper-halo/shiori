#ifndef ASSISTANTD_PLAYBACK_H
#define ASSISTANTD_PLAYBACK_H

#include "assistantd/config.h"
#include "assistantd/status.h"

assistantd_status_t assistantd_playback_play_wav(
    const assistantd_config_t *config,
    const char *wav_path);

#endif  // ASSISTANTD_PLAYBACK_H
