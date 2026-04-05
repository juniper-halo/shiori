#ifndef ASSISTANTD_AUDIO_CAPTURE_H
#define ASSISTANTD_AUDIO_CAPTURE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "assistantd/config.h"
#include "assistantd/status.h"

#define ASSISTANTD_AUDIO_CAPTURE_FRAME_BYTES 640

typedef enum {
  ASSISTANTD_AUDIO_CAPTURE_BACKEND_ARECORD = 0,
  ASSISTANTD_AUDIO_CAPTURE_BACKEND_NETWORK_TCP = 1
} assistantd_audio_capture_backend_t;

typedef struct {
  assistantd_audio_capture_backend_t backend;
  pid_t child_pid;
  int stdout_fd;
  int listener_fd;
  int client_fd;
  uint8_t frame_staging[ASSISTANTD_AUDIO_CAPTURE_FRAME_BYTES];
  size_t frame_staging_size;
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
