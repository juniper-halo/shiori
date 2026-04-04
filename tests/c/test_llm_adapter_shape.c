/** test_llm_adapter_shape
 * CS 341 - Spring 2026
 */

#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>

#include "assistantd/config.h"
#include "assistantd/llm_adapter.h"

// SECTION: helpers

static void write_temp_file(const char *path, const char *content) {
  FILE *f = fopen(path, "w");
  assert(f != NULL);
  fputs(content, f);
  fclose(f);
}

static void remove_temp_file(const char *path) {
  remove(path);
}

static assistantd_llm_adapter_t create_adapter_with_prompt(const char *path,
                                                           const char *text) {
  write_temp_file(path, text);

  assistantd_config_t config;
  assistantd_config_init_defaults(&config);
  snprintf(config.llm_system_prompt_path,
           sizeof(config.llm_system_prompt_path), "%s", path);

  assistantd_llm_adapter_t adapter;
  assistantd_status_t s = assistantd_llm_init(&adapter, &config);
  assert(s == ASSISTANTD_OK);
  return adapter;
}

// SECTION: tests

static void test_init_with_valid_prompt(void) {
  const char *path = "/tmp/test_baymax_prompt.txt";
  write_temp_file(path, "You are Baymax, a warm companion.");

  assistantd_config_t config;
  assistantd_config_init_defaults(&config);
  snprintf(config.llm_system_prompt_path,
           sizeof(config.llm_system_prompt_path), "%s", path);

  assistantd_llm_adapter_t adapter;
  assistantd_status_t status = assistantd_llm_init(&adapter, &config);
  assert(status == ASSISTANTD_OK);
  assert(adapter.initialized == 1);
  assert(strlen(adapter.system_prompt) > 0);
  assert(strstr(adapter.system_prompt, "Baymax") != NULL);
  assert(adapter.curl_handle != NULL);
  assert(strlen(adapter.api_base_url) > 0);
  assert(strlen(adapter.model_name) > 0);

  assistantd_llm_shutdown(&adapter);
  remove_temp_file(path);
}

static void test_init_stores_config_values(void) {
  const char *path = "/tmp/test_cfg_vals.txt";
  write_temp_file(path, "Test prompt.");

  assistantd_config_t config;
  assistantd_config_init_defaults(&config);
  snprintf(config.llm_system_prompt_path,
           sizeof(config.llm_system_prompt_path), "%s", path);
  snprintf(config.llm_api_base_url,
           sizeof(config.llm_api_base_url), "http://127.0.0.1:9999/v1");
  snprintf(config.llm_model,
           sizeof(config.llm_model), "test-model-name");

  assistantd_llm_adapter_t adapter;
  assert(assistantd_llm_init(&adapter, &config) == ASSISTANTD_OK);
  assert(strcmp(adapter.api_base_url, "http://127.0.0.1:9999/v1") == 0);
  assert(strcmp(adapter.model_name, "test-model-name") == 0);

  assistantd_llm_shutdown(&adapter);
  remove_temp_file(path);
}

static void test_init_with_missing_prompt(void) {
  assistantd_config_t config;
  assistantd_config_init_defaults(&config);
  snprintf(config.llm_system_prompt_path,
           sizeof(config.llm_system_prompt_path),
           "%s", "/tmp/nonexistent_prompt_file.txt");

  assistantd_llm_adapter_t adapter;
  assistantd_status_t status = assistantd_llm_init(&adapter, &config);
  assert(status == ASSISTANTD_ERR_IO);
  assert(adapter.initialized == 0);
}

static void test_init_with_empty_prompt(void) {
  const char *path = "/tmp/test_empty_prompt.txt";
  write_temp_file(path, "");

  assistantd_config_t config;
  assistantd_config_init_defaults(&config);
  snprintf(config.llm_system_prompt_path,
           sizeof(config.llm_system_prompt_path), "%s", path);

  assistantd_llm_adapter_t adapter;
  assistantd_status_t status = assistantd_llm_init(&adapter, &config);
  assert(status == ASSISTANTD_ERR_CONFIG);

  remove_temp_file(path);
}

static void test_init_null_args(void) {
  assistantd_config_t config;
  assistantd_config_init_defaults(&config);

  assert(assistantd_llm_init(NULL, &config) == ASSISTANTD_ERR_INVALID_ARGUMENT);
  assert(assistantd_llm_init(NULL, NULL) == ASSISTANTD_ERR_INVALID_ARGUMENT);

  assistantd_llm_adapter_t adapter;
  assert(assistantd_llm_init(&adapter, NULL) == ASSISTANTD_ERR_INVALID_ARGUMENT);
}

static void test_generate_null_args(void) {
  const char *path = "/tmp/test_gen_null.txt";
  assistantd_llm_adapter_t adapter = create_adapter_with_prompt(path, "Prompt.");

  assistantd_llm_request_t req = {.prompt = "Hello"};
  assistantd_llm_result_t res;

  assert(assistantd_llm_generate(NULL, &req, &res) == ASSISTANTD_ERR_INVALID_ARGUMENT);
  assert(assistantd_llm_generate(&adapter, NULL, &res) == ASSISTANTD_ERR_INVALID_ARGUMENT);
  assert(assistantd_llm_generate(&adapter, &req, NULL) == ASSISTANTD_ERR_INVALID_ARGUMENT);

  assistantd_llm_shutdown(&adapter);
  remove_temp_file(path);
}

static void test_generate_connection_refused(void) {
  const char *path = "/tmp/test_gen_connref.txt";
  write_temp_file(path, "You are Baymax.");

  assistantd_config_t config;
  assistantd_config_init_defaults(&config);
  snprintf(config.llm_system_prompt_path,
           sizeof(config.llm_system_prompt_path), "%s", path);
  snprintf(config.llm_api_base_url,
           sizeof(config.llm_api_base_url), "http://127.0.0.1:19999/v1");

  assistantd_llm_adapter_t adapter;
  assert(assistantd_llm_init(&adapter, &config) == ASSISTANTD_OK);

  assistantd_llm_request_t req = {.prompt = "Hello"};
  assistantd_llm_result_t res;
  assistantd_status_t status = assistantd_llm_generate(&adapter, &req, &res);
  assert(status == ASSISTANTD_ERR_IO);
  assert(res.response[0] == '\0');

  assistantd_llm_shutdown(&adapter);
  remove_temp_file(path);
}

static void test_shutdown_clears_state(void) {
  const char *path = "/tmp/test_shutdown_clear.txt";
  assistantd_llm_adapter_t adapter = create_adapter_with_prompt(path, "Prompt.");

  assert(adapter.initialized == 1);
  assert(adapter.curl_handle != NULL);

  assistantd_llm_shutdown(&adapter);
  assert(adapter.initialized == 0);
  assert(adapter.curl_handle == NULL);
  assert(adapter.system_prompt[0] == '\0');

  remove_temp_file(path);
}

static void test_shutdown_null(void) {
  assert(assistantd_llm_shutdown(NULL) == ASSISTANTD_ERR_INVALID_ARGUMENT);
}

static void test_set_shutdown_flag(void) {
  const char *path = "/tmp/test_flag.txt";
  assistantd_llm_adapter_t adapter = create_adapter_with_prompt(path, "Prompt.");

  assert(adapter.shutdown_flag == NULL);

  volatile sig_atomic_t flag = 0;
  assistantd_llm_set_shutdown_flag(&adapter, &flag);
  assert(adapter.shutdown_flag == &flag);

  assistantd_llm_set_shutdown_flag(NULL, &flag);

  assistantd_llm_shutdown(&adapter);
  remove_temp_file(path);
}

static void test_config_validates_prompt_path(void) {
  assistantd_config_t config;
  assistantd_config_init_defaults(&config);
  assert(assistantd_config_validate(&config) == ASSISTANTD_OK);

  config.llm_system_prompt_path[0] = '\0';
  assert(assistantd_config_validate(&config) == ASSISTANTD_ERR_CONFIG);
}

// SECTION: driver

int main(void) {
  test_init_with_valid_prompt();
  test_init_stores_config_values();
  test_init_with_missing_prompt();
  test_init_with_empty_prompt();
  test_init_null_args();
  test_generate_null_args();
  test_generate_connection_refused();
  test_shutdown_clears_state();
  test_shutdown_null();
  test_set_shutdown_flag();
  test_config_validates_prompt_path();
  return 0;
}
