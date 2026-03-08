#include "assistantd/supervisor.h"

#include <string.h>

#include "assistantd/logger.h"
#include "assistantd/playback.h"

/**
 * @todo Implementation Playbook
 * @brief Implement full runtime orchestration loop for always-listening local pipeline.
 * @ownership Daemon runtime orchestration and fault handling.
 * @inputs
 *   - Immutable startup config and module interfaces.
 *   - Continuous PCM stream from capture process.
 * @outputs
 *   - Completed interaction cycles: utterance -> transcript -> response -> audio playback.
 * @state
 *   - INIT -> READY -> RUNNING -> STOPPING -> STOPPED.
 *   - Track per-interaction substate for utterance assembly and model calls.
 * @concurrency
 *   - Supervisor thread controls lifecycle; worker threads handle capture and pipeline tasks.
 * @errors
 *   - Fail fast on stage errors in local-only mode and emit structured logs.
 *   - Ensure cleanup is idempotent on partial startup failure.
 * @child_process_contracts
 *   - Capture/STT/TTS processes have explicit startup, timeout, and shutdown contracts.
 * @acceptance
 *   - Clean startup/shutdown without orphan child processes.
 *   - Deterministic fail-fast behavior when any stage errors.
 *   - Single-turn responses with no retained history in v1.
 */

assistantd_status_t assistantd_supervisor_init(
    assistantd_supervisor_t *supervisor,
    const assistantd_config_t *config) {
  if (supervisor == NULL || config == NULL) {
    return ASSISTANTD_ERR_INVALID_ARGUMENT;
  }

  memset(supervisor, 0, sizeof(*supervisor));
  supervisor->config = config;
  supervisor->state = ASSISTANTD_SUPERVISOR_READY;
  return ASSISTANTD_OK;
}

assistantd_status_t assistantd_supervisor_start(assistantd_supervisor_t *supervisor) {
  if (supervisor == NULL) {
    return ASSISTANTD_ERR_INVALID_ARGUMENT;
  }

  if (supervisor->state != ASSISTANTD_SUPERVISOR_READY) {
    return ASSISTANTD_ERR_INVALID_ARGUMENT;
  }

  assistantd_status_t status = assistantd_vad_init(&supervisor->vad, supervisor->config);
  if (status != ASSISTANTD_OK) {
    return status;
  }

  status = assistantd_stt_init(&supervisor->stt, supervisor->config);
  if (status != ASSISTANTD_OK) {
    return status;
  }

  status = assistantd_llm_init(&supervisor->llm, supervisor->config);
  if (status != ASSISTANTD_OK) {
    return status;
  }

  status = assistantd_tts_init(&supervisor->tts, supervisor->config);
  if (status != ASSISTANTD_OK) {
    return status;
  }

  supervisor->state = ASSISTANTD_SUPERVISOR_RUNNING;
  return ASSISTANTD_OK;
}

assistantd_status_t assistantd_supervisor_run_once(assistantd_supervisor_t *supervisor) {
  if (supervisor == NULL) {
    return ASSISTANTD_ERR_INVALID_ARGUMENT;
  }

  if (supervisor->state != ASSISTANTD_SUPERVISOR_RUNNING) {
    return ASSISTANTD_ERR_INVALID_ARGUMENT;
  }

  assistantd_log(ASSISTANTD_LOG_INFO, "supervisor scaffold tick: pipeline execution TODO");
  return ASSISTANTD_ERR_UNIMPLEMENTED;
}

assistantd_status_t assistantd_supervisor_stop(assistantd_supervisor_t *supervisor) {
  if (supervisor == NULL) {
    return ASSISTANTD_ERR_INVALID_ARGUMENT;
  }

  supervisor->state = ASSISTANTD_SUPERVISOR_STOPPING;
  (void)assistantd_audio_capture_stop(&supervisor->capture);
  (void)assistantd_stt_shutdown(&supervisor->stt);
  (void)assistantd_llm_shutdown(&supervisor->llm);
  (void)assistantd_tts_shutdown(&supervisor->tts);
  supervisor->state = ASSISTANTD_SUPERVISOR_STOPPED;
  return ASSISTANTD_OK;
}
