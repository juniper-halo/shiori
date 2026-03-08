#ifndef ASSISTANTD_LLM_ADAPTER_H
#define ASSISTANTD_LLM_ADAPTER_H

#include "assistantd/config.h"
#include "assistantd/status.h"

typedef struct {
  int initialized;
} assistantd_llm_adapter_t;

typedef struct {
  const char *prompt;
} assistantd_llm_request_t;

typedef struct {
  char response[8192];
} assistantd_llm_result_t;

assistantd_status_t assistantd_llm_init(
    assistantd_llm_adapter_t *adapter,
    const assistantd_config_t *config);
assistantd_status_t assistantd_llm_generate(
    assistantd_llm_adapter_t *adapter,
    const assistantd_llm_request_t *request,
    assistantd_llm_result_t *result);
assistantd_status_t assistantd_llm_shutdown(assistantd_llm_adapter_t *adapter);

#endif  // ASSISTANTD_LLM_ADAPTER_H
