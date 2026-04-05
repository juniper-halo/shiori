#include <assert.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "assistantd/config.h"
#include "assistantd/status.h"
#include "assistantd/supervisor.h"

/**
 * @todo Extend with child process crash, timeout, and fail-fast lifecycle integration tests.
 */

static void write_temp_file(const char *path, const char *content) {
  FILE *f = fopen(path, "w");
  assert(f != NULL);
  fputs(content, f);
  fclose(f);
}

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

int main(void) {
  const char *prompt_path = "/tmp/test_supervisor_system_prompt.txt";
  write_temp_file(prompt_path, "You are a test assistant for supervisor shape tests.");

  assistantd_supervisor_t supervisor;

  int test_port = choose_test_port();
  if (test_port > 0) {
    assistantd_config_t config;
    assistantd_status_t status = assistantd_config_init_defaults(&config);
    assert(status == ASSISTANTD_OK);
    snprintf(config.llm_system_prompt_path, sizeof(config.llm_system_prompt_path), "%s", prompt_path);
    snprintf(config.audio_input_mode, sizeof(config.audio_input_mode), "%s", "network_tcp");
    config.audio_network_port = test_port;
    snprintf(config.dev_pipeline_mode, sizeof(config.dev_pipeline_mode), "%s", "stt_only");

    status = assistantd_supervisor_init(&supervisor, &config);
    assert(status == ASSISTANTD_OK);
    assert(supervisor.state == ASSISTANTD_SUPERVISOR_READY);

    status = assistantd_supervisor_start(&supervisor);
    assert(status == ASSISTANTD_OK);
    assert(supervisor.state == ASSISTANTD_SUPERVISOR_RUNNING);
    assert(supervisor.llm.initialized == 0);
    assert(supervisor.llm.curl_handle == NULL);
    assert(supervisor.tts.initialized == 0);

    status = assistantd_supervisor_run_once(&supervisor);
    assert(status == ASSISTANTD_OK);

    status = assistantd_supervisor_stop(&supervisor);
    assert(status == ASSISTANTD_OK);
    assert(supervisor.state == ASSISTANTD_SUPERVISOR_STOPPED);
  }

  assistantd_config_t scaffold_config;
  assistantd_status_t status = assistantd_config_init_defaults(&scaffold_config);
  assert(status == ASSISTANTD_OK);
  snprintf(scaffold_config.llm_system_prompt_path,
           sizeof(scaffold_config.llm_system_prompt_path),
           "%s",
           prompt_path);

  status = assistantd_supervisor_init(&supervisor, &scaffold_config);
  assert(status == ASSISTANTD_OK);
  assert(supervisor.state == ASSISTANTD_SUPERVISOR_READY);

  status = assistantd_supervisor_start(&supervisor);
  assert(status == ASSISTANTD_OK);
  assert(supervisor.state == ASSISTANTD_SUPERVISOR_RUNNING);
  assert(supervisor.llm.initialized == 1);
  assert(supervisor.llm.curl_handle != NULL);
  assert(supervisor.tts.initialized == 1);

  status = assistantd_supervisor_run_once(&supervisor);
  assert(status == ASSISTANTD_OK || status == ASSISTANTD_ERR_UNIMPLEMENTED ||
         status == ASSISTANTD_ERR_CHILD_PROCESS);

  status = assistantd_supervisor_stop(&supervisor);
  assert(status == ASSISTANTD_OK);
  assert(supervisor.state == ASSISTANTD_SUPERVISOR_STOPPED);

  remove(prompt_path);
  return 0;
}
