#include "assistantd/playback.h"

#include "assistantd/logger.h"

/**
 * @todo Implementation Playbook
 * @brief Implement playback subprocess integration (aplay/sox) with deterministic errors.
 * @ownership Audio output and device reliability.
 * @inputs
 *   - Configured audio device and synthesized WAV path.
 * @outputs
 *   - Audible playback completion event or explicit failure.
 * @state
 *   - Stateless call per utterance with strict timeout handling.
 * @concurrency
 *   - Ensure playback lock if future multi-turn queueing is introduced.
 * @errors
 *   - Handle missing file, device busy, subprocess timeout, and unsupported format.
 * @child_process_contracts
 *   - Playback command/flags are fixed and logged for reproducibility.
 * @acceptance
 *   - Playback succeeds on target Pi baseline and fails with actionable logs otherwise.
 */

assistantd_status_t assistantd_playback_play_wav(
    const assistantd_config_t *config,
    const char *wav_path) {
  (void)config;
  (void)wav_path;

  assistantd_log(ASSISTANTD_LOG_WARN, "playback scaffold: play_wav() is not implemented");
  return ASSISTANTD_ERR_UNIMPLEMENTED;
}
