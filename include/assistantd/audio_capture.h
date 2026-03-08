#ifndef ASSISTANTD_AUDIO_CAPTURE_H
#define ASSISTANTD_AUDIO_CAPTURE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "assistantd/config.h"
#include "assistantd/status.h"

typedef struct {
  pid_t child_pid;
  int stdout_fd;
  bool active;
} assistantd_audio_capture_t;

assistantd_status_t assistantd_audio_capture_start(
    assistantd_audio_capture_t *capture,
    const assistantd_config_t *config);
assistantd_status_t assistantd_audio_capture_read(
    assistantd_audio_capture_t *capture,
    uint8_t *buffer,
    size_t capacity,
    size_t *bytes_read);
assistantd_status_t assistantd_audio_capture_stop(assistantd_audio_capture_t *capture);

#endif  // ASSISTANTD_AUDIO_CAPTURE_H
