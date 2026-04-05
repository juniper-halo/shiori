#include "assistantd/utilities/logger.h"

#include <stdio.h>
#include <time.h>

static assistantd_log_level_t g_log_level = ASSISTANTD_LOG_INFO;

static const char *level_name(assistantd_log_level_t level) {
  switch (level) {
    case ASSISTANTD_LOG_DEBUG:
      return "DEBUG";
    case ASSISTANTD_LOG_INFO:
      return "INFO";
    case ASSISTANTD_LOG_WARN:
      return "WARN";
    case ASSISTANTD_LOG_ERROR:
      return "ERROR";
    default:
      return "UNKNOWN";
  }
}

void assistantd_log_set_level(assistantd_log_level_t level) { g_log_level = level; }

void assistantd_vlog(assistantd_log_level_t level, const char *fmt, va_list args) {
  if (level < g_log_level) {
    return;
  }

  const time_t now = time(NULL);
  struct tm tm_snapshot;
  localtime_r(&now, &tm_snapshot);

  char stamp[32];
  strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &tm_snapshot);

  fprintf(stderr, "%s [%s] ", stamp, level_name(level));
  vfprintf(stderr, fmt, args);
  fputc('\n', stderr);
}

void assistantd_log(assistantd_log_level_t level, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  assistantd_vlog(level, fmt, args);
  va_end(args);
}
