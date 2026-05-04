/** test_playback_shape.c
 * CS 341 - Spring 2026
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "assistantd/config.h"
#include "assistantd/playback.h"

int main(void) {
  assistantd_status_t status;
  assistantd_config_t config;

  // NULL guards.
  status = assistantd_playback_play_wav(NULL, "/tmp/test.wav");
  assert(status == ASSISTANTD_ERR_INVALID_ARGUMENT);

  assistantd_config_init_defaults(&config);
  status = assistantd_playback_play_wav(&config, NULL);
  assert(status == ASSISTANTD_ERR_INVALID_ARGUMENT);

  // Non-existent WAV file: aplay exits non-zero (or exec fails on macOS).
  // Both paths surface as ASSISTANTD_ERR_CHILD_PROCESS without blocking for the
  // full 30-second timeout, since aplay/exec fails immediately.
  assistantd_config_init_defaults(&config);
  snprintf(config.audio_playback_device, sizeof(config.audio_playback_device), "default");
  status = assistantd_playback_play_wav(&config,
                                        "/tmp/assistantd_test_nonexistent_audio.wav");
  assert(status == ASSISTANTD_ERR_CHILD_PROCESS);

  // Non-existent device with a non-existent file: same expected result.
  snprintf(config.audio_playback_device, sizeof(config.audio_playback_device),
           "assistantd_test_no_device");
  status = assistantd_playback_play_wav(&config,
                                        "/tmp/assistantd_test_nonexistent_audio.wav");
  assert(status == ASSISTANTD_ERR_CHILD_PROCESS);

  return 0;
}
