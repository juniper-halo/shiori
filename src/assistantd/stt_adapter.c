#include "assistantd/stt_adapter.h"

#include <string.h>

#include "assistantd/utilities/logger.h"

/**
 * @todo Implementation Playbook
 * @brief Implement whisper.cpp adapter with deterministic subprocess contract.
 * @ownership STT pipeline integration.
 * @inputs
 *   - Finalized utterance WAV path emitted by VAD/utterance assembler.
 * @outputs
 *   - UTF-8 transcript string for LLM stage.
 * @state
 *   - Adapter init stores executable and model path, transcribe is stateless per request.
 * @concurrency
 *   - Define whether multiple transcriptions can run concurrently or are serialized by supervisor.
 * @errors
 *   - Map subprocess exit codes/timeouts to status enum and logs.
 *   - Fail fast on empty transcript when whisper stdout indicates runtime error.
 * @child_process_contracts
 *   - Child stdin/stdout protocol and command-line flags are version-pinned in docs.
 * @acceptance
 *   - Known utterance fixtures produce deterministic text on pinned model.
 *   - Timeout and crash scenarios are covered with integration tests.
 */

assistantd_status_t assistantd_stt_init(
    assistantd_stt_adapter_t *adapter,
    const assistantd_config_t *config) {
  (void)config;
  if (adapter == NULL) {
    return ASSISTANTD_ERR_INVALID_ARGUMENT;
  }

  adapter->initialized = 1;
  return ASSISTANTD_OK;
}

assistantd_status_t assistantd_stt_transcribe(
    assistantd_stt_adapter_t *adapter,
    const assistantd_stt_request_t *request,
    assistantd_stt_result_t *result) {
  (void)request;
  if (adapter == NULL || result == NULL) {
    return ASSISTANTD_ERR_INVALID_ARGUMENT;
  }

  result->transcript[0] = '\0';
  assistantd_log(ASSISTANTD_LOG_WARN, "stt scaffold: transcribe() is not implemented");
  return ASSISTANTD_ERR_UNIMPLEMENTED;
}

assistantd_status_t assistantd_stt_shutdown(assistantd_stt_adapter_t *adapter) {
  if (adapter == NULL) {
    return ASSISTANTD_ERR_INVALID_ARGUMENT;
  }

  memset(adapter, 0, sizeof(*adapter));
  return ASSISTANTD_OK;
}
