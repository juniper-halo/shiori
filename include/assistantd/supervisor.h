#ifndef ASSISTANTD_SUPERVISOR_H
#define ASSISTANTD_SUPERVISOR_H

#include <stddef.h>
#include <stdint.h>

#include "assistantd/audio_capture.h"
#include "assistantd/config.h"
#include "assistantd/llm_adapter.h"
#include "assistantd/ring_buffer.h"
#include "assistantd/status.h"
#include "assistantd/stt_adapter.h"
#include "assistantd/tts_adapter.h"
#include "assistantd/utilities/artifact_queue.h"
#include "assistantd/vad_detector.h"

typedef enum {
  ASSISTANTD_SUPERVISOR_INIT = 0,
  ASSISTANTD_SUPERVISOR_READY = 1,
  ASSISTANTD_SUPERVISOR_RUNNING = 2,
  ASSISTANTD_SUPERVISOR_STOPPING = 3,
  ASSISTANTD_SUPERVISOR_STOPPED = 4
} assistantd_supervisor_state_t;

typedef struct {
  uint8_t *pcm;
  size_t size;
  size_t capacity;
} assistantd_utterance_metadata_t;

typedef struct {
  assistantd_supervisor_state_t state;
  const assistantd_config_t *config;
  assistantd_audio_capture_t capture;
  assistantd_ring_buffer_t capture_ring;
  assistantd_vad_detector_t vad;
  assistantd_stt_adapter_t stt;
  assistantd_llm_adapter_t llm;
  assistantd_tts_adapter_t tts;
  assistantd_utterance_metadata_t utterance_metadata;
  assistantd_artifact_queue_t artifact_queue;
  assistantd_llm_response_queue_t llm_response_queue;
  uint64_t next_artifact_sequence_id;
} assistantd_supervisor_t;

assistantd_status_t assistantd_supervisor_init(
    assistantd_supervisor_t *supervisor,
    const assistantd_config_t *config);
assistantd_status_t assistantd_supervisor_start(assistantd_supervisor_t *supervisor);
assistantd_status_t assistantd_supervisor_run_once(assistantd_supervisor_t *supervisor);
assistantd_status_t assistantd_supervisor_stop(assistantd_supervisor_t *supervisor);

#endif  // ASSISTANTD_SUPERVISOR_H
