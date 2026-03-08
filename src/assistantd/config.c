#include "assistantd/config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assistantd/logger.h"

/**
 * @todo Implementation Playbook
 * @brief Finalize robust config loader for local-only daemon mode.
 * @ownership Runtime platform and daemon reliability engineering.
 * @inputs
 *   - `assistantd.env` file path from CLI (`--config`).
 *   - Key/value lines for local-only operation.
 * @outputs
 *   - Fully populated `assistantd_config_t` with validated values.
 *   - Deterministic error codes for invalid/missing config.
 * @state
 *   - Parse defaults -> apply file overrides -> validate -> immutable runtime snapshot.
 * @concurrency
 *   - Config is read once at startup, then treated read-only by worker threads.
 * @errors
 *   - Distinguish parse errors, missing required keys, and semantic validation failures.
 *   - Fail fast on invalid mode (must be `local`) and malformed numeric VAD values.
 * @child_process_contracts
 *   - Ensure binary paths and model paths are valid before supervisor starts child processes.
 * @acceptance
 *   - Startup fails with actionable message for any invalid key.
 *   - Defaults + file overrides are deterministic and tested.
 *   - Unknown keys are logged at WARN without crashing.
 */

static char *trim(char *value) {
  while (*value != '\0' && isspace((unsigned char)*value)) {
    value++;
  }
  if (*value == '\0') {
    return value;
  }

  char *end = value + strlen(value) - 1;
  while (end > value && isspace((unsigned char)*end)) {
    *end = '\0';
    end--;
  }

  return value;
}

static void copy_config_value(char *dest, size_t dest_size, const char *src) {
  if (dest == NULL || dest_size == 0 || src == NULL) {
    return;
  }
  snprintf(dest, dest_size, "%s", src);
}

assistantd_status_t assistantd_config_init_defaults(assistantd_config_t *config) {
  if (config == NULL) {
    return ASSISTANTD_ERR_INVALID_ARGUMENT;
  }

  memset(config, 0, sizeof(*config));
  copy_config_value(config->assistant_mode, sizeof(config->assistant_mode), "local");
  copy_config_value(config->runtime_dir, sizeof(config->runtime_dir), "/var/lib/local-ai-assistant");
  copy_config_value(config->audio_device, sizeof(config->audio_device), "default");
  copy_config_value(config->whisper_bin, sizeof(config->whisper_bin), "/usr/local/bin/whisper-cli");
  copy_config_value(config->whisper_model_path, sizeof(config->whisper_model_path), "/opt/models/whisper.gguf");
  copy_config_value(config->llm_api_base_url, sizeof(config->llm_api_base_url), "http://127.0.0.1:8080/v1");
  copy_config_value(config->llm_model, sizeof(config->llm_model), "local-default-model");
  copy_config_value(config->tts_bin, sizeof(config->tts_bin), "/usr/local/bin/piper");
  copy_config_value(config->tts_voice_path, sizeof(config->tts_voice_path), "/opt/models/piper-voice.onnx");
  config->vad_aggressiveness = 2;
  config->vad_silence_ms = 700;

  return ASSISTANTD_OK;
}

assistantd_status_t assistantd_config_load_file(assistantd_config_t *config, const char *path) {
  if (config == NULL || path == NULL) {
    return ASSISTANTD_ERR_INVALID_ARGUMENT;
  }

  FILE *file = fopen(path, "r");
  if (file == NULL) {
    assistantd_log(ASSISTANTD_LOG_ERROR, "config file not found: %s", path);
    return ASSISTANTD_ERR_IO;
  }

  char line[1024];
  while (fgets(line, sizeof(line), file) != NULL) {
    char *cursor = trim(line);
    if (*cursor == '\0' || *cursor == '#') {
      continue;
    }

    char *equals = strchr(cursor, '=');
    if (equals == NULL) {
      assistantd_log(ASSISTANTD_LOG_WARN, "ignoring malformed config line: %s", cursor);
      continue;
    }

    *equals = '\0';
    char *key = trim(cursor);
    char *value = trim(equals + 1);

    if (strcmp(key, "ASSISTANT_MODE") == 0) {
      copy_config_value(config->assistant_mode, sizeof(config->assistant_mode), value);
    } else if (strcmp(key, "RUNTIME_DIR") == 0) {
      copy_config_value(config->runtime_dir, sizeof(config->runtime_dir), value);
    } else if (strcmp(key, "AUDIO_DEVICE") == 0) {
      copy_config_value(config->audio_device, sizeof(config->audio_device), value);
    } else if (strcmp(key, "WHISPER_BIN") == 0) {
      copy_config_value(config->whisper_bin, sizeof(config->whisper_bin), value);
    } else if (strcmp(key, "WHISPER_MODEL_PATH") == 0) {
      copy_config_value(config->whisper_model_path, sizeof(config->whisper_model_path), value);
    } else if (strcmp(key, "LLM_API_BASE_URL") == 0) {
      copy_config_value(config->llm_api_base_url, sizeof(config->llm_api_base_url), value);
    } else if (strcmp(key, "LLM_MODEL") == 0) {
      copy_config_value(config->llm_model, sizeof(config->llm_model), value);
    } else if (strcmp(key, "TTS_BIN") == 0) {
      copy_config_value(config->tts_bin, sizeof(config->tts_bin), value);
    } else if (strcmp(key, "TTS_VOICE_PATH") == 0) {
      copy_config_value(config->tts_voice_path, sizeof(config->tts_voice_path), value);
    } else if (strcmp(key, "VAD_AGGRESSIVENESS") == 0) {
      config->vad_aggressiveness = atoi(value);
    } else if (strcmp(key, "VAD_SILENCE_MS") == 0) {
      config->vad_silence_ms = atoi(value);
    } else {
      assistantd_log(ASSISTANTD_LOG_WARN, "unknown config key ignored: %s", key);
    }
  }

  fclose(file);
  return ASSISTANTD_OK;
}

assistantd_status_t assistantd_config_validate(const assistantd_config_t *config) {
  if (config == NULL) {
    return ASSISTANTD_ERR_INVALID_ARGUMENT;
  }

  if (strcmp(config->assistant_mode, "local") != 0) {
    assistantd_log(ASSISTANTD_LOG_ERROR, "ASSISTANT_MODE must be `local` in this scaffold");
    return ASSISTANTD_ERR_CONFIG;
  }

  if (config->runtime_dir[0] == '\0' || config->whisper_bin[0] == '\0' ||
      config->whisper_model_path[0] == '\0' || config->llm_api_base_url[0] == '\0' ||
      config->llm_model[0] == '\0' || config->tts_bin[0] == '\0' ||
      config->tts_voice_path[0] == '\0') {
    return ASSISTANTD_ERR_CONFIG;
  }

  if (config->vad_aggressiveness < 0 || config->vad_aggressiveness > 3) {
    return ASSISTANTD_ERR_CONFIG;
  }

  if (config->vad_silence_ms < 100 || config->vad_silence_ms > 5000) {
    return ASSISTANTD_ERR_CONFIG;
  }

  return ASSISTANTD_OK;
}

void assistantd_config_dump(const assistantd_config_t *config, FILE *stream) {
  if (config == NULL || stream == NULL) {
    return;
  }

  fprintf(stream, "ASSISTANT_MODE=%s\n", config->assistant_mode);
  fprintf(stream, "RUNTIME_DIR=%s\n", config->runtime_dir);
  fprintf(stream, "AUDIO_DEVICE=%s\n", config->audio_device);
  fprintf(stream, "WHISPER_BIN=%s\n", config->whisper_bin);
  fprintf(stream, "WHISPER_MODEL_PATH=%s\n", config->whisper_model_path);
  fprintf(stream, "LLM_API_BASE_URL=%s\n", config->llm_api_base_url);
  fprintf(stream, "LLM_MODEL=%s\n", config->llm_model);
  fprintf(stream, "TTS_BIN=%s\n", config->tts_bin);
  fprintf(stream, "TTS_VOICE_PATH=%s\n", config->tts_voice_path);
  fprintf(stream, "VAD_AGGRESSIVENESS=%d\n", config->vad_aggressiveness);
  fprintf(stream, "VAD_SILENCE_MS=%d\n", config->vad_silence_ms);
}
