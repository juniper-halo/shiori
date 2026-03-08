#ifndef ASSISTANTD_VAD_DETECTOR_H
#define ASSISTANTD_VAD_DETECTOR_H

#include <stddef.h>
#include <stdint.h>

#include "assistantd/config.h"
#include "assistantd/status.h"

typedef enum {
  ASSISTANTD_VAD_EVENT_NONE = 0,
  ASSISTANTD_VAD_EVENT_SPEECH_START = 1,
  ASSISTANTD_VAD_EVENT_SPEECH_CONTINUE = 2,
  ASSISTANTD_VAD_EVENT_SPEECH_END = 3
} assistantd_vad_event_t;

typedef struct {
  int aggressiveness;
  int silence_ms;
  int accumulated_silence_ms;
  int speech_active;
} assistantd_vad_detector_t;

assistantd_status_t assistantd_vad_init(
    assistantd_vad_detector_t *detector,
    const assistantd_config_t *config);
assistantd_status_t assistantd_vad_process_frame(
    assistantd_vad_detector_t *detector,
    const int16_t *pcm,
    size_t sample_count,
    int frame_duration_ms,
    assistantd_vad_event_t *event);
void assistantd_vad_reset(assistantd_vad_detector_t *detector);

#endif  // ASSISTANTD_VAD_DETECTOR_H
