#include "assistantd/audio_capture.h"

#include "assistantd/logger.h"

/**
 * @todo Implementation Playbook
 * @brief Implement managed capture process around `arecord` streaming raw PCM frames.
 * @ownership Audio ingestion and process supervision.
 * @inputs
 *   - Capture settings from config: device, sample rate, channel count, frame size.
 * @outputs
 *   - Framed PCM bytes to ring buffer writer.
 * @state
 *   - INACTIVE -> STARTING -> ACTIVE -> STOPPING -> INACTIVE.
 * @concurrency
 *   - One reader thread pulls child stdout; lifecycle controlled by supervisor thread.
 * @errors
 *   - Detect early child exit, broken pipe, and short reads.
 *   - Fail fast on spawn failure and report command-line used.
 * @child_process_contracts
 *   - Child must emit 16-bit mono PCM at agreed sample rate.
 *   - Stop path sends SIGTERM then SIGKILL timeout fallback.
 * @acceptance
 *   - Stream starts/stops cleanly with no zombie processes.
 *   - Supervisor can recover from child crash deterministically.
 */

assistantd_status_t assistantd_audio_capture_start(
    assistantd_audio_capture_t *capture,
    const assistantd_config_t *config) {
  (void)config;
  if (capture == NULL) {
    return ASSISTANTD_ERR_INVALID_ARGUMENT;
  }

  capture->active = false;
  capture->child_pid = -1;
  capture->stdout_fd = -1;
  assistantd_log(ASSISTANTD_LOG_WARN, "audio capture scaffold: start() is not implemented");
  return ASSISTANTD_ERR_UNIMPLEMENTED;
}

assistantd_status_t assistantd_audio_capture_read(
    assistantd_audio_capture_t *capture,
    uint8_t *buffer,
    size_t capacity,
    size_t *bytes_read) {
  (void)capture;
  (void)buffer;
  (void)capacity;
  if (bytes_read != NULL) {
    *bytes_read = 0;
  }
  return ASSISTANTD_ERR_UNIMPLEMENTED;
}

assistantd_status_t assistantd_audio_capture_stop(assistantd_audio_capture_t *capture) {
  if (capture == NULL) {
    return ASSISTANTD_ERR_INVALID_ARGUMENT;
  }

  capture->active = false;
  return ASSISTANTD_ERR_UNIMPLEMENTED;
}
