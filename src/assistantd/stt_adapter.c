#include "assistantd/stt_adapter.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <curl/curl.h>

#include "assistantd/utilities/logger.h"
#include "cJSON.h"

/**
 * @todo Implementation Playbook
 * @brief Implement whisper.cpp HTTP adapter with deterministic request/response contract.
 * @ownership STT pipeline integration.
 * @inputs
 *   - Finalized utterance WAV path emitted by VAD/utterance assembler.
 * @outputs
 *   - UTF-8 transcript string for LLM stage.
 * @state
 *   - Adapter init stores STT API base URL and creates a persistent CURL handle.
 * @concurrency
 *   - Supervisor serializes transcriptions; adapter is not safe for concurrent calls.
 * @errors
 *   - Map transport, HTTP status, and JSON parse errors to status enum and logs.
 *   - Fail fast on missing/empty transcript text.
 * @child_process_contracts
 *   - None. STT is performed by an external whisper-server over HTTP.
 * @acceptance
 *   - Known utterance fixtures produce deterministic text on pinned model/server.
 *   - Connection failure and malformed response paths are covered with shape tests.
 */

typedef struct {
  char *data;
  size_t size;
  size_t capacity;
} assistantd_stt_response_buffer_t;

static size_t assistantd_stt_write_callback(
    char *ptr,
    size_t size,
    size_t nmemb,
    void *userdata) {
  assistantd_stt_response_buffer_t *buffer =
      (assistantd_stt_response_buffer_t *)userdata;
  size_t bytes = size * nmemb;

  if (buffer == NULL || ptr == NULL) {
    return 0;
  }

  if (buffer->size + bytes + 1 > buffer->capacity) {
    size_t new_capacity = (buffer->capacity == 0) ? 4096 : buffer->capacity;
    while (new_capacity < buffer->size + bytes + 1) {
      new_capacity *= 2;
    }

    char *new_data = realloc(buffer->data, new_capacity);
    if (new_data == NULL) {
      return 0;
    }

    buffer->data = new_data;
    buffer->capacity = new_capacity;
  }

  memcpy(buffer->data + buffer->size, ptr, bytes);
  buffer->size += bytes;
  buffer->data[buffer->size] = '\0';
  return bytes;
}

static void assistantd_stt_trim_span(const char **start, const char **end) {
  while (*start < *end && isspace((unsigned char)**start)) {
    (*start)++;
  }
  while (*end > *start && isspace((unsigned char)*((*end) - 1))) {
    (*end)--;
  }
}

static assistantd_status_t assistantd_stt_parse_json_response(
    const char *raw,
    char *transcript,
    size_t transcript_size) {
  if (raw == NULL || transcript == NULL || transcript_size == 0) {
    return ASSISTANTD_ERR_INVALID_ARGUMENT;
  }

  cJSON *root = cJSON_Parse(raw);
  if (root == NULL) {
    assistantd_log(ASSISTANTD_LOG_ERROR, "stt: failed to parse JSON response");
    return ASSISTANTD_ERR_IO;
  }

  cJSON *text = cJSON_GetObjectItemCaseSensitive(root, "text");
  if (!cJSON_IsString(text) || text->valuestring == NULL) {
    assistantd_log(ASSISTANTD_LOG_ERROR, "stt: response missing `text` field");
    cJSON_Delete(root);
    return ASSISTANTD_ERR_IO;
  }

  const char *start = text->valuestring;
  const char *end = start + strlen(start);
  assistantd_stt_trim_span(&start, &end);
  if (start >= end) {
    assistantd_log(ASSISTANTD_LOG_ERROR, "stt: response `text` field is empty");
    cJSON_Delete(root);
    return ASSISTANTD_ERR_IO;
  }

  size_t copy_len = (size_t)(end - start);
  if (copy_len >= transcript_size) {
    copy_len = transcript_size - 1;
  }
  memcpy(transcript, start, copy_len);
  transcript[copy_len] = '\0';

  cJSON_Delete(root);
  return ASSISTANTD_OK;
}

static void assistantd_stt_build_inference_url(
    const char *base_url,
    char *url,
    size_t url_size) {
  if (base_url == NULL || url == NULL || url_size == 0) {
    return;
  }

  size_t len = strlen(base_url);
  if (len > 0 && base_url[len - 1] == '/') {
    snprintf(url, url_size, "%sinference", base_url);
    return;
  }

  snprintf(url, url_size, "%s/inference", base_url);
}

assistantd_status_t assistantd_stt_init(
    assistantd_stt_adapter_t *adapter,
    const assistantd_config_t *config) {
  if (adapter == NULL || config == NULL) {
    return ASSISTANTD_ERR_INVALID_ARGUMENT;
  }
  if (config->stt_api_base_url[0] == '\0') {
    return ASSISTANTD_ERR_CONFIG;
  }

  memset(adapter, 0, sizeof(*adapter));
  snprintf(adapter->api_base_url, sizeof(adapter->api_base_url), "%s", config->stt_api_base_url);

  CURL *curl = curl_easy_init();
  if (curl == NULL) {
    assistantd_log(ASSISTANTD_LOG_ERROR, "stt: curl_easy_init failed");
    return ASSISTANTD_ERR_IO;
  }

  adapter->curl_handle = curl;
  adapter->initialized = 1;

  assistantd_log(ASSISTANTD_LOG_INFO,
                 "stt: adapter initialized (url=%s)",
                 adapter->api_base_url);
  return ASSISTANTD_OK;
}

assistantd_status_t assistantd_stt_transcribe(
    assistantd_stt_adapter_t *adapter,
    const assistantd_stt_request_t *request,
    assistantd_stt_result_t *result) {
  if (adapter == NULL || request == NULL || result == NULL) {
    return ASSISTANTD_ERR_INVALID_ARGUMENT;
  }
  if (!adapter->initialized || adapter->curl_handle == NULL ||
      request->utterance_wav_path == NULL || request->utterance_wav_path[0] == '\0') {
    return ASSISTANTD_ERR_INVALID_ARGUMENT;
  }

  result->transcript[0] = '\0';

  if (access(request->utterance_wav_path, R_OK) != 0) {
    assistantd_log(ASSISTANTD_LOG_ERROR,
                   "stt: utterance file not readable: %s",
                   request->utterance_wav_path);
    return ASSISTANTD_ERR_IO;
  }

  char url[ASSISTANTD_CONFIG_VALUE_MAX + 32];
  assistantd_stt_build_inference_url(adapter->api_base_url, url, sizeof(url));

  CURL *curl = (CURL *)adapter->curl_handle;
  curl_easy_reset(curl);

  assistantd_stt_response_buffer_t response = {
      .data = malloc(1),
      .size = 0,
      .capacity = 1,
  };
  if (response.data == NULL) {
    return ASSISTANTD_ERR_IO;
  }
  response.data[0] = '\0';

  curl_mime *form = curl_mime_init(curl);
  if (form == NULL) {
    free(response.data);
    return ASSISTANTD_ERR_IO;
  }

  CURLcode form_rc = CURLE_OK;
  curl_mimepart *part = curl_mime_addpart(form);
  if (part == NULL) {
    form_rc = CURLE_OUT_OF_MEMORY;
  } else {
    form_rc = curl_mime_name(part, "file");
    if (form_rc == CURLE_OK) {
      form_rc = curl_mime_filedata(part, request->utterance_wav_path);
    }
  }

  if (form_rc == CURLE_OK) {
    part = curl_mime_addpart(form);
    if (part == NULL) {
      form_rc = CURLE_OUT_OF_MEMORY;
    } else {
      form_rc = curl_mime_name(part, "response_format");
      if (form_rc == CURLE_OK) {
        form_rc = curl_mime_data(part, "json", CURL_ZERO_TERMINATED);
      }
    }
  }

  if (form_rc == CURLE_OK) {
    part = curl_mime_addpart(form);
    if (part == NULL) {
      form_rc = CURLE_OUT_OF_MEMORY;
    } else {
      form_rc = curl_mime_name(part, "language");
      if (form_rc == CURLE_OK) {
        form_rc = curl_mime_data(part, "en", CURL_ZERO_TERMINATED);
      }
    }
  }

  if (form_rc != CURLE_OK) {
    assistantd_log(ASSISTANTD_LOG_ERROR,
                   "stt: failed to build multipart form: %s",
                   curl_easy_strerror(form_rc));
    curl_mime_free(form);
    free(response.data);
    return ASSISTANTD_ERR_IO;
  }

  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_MIMEPOST, form);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, assistantd_stt_write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)ASSISTANTD_STT_TIMEOUT_SECONDS);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

  CURLcode rc = curl_easy_perform(curl);

  long http_status = 0;
  if (rc == CURLE_OK) {
    (void)curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
  }

  curl_mime_free(form);

  if (rc != CURLE_OK) {
    assistantd_log(ASSISTANTD_LOG_ERROR,
                   "stt: HTTP request failed: %s",
                   curl_easy_strerror(rc));
    free(response.data);
    return ASSISTANTD_ERR_IO;
  }

  if (http_status != 200) {
    assistantd_log(ASSISTANTD_LOG_ERROR,
                   "stt: HTTP status %ld with body: %s",
                   http_status,
                   response.data);
    free(response.data);
    return ASSISTANTD_ERR_IO;
  }

  assistantd_status_t parse_status =
      assistantd_stt_parse_json_response(response.data, result->transcript, sizeof(result->transcript));
  free(response.data);
  if (parse_status != ASSISTANTD_OK) {
    return parse_status;
  }

  assistantd_log(ASSISTANTD_LOG_INFO,
                 "stt transcript produced: chars=%zu",
                 strlen(result->transcript));
  return ASSISTANTD_OK;
}

assistantd_status_t assistantd_stt_shutdown(assistantd_stt_adapter_t *adapter) {
  if (adapter == NULL) {
    return ASSISTANTD_ERR_INVALID_ARGUMENT;
  }

  if (adapter->curl_handle != NULL) {
    curl_easy_cleanup((CURL *)adapter->curl_handle);
    adapter->curl_handle = NULL;
  }

  memset(adapter, 0, sizeof(*adapter));
  return ASSISTANTD_OK;
}
