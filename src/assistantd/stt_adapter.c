#include "assistantd/stt_adapter.h"

#include <ctype.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "assistantd/utilities/logger.h"

#define ASSISTANTD_STT_OUTPUT_CAPTURE_MAX 65536

/**
 * @todo Implementation Playbook
 * @brief Implement whisper.cpp adapter with deterministic subprocess contract.
 * @ownership STT pipeline integration.
 * @inputs
 *   - Finalized utterance WAV path emitted by VAD/utterance assembler.
 * @outputs
 *   - UTF-8 transcript string for LLM stage.
 * @state
 *   - Adapter init stores executable and model path, transcribe is stateless per request.
 * @concurrency
 *   - Define whether multiple transcriptions can run concurrently or are serialized by supervisor.
 * @errors
 *   - Map subprocess exit codes/timeouts to status enum and logs.
 *   - Fail fast on empty transcript when whisper stdout indicates runtime error.
 * @child_process_contracts
 *   - Child stdin/stdout protocol and command-line flags are version-pinned in docs.
 * @acceptance
 *   - Known utterance fixtures produce deterministic text on pinned model.
 *   - Timeout and crash scenarios are covered with integration tests.
 */

static void assistantd_stt_trim_span(const char **start, const char **end) {
  while (*start < *end && isspace((unsigned char)**start)) {
    (*start)++;
  }
  while (*end > *start && isspace((unsigned char)*((*end) - 1))) {
    (*end)--;
  }
}

static void assistantd_stt_append_token(
    char *dest,
    size_t dest_size,
    size_t *dest_len,
    const char *token,
    size_t token_len) {
  if (dest == NULL || dest_len == NULL || token == NULL || token_len == 0 || dest_size == 0) {
    return;
  }

  size_t write_len = *dest_len;
  if (write_len >= dest_size - 1) {
    return;
  }

  if (write_len > 0) {
    dest[write_len++] = ' ';
  }

  size_t available = (dest_size - 1) - write_len;
  size_t to_copy = (token_len < available) ? token_len : available;
  memcpy(dest + write_len, token, to_copy);
  write_len += to_copy;
  dest[write_len] = '\0';
  *dest_len = write_len;
}

static void assistantd_stt_parse_output(
    const char *raw,
    char *transcript,
    size_t transcript_size) {
  if (raw == NULL || transcript == NULL || transcript_size == 0) {
    return;
  }

  transcript[0] = '\0';
  size_t transcript_len = 0;
  const char *cursor = raw;
  while (*cursor != '\0') {
    const char *line_start = cursor;
    while (*cursor != '\0' && *cursor != '\n') {
      cursor++;
    }
    const char *line_end = cursor;
    if (*cursor == '\n') {
      cursor++;
    }
    if (line_end > line_start && *(line_end - 1) == '\r') {
      line_end--;
    }

    assistantd_stt_trim_span(&line_start, &line_end);
    if (line_start >= line_end) {
      continue;
    }

    if (*line_start == '[') {
      const char *close_bracket = line_start;
      while (close_bracket < line_end && *close_bracket != ']') {
        close_bracket++;
      }
      if (close_bracket < line_end && *close_bracket == ']') {
        line_start = close_bracket + 1;
        assistantd_stt_trim_span(&line_start, &line_end);
      }
    }
    if (line_start >= line_end) {
      continue;
    }

    assistantd_stt_append_token(
        transcript,
        transcript_size,
        &transcript_len,
        line_start,
        (size_t)(line_end - line_start));
  }
}

static assistantd_status_t assistantd_stt_collect_stdout(
    int stdout_fd,
    char *capture,
    size_t capture_size) {
  if (stdout_fd < 0 || capture == NULL || capture_size == 0) {
    return ASSISTANTD_ERR_INVALID_ARGUMENT;
  }

  capture[0] = '\0';
  size_t total = 0;
  char discard[1024];
  for (;;) {
    if (total < capture_size - 1) {
      ssize_t n = read(stdout_fd, capture + total, (capture_size - 1) - total);
      if (n > 0) {
        total += (size_t)n;
        continue;
      }
      if (n == 0) {
        break;
      }
      if (errno == EINTR) {
        continue;
      }
      return ASSISTANTD_ERR_IO;
    }

    ssize_t n = read(stdout_fd, discard, sizeof(discard));
    if (n > 0) {
      continue;
    }
    if (n == 0) {
      break;
    }
    if (errno == EINTR) {
      continue;
    }
    return ASSISTANTD_ERR_IO;
  }

  capture[total] = '\0';
  return ASSISTANTD_OK;
}

assistantd_status_t assistantd_stt_init(
    assistantd_stt_adapter_t *adapter,
    const assistantd_config_t *config) {
  if (adapter == NULL || config == NULL) {
    return ASSISTANTD_ERR_INVALID_ARGUMENT;
  }
  if (config->whisper_bin[0] == '\0' || config->whisper_model_path[0] == '\0') {
    return ASSISTANTD_ERR_CONFIG;
  }

  memset(adapter, 0, sizeof(*adapter));
  snprintf(adapter->whisper_bin, sizeof(adapter->whisper_bin), "%s", config->whisper_bin);
  snprintf(adapter->whisper_model_path,
           sizeof(adapter->whisper_model_path),
           "%s",
           config->whisper_model_path);
  adapter->initialized = 1;
  return ASSISTANTD_OK;
}

assistantd_status_t assistantd_stt_transcribe(
    assistantd_stt_adapter_t *adapter,
    const assistantd_stt_request_t *request,
    assistantd_stt_result_t *result) {
  if (adapter == NULL || request == NULL || result == NULL) {
    return ASSISTANTD_ERR_INVALID_ARGUMENT;
  }
  if (!adapter->initialized || request->utterance_wav_path == NULL || request->utterance_wav_path[0] == '\0') {
    return ASSISTANTD_ERR_INVALID_ARGUMENT;
  }

  result->transcript[0] = '\0';

  if (access(adapter->whisper_bin, X_OK) != 0) {
    assistantd_log(ASSISTANTD_LOG_WARN,
                   "stt unavailable: whisper binary not executable at `%s`",
                   adapter->whisper_bin);
    return ASSISTANTD_ERR_UNIMPLEMENTED;
  }
  if (access(adapter->whisper_model_path, R_OK) != 0) {
    assistantd_log(ASSISTANTD_LOG_WARN,
                   "stt unavailable: whisper model not readable at `%s`",
                   adapter->whisper_model_path);
    return ASSISTANTD_ERR_UNIMPLEMENTED;
  }

  int pipe_fds[2] = {-1, -1};
  if (pipe(pipe_fds) != 0) {
    return ASSISTANTD_ERR_IO;
  }

  pid_t child_pid = fork();
  if (child_pid < 0) {
    (void)close(pipe_fds[0]);
    (void)close(pipe_fds[1]);
    return ASSISTANTD_ERR_CHILD_PROCESS;
  }

  if (child_pid == 0) {
    (void)close(pipe_fds[0]);
    if (dup2(pipe_fds[1], STDOUT_FILENO) < 0) {
      _exit(127);
    }
    (void)close(pipe_fds[1]);

    execl(adapter->whisper_bin,
          adapter->whisper_bin,
          "-m",
          adapter->whisper_model_path,
          "-f",
          request->utterance_wav_path,
          (char *)NULL);
    _exit(127);
  }

  (void)close(pipe_fds[1]);

  char raw_stdout[ASSISTANTD_STT_OUTPUT_CAPTURE_MAX];
  assistantd_status_t read_status =
      assistantd_stt_collect_stdout(pipe_fds[0], raw_stdout, sizeof(raw_stdout));
  (void)close(pipe_fds[0]);

  int wait_status = 0;
  if (waitpid(child_pid, &wait_status, 0) < 0) {
    return ASSISTANTD_ERR_CHILD_PROCESS;
  }
  if (read_status != ASSISTANTD_OK) {
    return read_status;
  }
  if (!WIFEXITED(wait_status) || WEXITSTATUS(wait_status) != 0) {
    assistantd_log(ASSISTANTD_LOG_ERROR,
                   "stt child failed: pid=%d status=%d",
                   (int)child_pid,
                   wait_status);
    return ASSISTANTD_ERR_CHILD_PROCESS;
  }

  assistantd_stt_parse_output(raw_stdout, result->transcript, sizeof(result->transcript));
  if (result->transcript[0] == '\0') {
    assistantd_log(ASSISTANTD_LOG_ERROR,
                   "stt produced empty transcript for `%s`",
                   request->utterance_wav_path);
    return ASSISTANTD_ERR_IO;
  }

  assistantd_log(ASSISTANTD_LOG_INFO,
                 "stt transcript produced: chars=%zu",
                 strlen(result->transcript));
  return ASSISTANTD_OK;
}

assistantd_status_t assistantd_stt_shutdown(assistantd_stt_adapter_t *adapter) {
  if (adapter == NULL) {
    return ASSISTANTD_ERR_INVALID_ARGUMENT;
  }

  memset(adapter, 0, sizeof(*adapter));
  return ASSISTANTD_OK;
}
