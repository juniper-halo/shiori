/** test_audio_capture_shape.c
 * CS 341 - Spring 2026
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "assistantd/audio_capture.h"
#include "assistantd/config.h"

int main(void) {
  assistantd_status_t status;
  assistantd_config_t config;
  assistantd_audio_capture_t capture;

  // NULL guards: start()
  status = assistantd_audio_capture_start(NULL, NULL);
  assert(status == ASSISTANTD_ERR_INVALID_ARGUMENT);

  assistantd_config_init_defaults(&config);
  status = assistantd_audio_capture_start(NULL, &config);
  assert(status == ASSISTANTD_ERR_INVALID_ARGUMENT);

  memset(&capture, 0, sizeof(capture));
  status = assistantd_audio_capture_start(&capture, NULL);
  assert(status == ASSISTANTD_ERR_INVALID_ARGUMENT);

  // NULL guards: read()
  {
    uint8_t buf[16];
    size_t n = 0;

    status = assistantd_audio_capture_read(NULL, buf, sizeof(buf), &n);
    assert(status == ASSISTANTD_ERR_INVALID_ARGUMENT);

    memset(&capture, 0, sizeof(capture));
    status = assistantd_audio_capture_read(&capture, NULL, sizeof(buf), &n);
    assert(status == ASSISTANTD_ERR_INVALID_ARGUMENT);

    status = assistantd_audio_capture_read(&capture, buf, 0, &n);
    assert(status == ASSISTANTD_ERR_INVALID_ARGUMENT);

    status = assistantd_audio_capture_read(&capture, buf, sizeof(buf), NULL);
    assert(status == ASSISTANTD_ERR_INVALID_ARGUMENT);
  }

  // NULL guard: stop()
  status = assistantd_audio_capture_stop(NULL);
  assert(status == ASSISTANTD_ERR_INVALID_ARGUMENT);

  // stop() on an inactive capture is idempotent.
  memset(&capture, 0, sizeof(capture));
  status = assistantd_audio_capture_stop(&capture);
  assert(status == ASSISTANTD_OK);

  // read() on an inactive capture returns IO error.
  {
    uint8_t buf[16];
    size_t n = 0;
    memset(&capture, 0, sizeof(capture));
    status = assistantd_audio_capture_read(&capture, buf, sizeof(buf), &n);
    assert(status == ASSISTANTD_ERR_IO);
  }

  // Process lifecycle: start() with an invalid device so arecord exits immediately
  // on all platforms (no arecord binary on macOS; invalid device on Linux CI).
  // This exercises fork/exec/pipe teardown without real audio hardware.
  {
    uint8_t buf[4096];
    size_t n = 0;

    memset(&capture, 0, sizeof(capture));
    assistantd_config_init_defaults(&config);
    snprintf(config.audio_capture_device, sizeof(config.audio_capture_device),
             "assistantd_test_no_device");

    status = assistantd_audio_capture_start(&capture, &config);
    assert(status == ASSISTANTD_OK);
    assert(capture.active == true);
    assert(capture.child_pid > 0);
    assert(capture.stdout_fd >= 0);

    // The child exits because arecord is absent or the device does not exist.
    // read() must return ASSISTANTD_ERR_CHILD_PROCESS (EOF) or ASSISTANTD_OK with data
    // on the unlikely path where arecord exists and manages to write something.
    status = assistantd_audio_capture_read(&capture, buf, sizeof(buf), &n);
    assert(status == ASSISTANTD_ERR_CHILD_PROCESS || status == ASSISTANTD_OK);

    // stop() reaps the child and resets all fields.
    status = assistantd_audio_capture_stop(&capture);
    assert(status == ASSISTANTD_OK);
    assert(capture.active == false);
    assert(capture.child_pid == -1);
    assert(capture.stdout_fd == -1);

    // Second stop() is idempotent.
    status = assistantd_audio_capture_stop(&capture);
    assert(status == ASSISTANTD_OK);
  }

  // start() on an already-active capture is rejected.
  {
    memset(&capture, 0, sizeof(capture));
    assistantd_config_init_defaults(&config);
    snprintf(config.audio_capture_device, sizeof(config.audio_capture_device),
             "assistantd_test_no_device");

    status = assistantd_audio_capture_start(&capture, &config);
    assert(status == ASSISTANTD_OK);

    status = assistantd_audio_capture_start(&capture, &config);
    assert(status == ASSISTANTD_ERR_INVALID_ARGUMENT);

    assistantd_audio_capture_stop(&capture);
  }

  return 0;
}
