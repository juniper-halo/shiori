#include <assert.h>
#include <stdio.h>

#include "assistantd/config.h"
#include "assistantd/status.h"
#include "assistantd/supervisor.h"

/**
 * @todo Extend with child process crash, timeout, and fail-fast lifecycle integration tests.
 */

static void write_temp_file(const char *path, const char *content) {
  FILE *f = fopen(path, "w");
  assert(f != NULL);
  fputs(content, f);
  fclose(f);
}

int main(void) {
  const char *prompt_path = "/tmp/test_supervisor_system_prompt.txt";
  write_temp_file(prompt_path, "You are a test assistant for supervisor shape tests.");

  assistantd_config_t config;
  assistantd_status_t status = assistantd_config_init_defaults(&config);
  assert(status == ASSISTANTD_OK);
  snprintf(config.llm_system_prompt_path, sizeof(config.llm_system_prompt_path), "%s", prompt_path);

  assistantd_supervisor_t supervisor;
  status = assistantd_supervisor_init(&supervisor, &config);
  assert(status == ASSISTANTD_OK);
  assert(supervisor.state == ASSISTANTD_SUPERVISOR_READY);

  status = assistantd_supervisor_start(&supervisor);
  assert(status == ASSISTANTD_OK);
  assert(supervisor.state == ASSISTANTD_SUPERVISOR_RUNNING);

  status = assistantd_supervisor_run_once(&supervisor);
  assert(status == ASSISTANTD_ERR_UNIMPLEMENTED);

  status = assistantd_supervisor_stop(&supervisor);
  assert(status == ASSISTANTD_OK);
  assert(supervisor.state == ASSISTANTD_SUPERVISOR_STOPPED);

  remove(prompt_path);
  return 0;
}
