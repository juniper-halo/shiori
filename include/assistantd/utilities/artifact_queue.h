#ifndef ASSISTANTD_UTILITIES_ARTIFACT_QUEUE_H
#define ASSISTANTD_UTILITIES_ARTIFACT_QUEUE_H

#include <stddef.h>
#include <stdint.h>

#include "assistantd/config.h"

#define ASSISTANTD_SUPERVISOR_ARTIFACT_QUEUE_CAPACITY 16
#define ASSISTANTD_SUPERVISOR_WAV_PATH_MAX ASSISTANTD_CONFIG_VALUE_MAX
#define ASSISTANTD_SUPERVISOR_LLM_RESPONSE_QUEUE_CAPACITY 16
#define ASSISTANTD_SUPERVISOR_LLM_RESPONSE_TEXT_MAX 8192

typedef struct {
  uint64_t sequence_id;
  size_t pcm_bytes;
  char wav_path[ASSISTANTD_SUPERVISOR_WAV_PATH_MAX];
} assistantd_supervisor_artifact_t;

typedef struct {
  assistantd_supervisor_artifact_t items[ASSISTANTD_SUPERVISOR_ARTIFACT_QUEUE_CAPACITY];
  size_t head;
  size_t tail;
  size_t size;
} assistantd_artifact_queue_t;

typedef struct {
  uint64_t sequence_id;
  int used_fallback;
  char response_text[ASSISTANTD_SUPERVISOR_LLM_RESPONSE_TEXT_MAX];
} assistantd_supervisor_llm_response_t;

typedef struct {
  assistantd_supervisor_llm_response_t items[ASSISTANTD_SUPERVISOR_LLM_RESPONSE_QUEUE_CAPACITY];
  size_t head;
  size_t tail;
  size_t size;
} assistantd_llm_response_queue_t;

void assistantd_artifact_queue_init(assistantd_artifact_queue_t *queue);
int assistantd_artifact_queue_enqueue(
    assistantd_artifact_queue_t *queue,
    const assistantd_supervisor_artifact_t *artifact);
int assistantd_artifact_queue_dequeue(
    assistantd_artifact_queue_t *queue,
    assistantd_supervisor_artifact_t *artifact);
size_t assistantd_artifact_queue_size(const assistantd_artifact_queue_t *queue);

void assistantd_llm_response_queue_init(assistantd_llm_response_queue_t *queue);
int assistantd_llm_response_queue_enqueue(
    assistantd_llm_response_queue_t *queue,
    const assistantd_supervisor_llm_response_t *response);
int assistantd_llm_response_queue_dequeue(
    assistantd_llm_response_queue_t *queue,
    assistantd_supervisor_llm_response_t *response);
size_t assistantd_llm_response_queue_size(const assistantd_llm_response_queue_t *queue);

#endif  // ASSISTANTD_UTILITIES_ARTIFACT_QUEUE_H
