#ifndef ASSISTANTD_STT_ADAPTER_H
#define ASSISTANTD_STT_ADAPTER_H

#include "assistantd/config.h"
#include "assistantd/status.h"

typedef struct {
  int initialized;
  char whisper_bin[ASSISTANTD_CONFIG_VALUE_MAX];
  char whisper_model_path[ASSISTANTD_CONFIG_VALUE_MAX];
} assistantd_stt_adapter_t;

typedef struct {
  const char *utterance_wav_path;
} assistantd_stt_request_t;

typedef struct {
  char transcript[4096];
} assistantd_stt_result_t;

assistantd_status_t assistantd_stt_init(
    assistantd_stt_adapter_t *adapter,
    const assistantd_config_t *config);
assistantd_status_t assistantd_stt_transcribe(
    assistantd_stt_adapter_t *adapter,
    const assistantd_stt_request_t *request,
    assistantd_stt_result_t *result);
assistantd_status_t assistantd_stt_shutdown(assistantd_stt_adapter_t *adapter);

#endif  // ASSISTANTD_STT_ADAPTER_H
