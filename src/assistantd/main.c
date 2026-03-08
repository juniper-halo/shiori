#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "assistantd/config.h"
#include "assistantd/logger.h"
#include "assistantd/shutdown.h"
#include "assistantd/status.h"
#include "assistantd/supervisor.h"

static void print_usage(const char *argv0) {
  fprintf(stderr,
          "Usage: %s [--config /path/to/assistantd.env] [--foreground]\n",
          argv0);
}

int main(int argc, char **argv) {
  const char *config_path = "/etc/local-ai-assistant/assistantd.env";
  bool foreground = false;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--config") == 0) {
      if (i + 1 >= argc) {
        print_usage(argv[0]);
        return ASSISTANTD_ERR_INVALID_ARGUMENT;
      }
      config_path = argv[++i];
    } else if (strcmp(argv[i], "--foreground") == 0) {
      foreground = true;
    } else if (strcmp(argv[i], "--help") == 0) {
      print_usage(argv[0]);
      return ASSISTANTD_OK;
    } else {
      print_usage(argv[0]);
      return ASSISTANTD_ERR_INVALID_ARGUMENT;
    }
  }

  if (!foreground) {
    assistantd_log(ASSISTANTD_LOG_WARN,
                   "daemonization is scaffold-only for now; running in foreground");
  }

  assistantd_config_t config;
  assistantd_status_t status = assistantd_config_init_defaults(&config);
  if (status != ASSISTANTD_OK) {
    return status;
  }

  status = assistantd_config_load_file(&config, config_path);
  if (status != ASSISTANTD_OK) {
    return status;
  }

  status = assistantd_config_validate(&config);
  if (status != ASSISTANTD_OK) {
    return status;
  }

  status = assistantd_shutdown_install_handlers();
  if (status != ASSISTANTD_OK) {
    return status;
  }

  assistantd_supervisor_t supervisor;
  status = assistantd_supervisor_init(&supervisor, &config);
  if (status != ASSISTANTD_OK) {
    return status;
  }

  status = assistantd_supervisor_start(&supervisor);
  if (status != ASSISTANTD_OK) {
    (void)assistantd_supervisor_stop(&supervisor);
    return status;
  }

  while (!assistantd_shutdown_requested()) {
    status = assistantd_supervisor_run_once(&supervisor);
    if (status == ASSISTANTD_ERR_UNIMPLEMENTED) {
      assistantd_log(ASSISTANTD_LOG_INFO,
                     "scaffold run loop reached TODO boundary; exiting cleanly");
      break;
    }

    if (status != ASSISTANTD_OK) {
      assistantd_log(ASSISTANTD_LOG_ERROR,
                     "supervisor loop failed: %s",
                     assistantd_status_to_string(status));
      break;
    }

    usleep(10000);
  }

  (void)assistantd_supervisor_stop(&supervisor);
  return ASSISTANTD_OK;
}
