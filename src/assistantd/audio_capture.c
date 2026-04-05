#define _POSIX_C_SOURCE 200809L

#include "assistantd/audio_capture.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "assistantd/utilities/logger.h"

#define ASSISTANTD_ARECORD_PATH "/usr/bin/arecord"
#define ASSISTANTD_CAPTURE_SAMPLE_RATE "16000"
#define ASSISTANTD_CAPTURE_CHANNELS "1"
#define ASSISTANTD_CAPTURE_TERM_WAIT_MS 1000
#define ASSISTANTD_CAPTURE_TERM_POLL_MS 20
#define ASSISTANTD_CAPTURE_NETWORK_LISTEN_BACKLOG 1

/**
 * @todo Implementation Playbook
 * @brief Managed capture backends for `arecord` and loopback TCP PCM streaming.
 * @ownership Audio ingestion and process supervision.
 * @inputs
 *   - Capture settings from config: arecord device or network TCP port.
 * @outputs
 *   - Framed PCM bytes to ring buffer writer.
 * @state
 *   - INACTIVE -> STARTING -> ACTIVE -> STOPPING -> INACTIVE for either backend.
 * @concurrency
 *   - One reader thread pulls child stdout; lifecycle controlled by supervisor thread.
 * @errors
 *   - Detect early child exit, broken pipe, and short reads.
 *   - Fail fast on spawn/read failures and report status through `assistantd_status_t`.
 * @child_process_contracts
 *   - Child must emit 16-bit mono PCM at agreed sample rate.
 *   - TCP mode binds only to 127.0.0.1 and emits fixed 20ms frames (640 bytes).
 *   - Stop path sends SIGTERM then SIGKILL timeout fallback for arecord backend.
 * @acceptance
 *   - Stream starts/stops cleanly with no zombie processes.
 *   - TCP mode tolerates disconnect/reconnect and resumes framing without restart.
 *   - Supervisor can recover from child crash deterministically.
 * @remaining
 *   - Configurable capture format/rate and richer diagnostics can be added later.
 */

static int assistantd_capture_set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0) {
    return -1;
  }
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void assistantd_capture_close_fd(int *fd) {
  if (fd == NULL || *fd < 0) {
    return;
  }

  (void)close(*fd);
  *fd = -1;
}

static void assistantd_capture_close_network_client(assistantd_audio_capture_t *capture) {
  if (capture == NULL) {
    return;
  }

  assistantd_capture_close_fd(&capture->client_fd);
  capture->frame_staging_size = 0;
}

static void assistantd_capture_reset_state(assistantd_audio_capture_t *capture) {
  if (capture == NULL) {
    return;
  }

  capture->backend = ASSISTANTD_AUDIO_CAPTURE_BACKEND_ARECORD;
  capture->active = false;
  capture->child_pid = -1;
  capture->stdout_fd = -1;
  capture->listener_fd = -1;
  capture->client_fd = -1;
  capture->frame_staging_size = 0;
}

static void assistantd_capture_shutdown_resources(assistantd_audio_capture_t *capture) {
  if (capture == NULL) {
    return;
  }

  assistantd_capture_close_fd(&capture->stdout_fd);
  assistantd_capture_close_network_client(capture);
  assistantd_capture_close_fd(&capture->listener_fd);
  capture->child_pid = -1;
  capture->active = false;
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
  if (capture == NULL || capture->backend != ASSISTANTD_AUDIO_CAPTURE_BACKEND_ARECORD ||
      capture->child_pid <= 0) {
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
  assistantd_capture_close_fd(&capture->stdout_fd);
  return ASSISTANTD_ERR_CHILD_PROCESS;
}

static assistantd_status_t assistantd_capture_start_arecord(
    assistantd_audio_capture_t *capture, const assistantd_config_t *config) {
  if (capture == NULL || config == NULL) {
    return ASSISTANTD_ERR_INVALID_ARGUMENT;
  }
  if (config->audio_device[0] == '\0') {
    return ASSISTANTD_ERR_CONFIG;
  }

  capture->backend = ASSISTANTD_AUDIO_CAPTURE_BACKEND_ARECORD;

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
          config->audio_device,
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
  if (assistantd_capture_set_nonblocking(pipe_fds[0]) != 0) {
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

static assistantd_status_t assistantd_capture_start_network_tcp(
    assistantd_audio_capture_t *capture,
    const assistantd_config_t *config) {
  if (capture == NULL || config == NULL) {
    return ASSISTANTD_ERR_INVALID_ARGUMENT;
  }
  if (config->audio_network_port <= 0 || config->audio_network_port > 65535) {
    return ASSISTANTD_ERR_CONFIG;
  }

  capture->backend = ASSISTANTD_AUDIO_CAPTURE_BACKEND_NETWORK_TCP;
  capture->frame_staging_size = 0;

  int listener_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (listener_fd < 0) {
    return ASSISTANTD_ERR_IO;
  }

  int reuse_addr = 1;
  (void)setsockopt(listener_fd, SOL_SOCKET, SO_REUSEADDR, &reuse_addr, sizeof(reuse_addr));

  struct sockaddr_in address;
  memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_port = htons((uint16_t)config->audio_network_port);
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  if (bind(listener_fd, (const struct sockaddr *)&address, sizeof(address)) != 0 ||
      listen(listener_fd, ASSISTANTD_CAPTURE_NETWORK_LISTEN_BACKLOG) != 0 ||
      assistantd_capture_set_nonblocking(listener_fd) != 0) {
    (void)close(listener_fd);
    return ASSISTANTD_ERR_IO;
  }

  capture->listener_fd = listener_fd;
  capture->client_fd = -1;
  capture->active = true;
  assistantd_log(ASSISTANTD_LOG_INFO,
                 "audio capture network_tcp ready on 127.0.0.1:%d",
                 config->audio_network_port);
  return ASSISTANTD_OK;
}

assistantd_status_t assistantd_audio_capture_start(
    assistantd_audio_capture_t *capture,
    const assistantd_config_t *config) {
  if (capture == NULL || config == NULL) {
    return ASSISTANTD_ERR_INVALID_ARGUMENT;
  }
  if (capture->active) {
    return ASSISTANTD_OK;
  }

  assistantd_capture_reset_state(capture);

  if (strcmp(config->audio_input_mode, "network_tcp") == 0) {
    return assistantd_capture_start_network_tcp(capture, config);
  }

  return assistantd_capture_start_arecord(capture, config);
}

static assistantd_status_t assistantd_capture_accept_network_client(
    assistantd_audio_capture_t *capture) {
  if (capture == NULL || capture->listener_fd < 0) {
    return ASSISTANTD_ERR_CHILD_PROCESS;
  }
  if (capture->client_fd >= 0) {
    return ASSISTANTD_OK;
  }

  int client_fd = accept(capture->listener_fd, NULL, NULL);
  if (client_fd < 0) {
    if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
      return ASSISTANTD_OK;
    }
    return ASSISTANTD_ERR_IO;
  }

  if (assistantd_capture_set_nonblocking(client_fd) != 0) {
    (void)close(client_fd);
    return ASSISTANTD_ERR_IO;
  }

  capture->client_fd = client_fd;
  capture->frame_staging_size = 0;
  assistantd_log(ASSISTANTD_LOG_INFO, "audio capture network_tcp client connected");
  return ASSISTANTD_OK;
}

static assistantd_status_t assistantd_capture_read_network_frame(
    assistantd_audio_capture_t *capture,
    uint8_t *buffer,
    size_t capacity,
    size_t *bytes_read) {
  if (capture == NULL || buffer == NULL || capacity < ASSISTANTD_AUDIO_CAPTURE_FRAME_BYTES) {
    return ASSISTANTD_ERR_INVALID_ARGUMENT;
  }
  if (!capture->active || capture->listener_fd < 0) {
    return ASSISTANTD_ERR_CHILD_PROCESS;
  }

  assistantd_status_t accept_status = assistantd_capture_accept_network_client(capture);
  if (accept_status != ASSISTANTD_OK) {
    return accept_status;
  }
  if (capture->client_fd < 0) {
    return ASSISTANTD_OK;
  }

  while (capture->frame_staging_size < ASSISTANTD_AUDIO_CAPTURE_FRAME_BYTES) {
    ssize_t n = read(capture->client_fd,
                     capture->frame_staging + capture->frame_staging_size,
                     ASSISTANTD_AUDIO_CAPTURE_FRAME_BYTES - capture->frame_staging_size);
    if (n > 0) {
      capture->frame_staging_size += (size_t)n;
      continue;
    }

    if (n == 0) {
      assistantd_log(ASSISTANTD_LOG_INFO, "audio capture network_tcp client disconnected");
      assistantd_capture_close_network_client(capture);
      return ASSISTANTD_OK;
    }

    if (errno == EINTR) {
      continue;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      break;
    }
    if (errno == ECONNRESET || errno == ENOTCONN || errno == EPIPE) {
      assistantd_log(ASSISTANTD_LOG_WARN, "audio capture network_tcp client dropped");
      assistantd_capture_close_network_client(capture);
      return ASSISTANTD_OK;
    }
    return ASSISTANTD_ERR_IO;
  }

  if (capture->frame_staging_size == ASSISTANTD_AUDIO_CAPTURE_FRAME_BYTES) {
    memcpy(buffer, capture->frame_staging, ASSISTANTD_AUDIO_CAPTURE_FRAME_BYTES);
    capture->frame_staging_size = 0;
    if (bytes_read != NULL) {
      *bytes_read = ASSISTANTD_AUDIO_CAPTURE_FRAME_BYTES;
    }
  }

  return ASSISTANTD_OK;
}

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
  if (capture->backend == ASSISTANTD_AUDIO_CAPTURE_BACKEND_NETWORK_TCP) {
    return assistantd_capture_read_network_frame(capture, buffer, capacity, bytes_read);
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

assistantd_status_t assistantd_audio_capture_stop(assistantd_audio_capture_t *capture) {
  if (capture == NULL) {
    return ASSISTANTD_ERR_INVALID_ARGUMENT;
  }

  assistantd_status_t status = ASSISTANTD_OK;
  pid_t child_pid = capture->child_pid;
  assistantd_capture_shutdown_resources(capture);

  if (capture->backend == ASSISTANTD_AUDIO_CAPTURE_BACKEND_ARECORD && child_pid > 0) {
    if (kill(child_pid, SIGTERM) != 0 && errno != ESRCH) {
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

  assistantd_capture_reset_state(capture);
  return status;
}
