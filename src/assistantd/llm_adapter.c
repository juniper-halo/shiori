#include "assistantd/llm_adapter.h"

#include <string.h>

#include "assistantd/logger.h"

/**
 * @todo Implementation Playbook
 * @brief Build local-only LLM adapter behind abstract interface.
 * @ownership LLM integration and prompt/response protocol stability.
 * @inputs
 *   - Single-turn prompt string from STT output.
 * @outputs
 *   - Assistant response text for TTS stage.
 * @state
 *   - Init allocates client state; generate performs request; shutdown releases state.
 * @concurrency
 *   - Define thread-safety guarantees for concurrent calls.
 * @errors
 *   - Distinguish transport failures, malformed responses, and model-level errors.
 *   - Fail fast with actionable logs for startup and inference errors.
 * @child_process_contracts
 *   - Keep adapter abstract; do not hardcode llama.cpp assumptions in interface.
 * @acceptance
 *   - Adapter can be swapped without supervisor changes.
 *   - Local-only mode enforces no remote fallback path.
 */

assistantd_status_t assistantd_llm_init(
    assistantd_llm_adapter_t *adapter,
    const assistantd_config_t *config) {
  (void)config;
  if (adapter == NULL) {
    return ASSISTANTD_ERR_INVALID_ARGUMENT;
  }

  adapter->initialized = 1;
  return ASSISTANTD_OK;
}

assistantd_status_t assistantd_llm_generate(
    assistantd_llm_adapter_t *adapter,
    const assistantd_llm_request_t *request,
    assistantd_llm_result_t *result) {
  (void)request;
  if (adapter == NULL || result == NULL) {
    return ASSISTANTD_ERR_INVALID_ARGUMENT;
  }

  result->response[0] = '\0';
  assistantd_log(ASSISTANTD_LOG_WARN, "llm scaffold: generate() is not implemented");
  return ASSISTANTD_ERR_UNIMPLEMENTED;
}

assistantd_status_t assistantd_llm_shutdown(assistantd_llm_adapter_t *adapter) {
  if (adapter == NULL) {
    return ASSISTANTD_ERR_INVALID_ARGUMENT;
  }

  memset(adapter, 0, sizeof(*adapter));
  return ASSISTANTD_OK;
}
