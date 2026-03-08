#ifndef ASSISTANTD_STATUS_H
#define ASSISTANTD_STATUS_H

typedef enum {
  ASSISTANTD_OK = 0,
  ASSISTANTD_ERR_INVALID_ARGUMENT = 1,
  ASSISTANTD_ERR_CONFIG = 2,
  ASSISTANTD_ERR_IO = 3,
  ASSISTANTD_ERR_CHILD_PROCESS = 4,
  ASSISTANTD_ERR_UNIMPLEMENTED = 5
} assistantd_status_t;

const char *assistantd_status_to_string(assistantd_status_t status);

#endif  // ASSISTANTD_STATUS_H
