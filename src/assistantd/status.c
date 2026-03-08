#include "assistantd/status.h"

const char *assistantd_status_to_string(assistantd_status_t status) {
  switch (status) {
    case ASSISTANTD_OK:
      return "ok";
    case ASSISTANTD_ERR_INVALID_ARGUMENT:
      return "invalid_argument";
    case ASSISTANTD_ERR_CONFIG:
      return "config_error";
    case ASSISTANTD_ERR_IO:
      return "io_error";
    case ASSISTANTD_ERR_CHILD_PROCESS:
      return "child_process_error";
    case ASSISTANTD_ERR_UNIMPLEMENTED:
      return "unimplemented";
    default:
      return "unknown";
  }
}
