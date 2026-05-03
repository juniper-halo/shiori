#include "assistantd/audio_capture.h"

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>

#include "assistantd/logger.h"

/**
 * @todo Implementation Playbook — Phase 2 implemented; Phase 4 wiring remains.
 * @brief Managed arecord child process producing 16-bit mono PCM at 16 kHz.
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
 *   - start() fails fast if pipe() or fork() fails; exec failure is detected by the
 *     caller on the first read() (EOF when arecord is absent or the device is invalid).
 *   - read() distinguishes EOF (ASSISTANTD_ERR_CHILD_PROCESS), EINTR/EAGAIN (return OK
 *     with bytes_read=0 for caller retry), and hard I/O errors (ASSISTANTD_ERR_IO).
 *   - stop() sends SIGTERM, polls up to 2 s, escalates to SIGKILL, then reaps the child
 *     unconditionally to prevent zombies.
 * @child_process_contracts
 *   - arecord args: -D <device> -f S16_LE -r 16000 -c 1 -t raw
 *   - stdout is the raw PCM stream; stderr is inherited for journald visibility.
 * @remaining
 *   - Ring buffer wiring belongs in assistantd_supervisor_run_once (Phase 4).
 */

// SECTION: helpers

/**
 * Poll for child exit in 50 ms increments up to timeout_ms.
 * Returns ASSISTANTD_OK once the child is reaped, ASSISTANTD_ERR_CHILD_PROCESS on timeout.
 */
static assistantd_status_t wait_for_child(pid_t pid, int timeout_ms) {
  int status;
  const int interval_ms = 50;

  for (int elapsed = 0; elapsed < timeout_ms; elapsed += interval_ms) {
    pid_t result = waitpid(pid, &status, WNOHANG);
    if (result == pid) return ASSISTANTD_OK;
    if (result < 0) return (errno == ECHILD) ? ASSISTANTD_OK : ASSISTANTD_ERR_CHILD_PROCESS;

    struct timespec ts = {0, (long)interval_ms * 1000000L};
    nanosleep(&ts, NULL);
  }
  return ASSISTANTD_ERR_CHILD_PROCESS;
}

// SECTION: implementation

/**
 * Fork arecord with a pipe on stdout. On return, capture->stdout_fd carries the PCM
 * read-end. Exec failure is silent here; callers detect it via the first read() EOF.
 */
assistantd_status_t assistantd_audio_capture_start(
    assistantd_audio_capture_t *capture,
    const assistantd_config_t *config) {
  if (capture == NULL || config == NULL) return ASSISTANTD_ERR_INVALID_ARGUMENT;
  if (capture->active) return ASSISTANTD_ERR_INVALID_ARGUMENT;

  int pipefd[2];
  if (pipe(pipefd) < 0) {
    assistantd_log(ASSISTANTD_LOG_ERROR, "audio_capture: pipe() failed: %s", strerror(errno));
    return ASSISTANTD_ERR_IO;
  }

  const char *args[] = {
    "arecord", "-D", config->audio_capture_device,
    "-f", "S16_LE", "-r", "16000", "-c", "1", "-t", "raw", NULL
  };

  pid_t pid = fork();
  if (pid < 0) {
    close(pipefd[0]);
    close(pipefd[1]);
    assistantd_log(ASSISTANTD_LOG_ERROR, "audio_capture: fork() failed: %s", strerror(errno));
    return ASSISTANTD_ERR_CHILD_PROCESS;
  }

  if (pid == 0) {
    close(pipefd[0]);
    if (dup2(pipefd[1], STDOUT_FILENO) < 0) _exit(1);
    close(pipefd[1]);
    execvp("arecord", (char *const *)args);
    _exit(1);
  }

  close(pipefd[1]);
  capture->stdout_fd = pipefd[0];
  capture->child_pid = pid;
  capture->active = true;
  assistantd_log(ASSISTANTD_LOG_INFO, "audio_capture: arecord pid=%d device=%s",
                 pid, config->audio_capture_device);
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
  if (capture == NULL || buffer == NULL || capacity == 0 || bytes_read == NULL)
    return ASSISTANTD_ERR_INVALID_ARGUMENT;
  if (!capture->active) return ASSISTANTD_ERR_IO;

  *bytes_read = 0;
  ssize_t n = read(capture->stdout_fd, buffer, capacity);
  if (n == 0) {
    assistantd_log(ASSISTANTD_LOG_WARN, "audio_capture: EOF on pipe; child exited");
    return ASSISTANTD_ERR_CHILD_PROCESS;
  }
  if (n < 0) {
    if (errno == EINTR || errno == EAGAIN) return ASSISTANTD_OK;
    assistantd_log(ASSISTANTD_LOG_ERROR, "audio_capture: read() failed: %s", strerror(errno));
    return ASSISTANTD_ERR_IO;
  }
  *bytes_read = (size_t)n;
  return ASSISTANTD_OK;
}

/**
 * Terminate arecord cleanly: SIGTERM → 2 s poll → SIGKILL → reap.
 * Always resets capture state so the struct can be reused.
 * Idempotent: safe to call on an already-inactive capture.
 */
assistantd_status_t assistantd_audio_capture_stop(assistantd_audio_capture_t *capture) {
  if (capture == NULL) return ASSISTANTD_ERR_INVALID_ARGUMENT;
  if (!capture->active) return ASSISTANTD_OK;

  if (kill(capture->child_pid, SIGTERM) < 0 && errno != ESRCH) {
    assistantd_log(ASSISTANTD_LOG_WARN, "audio_capture: kill(SIGTERM,%d) failed: %s",
                   capture->child_pid, strerror(errno));
  }

  if (wait_for_child(capture->child_pid, 2000) != ASSISTANTD_OK) {
    assistantd_log(ASSISTANTD_LOG_WARN, "audio_capture: SIGTERM timeout; escalating to SIGKILL pid=%d",
                   capture->child_pid);
    kill(capture->child_pid, SIGKILL);
    int status;
    waitpid(capture->child_pid, &status, 0);
  }

  close(capture->stdout_fd);
  capture->stdout_fd = -1;
  capture->child_pid = -1;
  capture->active = false;
  return ASSISTANTD_OK;
}
