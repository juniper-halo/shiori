#include "assistantd/supervisor.h"

#include <stdatomic.h>
#include <string.h>

#include "assistantd/logger.h"
#include "assistantd/playback.h"

#define ASSISTANTD_SUPERVISOR_CAPTURE_CHUNK_BYTES 640
#define ASSISTANTD_SUPERVISOR_RING_CAPACITY_BYTES (16000 * 2 * 2)

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
 *   - Capture is now supervisor-owned: run loop starts/stops capture and routes bytes to ring buffer.
 *   - Clean startup/shutdown without orphan child processes.
 *   - Deterministic fail-fast behavior when any stage errors.
 * @remaining
 *   - VAD/STT/LLM/TTS turn-level orchestration in run_once remains TODO.
 */

static assistantd_status_t assistantd_supervisor_initialize_modules(
    assistantd_supervisor_t *supervisor) {
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

  status = assistantd_ring_buffer_init(
      &supervisor->capture_ring, ASSISTANTD_SUPERVISOR_RING_CAPACITY_BYTES);
  if (status != ASSISTANTD_OK) {
    return status;
  }

  return ASSISTANTD_OK;
}

static void assistantd_supervisor_shutdown_modules(assistantd_supervisor_t *supervisor) {
  if (supervisor == NULL) {
    return;
  }

  (void)assistantd_audio_capture_stop(&supervisor->capture);
  assistantd_ring_buffer_free(&supervisor->capture_ring);
  (void)assistantd_stt_shutdown(&supervisor->stt);
  (void)assistantd_llm_shutdown(&supervisor->llm);
  (void)assistantd_tts_shutdown(&supervisor->tts);
}

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

  assistantd_status_t status = assistantd_supervisor_initialize_modules(supervisor);
  if (status != ASSISTANTD_OK) {
    assistantd_supervisor_shutdown_modules(supervisor);
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

  if (!supervisor->capture.active) {
    assistantd_status_t start_status =
        assistantd_audio_capture_start(&supervisor->capture, supervisor->config);
    if (start_status != ASSISTANTD_OK) {
      return start_status;
    }
  }

  uint8_t capture_chunk[ASSISTANTD_SUPERVISOR_CAPTURE_CHUNK_BYTES];
  size_t bytes_read = 0;
  assistantd_status_t read_status = assistantd_audio_capture_read(
      &supervisor->capture, capture_chunk, sizeof(capture_chunk), &bytes_read);
  if (read_status != ASSISTANTD_OK) {
    return read_status;
  }

  if (bytes_read > 0) {
    size_t written = assistantd_ring_buffer_write(&supervisor->capture_ring, capture_chunk, bytes_read);
    if (written < bytes_read) {
      size_t dropped = bytes_read - written;
      size_t total_overflow =
          atomic_load_explicit(&supervisor->capture_ring.overflow, memory_order_relaxed);
      assistantd_log(ASSISTANTD_LOG_WARN,
                     "capture ring overflow: dropped=%zu total_overflow=%zu",
                     dropped,
                     total_overflow);
    }
  }

  assistantd_log(ASSISTANTD_LOG_INFO,
                 "supervisor capture tick: buffered=%zu bytes",
                 assistantd_ring_buffer_available(&supervisor->capture_ring));
  return ASSISTANTD_ERR_UNIMPLEMENTED;
}

assistantd_status_t assistantd_supervisor_stop(assistantd_supervisor_t *supervisor) {
  if (supervisor == NULL) {
    return ASSISTANTD_ERR_INVALID_ARGUMENT;
  }

  supervisor->state = ASSISTANTD_SUPERVISOR_STOPPING;
  assistantd_supervisor_shutdown_modules(supervisor);
  supervisor->state = ASSISTANTD_SUPERVISOR_STOPPED;
  return ASSISTANTD_OK;
}
