#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "assistantd/config.h"

/**
 * @todo Add fixture-based parse tests for malformed keys, whitespace edge cases, and strict mode.
 */
int main(void) {
  assistantd_config_t config;
  assistantd_status_t status = assistantd_config_init_defaults(&config);
  assert(status == ASSISTANTD_OK);

  status = assistantd_config_validate(&config);
  assert(status == ASSISTANTD_OK);

  snprintf(config.assistant_mode, sizeof(config.assistant_mode), "%s", "remote");
  status = assistantd_config_validate(&config);
  assert(status == ASSISTANTD_ERR_CONFIG);

  assert(assistantd_config_init_defaults(&config) == ASSISTANTD_OK);
  snprintf(config.audio_input_mode, sizeof(config.audio_input_mode), "%s", "invalid_mode");
  status = assistantd_config_validate(&config);
  assert(status == ASSISTANTD_ERR_CONFIG);

  assert(assistantd_config_init_defaults(&config) == ASSISTANTD_OK);
  snprintf(config.audio_input_mode, sizeof(config.audio_input_mode), "%s", "network_tcp");
  config.audio_network_port = 0;
  status = assistantd_config_validate(&config);
  assert(status == ASSISTANTD_ERR_CONFIG);

  assert(assistantd_config_init_defaults(&config) == ASSISTANTD_OK);
  snprintf(config.audio_input_mode, sizeof(config.audio_input_mode), "%s", "network_tcp");
  config.audio_network_port = 5555;
  status = assistantd_config_validate(&config);
  assert(status == ASSISTANTD_OK);

  assert(assistantd_config_init_defaults(&config) == ASSISTANTD_OK);
  snprintf(config.dev_pipeline_mode, sizeof(config.dev_pipeline_mode), "%s", "unknown");
  status = assistantd_config_validate(&config);
  assert(status == ASSISTANTD_ERR_CONFIG);

  assert(assistantd_config_init_defaults(&config) == ASSISTANTD_OK);
  snprintf(config.dev_pipeline_mode, sizeof(config.dev_pipeline_mode), "%s", "stt_only");
  status = assistantd_config_validate(&config);
  assert(status == ASSISTANTD_OK);

  config.llm_api_base_url[0] = '\0';
  config.llm_model[0] = '\0';
  config.llm_system_prompt_path[0] = '\0';
  config.tts_bin[0] = '\0';
  config.tts_voice_path[0] = '\0';
  status = assistantd_config_validate(&config);
  assert(status == ASSISTANTD_OK);

  snprintf(config.dev_pipeline_mode, sizeof(config.dev_pipeline_mode), "%s", "scaffold");
  status = assistantd_config_validate(&config);
  assert(status == ASSISTANTD_ERR_CONFIG);

  assert(strcmp(config.audio_input_mode, "arecord") == 0);
  assert(config.audio_network_port == 5555);
  assert(strcmp(config.dev_pipeline_mode, "scaffold") == 0);

  const char *temp_path = "/tmp/test_config_shape_new_keys.env";
  FILE *temp = fopen(temp_path, "w");
  assert(temp != NULL);
  fputs("AUDIO_INPUT_MODE=network_tcp\n", temp);
  fputs("AUDIO_NETWORK_PORT=7777\n", temp);
  fputs("DEV_PIPELINE_MODE=stt_only\n", temp);
  fclose(temp);

  assert(assistantd_config_init_defaults(&config) == ASSISTANTD_OK);
  assert(assistantd_config_load_file(&config, temp_path) == ASSISTANTD_OK);
  assert(strcmp(config.audio_input_mode, "network_tcp") == 0);
  assert(config.audio_network_port == 7777);
  assert(strcmp(config.dev_pipeline_mode, "stt_only") == 0);
  assert(assistantd_config_validate(&config) == ASSISTANTD_OK);
  remove(temp_path);

  return 0;
}
