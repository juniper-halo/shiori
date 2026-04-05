#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "assistantd/config.h"
#include "assistantd/vad_detector.h"

static void fill_frame(int16_t *frame, size_t count, int16_t value) {
  for (size_t i = 0; i < count; i++) {
    frame[i] = value;
  }
}

static void test_init_validation(void) {
  assistantd_config_t config;
  assert(assistantd_config_init_defaults(&config) == ASSISTANTD_OK);

  assistantd_vad_detector_t detector;
  detector.backend = NULL;
  assert(assistantd_vad_init(NULL, &config) == ASSISTANTD_ERR_INVALID_ARGUMENT);
  assert(assistantd_vad_init(&detector, NULL) == ASSISTANTD_ERR_INVALID_ARGUMENT);

  config.vad_aggressiveness = 10;
  assert(assistantd_vad_init(&detector, &config) == ASSISTANTD_ERR_CONFIG);

  assert(assistantd_config_init_defaults(&config) == ASSISTANTD_OK);
  config.vad_silence_ms = 0;
  assert(assistantd_vad_init(&detector, &config) == ASSISTANTD_ERR_CONFIG);
}

static void test_argument_validation(void) {
  assistantd_config_t config;
  assert(assistantd_config_init_defaults(&config) == ASSISTANTD_OK);
  config.vad_silence_ms = 40;

  assistantd_vad_detector_t detector;
  assert(assistantd_vad_init(&detector, &config) == ASSISTANTD_OK);
  assert(detector.backend != NULL);

  int16_t frame[320];
  fill_frame(frame, 320, 2000);
  assistantd_vad_event_t event = ASSISTANTD_VAD_EVENT_NONE;

  assert(assistantd_vad_process_frame(NULL, frame, 320, 20, &event) == ASSISTANTD_ERR_INVALID_ARGUMENT);
  assert(assistantd_vad_process_frame(&detector, NULL, 320, 20, &event) == ASSISTANTD_ERR_INVALID_ARGUMENT);
  assert(assistantd_vad_process_frame(&detector, frame, 320, 20, NULL) == ASSISTANTD_ERR_INVALID_ARGUMENT);
  assert(assistantd_vad_process_frame(&detector, frame, 0, 20, &event) == ASSISTANTD_ERR_INVALID_ARGUMENT);
  assert(assistantd_vad_process_frame(&detector, frame, 320, 15, &event) == ASSISTANTD_ERR_INVALID_ARGUMENT);
  assert(assistantd_vad_process_frame(&detector, frame, 319, 20, &event) == ASSISTANTD_ERR_INVALID_ARGUMENT);
  assert(assistantd_vad_shutdown(&detector) == ASSISTANTD_OK);
  assert(detector.backend == NULL);
}

static void test_state_transitions(void) {
  assistantd_config_t config;
  assert(assistantd_config_init_defaults(&config) == ASSISTANTD_OK);
  config.vad_silence_ms = 40;
  config.vad_aggressiveness = 2;

  assistantd_vad_detector_t detector;
  assert(assistantd_vad_init(&detector, &config) == ASSISTANTD_OK);
  assert(detector.backend != NULL);

  int16_t silence[320];
  fill_frame(silence, 320, 0);

  assistantd_vad_event_t event = ASSISTANTD_VAD_EVENT_NONE;

  assert(assistantd_vad_process_frame(&detector, silence, 320, 20, &event) == ASSISTANTD_OK);
  assert(event == ASSISTANTD_VAD_EVENT_NONE);

  detector.speech_active = 1;
  detector.accumulated_silence_ms = 0;

  assert(assistantd_vad_process_frame(&detector, silence, 320, 20, &event) == ASSISTANTD_OK);
  assert(event == ASSISTANTD_VAD_EVENT_SPEECH_CONTINUE);
  assert(detector.speech_active == 1);
  assert(detector.accumulated_silence_ms == 20);

  assert(assistantd_vad_process_frame(&detector, silence, 320, 20, &event) == ASSISTANTD_OK);
  assert(event == ASSISTANTD_VAD_EVENT_SPEECH_END);
  assert(detector.speech_active == 0);
  assert(detector.accumulated_silence_ms == 0);

  assert(assistantd_vad_process_frame(&detector, silence, 320, 20, &event) == ASSISTANTD_OK);
  assert(event == ASSISTANTD_VAD_EVENT_NONE);
  assert(assistantd_vad_shutdown(&detector) == ASSISTANTD_OK);
  assert(detector.backend == NULL);
}

static void test_reset_behavior(void) {
  assistantd_config_t config;
  assert(assistantd_config_init_defaults(&config) == ASSISTANTD_OK);
  config.vad_silence_ms = 40;

  assistantd_vad_detector_t detector;
  assert(assistantd_vad_init(&detector, &config) == ASSISTANTD_OK);
  assert(detector.backend != NULL);

  int16_t silence[320];
  fill_frame(silence, 320, 0);

  assistantd_vad_event_t event = ASSISTANTD_VAD_EVENT_NONE;
  detector.speech_active = 1;
  detector.accumulated_silence_ms = 20;

  assistantd_vad_reset(&detector);
  assert(detector.speech_active == 0);
  assert(detector.accumulated_silence_ms == 0);

  assert(assistantd_vad_process_frame(&detector, silence, 320, 20, &event) == ASSISTANTD_OK);
  assert(event == ASSISTANTD_VAD_EVENT_NONE);
  assert(assistantd_vad_shutdown(&detector) == ASSISTANTD_OK);
  assert(detector.backend == NULL);
}

static void test_shutdown_validation(void) {
  assert(assistantd_vad_shutdown(NULL) == ASSISTANTD_ERR_INVALID_ARGUMENT);
}

int main(void) {
  test_init_validation();
  test_argument_validation();
  test_state_transitions();
  test_reset_behavior();
  test_shutdown_validation();
  return 0;
}
