#include "assistantd/tts_adapter.h"

#include <string.h>

#include "assistantd/logger.h"

/**
 * @todo Implementation Playbook
 * @brief Implement Piper-based TTS adapter and deterministic output file policy.
 * @ownership Speech synthesis integration.
 * @inputs
 *   - UTF-8 assistant text and output WAV path.
 * @outputs
 *   - Synthesized WAV ready for playback stage.
 * @state
 *   - Adapter init caches binary/voice paths; synth requests are per-utterance.
 * @concurrency
 *   - Define whether synthesis requests are serialized or queue-based.
 * @errors
 *   - Handle subprocess timeouts, non-zero exits, and missing voice model.
 * @child_process_contracts
 *   - Piper CLI invocation and expected stdout/stderr semantics are version-pinned.
 * @acceptance
 *   - Sample prompts produce playable WAV artifacts and deterministic failure logs.
 */

assistantd_status_t assistantd_tts_init(
    assistantd_tts_adapter_t *adapter,
    const assistantd_config_t *config) {
  (void)config;
  if (adapter == NULL) {
    return ASSISTANTD_ERR_INVALID_ARGUMENT;
  }

  adapter->initialized = 1;
  return ASSISTANTD_OK;
}

assistantd_status_t assistantd_tts_synthesize(
    assistantd_tts_adapter_t *adapter,
    const assistantd_tts_request_t *request) {
  (void)request;
  if (adapter == NULL) {
    return ASSISTANTD_ERR_INVALID_ARGUMENT;
  }

  assistantd_log(ASSISTANTD_LOG_WARN, "tts scaffold: synthesize() is not implemented");
  return ASSISTANTD_ERR_UNIMPLEMENTED;
}

assistantd_status_t assistantd_tts_shutdown(assistantd_tts_adapter_t *adapter) {
  if (adapter == NULL) {
    return ASSISTANTD_ERR_INVALID_ARGUMENT;
  }

  memset(adapter, 0, sizeof(*adapter));
  return ASSISTANTD_OK;
}
