#include <assert.h>

#include "assistantd/config.h"
#include "assistantd/status.h"
#include "assistantd/supervisor.h"

/**
 * @todo Extend with child process crash, timeout, and fail-fast lifecycle integration tests.
 */
int main(void) {
  assistantd_config_t config;
  assistantd_status_t status = assistantd_config_init_defaults(&config);
  assert(status == ASSISTANTD_OK);

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

  return 0;
}
