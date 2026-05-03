#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "assistantd/audio_capture.h"
#include "assistantd/config.h"

int main(void) {
  assistantd_config_t config;
  assistantd_status_t status = assistantd_config_init_defaults(&config);
  assert(status == ASSISTANTD_OK);

  assert(assistantd_audio_capture_start(NULL, &config) == ASSISTANTD_ERR_INVALID_ARGUMENT);
  assert(assistantd_audio_capture_start(NULL, NULL) == ASSISTANTD_ERR_INVALID_ARGUMENT);

  assistantd_audio_capture_t capture;
  memset(&capture, 0, sizeof(capture));
  capture.child_pid = -1;
  capture.stdout_fd = -1;

  uint8_t byte = 0;
  size_t bytes_read = 0;
  assert(assistantd_audio_capture_read(NULL, &byte, 1, &bytes_read) == ASSISTANTD_ERR_INVALID_ARGUMENT);
  assert(assistantd_audio_capture_read(&capture, NULL, 1, &bytes_read) == ASSISTANTD_ERR_INVALID_ARGUMENT);
  assert(assistantd_audio_capture_read(&capture, &byte, 0, &bytes_read) == ASSISTANTD_ERR_INVALID_ARGUMENT);

  assert(assistantd_audio_capture_stop(NULL) == ASSISTANTD_ERR_INVALID_ARGUMENT);

  status = assistantd_audio_capture_start(&capture, &config);
  assert(status == ASSISTANTD_OK || status == ASSISTANTD_ERR_UNIMPLEMENTED);

  if (status == ASSISTANTD_OK) {
    uint8_t buffer[320];
    bytes_read = 0;
    assistantd_status_t read_status =
        assistantd_audio_capture_read(&capture, buffer, sizeof(buffer), &bytes_read);
    assert(read_status == ASSISTANTD_OK || read_status == ASSISTANTD_ERR_CHILD_PROCESS);
  }

  assert(assistantd_audio_capture_stop(&capture) == ASSISTANTD_OK);
  return 0;
}
