#include <assert.h>
#include <stdio.h>

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

  return 0;
}
