#define _POSIX_C_SOURCE 200809L

#include "assistantd/audio_capture.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "assistantd/utilities/logger.h"

#define ASSISTANTD_ARECORD_PATH "/usr/bin/arecord"
#define ASSISTANTD_CAPTURE_SAMPLE_RATE "16000"
#define ASSISTANTD_CAPTURE_CHANNELS "1"
#define ASSISTANTD_CAPTURE_TERM_WAIT_MS 1000
#define ASSISTANTD_CAPTURE_TERM_POLL_MS 20

/**
 * @todo Implementation Playbook
 * @brief Managed capture process around `arecord` streaming raw PCM frames.
 * @ownership Audio ingestion and process supervision.
 * @inputs
 *   - config->audio_capture_device: ALSA device string (e.g. "hw:1,0", "default").
 * @outputs
 *   - Raw S16_LE PCM bytes readable from capture->stdout_fd for the ring buffer writer.
 * @state
 *   - capture->active tracks ACTIVE vs INACTIVE. Formal state machine transitions
 *     (STARTING, STOPPING) are a Phase 4 supervisor concern.
 * @concurrency
 *   - One reader thread calls audio_capture_read(); lifecycle owned by supervisor thread.
 *   - stdout_fd is read-only after start() returns; no per-read locking needed.
 * @errors
 *   - Detect early child exit, broken pipe, and short reads.
 *   - Fail fast on spawn/read failures and report status through `assistantd_status_t`.
 * @child_process_contracts
 *   - Child must emit 16-bit mono PCM at agreed sample rate.
 *   - Stop path sends SIGTERM then SIGKILL timeout fallback.
 * @acceptance
 *   - Stream starts/stops cleanly with no zombie processes.
 *   - Supervisor can recover from child crash deterministically.
 * @remaining
 *   - Configurable capture format/rate and richer diagnostics can be added later.
 */

static void assistantd_capture_reset(assistantd_audio_capture_t *capture) {
  if (capture == NULL) {
    return;
  }

  capture->active = false;
  capture->child_pid = -1;
  capture->stdout_fd = -1;
}

static void assistantd_capture_close_fd(assistantd_audio_capture_t *capture) {
  if (capture == NULL || capture->stdout_fd < 0) {
    return;
  }

  (void)close(capture->stdout_fd);
  capture->stdout_fd = -1;
}

static bool assistantd_capture_wait_for_exit(pid_t pid, int timeout_ms) {
  if (pid <= 0) {
    return true;
  }

  int waited_ms = 0;
  while (waited_ms < timeout_ms) {
    int status = 0;
    pid_t waited = waitpid(pid, &status, WNOHANG);
    if (waited == pid) {
      return true;
    }
    if (waited == -1 && errno == ECHILD) {
      return true;
    }
    if (waited == -1) {
      return false;
    }

    struct timespec delay = {
        .tv_sec = 0,
        .tv_nsec = ASSISTANTD_CAPTURE_TERM_POLL_MS * 1000 * 1000,
    };
    (void)nanosleep(&delay, NULL);
    waited_ms += ASSISTANTD_CAPTURE_TERM_POLL_MS;
  }

  return false;
}

static assistantd_status_t assistantd_capture_reap_child(assistantd_audio_capture_t *capture) {
  if (capture == NULL || capture->child_pid <= 0) {
    return ASSISTANTD_OK;
  }

  int status = 0;
  pid_t waited = waitpid(capture->child_pid, &status, WNOHANG);
  if (waited == 0) {
    return ASSISTANTD_OK;
  }
  if (waited == -1 && errno != ECHILD) {
    return ASSISTANTD_ERR_CHILD_PROCESS;
  }

  capture->child_pid = -1;
  capture->active = false;
  assistantd_capture_close_fd(capture);
  return ASSISTANTD_ERR_CHILD_PROCESS;
}

assistantd_status_t assistantd_audio_capture_start(
    assistantd_audio_capture_t *capture,
    const assistantd_config_t *config) {
  if (capture == NULL || config == NULL) {
    return ASSISTANTD_ERR_INVALID_ARGUMENT;
  }
  if (config->audio_capture_device[0] == '\0') {
    return ASSISTANTD_ERR_CONFIG;
  }
  if (capture->active) {
    return ASSISTANTD_OK;
  }

  assistantd_capture_reset(capture);

  if (access(ASSISTANTD_ARECORD_PATH, X_OK) != 0) {
    assistantd_log(ASSISTANTD_LOG_WARN,
                   "audio capture unavailable: `%s` not found; keeping scaffold boundary",
                   ASSISTANTD_ARECORD_PATH);
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

    execl(ASSISTANTD_ARECORD_PATH,
          "arecord",
          "-D",
          config->audio_capture_device,
          "-f",
          "S16_LE",
          "-c",
          ASSISTANTD_CAPTURE_CHANNELS,
          "-r",
          ASSISTANTD_CAPTURE_SAMPLE_RATE,
          "-t",
          "raw",
          (char *)NULL);
    _exit(127);
  }

  (void)close(pipe_fds[1]);
  int flags = fcntl(pipe_fds[0], F_GETFL, 0);
  if (flags < 0 || fcntl(pipe_fds[0], F_SETFL, flags | O_NONBLOCK) < 0) {
    (void)kill(child_pid, SIGTERM);
    (void)waitpid(child_pid, NULL, 0);
    (void)close(pipe_fds[0]);
    return ASSISTANTD_ERR_IO;
  }

  capture->child_pid = child_pid;
  capture->stdout_fd = pipe_fds[0];
  capture->active = true;
  return ASSISTANTD_OK;
}

/**
 * Read a chunk of raw PCM bytes from the arecord pipe.
 * EINTR/EAGAIN → returns OK with bytes_read=0 so the caller retries.
 * EOF          → returns ASSISTANTD_ERR_CHILD_PROCESS; caller must invoke stop().
 */
assistantd_status_t assistantd_audio_capture_read(
    assistantd_audio_capture_t *capture,
    uint8_t *buffer,
    size_t capacity,
    size_t *bytes_read) {
  if (bytes_read != NULL) {
    *bytes_read = 0;
  }
  if (capture == NULL || buffer == NULL || capacity == 0) {
    return ASSISTANTD_ERR_INVALID_ARGUMENT;
  }
  if (!capture->active || capture->stdout_fd < 0 || capture->child_pid <= 0) {
    return ASSISTANTD_ERR_CHILD_PROCESS;
  }

  ssize_t read_count = read(capture->stdout_fd, buffer, capacity);
  if (read_count > 0) {
    if (bytes_read != NULL) {
      *bytes_read = (size_t)read_count;
    }
    return ASSISTANTD_OK;
  }

  if (read_count == 0) {
    (void)assistantd_capture_reap_child(capture);
    return ASSISTANTD_ERR_CHILD_PROCESS;
  }

  if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
    assistantd_status_t child_status = assistantd_capture_reap_child(capture);
    if (child_status != ASSISTANTD_OK) {
      return child_status;
    }
    return ASSISTANTD_OK;
  }

  return ASSISTANTD_ERR_IO;
}

/**
 * Terminate arecord cleanly: SIGTERM → timeout poll → SIGKILL → reap.
 * Always resets capture state so the struct can be reused.
 * Idempotent: safe to call on an already-inactive capture.
 */
assistantd_status_t assistantd_audio_capture_stop(assistantd_audio_capture_t *capture) {
  if (capture == NULL) {
    return ASSISTANTD_ERR_INVALID_ARGUMENT;
  }
  if (!capture->active) {
    return ASSISTANTD_OK;
  }

  assistantd_status_t status = ASSISTANTD_OK;
  pid_t child_pid = capture->child_pid;
  assistantd_capture_close_fd(capture);

  if (child_pid > 0) {
    if (kill(child_pid, SIGTERM) != 0 && errno != ESRCH) {
      assistantd_log(ASSISTANTD_LOG_WARN,
                     "audio_capture: kill(SIGTERM,%d) failed: %s",
                     child_pid,
                     strerror(errno));
      status = ASSISTANTD_ERR_CHILD_PROCESS;
    }

    bool exited = assistantd_capture_wait_for_exit(child_pid, ASSISTANTD_CAPTURE_TERM_WAIT_MS);
    if (!exited) {
      if (kill(child_pid, SIGKILL) != 0 && errno != ESRCH) {
        status = ASSISTANTD_ERR_CHILD_PROCESS;
      }
      (void)waitpid(child_pid, NULL, 0);
    }
  }

  capture->active = false;
  capture->child_pid = -1;
  return status;
}
