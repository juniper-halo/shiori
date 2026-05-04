#include "assistantd/supervisor.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dr_wav.h"
#include "assistantd/utilities/logger.h"
#include "assistantd/playback.h"

#define ASSISTANTD_SUPERVISOR_CAPTURE_CHUNK_BYTES 640
#define ASSISTANTD_SUPERVISOR_RING_CAPACITY_BYTES (16000 * 2 * 2)
#define ASSISTANTD_SUPERVISOR_PCM_SAMPLE_RATE_HZ 16000
#define ASSISTANTD_SUPERVISOR_PCM_CHANNELS 1
#define ASSISTANTD_SUPERVISOR_PCM_BYTES_PER_SAMPLE 2
#define ASSISTANTD_SUPERVISOR_VAD_FRAME_DURATION_MS 20
#define ASSISTANTD_SUPERVISOR_VAD_FRAME_SAMPLES \
  ((ASSISTANTD_SUPERVISOR_PCM_SAMPLE_RATE_HZ / 1000) * ASSISTANTD_SUPERVISOR_VAD_FRAME_DURATION_MS)
#define ASSISTANTD_SUPERVISOR_VAD_FRAME_BYTES \
  (ASSISTANTD_SUPERVISOR_VAD_FRAME_SAMPLES * ASSISTANTD_SUPERVISOR_PCM_BYTES_PER_SAMPLE)
#define ASSISTANTD_SUPERVISOR_INITIAL_UTTERANCE_CAPACITY (ASSISTANTD_SUPERVISOR_VAD_FRAME_BYTES * 16)
#define ASSISTANTD_SUPERVISOR_LLM_FALLBACK_TEXT "I'm having trouble right now. Please try again in a moment."

_Static_assert(ASSISTANTD_SUPERVISOR_CAPTURE_CHUNK_BYTES == ASSISTANTD_SUPERVISOR_VAD_FRAME_BYTES, "capture chunk bytes must match VAD frame bytes");

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
 *   - Completed VAD utterances are persisted to WAV files and queued in sequence order for STT.
 *   - Clean startup/shutdown without orphan child processes.
 *   - Deterministic fail-fast behavior when any stage errors.
 * @remaining
 *   - TTS synthesis and playback consumption of queued LLM responses remains TODO.
 */

/** @brief Reset the active utterance length while keeping allocated memory. */
static void assistantd_supervisor_reset_utterance(assistantd_supervisor_t *supervisor) {
  if (supervisor == NULL) {
    return;
  }

  supervisor->utterance_metadata.size = 0;
}

/** @brief Append PCM bytes to the active utterance, expanding capacity as needed. */
static assistantd_status_t assistantd_supervisor_append_utterance_pcm(
    assistantd_supervisor_t *supervisor,
    const uint8_t *pcm,
    size_t pcm_bytes) {
  if (supervisor == NULL || pcm == NULL || pcm_bytes == 0) {
    return ASSISTANTD_ERR_INVALID_ARGUMENT;
  }

  if (supervisor->utterance_metadata.size > SIZE_MAX - pcm_bytes) {
    return ASSISTANTD_ERR_IO;
  }
  size_t required_capacity = supervisor->utterance_metadata.size + pcm_bytes;
  if (required_capacity > supervisor->utterance_metadata.capacity) {
    size_t new_capacity = supervisor->utterance_metadata.capacity;
    if (new_capacity == 0) {
      new_capacity = ASSISTANTD_SUPERVISOR_INITIAL_UTTERANCE_CAPACITY;
    }
    while (new_capacity < required_capacity) {
      if (new_capacity > (SIZE_MAX / 2)) {
        return ASSISTANTD_ERR_IO;
      }
      new_capacity *= 2;
    }

    uint8_t *resized = (uint8_t *)realloc(supervisor->utterance_metadata.pcm, new_capacity);
    if (resized == NULL) {
      return ASSISTANTD_ERR_IO;
    }
    supervisor->utterance_metadata.pcm = resized;
    supervisor->utterance_metadata.capacity = new_capacity;
  }

  memcpy(supervisor->utterance_metadata.pcm + supervisor->utterance_metadata.size, pcm, pcm_bytes);
  supervisor->utterance_metadata.size += pcm_bytes;
  return ASSISTANTD_OK;
}

/** @brief Remove queued artifacts and unlink their backing WAV files. */
static void assistantd_supervisor_discard_queued_artifacts(assistantd_supervisor_t *supervisor) {
  if (supervisor == NULL) {
    return;
  }

  assistantd_supervisor_artifact_t artifact;
  while (assistantd_artifact_queue_dequeue(&supervisor->artifact_queue, &artifact)) {
    if (artifact.wav_path[0] != '\0') {
      (void)unlink(artifact.wav_path);
    }
    memset(&artifact, 0, sizeof(artifact));
  }
}

/** @brief Remove queued LLM responses that are awaiting future TTS integration. */
static void assistantd_supervisor_discard_queued_llm_responses(assistantd_supervisor_t *supervisor) {
  if (supervisor == NULL) {
    return;
  }

  assistantd_supervisor_llm_response_t response;
  while (assistantd_llm_response_queue_dequeue(&supervisor->llm_response_queue, &response)) {
    memset(&response, 0, sizeof(response));
  }
}

/** @brief Enqueue one LLM response job and soft-drop when queue is full. */
static assistantd_status_t assistantd_supervisor_enqueue_llm_response(
    assistantd_supervisor_t *supervisor,
    uint64_t sequence_id,
    const char *response_text,
    int used_fallback) {
  if (supervisor == NULL || response_text == NULL) {
    return ASSISTANTD_ERR_INVALID_ARGUMENT;
  }

  assistantd_supervisor_llm_response_t queued = {
      .sequence_id = sequence_id,
      .used_fallback = used_fallback,
      .response_text = {0},
  };
  snprintf(queued.response_text, sizeof(queued.response_text), "%s", response_text);

  if (!assistantd_llm_response_queue_enqueue(&supervisor->llm_response_queue, &queued)) {
    assistantd_log(ASSISTANTD_LOG_WARN,
                   "llm response queue full: dropped sequence_id=%" PRIu64 " used_fallback=%d",
                   sequence_id,
                   used_fallback);
    return ASSISTANTD_OK;
  }

  assistantd_log(ASSISTANTD_LOG_INFO,
                 "queued llm response: sequence_id=%" PRIu64 " used_fallback=%d chars=%zu",
                 sequence_id,
                 used_fallback,
                 strlen(queued.response_text));
  return ASSISTANTD_OK;
}

/** @brief Build a deterministic WAV output path for a finalized utterance sequence. */
static assistantd_status_t assistantd_supervisor_build_artifact_path(
    const assistantd_supervisor_t *supervisor,
    uint64_t sequence_id,
    char *wav_path,
    size_t wav_path_size) {
  if (supervisor == NULL || supervisor->config == NULL || wav_path == NULL || wav_path_size == 0) {
    return ASSISTANTD_ERR_INVALID_ARGUMENT;
  }

  int written = snprintf(wav_path,
                         wav_path_size,
                         "%s/utterance-%020" PRIu64 ".wav",
                         supervisor->config->runtime_dir,
                         sequence_id);
  if (written < 0 || (size_t)written >= wav_path_size) {
    return ASSISTANTD_ERR_IO;
  }
  return ASSISTANTD_OK;
}

/** @brief Write mono 16 kHz PCM bytes to a RIFF/WAVE file. */
static assistantd_status_t assistantd_supervisor_write_wav_file(
    const char *wav_path,
    const uint8_t *pcm,
    size_t pcm_bytes) {
  if (wav_path == NULL || pcm == NULL) {
    return ASSISTANTD_ERR_INVALID_ARGUMENT;
  }
  size_t bytes_per_frame = ASSISTANTD_SUPERVISOR_PCM_CHANNELS * ASSISTANTD_SUPERVISOR_PCM_BYTES_PER_SAMPLE;
  if (bytes_per_frame == 0 || (pcm_bytes % bytes_per_frame) != 0) {
    return ASSISTANTD_ERR_INVALID_ARGUMENT;
  }
  drwav_uint64 frame_count = (drwav_uint64)(pcm_bytes / bytes_per_frame);
  if ((size_t)frame_count != (pcm_bytes / bytes_per_frame)) {
    return ASSISTANTD_ERR_IO;
  }

  drwav_data_format format;
  memset(&format, 0, sizeof(format));
  format.container = drwav_container_riff;
  format.format = DR_WAVE_FORMAT_PCM;
  format.channels = ASSISTANTD_SUPERVISOR_PCM_CHANNELS;
  format.sampleRate = ASSISTANTD_SUPERVISOR_PCM_SAMPLE_RATE_HZ;
  format.bitsPerSample = ASSISTANTD_SUPERVISOR_PCM_BYTES_PER_SAMPLE * 8;

  drwav wav;
  if (!drwav_init_file_write(&wav, wav_path, &format, NULL)) {
    return ASSISTANTD_ERR_IO;
  }

  drwav_uint64 written_frames = drwav_write_pcm_frames(&wav, frame_count, pcm);
  drwav_result close_result = drwav_uninit(&wav);
  if (written_frames != frame_count || close_result != DRWAV_SUCCESS) {
    (void)unlink(wav_path);
    return ASSISTANTD_ERR_IO;
  }

  return ASSISTANTD_OK;
}

/** @brief Flush the active utterance to WAV and enqueue artifact metadata for STT. */
static assistantd_status_t assistantd_supervisor_finalize_utterance(
    assistantd_supervisor_t *supervisor) {
  if (supervisor == NULL) {
    return ASSISTANTD_ERR_INVALID_ARGUMENT;
  }
  if (supervisor->utterance_metadata.size == 0 || supervisor->utterance_metadata.pcm == NULL) {
    assistantd_supervisor_reset_utterance(supervisor);
    return ASSISTANTD_OK;
  }

  uint64_t sequence_id = supervisor->next_artifact_sequence_id++;
  size_t pcm_bytes = supervisor->utterance_metadata.size;

  char wav_path[ASSISTANTD_SUPERVISOR_WAV_PATH_MAX];
  assistantd_status_t status = assistantd_supervisor_build_artifact_path(
      supervisor, sequence_id, wav_path, sizeof(wav_path));
  if (status != ASSISTANTD_OK) {
    assistantd_supervisor_reset_utterance(supervisor);
    return status;
  }

  status = assistantd_supervisor_write_wav_file(wav_path, supervisor->utterance_metadata.pcm, pcm_bytes);
  if (status != ASSISTANTD_OK) {
    assistantd_supervisor_reset_utterance(supervisor);
    return status;
  }

  assistantd_supervisor_artifact_t artifact = {
      .sequence_id = sequence_id,
      .pcm_bytes = pcm_bytes,
      .wav_path = {0},
  };
  snprintf(artifact.wav_path, sizeof(artifact.wav_path), "%s", wav_path);
  if (!assistantd_artifact_queue_enqueue(&supervisor->artifact_queue, &artifact)) {
    assistantd_log(ASSISTANTD_LOG_WARN,
                   "artifact queue full: dropped sequence_id=%" PRIu64 " path=%s",
                   sequence_id,
                   wav_path);
    (void)unlink(wav_path);
    assistantd_supervisor_reset_utterance(supervisor);
    return ASSISTANTD_OK;
  }

  assistantd_log(ASSISTANTD_LOG_INFO,
                 "queued utterance artifact: sequence_id=%" PRIu64 " pcm_bytes=%zu",
                 sequence_id,
                 pcm_bytes);
  assistantd_supervisor_reset_utterance(supervisor);
  return ASSISTANTD_OK;
}

/** @brief Drain capture frames, run VAD state transitions, and manage utterance assembly. */
static assistantd_status_t assistantd_supervisor_drain_capture_ring_into_vad(
    assistantd_supervisor_t *supervisor) {
  if (supervisor == NULL) {
    return ASSISTANTD_ERR_INVALID_ARGUMENT;
  }

  int16_t frame[ASSISTANTD_SUPERVISOR_VAD_FRAME_SAMPLES];
  while (assistantd_ring_buffer_available(&supervisor->capture_ring) >=
         ASSISTANTD_SUPERVISOR_VAD_FRAME_BYTES) {
    size_t frame_bytes = assistantd_ring_buffer_read(
        &supervisor->capture_ring, (uint8_t *)frame, ASSISTANTD_SUPERVISOR_VAD_FRAME_BYTES);
    if (frame_bytes != ASSISTANTD_SUPERVISOR_VAD_FRAME_BYTES) {
      return ASSISTANTD_ERR_IO;
    }

    int was_active = supervisor->vad.speech_active;
    assistantd_vad_event_t event = ASSISTANTD_VAD_EVENT_NONE;
    assistantd_status_t status = assistantd_vad_process_frame(
        &supervisor->vad,
        frame,
        ASSISTANTD_SUPERVISOR_VAD_FRAME_SAMPLES,
        ASSISTANTD_SUPERVISOR_VAD_FRAME_DURATION_MS,
        &event);
    if (status != ASSISTANTD_OK) {
      return status;
    }

    if (event == ASSISTANTD_VAD_EVENT_SPEECH_START) {
      assistantd_supervisor_reset_utterance(supervisor);
    }
    if (event == ASSISTANTD_VAD_EVENT_SPEECH_START ||
        (event == ASSISTANTD_VAD_EVENT_SPEECH_CONTINUE && was_active) ||
        (event == ASSISTANTD_VAD_EVENT_SPEECH_END && was_active)) {
      status = assistantd_supervisor_append_utterance_pcm(
          supervisor, (const uint8_t *)frame, ASSISTANTD_SUPERVISOR_VAD_FRAME_BYTES);
      if (status != ASSISTANTD_OK) {
        return status;
      }
    }
    if (event == ASSISTANTD_VAD_EVENT_SPEECH_END) {
      status = assistantd_supervisor_finalize_utterance(supervisor);
      if (status != ASSISTANTD_OK) {
        return status;
      }
    }
  }

  return ASSISTANTD_OK;
}

/** @brief Run STT then LLM for one artifact, then enqueue pending TTS response work. */
static assistantd_status_t assistantd_supervisor_process_stt_queue(
    assistantd_supervisor_t *supervisor) {
  if (supervisor == NULL) {
    return ASSISTANTD_ERR_INVALID_ARGUMENT;
  }

  assistantd_supervisor_artifact_t artifact;
  if (!assistantd_artifact_queue_dequeue(&supervisor->artifact_queue, &artifact)) {
    return ASSISTANTD_OK;
  }

  assistantd_stt_request_t request = {
      .utterance_wav_path = artifact.wav_path,
  };
  assistantd_stt_result_t result;
  assistantd_status_t status = assistantd_stt_transcribe(&supervisor->stt, &request, &result);
  if (status != ASSISTANTD_OK) {
    assistantd_log(ASSISTANTD_LOG_ERROR,
                   "stt failed for sequence_id=%" PRIu64 " path=%s",
                   artifact.sequence_id,
                   artifact.wav_path);
    return status;
  }

  if (artifact.wav_path[0] != '\0' && unlink(artifact.wav_path) != 0) {
    assistantd_log(ASSISTANTD_LOG_WARN,
                   "failed to remove consumed artifact file: %s",
                   artifact.wav_path);
  }

  assistantd_log(ASSISTANTD_LOG_INFO,
                 "stt complete: sequence_id=%" PRIu64 " transcript=%s",
                 artifact.sequence_id,
                 result.transcript);

  assistantd_llm_request_t llm_request = {
      .prompt = result.transcript,
  };
  assistantd_llm_result_t llm_result;
  assistantd_status_t llm_status =
      assistantd_llm_generate(&supervisor->llm, &llm_request, &llm_result);
  if (llm_status != ASSISTANTD_OK) {
    assistantd_log(ASSISTANTD_LOG_ERROR,
                   "llm failed for sequence_id=%" PRIu64 " status=%d; using fallback response",
                   artifact.sequence_id,
                   llm_status);
    return assistantd_supervisor_enqueue_llm_response(supervisor,
                                                      artifact.sequence_id,
                                                      ASSISTANTD_SUPERVISOR_LLM_FALLBACK_TEXT,
                                                      1);
  }

  assistantd_log(ASSISTANTD_LOG_INFO,
                 "llm complete: sequence_id=%" PRIu64 " chars=%zu",
                 artifact.sequence_id,
                 strlen(llm_result.response));
  return assistantd_supervisor_enqueue_llm_response(supervisor,
                                                    artifact.sequence_id,
                                                    llm_result.response,
                                                    0);
}

/** @brief Drain one queued LLM response and discard it as a placeholder for future TTS. */
static assistantd_status_t assistantd_supervisor_drain_llm_response_queue(
    assistantd_supervisor_t *supervisor) {
  if (supervisor == NULL) {
    return ASSISTANTD_ERR_INVALID_ARGUMENT;
  }

  assistantd_supervisor_llm_response_t response;
  if (!assistantd_llm_response_queue_dequeue(&supervisor->llm_response_queue, &response)) {
    return ASSISTANTD_OK;
  }

  assistantd_log(ASSISTANTD_LOG_INFO,
                 "tts placeholder drain: sequence_id=%" PRIu64 " used_fallback=%d text=%s",
                 response.sequence_id,
                 response.used_fallback,
                 response.response_text);
  return ASSISTANTD_OK;
}

static assistantd_status_t assistantd_supervisor_initialize_modules(
    assistantd_supervisor_t *supervisor) {
  assistantd_supervisor_reset_utterance(supervisor);
  assistantd_artifact_queue_init(&supervisor->artifact_queue);
  assistantd_llm_response_queue_init(&supervisor->llm_response_queue);

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
  assistantd_supervisor_discard_queued_artifacts(supervisor);
  assistantd_supervisor_discard_queued_llm_responses(supervisor);
  free(supervisor->utterance_metadata.pcm);
  supervisor->utterance_metadata.pcm = NULL;
  supervisor->utterance_metadata.size = 0;
  supervisor->utterance_metadata.capacity = 0;
  supervisor->next_artifact_sequence_id = 0;
  (void)assistantd_vad_shutdown(&supervisor->vad);
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

  assistantd_status_t vad_status = assistantd_supervisor_drain_capture_ring_into_vad(supervisor);
  if (vad_status != ASSISTANTD_OK) {
    return vad_status;
  }

  assistantd_status_t stt_status = assistantd_supervisor_process_stt_queue(supervisor);
  if (stt_status != ASSISTANTD_OK) {
    return stt_status;
  }

  assistantd_status_t llm_queue_status = assistantd_supervisor_drain_llm_response_queue(supervisor);
  if (llm_queue_status != ASSISTANTD_OK) {
    return llm_queue_status;
  }

  size_t queued_artifacts = assistantd_artifact_queue_size(&supervisor->artifact_queue);
  size_t queued_llm_responses = assistantd_llm_response_queue_size(&supervisor->llm_response_queue);
  assistantd_log(ASSISTANTD_LOG_INFO,
                 "supervisor capture tick: buffered=%zu bytes queued_artifacts=%zu queued_llm_responses=%zu",
                 assistantd_ring_buffer_available(&supervisor->capture_ring),
                 queued_artifacts,
                 queued_llm_responses);
  return ASSISTANTD_OK;
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
