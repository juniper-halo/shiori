#include "assistantd/vad_detector.h"

#include <stddef.h>
#include <stdint.h>

#include <fvad.h>

#include "assistantd/utilities/logger.h"

#define ASSISTANTD_VAD_SAMPLE_RATE_HZ 16000

/**
 * @todo Implementation Playbook
 * @brief Implement WebRTC VAD-backed frame-level state machine for end-of-speech segmentation.
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
 *   - Deterministic start/continue/end event transitions for valid frame cadence.
 *   - No duplicate speech-end events per utterance.
 * @remaining
 *   - Acoustic threshold tuning and environment-specific calibration on target hardware.
 */

static int assistantd_vad_is_valid_frame_duration(int frame_duration_ms) {
  return frame_duration_ms == 10 || frame_duration_ms == 20 || frame_duration_ms == 30;
}

static size_t assistantd_vad_expected_sample_count(int frame_duration_ms) {
  return (size_t)((ASSISTANTD_VAD_SAMPLE_RATE_HZ / 1000) * frame_duration_ms);
}

assistantd_status_t assistantd_vad_init(
    assistantd_vad_detector_t *detector,
    const assistantd_config_t *config) {
  if (detector == NULL || config == NULL) {
    return ASSISTANTD_ERR_INVALID_ARGUMENT;
  }
  if (config->vad_aggressiveness < 0 || config->vad_aggressiveness > 3) {
    return ASSISTANTD_ERR_CONFIG;
  }
  if (config->vad_silence_ms <= 0) {
    return ASSISTANTD_ERR_CONFIG;
  }

  detector->aggressiveness = config->vad_aggressiveness;
  detector->silence_ms = config->vad_silence_ms;
  detector->accumulated_silence_ms = 0;
  detector->speech_active = 0;

  Fvad *vad = fvad_new();
  if (vad == NULL) {
    return ASSISTANTD_ERR_IO;
  }
  if (fvad_set_mode(vad, config->vad_aggressiveness) != 0) {
    fvad_free(vad);
    return ASSISTANTD_ERR_CONFIG;
  }
  if (fvad_set_sample_rate(vad, ASSISTANTD_VAD_SAMPLE_RATE_HZ) != 0) {
    fvad_free(vad);
    return ASSISTANTD_ERR_CONFIG;
  }

  detector->backend = (void *)vad;
  return ASSISTANTD_OK;
}

assistantd_status_t assistantd_vad_process_frame(
    assistantd_vad_detector_t *detector,
    const int16_t *pcm,
    size_t sample_count,
    int frame_duration_ms,
    assistantd_vad_event_t *event) {
  if (detector == NULL || pcm == NULL || event == NULL || sample_count == 0) {
    return ASSISTANTD_ERR_INVALID_ARGUMENT;
  }
  if (!assistantd_vad_is_valid_frame_duration(frame_duration_ms)) {
    return ASSISTANTD_ERR_INVALID_ARGUMENT;
  }

  size_t expected_samples = assistantd_vad_expected_sample_count(frame_duration_ms);
  if (sample_count != expected_samples) {
    assistantd_log(ASSISTANTD_LOG_WARN,
                   "vad: invalid sample count for %dms frame (expected=%zu got=%zu)",
                   frame_duration_ms,
                   expected_samples,
                   sample_count);
    return ASSISTANTD_ERR_INVALID_ARGUMENT;
  }

  if (detector->backend == NULL) {
    return ASSISTANTD_ERR_INVALID_ARGUMENT;
  }

  Fvad *vad = (Fvad *)detector->backend;
  int speech_result = fvad_process(vad, pcm, sample_count);
  if (speech_result < 0) {
    return ASSISTANTD_ERR_IO;
  }
  int frame_has_speech = (speech_result == 1) ? 1 : 0;

  *event = ASSISTANTD_VAD_EVENT_NONE;
  if (!detector->speech_active) {
    if (frame_has_speech) {
      detector->speech_active = 1;
      detector->accumulated_silence_ms = 0;
      *event = ASSISTANTD_VAD_EVENT_SPEECH_START;
    }
    return ASSISTANTD_OK;
  }

  if (frame_has_speech) {
    detector->accumulated_silence_ms = 0;
    *event = ASSISTANTD_VAD_EVENT_SPEECH_CONTINUE;
    return ASSISTANTD_OK;
  }

  detector->accumulated_silence_ms += frame_duration_ms;
  if (detector->accumulated_silence_ms >= detector->silence_ms) {
    detector->speech_active = 0;
    detector->accumulated_silence_ms = 0;
    *event = ASSISTANTD_VAD_EVENT_SPEECH_END;
  } else {
    *event = ASSISTANTD_VAD_EVENT_SPEECH_CONTINUE;
  }

  return ASSISTANTD_OK;
}

assistantd_status_t assistantd_vad_shutdown(assistantd_vad_detector_t *detector) {
  if (detector == NULL) {
    return ASSISTANTD_ERR_INVALID_ARGUMENT;
  }

  if (detector->backend != NULL) {
    fvad_free((Fvad *)detector->backend);
    detector->backend = NULL;
  }
  detector->accumulated_silence_ms = 0;
  detector->speech_active = 0;
  return ASSISTANTD_OK;
}

void assistantd_vad_reset(assistantd_vad_detector_t *detector) {
  if (detector == NULL) {
    return;
  }

  if (detector->backend != NULL) {
    Fvad *vad = (Fvad *)detector->backend;
    fvad_reset(vad);
    if (fvad_set_mode(vad, detector->aggressiveness) != 0) {
      assistantd_log(ASSISTANTD_LOG_WARN, "vad: failed to restore aggressiveness on reset");
    }
    if (fvad_set_sample_rate(vad, ASSISTANTD_VAD_SAMPLE_RATE_HZ) != 0) {
      assistantd_log(ASSISTANTD_LOG_WARN, "vad: failed to restore sample rate on reset");
    }
  }
  detector->accumulated_silence_ms = 0;
  detector->speech_active = 0;
}
