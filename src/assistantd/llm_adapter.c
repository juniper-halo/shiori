/** llm_adapter
 * CS 341 - Spring 2026
 */

#include "assistantd/llm_adapter.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <curl/curl.h>

#include "assistantd/logger.h"
#include "cJSON.h"

/**
 * @todo Implementation Playbook — LLM Model Integration
 * @brief Local-only LLM adapter connecting the assistantd voice pipeline to a
 *   llama-server instance running SmolLM2-1.7B-Instruct (Q4_K_M) via the
 *   OpenAI-compatible /v1/chat/completions HTTP endpoint.
 *
 * @ownership LLM integration, prompt/response protocol, and system prompt
 *   lifecycle across config, adapter, and supervisor modules.
 *
 * @architecture
 *   The LLM adapter is the only pipeline stage that communicates over HTTP
 *   rather than fork+exec. llama-server runs as a separate always-on systemd
 *   service on 127.0.0.1:8080 with the model loaded in memory. assistantd
 *   holds a persistent libcurl handle and reuses it across interactions.
 *
 *   Pipeline data flow for a single interaction:
 *     stt_adapter.transcript (char[4096])
 *       -> supervisor copies to llm_request.prompt
 *       -> llm_adapter.generate() wraps in JSON with system prompt
 *       -> HTTP POST to llama-server /v1/chat/completions
 *       -> parse choices[0].message.content from JSON response
 *       -> llm_result.response (char[8192])
 *       -> supervisor copies to tts_request.text
 *
 * @files_modified
 *   config/system_prompt.txt       — Baymax persona prompt (~190 tokens).
 *                                    Loaded at daemon startup by llm_init().
 *   config/assistantd.env.example  — Added LLM_SYSTEM_PROMPT_PATH key.
 *   include/assistantd/config.h    — Added llm_system_prompt_path field to
 *                                    assistantd_config_t.
 *   src/assistantd/config.c        — Default, parsing, validation, and dump
 *                                    for LLM_SYSTEM_PROMPT_PATH.
 *   include/assistantd/llm_adapter.h — Expanded adapter struct with:
 *                                    api_base_url, model_name, shutdown_flag,
 *                                    curl_handle. Added set_shutdown_flag().
 *                                    Defined ASSISTANTD_LLM_TIMEOUT_SECONDS.
 *   src/assistantd/llm_adapter.c   — (this file) Full generate() implementation.
 *   src/third_party/cJSON/         — Vendored cJSON v1.7.18 (MIT) for JSON
 *                                    request construction and response parsing.
 *   CMakeLists.txt                 — Added cJSON source, include path, and
 *                                    find_package(CURL REQUIRED) linkage.
 *   tests/c/test_llm_adapter_shape.c — 11 shape tests covering init, generate,
 *                                    shutdown, null args, connection refused,
 *                                    config validation, and shutdown flag.
 *
 * @dependencies
 *   - libcurl (apt install libcurl4-openssl-dev on Pi OS).
 *   - cJSON v1.7.18, vendored at src/third_party/cJSON/ (single .c/.h).
 *   - llama-server (llama.cpp) running on 127.0.0.1:8080 with the target
 *     model loaded (SmolLM2-1.7B-Instruct-Q4_K_M.gguf).
 *
 * @inputs
 *   - Single-turn prompt string from stt_adapter transcript.
 *   - System prompt text loaded from LLM_SYSTEM_PROMPT_PATH at init.
 *   - LLM_API_BASE_URL and LLM_MODEL from config for HTTP endpoint targeting.
 *
 * @outputs
 *   - Plain-text assistant response (TTS-friendly, no markdown) for tts_adapter.
 *
 * @state
 *   - init: loads system prompt file, copies config values (api_base_url,
 *     model_name), creates persistent CURL handle. Fail-fast on missing or
 *     empty prompt file.
 *   - generate: stateless per call. Builds JSON, executes HTTP POST, parses
 *     response. Curl handle is reset and reused each call.
 *   - shutdown: cleans up CURL handle, zeroes all adapter state.
 *
 * @concurrency
 *   - Single-threaded: supervisor serializes calls. Not safe for concurrent
 *     generate() invocations.
 *   - Shutdown cancellation uses a volatile sig_atomic_t flag read by curl's
 *     XFERINFOFUNCTION progress callback. The signal handler writes the flag
 *     (async-signal-safe), the callback reads it (~1s polling), and returns
 *     non-zero to abort curl_easy_perform() with CURLE_ABORTED_BY_CALLBACK.
 *   - CURLOPT_NOSIGNAL is set to prevent curl from using SIGALRM internally,
 *     which would conflict with the daemon's own signal handling.
 *
 * @timeout
 *   - 40-second wall-clock upper bound per generate() call via CURLOPT_TIMEOUT.
 *   - Starts when generate() enters, not including prior VAD/STT time.
 *   - On expiry: curl returns CURLE_OPERATION_TIMEDOUT, mapped to ERR_IO.
 *
 * @errors
 *   - CURLE_COULDNT_CONNECT (server down)      -> ASSISTANTD_ERR_IO.
 *   - CURLE_OPERATION_TIMEDOUT (40s exceeded)   -> ASSISTANTD_ERR_IO.
 *   - CURLE_ABORTED_BY_CALLBACK (shutdown)      -> ASSISTANTD_ERR_IO.
 *   - Any other curl error                      -> ASSISTANTD_ERR_IO.
 *   - HTTP status != 200                        -> ASSISTANTD_ERR_IO.
 *   - JSON parse failure on response            -> ASSISTANTD_ERR_IO.
 *   - Missing choices[0].message.content        -> ASSISTANTD_ERR_IO.
 *   - Empty system prompt file at init          -> ASSISTANTD_ERR_CONFIG.
 *   - Unreadable system prompt file at init     -> ASSISTANTD_ERR_IO.
 *   All errors are logged with specific messages before returning status.
 *
 * @child_process_contracts
 *   - No child processes. This adapter communicates via HTTP to llama-server
 *     which is managed as a separate systemd service, not by assistantd.
 *
 * @acceptance
 *   - System prompt is loaded into adapter state at init and persists until
 *     shutdown.
 *   - generate() sends a well-formed OpenAI-compatible chat completion request
 *     with system + user messages and extracts the response content string.
 *   - Timeout and shutdown cancellation unblock generate() within ~1 second.
 *   - Adapter struct can be swapped or extended without supervisor changes.
 *   - Local-only mode is enforced: api_base_url must point to 127.0.0.1.
 *
 * @done
 *   - System prompt file loading in init (Phase 3 prep).
 *   - Config plumbing: LLM_SYSTEM_PROMPT_PATH in config.h/.c and .env.
 *   - curl-based HTTP transport with persistent handle reuse.
 *   - cJSON-based JSON request construction and response parsing.
 *   - Progress-callback shutdown cancellation via sig_atomic_t flag.
 *   - 40-second timeout policy via CURLOPT_TIMEOUT.
 *   - Shape tests for init/generate/shutdown/error paths (11 tests).
 */

// SECTION: types

typedef struct {
  char *data;
  size_t size;
  size_t capacity;
} response_buffer_t;

// SECTION: forward declarations

static assistantd_status_t load_system_prompt(
    assistantd_llm_adapter_t *adapter, const char *path);
static size_t write_callback(
    char *ptr, size_t size, size_t nmemb, void *userdata);
static int progress_callback(
    void *clientp, curl_off_t dltotal, curl_off_t dlnow,
    curl_off_t ultotal, curl_off_t ulnow);
static char *build_request_json(
    const assistantd_llm_adapter_t *adapter, const char *user_prompt);
static assistantd_status_t parse_response_json(
    const char *raw, char *out, size_t out_size);

// SECTION: implementation

static assistantd_status_t load_system_prompt(
    assistantd_llm_adapter_t *adapter,
    const char *path) {
  FILE *file = fopen(path, "r");
  if (file == NULL) {
    assistantd_log(ASSISTANTD_LOG_ERROR, "llm: cannot open system prompt: %s", path);
    return ASSISTANTD_ERR_IO;
  }

  size_t total = 0;
  size_t n;
  while (total < ASSISTANTD_LLM_SYSTEM_PROMPT_MAX - 1 &&
         (n = fread(adapter->system_prompt + total, 1,
                    ASSISTANTD_LLM_SYSTEM_PROMPT_MAX - 1 - total, file)) > 0) {
    total += n;
  }
  adapter->system_prompt[total] = '\0';

  if (ferror(file)) {
    fclose(file);
    assistantd_log(ASSISTANTD_LOG_ERROR, "llm: read error on system prompt: %s", path);
    return ASSISTANTD_ERR_IO;
  }

  fclose(file);

  if (total == 0) {
    assistantd_log(ASSISTANTD_LOG_ERROR, "llm: system prompt file is empty: %s", path);
    return ASSISTANTD_ERR_CONFIG;
  }

  assistantd_log(ASSISTANTD_LOG_INFO, "llm: loaded system prompt (%zu bytes) from %s", total, path);
  return ASSISTANTD_OK;
}

static size_t write_callback(
    char *ptr, size_t size, size_t nmemb, void *userdata) {
  response_buffer_t *buf = (response_buffer_t *)userdata;
  size_t bytes = size * nmemb;

  if (buf->size + bytes >= buf->capacity) {
    size_t new_cap = (buf->capacity == 0) ? 4096 : buf->capacity * 2;
    while (new_cap <= buf->size + bytes) {
      new_cap *= 2;
    }
    char *tmp = realloc(buf->data, new_cap);
    if (tmp == NULL) {
      return 0;
    }
    buf->data = tmp;
    buf->capacity = new_cap;
  }

  memcpy(buf->data + buf->size, ptr, bytes);
  buf->size += bytes;
  buf->data[buf->size] = '\0';
  return bytes;
}

static int progress_callback(
    void *clientp, curl_off_t dltotal, curl_off_t dlnow,
    curl_off_t ultotal, curl_off_t ulnow) {
  (void)dltotal;
  (void)dlnow;
  (void)ultotal;
  (void)ulnow;

  volatile sig_atomic_t *flag = (volatile sig_atomic_t *)clientp;
  if (flag != NULL && *flag != 0) {
    assistantd_log(ASSISTANTD_LOG_INFO, "llm: aborting request (shutdown)");
    return 1;
  }
  return 0;
}

static char *build_request_json(
    const assistantd_llm_adapter_t *adapter, const char *user_prompt) {
  cJSON *root = cJSON_CreateObject();
  if (root == NULL) {
    return NULL;
  }

  cJSON_AddStringToObject(root, "model", adapter->model_name);

  cJSON *messages = cJSON_AddArrayToObject(root, "messages");
  if (messages == NULL) {
    cJSON_Delete(root);
    return NULL;
  }

  cJSON *sys_msg = cJSON_CreateObject();
  cJSON_AddStringToObject(sys_msg, "role", "system");
  cJSON_AddStringToObject(sys_msg, "content", adapter->system_prompt);
  cJSON_AddItemToArray(messages, sys_msg);

  cJSON *usr_msg = cJSON_CreateObject();
  cJSON_AddStringToObject(usr_msg, "role", "user");
  cJSON_AddStringToObject(usr_msg, "content", user_prompt);
  cJSON_AddItemToArray(messages, usr_msg);

  cJSON_AddNumberToObject(root, "max_tokens", 300);
  cJSON_AddNumberToObject(root, "temperature", 0.7);

  char *json_str = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  return json_str;
}

static assistantd_status_t parse_response_json(
    const char *raw, char *out, size_t out_size) {
  cJSON *root = cJSON_Parse(raw);
  if (root == NULL) {
    assistantd_log(ASSISTANTD_LOG_ERROR, "llm: failed to parse response JSON");
    return ASSISTANTD_ERR_IO;
  }

  cJSON *choices = cJSON_GetObjectItemCaseSensitive(root, "choices");
  if (!cJSON_IsArray(choices) || cJSON_GetArraySize(choices) == 0) {
    assistantd_log(ASSISTANTD_LOG_ERROR, "llm: response has no choices array");
    cJSON_Delete(root);
    return ASSISTANTD_ERR_IO;
  }

  cJSON *first = cJSON_GetArrayItem(choices, 0);
  cJSON *message = cJSON_GetObjectItemCaseSensitive(first, "message");
  cJSON *content = cJSON_GetObjectItemCaseSensitive(message, "content");

  if (!cJSON_IsString(content) || content->valuestring == NULL) {
    assistantd_log(ASSISTANTD_LOG_ERROR, "llm: response content is missing or not a string");
    cJSON_Delete(root);
    return ASSISTANTD_ERR_IO;
  }

  snprintf(out, out_size, "%s", content->valuestring);
  cJSON_Delete(root);
  return ASSISTANTD_OK;
}

// SECTION: public API

assistantd_status_t assistantd_llm_init(
    assistantd_llm_adapter_t *adapter,
    const assistantd_config_t *config) {
  if (adapter == NULL || config == NULL) {
    return ASSISTANTD_ERR_INVALID_ARGUMENT;
  }

  memset(adapter, 0, sizeof(*adapter));

  assistantd_status_t status = load_system_prompt(adapter, config->llm_system_prompt_path);
  if (status != ASSISTANTD_OK) {
    return status;
  }

  snprintf(adapter->api_base_url, sizeof(adapter->api_base_url), "%s", config->llm_api_base_url);
  snprintf(adapter->model_name, sizeof(adapter->model_name), "%s", config->llm_model);

  CURL *curl = curl_easy_init();
  if (curl == NULL) {
    assistantd_log(ASSISTANTD_LOG_ERROR, "llm: curl_easy_init failed");
    return ASSISTANTD_ERR_IO;
  }
  adapter->curl_handle = curl;

  adapter->initialized = 1;
  assistantd_log(ASSISTANTD_LOG_INFO, "llm: adapter initialized (model=%s, url=%s)",
                 adapter->model_name, adapter->api_base_url);
  return ASSISTANTD_OK;
}

assistantd_status_t assistantd_llm_generate(
    assistantd_llm_adapter_t *adapter,
    const assistantd_llm_request_t *request,
    assistantd_llm_result_t *result) {
  if (adapter == NULL || request == NULL || result == NULL) {
    return ASSISTANTD_ERR_INVALID_ARGUMENT;
  }
  if (!adapter->initialized || adapter->curl_handle == NULL) {
    return ASSISTANTD_ERR_INVALID_ARGUMENT;
  }

  result->response[0] = '\0';

  char *body = build_request_json(adapter, request->prompt);
  if (body == NULL) {
    assistantd_log(ASSISTANTD_LOG_ERROR, "llm: failed to build request JSON");
    return ASSISTANTD_ERR_IO;
  }

  char url[ASSISTANTD_CONFIG_VALUE_MAX + 32];
  snprintf(url, sizeof(url), "%s/chat/completions", adapter->api_base_url);

  response_buffer_t resp_buf = {.data = NULL, .size = 0, .capacity = 0};

  CURL *curl = (CURL *)adapter->curl_handle;
  curl_easy_reset(curl);

  struct curl_slist *headers = NULL;
  headers = curl_slist_append(headers, "Content-Type: application/json");

  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp_buf);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)ASSISTANTD_LLM_TIMEOUT_SECONDS);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

  if (adapter->shutdown_flag != NULL) {
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_callback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, (void *)adapter->shutdown_flag);
  }

  CURLcode rc = curl_easy_perform(curl);

  curl_slist_free_all(headers);
  free(body);

  if (rc == CURLE_ABORTED_BY_CALLBACK) {
    assistantd_log(ASSISTANTD_LOG_INFO, "llm: request aborted by shutdown");
    free(resp_buf.data);
    return ASSISTANTD_ERR_IO;
  }

  if (rc == CURLE_OPERATION_TIMEDOUT) {
    assistantd_log(ASSISTANTD_LOG_ERROR, "llm: request timed out after %ds",
                   ASSISTANTD_LLM_TIMEOUT_SECONDS);
    free(resp_buf.data);
    return ASSISTANTD_ERR_IO;
  }

  if (rc != CURLE_OK) {
    assistantd_log(ASSISTANTD_LOG_ERROR, "llm: curl error: %s", curl_easy_strerror(rc));
    free(resp_buf.data);
    return ASSISTANTD_ERR_IO;
  }

  long http_code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
  if (http_code != 200) {
    assistantd_log(ASSISTANTD_LOG_ERROR, "llm: HTTP %ld from llama-server", http_code);
    free(resp_buf.data);
    return ASSISTANTD_ERR_IO;
  }

  if (resp_buf.data == NULL || resp_buf.size == 0) {
    assistantd_log(ASSISTANTD_LOG_ERROR, "llm: empty response body");
    free(resp_buf.data);
    return ASSISTANTD_ERR_IO;
  }

  assistantd_status_t status = parse_response_json(
      resp_buf.data, result->response, sizeof(result->response));
  free(resp_buf.data);

  if (status != ASSISTANTD_OK) {
    return status;
  }

  assistantd_log(ASSISTANTD_LOG_INFO, "llm: generated response (%zu bytes)",
                 strlen(result->response));
  return ASSISTANTD_OK;
}

assistantd_status_t assistantd_llm_shutdown(assistantd_llm_adapter_t *adapter) {
  if (adapter == NULL) {
    return ASSISTANTD_ERR_INVALID_ARGUMENT;
  }

  if (adapter->curl_handle != NULL) {
    curl_easy_cleanup((CURL *)adapter->curl_handle);
  }

  memset(adapter, 0, sizeof(*adapter));
  return ASSISTANTD_OK;
}

void assistantd_llm_set_shutdown_flag(
    assistantd_llm_adapter_t *adapter,
    volatile sig_atomic_t *flag) {
  if (adapter != NULL) {
    adapter->shutdown_flag = flag;
  }
}
