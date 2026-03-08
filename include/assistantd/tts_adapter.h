#ifndef ASSISTANTD_TTS_ADAPTER_H
#define ASSISTANTD_TTS_ADAPTER_H

#include "assistantd/config.h"
#include "assistantd/status.h"

typedef struct {
  int initialized;
} assistantd_tts_adapter_t;

typedef struct {
  const char *text;
  const char *output_wav_path;
} assistantd_tts_request_t;

assistantd_status_t assistantd_tts_init(
    assistantd_tts_adapter_t *adapter,
    const assistantd_config_t *config);
assistantd_status_t assistantd_tts_synthesize(
    assistantd_tts_adapter_t *adapter,
    const assistantd_tts_request_t *request);
assistantd_status_t assistantd_tts_shutdown(assistantd_tts_adapter_t *adapter);

#endif  // ASSISTANTD_TTS_ADAPTER_H
