#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "assistantd/audio_capture.h"
#include "assistantd/config.h"

static int choose_test_port(void) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    return -1;
  }

  struct sockaddr_in address;
  memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_port = htons(0);
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (bind(fd, (const struct sockaddr *)&address, sizeof(address)) != 0) {
    (void)close(fd);
    return -1;
  }

  socklen_t address_len = (socklen_t)sizeof(address);
  if (getsockname(fd, (struct sockaddr *)&address, &address_len) != 0 ||
      ntohs(address.sin_port) == 0) {
    (void)close(fd);
    return -1;
  }

  int port = (int)ntohs(address.sin_port);
  (void)close(fd);
  return port;
}

static void short_sleep(void) {
  struct timespec delay = {
      .tv_sec = 0,
      .tv_nsec = 10 * 1000 * 1000,
  };
  (void)nanosleep(&delay, NULL);
}

static int connect_loopback_retry(int port) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    return -1;
  }

  struct sockaddr_in address;
  memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_port = htons((uint16_t)port);
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  for (int attempt = 0; attempt < 100; attempt++) {
    if (connect(fd, (const struct sockaddr *)&address, sizeof(address)) == 0) {
      return fd;
    }
    if (errno != ECONNREFUSED && errno != EINTR) {
      break;
    }
    short_sleep();
  }

  (void)close(fd);
  return -1;
}

static void send_all(int fd, const uint8_t *data, size_t length) {
  size_t sent_total = 0;
  while (sent_total < length) {
    ssize_t sent = send(fd, data + sent_total, length - sent_total, 0);
    assert(sent > 0);
    sent_total += (size_t)sent;
  }
}

static size_t read_with_retries(
    assistantd_audio_capture_t *capture,
    uint8_t *buffer,
    size_t capacity,
    int attempts) {
  for (int attempt = 0; attempt < attempts; attempt++) {
    size_t bytes_read = 0;
    assistantd_status_t status =
        assistantd_audio_capture_read(capture, buffer, capacity, &bytes_read);
    assert(status == ASSISTANTD_OK);
    if (bytes_read > 0) {
      return bytes_read;
    }
    short_sleep();
  }
  return 0;
}

int main(void) {
  assistantd_config_t config;
  assistantd_status_t status = assistantd_config_init_defaults(&config);
  assert(status == ASSISTANTD_OK);

  assert(assistantd_audio_capture_start(NULL, &config) == ASSISTANTD_ERR_INVALID_ARGUMENT);
  assert(assistantd_audio_capture_start(NULL, NULL) == ASSISTANTD_ERR_INVALID_ARGUMENT);

  assistantd_audio_capture_t capture;
  memset(&capture, 0, sizeof(capture));
  capture.child_pid = -1;
  capture.stdout_fd = -1;

  uint8_t byte = 0;
  size_t bytes_read = 0;
  assert(assistantd_audio_capture_read(NULL, &byte, 1, &bytes_read) == ASSISTANTD_ERR_INVALID_ARGUMENT);
  assert(assistantd_audio_capture_read(&capture, NULL, 1, &bytes_read) == ASSISTANTD_ERR_INVALID_ARGUMENT);
  assert(assistantd_audio_capture_read(&capture, &byte, 0, &bytes_read) == ASSISTANTD_ERR_INVALID_ARGUMENT);

  assert(assistantd_audio_capture_stop(NULL) == ASSISTANTD_ERR_INVALID_ARGUMENT);

  status = assistantd_config_init_defaults(&config);
  assert(status == ASSISTANTD_OK);
  snprintf(config.audio_input_mode, sizeof(config.audio_input_mode), "%s", "network_tcp");
  config.audio_network_port = choose_test_port();

  if (config.audio_network_port > 0) {
    status = assistantd_audio_capture_start(&capture, &config);
    assert(status == ASSISTANTD_OK);

    uint8_t frame[ASSISTANTD_AUDIO_CAPTURE_FRAME_BYTES];
    memset(frame, 0, sizeof(frame));
    bytes_read = 0;
    assert(assistantd_audio_capture_read(&capture, frame, sizeof(frame), &bytes_read) ==
           ASSISTANTD_OK);
    assert(bytes_read == 0);

    int client_fd = connect_loopback_retry(config.audio_network_port);
    assert(client_fd >= 0);

    uint8_t first_half[ASSISTANTD_AUDIO_CAPTURE_FRAME_BYTES / 2];
    uint8_t second_half[ASSISTANTD_AUDIO_CAPTURE_FRAME_BYTES / 2];
    uint8_t expected_frame[ASSISTANTD_AUDIO_CAPTURE_FRAME_BYTES];
    for (size_t i = 0; i < sizeof(first_half); i++) {
      first_half[i] = (uint8_t)i;
      second_half[i] = (uint8_t)(i + 53);
    }
    memcpy(expected_frame, first_half, sizeof(first_half));
    memcpy(expected_frame + sizeof(first_half), second_half, sizeof(second_half));

    send_all(client_fd, first_half, sizeof(first_half));
    size_t partial_result = read_with_retries(&capture, frame, sizeof(frame), 20);
    assert(partial_result == 0);

    send_all(client_fd, second_half, sizeof(second_half));
    size_t full_frame_result = read_with_retries(&capture, frame, sizeof(frame), 50);
    assert(full_frame_result == ASSISTANTD_AUDIO_CAPTURE_FRAME_BYTES);
    assert(memcmp(frame, expected_frame, sizeof(expected_frame)) == 0);

    (void)close(client_fd);
    size_t disconnect_result = read_with_retries(&capture, frame, sizeof(frame), 20);
    assert(disconnect_result == 0);

    int second_client_fd = connect_loopback_retry(config.audio_network_port);
    assert(second_client_fd >= 0);
    uint8_t reconnect_frame[ASSISTANTD_AUDIO_CAPTURE_FRAME_BYTES];
    for (size_t i = 0; i < sizeof(reconnect_frame); i++) {
      reconnect_frame[i] = (uint8_t)(255u - (uint8_t)i);
    }
    send_all(second_client_fd, reconnect_frame, sizeof(reconnect_frame));

    size_t reconnect_result = read_with_retries(&capture, frame, sizeof(frame), 50);
    assert(reconnect_result == ASSISTANTD_AUDIO_CAPTURE_FRAME_BYTES);
    assert(memcmp(frame, reconnect_frame, sizeof(reconnect_frame)) == 0);

    (void)close(second_client_fd);
    assert(assistantd_audio_capture_stop(&capture) == ASSISTANTD_OK);
    assert(assistantd_audio_capture_stop(&capture) == ASSISTANTD_OK);
  }

  assert(assistantd_config_init_defaults(&config) == ASSISTANTD_OK);
  status = assistantd_audio_capture_start(&capture, &config);
  assert(status == ASSISTANTD_OK || status == ASSISTANTD_ERR_UNIMPLEMENTED);

  if (status == ASSISTANTD_OK) {
    uint8_t buffer[320];
    bytes_read = 0;
    assistantd_status_t read_status =
        assistantd_audio_capture_read(&capture, buffer, sizeof(buffer), &bytes_read);
    assert(read_status == ASSISTANTD_OK || read_status == ASSISTANTD_ERR_CHILD_PROCESS);
  }

  assert(assistantd_audio_capture_stop(&capture) == ASSISTANTD_OK);
  return 0;
}
