#include "assistantd/playback.h"

#include "assistantd/utilities/logger.h"

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
/**
 * @todo Implementation Playbook — implemented.
 * @brief Stateless aplay subprocess that plays a WAV file to the configured output device.
 * @ownership Audio output and device reliability.
 * @inputs
 *   - config->audio_playback_device: ALSA device string
 *     (e.g. "bluealsa:DEV=XX:XX:XX:XX:XX:XX,PROFILE=a2dp", "default").
 *   - wav_path: absolute path to a WAV file produced by the TTS adapter.
 * @outputs
 *   - ASSISTANTD_OK on clean aplay exit (status 0).
 *   - ASSISTANTD_ERR_CHILD_PROCESS on non-zero exit, exec failure, or timeout.
 * @state
 *   - Stateless per-utterance call; no internal state is modified.
 * @concurrency
 *   - The supervisor must ensure only one call is in flight at a time. If multi-turn
 *     queueing is ever introduced, a playback lock belongs in the supervisor, not here.
 * @errors
 *   - Non-zero aplay exit (missing file, device busy, BT disconnect): logged with device
 *     and file path for actionable debugging, then ASSISTANTD_ERR_CHILD_PROCESS returned.
 *   - Timeout (PLAYBACK_TIMEOUT_MS exceeded): SIGTERM sent, child reaped, error returned.
 *     Retry policy is the supervisor's concern (Phase 4).
 * @child_process_contracts
 *   - aplay args: -D <device> <wav_path>; stderr inherited for journald visibility.
 */

// SECTION: helpers

#define PLAYBACK_TIMEOUT_MS 30000

/**
 * Poll for child exit in 50 ms increments up to timeout_ms.
 * Returns ASSISTANTD_OK on clean exit (status 0), ASSISTANTD_ERR_CHILD_PROCESS otherwise.
 * On timeout the child is still alive; caller is responsible for cleanup.
 */
static assistantd_status_t playback_wait(pid_t pid, int timeout_ms) {
  int status;
  const int interval_ms = 50;

  for (int elapsed = 0; elapsed < timeout_ms; elapsed += interval_ms) {
    pid_t result = waitpid(pid, &status, WNOHANG);
    if (result == pid) {
      return (WIFEXITED(status) && WEXITSTATUS(status) == 0)
                 ? ASSISTANTD_OK
                 : ASSISTANTD_ERR_CHILD_PROCESS;
    }
    if (result < 0) return (errno == ECHILD) ? ASSISTANTD_OK : ASSISTANTD_ERR_CHILD_PROCESS;

    struct timespec ts = {0, (long)interval_ms * 1000000L};
    nanosleep(&ts, NULL);
  }
  return ASSISTANTD_ERR_CHILD_PROCESS;
}

// SECTION: implementation

/**
 * Spawn aplay to play wav_path on config->audio_playback_device and wait for completion.
 * The call is blocking (up to PLAYBACK_TIMEOUT_MS). On failure the reason is logged with
 * enough context (device, file) for the supervisor to make a retry decision.
 */
assistantd_status_t assistantd_playback_play_wav(
    const assistantd_config_t *config,
    const char *wav_path) {
  if (config == NULL || wav_path == NULL) return ASSISTANTD_ERR_INVALID_ARGUMENT;

  const char *args[] = {
    "aplay", "-D", config->audio_playback_device, wav_path, NULL
  };

  pid_t pid = fork();
  if (pid < 0) {
    assistantd_log(ASSISTANTD_LOG_ERROR, "playback: fork() failed: %s", strerror(errno));
    return ASSISTANTD_ERR_CHILD_PROCESS;
  }

  if (pid == 0) {
    execvp("aplay", (char *const *)args);
    _exit(1);
  }

  assistantd_status_t result = playback_wait(pid, PLAYBACK_TIMEOUT_MS);
  if (result != ASSISTANTD_OK) {
    if (kill(pid, SIGTERM) == 0) {
      /* Timed out — child was still alive; reap after SIGTERM. */
      int st;
      waitpid(pid, &st, 0);
      assistantd_log(ASSISTANTD_LOG_ERROR,
                     "playback: aplay timed out after %ds; killed (device=%s file=%s)",
                     PLAYBACK_TIMEOUT_MS / 1000, config->audio_playback_device, wav_path);
    } else {
      /* Child already exited with non-zero status (reaped in wait loop). */
      assistantd_log(ASSISTANTD_LOG_ERROR,
                     "playback: aplay exited with error (device=%s file=%s)",
                     config->audio_playback_device, wav_path);
    }
    return ASSISTANTD_ERR_CHILD_PROCESS;
  }

  return ASSISTANTD_OK;
}
