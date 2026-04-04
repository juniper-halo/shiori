#ifndef ASSISTANTD_LLM_ADAPTER_H
#define ASSISTANTD_LLM_ADAPTER_H

#include <signal.h>

#include "assistantd/config.h"
#include "assistantd/status.h"

#define ASSISTANTD_LLM_SYSTEM_PROMPT_MAX 4096
#define ASSISTANTD_LLM_TIMEOUT_SECONDS 40

typedef struct {
    int initialized;
    char system_prompt[ASSISTANTD_LLM_SYSTEM_PROMPT_MAX];
    char api_base_url[ASSISTANTD_CONFIG_VALUE_MAX];
    char model_name[ASSISTANTD_CONFIG_VALUE_MAX];
    volatile sig_atomic_t *shutdown_flag;
    void *curl_handle;
} assistantd_llm_adapter_t;

typedef struct {
    const char *prompt;
} assistantd_llm_request_t;

typedef struct {
    char response[8192];
} assistantd_llm_result_t;

assistantd_status_t assistantd_llm_init(assistantd_llm_adapter_t *adapter,
                                        const assistantd_config_t *config);
assistantd_status_t
assistantd_llm_generate(assistantd_llm_adapter_t *adapter,
                        const assistantd_llm_request_t *request,
                        assistantd_llm_result_t *result);
assistantd_status_t assistantd_llm_shutdown(assistantd_llm_adapter_t *adapter);

void assistantd_llm_set_shutdown_flag(assistantd_llm_adapter_t *adapter,
                                      volatile sig_atomic_t *flag);

#endif // ASSISTANTD_LLM_ADAPTER_H
