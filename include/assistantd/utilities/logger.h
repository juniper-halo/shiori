#ifndef ASSISTANTD_UTILITIES_LOGGER_H
#define ASSISTANTD_UTILITIES_LOGGER_H

#include <stdarg.h>

typedef enum {
  ASSISTANTD_LOG_DEBUG = 0,
  ASSISTANTD_LOG_INFO = 1,
  ASSISTANTD_LOG_WARN = 2,
  ASSISTANTD_LOG_ERROR = 3
} assistantd_log_level_t;

void assistantd_log_set_level(assistantd_log_level_t level);
void assistantd_log(assistantd_log_level_t level, const char *fmt, ...);
void assistantd_vlog(assistantd_log_level_t level, const char *fmt, va_list args);

#endif  // ASSISTANTD_UTILITIES_LOGGER_H
