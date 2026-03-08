#ifndef ASSISTANTD_CONFIG_H
#define ASSISTANTD_CONFIG_H

#include <stdbool.h>
#include <stdio.h>

#include "assistantd/status.h"

#define ASSISTANTD_CONFIG_VALUE_MAX 512

typedef struct {
  char assistant_mode[16];
  char runtime_dir[ASSISTANTD_CONFIG_VALUE_MAX];
  char audio_device[ASSISTANTD_CONFIG_VALUE_MAX];
  char whisper_bin[ASSISTANTD_CONFIG_VALUE_MAX];
  char whisper_model_path[ASSISTANTD_CONFIG_VALUE_MAX];
  char llm_api_base_url[ASSISTANTD_CONFIG_VALUE_MAX];
  char llm_model[ASSISTANTD_CONFIG_VALUE_MAX];
  char tts_bin[ASSISTANTD_CONFIG_VALUE_MAX];
  char tts_voice_path[ASSISTANTD_CONFIG_VALUE_MAX];
  int vad_aggressiveness;
  int vad_silence_ms;
} assistantd_config_t;

assistantd_status_t assistantd_config_init_defaults(assistantd_config_t *config);
assistantd_status_t assistantd_config_load_file(assistantd_config_t *config, const char *path);
assistantd_status_t assistantd_config_validate(const assistantd_config_t *config);
void assistantd_config_dump(const assistantd_config_t *config, FILE *stream);

#endif  // ASSISTANTD_CONFIG_H
