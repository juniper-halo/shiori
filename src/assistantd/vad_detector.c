#include "assistantd/vad_detector.h"

#include "assistantd/logger.h"

/**
 * @todo Implementation Playbook
 * @brief Integrate WebRTC VAD (`libfvad`) for end-of-speech segmentation.
 * @ownership Speech segmentation and interaction triggering.
 * @inputs
 *   - Fixed-size PCM frames (10/20/30ms).
 *   - Configured VAD aggressiveness and silence timeout.
 * @outputs
 *   - Event stream: speech start, continue, and end.
 * @state
 *   - Idle and speaking states with monotonic silence accumulation.
 * @concurrency
 *   - Detector state is owned by one pipeline worker to avoid locking.
 * @errors
 *   - Handle invalid frame duration and non-PCM frame sizes as immediate failures.
 * @child_process_contracts
 *   - Downstream STT invoked only on `SPEECH_END` with finalized utterance buffer.
 * @acceptance
 *   - Robust segmentation in noisy room baseline tests.
 *   - No duplicate speech-end events per utterance.
 */

assistantd_status_t assistantd_vad_init(
    assistantd_vad_detector_t *detector,
    const assistantd_config_t *config) {
  if (detector == NULL || config == NULL) {
    return ASSISTANTD_ERR_INVALID_ARGUMENT;
  }

  detector->aggressiveness = config->vad_aggressiveness;
  detector->silence_ms = config->vad_silence_ms;
  detector->accumulated_silence_ms = 0;
  detector->speech_active = 0;
  return ASSISTANTD_OK;
}

assistantd_status_t assistantd_vad_process_frame(
    assistantd_vad_detector_t *detector,
    const int16_t *pcm,
    size_t sample_count,
    int frame_duration_ms,
    assistantd_vad_event_t *event) {
  (void)detector;
  (void)pcm;
  (void)sample_count;
  (void)frame_duration_ms;
  if (event != NULL) {
    *event = ASSISTANTD_VAD_EVENT_NONE;
  }

  assistantd_log(ASSISTANTD_LOG_WARN, "vad scaffold: process_frame() is not implemented");
  return ASSISTANTD_ERR_UNIMPLEMENTED;
}

void assistantd_vad_reset(assistantd_vad_detector_t *detector) {
  if (detector == NULL) {
    return;
  }

  detector->accumulated_silence_ms = 0;
  detector->speech_active = 0;
}
