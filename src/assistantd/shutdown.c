#define _POSIX_C_SOURCE 200809L

#include "assistantd/shutdown.h"

#include <stddef.h>
#include <signal.h>

/**
 * @todo Implementation Playbook
 * @brief Harden signal handling and shutdown coordination for multi-threaded daemon runtime.
 * @ownership Runtime lifecycle and resilience.
 * @inputs
 *   - SIGINT/SIGTERM from systemd or operator.
 * @outputs
 *   - Atomic shutdown flag observed by supervisor and worker loops.
 * @state
 *   - Running -> shutdown_requested -> stopping -> stopped.
 * @concurrency
 *   - Signal handler must only perform async-signal-safe operations.
 * @errors
 *   - Surface handler-install failures with immediate startup abort.
 * @child_process_contracts
 *   - Shutdown sequence must terminate child processes in bounded time.
 * @acceptance
 *   - Clean service stop with no zombies and no deadlocks.
 */
static volatile sig_atomic_t g_shutdown_requested = 0;

static void assistantd_on_signal(int signal_number) {
  (void)signal_number;
  g_shutdown_requested = 1;
}

assistantd_status_t assistantd_shutdown_install_handlers(void) {
  struct sigaction action;
  action.sa_handler = assistantd_on_signal;
  sigemptyset(&action.sa_mask);
  action.sa_flags = 0;

  if (sigaction(SIGINT, &action, NULL) != 0) {
    return ASSISTANTD_ERR_IO;
  }
  if (sigaction(SIGTERM, &action, NULL) != 0) {
    return ASSISTANTD_ERR_IO;
  }

  return ASSISTANTD_OK;
}

void assistantd_shutdown_request(void) { g_shutdown_requested = 1; }

int assistantd_shutdown_requested(void) { return g_shutdown_requested ? 1 : 0; }
