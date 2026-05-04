#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "assistantd/config.h"
#include "assistantd/stt_adapter.h"

static void write_temp_file(const char *path, const char *content) {
  FILE *f = fopen(path, "w");
  assert(f != NULL);
  fputs(content, f);
  fclose(f);
}

static void remove_temp_file(const char *path) {
  remove(path);
}

static assistantd_stt_adapter_t create_adapter_with_url(const char *url) {
  assistantd_config_t config;
  assistantd_status_t status = assistantd_config_init_defaults(&config);
  assert(status == ASSISTANTD_OK);
  snprintf(config.stt_api_base_url, sizeof(config.stt_api_base_url), "%s", url);

  assistantd_stt_adapter_t adapter;
  status = assistantd_stt_init(&adapter, &config);
  assert(status == ASSISTANTD_OK);
  return adapter;
}

static void test_init_null_args(void) {
  assistantd_config_t config;
  assistantd_status_t status = assistantd_config_init_defaults(&config);
  assert(status == ASSISTANTD_OK);

  assistantd_stt_adapter_t adapter;
  assert(assistantd_stt_init(NULL, &config) == ASSISTANTD_ERR_INVALID_ARGUMENT);
  assert(assistantd_stt_init(&adapter, NULL) == ASSISTANTD_ERR_INVALID_ARGUMENT);
  assert(assistantd_stt_init(NULL, NULL) == ASSISTANTD_ERR_INVALID_ARGUMENT);
}

static void test_init_with_empty_url(void) {
  assistantd_config_t config;
  assistantd_status_t status = assistantd_config_init_defaults(&config);
  assert(status == ASSISTANTD_OK);
  config.stt_api_base_url[0] = '\0';

  assistantd_stt_adapter_t adapter;
  assert(assistantd_stt_init(&adapter, &config) == ASSISTANTD_ERR_CONFIG);
}

static void test_init_stores_url(void) {
  assistantd_stt_adapter_t adapter = create_adapter_with_url("http://127.0.0.1:9000");

  assert(adapter.initialized == 1);
  assert(strcmp(adapter.api_base_url, "http://127.0.0.1:9000") == 0);
  assert(adapter.curl_handle != NULL);

  assert(assistantd_stt_shutdown(&adapter) == ASSISTANTD_OK);
}

static void test_transcribe_null_args(void) {
  assistantd_stt_adapter_t adapter = create_adapter_with_url("http://127.0.0.1:9000");

  assistantd_stt_request_t request = {.utterance_wav_path = "/tmp/nonexistent.wav"};
  assistantd_stt_result_t result;

  assert(assistantd_stt_transcribe(NULL, &request, &result) == ASSISTANTD_ERR_INVALID_ARGUMENT);
  assert(assistantd_stt_transcribe(&adapter, NULL, &result) == ASSISTANTD_ERR_INVALID_ARGUMENT);
  assert(assistantd_stt_transcribe(&adapter, &request, NULL) == ASSISTANTD_ERR_INVALID_ARGUMENT);

  assert(assistantd_stt_shutdown(&adapter) == ASSISTANTD_OK);
}

static void test_transcribe_uninitialized(void) {
  assistantd_stt_adapter_t adapter;
  memset(&adapter, 0, sizeof(adapter));

  assistantd_stt_request_t request = {.utterance_wav_path = "/tmp/nonexistent.wav"};
  assistantd_stt_result_t result;

  assert(assistantd_stt_transcribe(&adapter, &request, &result) == ASSISTANTD_ERR_INVALID_ARGUMENT);
}

static void test_transcribe_connection_refused(void) {
  const char *wav_path = "/tmp/test_stt_conn_refused.wav";
  write_temp_file(wav_path, "RIFFxxxxWAVEfmt ");

  assistantd_stt_adapter_t adapter = create_adapter_with_url("http://127.0.0.1:19998");

  assistantd_stt_request_t request = {.utterance_wav_path = wav_path};
  assistantd_stt_result_t result;
  assistantd_status_t status = assistantd_stt_transcribe(&adapter, &request, &result);
  assert(status == ASSISTANTD_ERR_IO);
  assert(result.transcript[0] == '\0');

  assert(assistantd_stt_shutdown(&adapter) == ASSISTANTD_OK);
  remove_temp_file(wav_path);
}

static void test_shutdown_clears_state(void) {
  assistantd_stt_adapter_t adapter = create_adapter_with_url("http://127.0.0.1:9000");

  assert(adapter.initialized == 1);
  assert(adapter.curl_handle != NULL);

  assert(assistantd_stt_shutdown(&adapter) == ASSISTANTD_OK);
  assert(adapter.initialized == 0);
  assert(adapter.curl_handle == NULL);
  assert(adapter.api_base_url[0] == '\0');
}

static void test_shutdown_null(void) {
  assert(assistantd_stt_shutdown(NULL) == ASSISTANTD_ERR_INVALID_ARGUMENT);
}

int main(void) {
  test_init_null_args();
  test_init_with_empty_url();
  test_init_stores_url();
  test_transcribe_null_args();
  test_transcribe_uninitialized();
  test_transcribe_connection_refused();
  test_shutdown_clears_state();
  test_shutdown_null();
  return 0;
}
