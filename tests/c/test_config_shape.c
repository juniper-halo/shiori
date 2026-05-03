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

  // Default audio device fields are "default".
  assistantd_config_init_defaults(&config);
  assert(strcmp(config.audio_capture_device, "default") == 0);
  assert(strcmp(config.audio_playback_device, "default") == 0);

  // Empty audio_capture_device must fail validation.
  assistantd_config_init_defaults(&config);
  config.audio_capture_device[0] = '\0';
  status = assistantd_config_validate(&config);
  assert(status == ASSISTANTD_ERR_CONFIG);

  // Empty audio_playback_device must fail validation.
  assistantd_config_init_defaults(&config);
  config.audio_playback_device[0] = '\0';
  status = assistantd_config_validate(&config);
  assert(status == ASSISTANTD_ERR_CONFIG);

  // AUDIO_CAPTURE_DEVICE and AUDIO_PLAYBACK_DEVICE parse correctly from a file.
  {
    const char *tmp_path = "/tmp/assistantd_test_audio_split.env";
    FILE *f = fopen(tmp_path, "w");
    assert(f != NULL);
    fprintf(f, "AUDIO_CAPTURE_DEVICE=hw:1,0\n");
    fprintf(f,
            "AUDIO_PLAYBACK_DEVICE=bluealsa:DEV=AA:BB:CC:DD:EE:FF,PROFILE=a2dp\n");
    fclose(f);

    assistantd_config_init_defaults(&config);
    status = assistantd_config_load_file(&config, tmp_path);
    assert(status == ASSISTANTD_OK);
    assert(strcmp(config.audio_capture_device, "hw:1,0") == 0);
    assert(strcmp(config.audio_playback_device,
                  "bluealsa:DEV=AA:BB:CC:DD:EE:FF,PROFILE=a2dp") == 0);

    remove(tmp_path);
  }

  return 0;
}
