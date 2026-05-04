#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "assistantd/utilities/artifact_queue.h"

static void test_init_and_invalid_args(void) {
  assistantd_llm_response_queue_t queue;
  memset(&queue, 0xAB, sizeof(queue));

  assistantd_llm_response_queue_init(&queue);
  assert(queue.size == 0);
  assert(queue.head == 0);
  assert(queue.tail == 0);
  assert(assistantd_llm_response_queue_size(&queue) == 0);
  assert(assistantd_llm_response_queue_size(NULL) == 0);

  assistantd_supervisor_llm_response_t response = {
      .sequence_id = 1,
      .used_fallback = 0,
      .response_text = "test",
  };
  assert(assistantd_llm_response_queue_enqueue(NULL, &response) == 0);
  assert(assistantd_llm_response_queue_enqueue(&queue, NULL) == 0);
  assert(assistantd_llm_response_queue_dequeue(NULL, &response) == 0);
  assert(assistantd_llm_response_queue_dequeue(&queue, NULL) == 0);
}

static void test_fifo_and_size_accounting(void) {
  assistantd_llm_response_queue_t queue;
  assistantd_llm_response_queue_init(&queue);

  assistantd_supervisor_llm_response_t first = {
      .sequence_id = 101,
      .used_fallback = 0,
      .response_text = "first response",
  };
  assistantd_supervisor_llm_response_t second = {
      .sequence_id = 102,
      .used_fallback = 1,
      .response_text = "fallback response",
  };

  assert(assistantd_llm_response_queue_enqueue(&queue, &first) == 1);
  assert(assistantd_llm_response_queue_size(&queue) == 1);
  assert(assistantd_llm_response_queue_enqueue(&queue, &second) == 1);
  assert(assistantd_llm_response_queue_size(&queue) == 2);

  assistantd_supervisor_llm_response_t out = {0};
  assert(assistantd_llm_response_queue_dequeue(&queue, &out) == 1);
  assert(out.sequence_id == first.sequence_id);
  assert(out.used_fallback == first.used_fallback);
  assert(strcmp(out.response_text, first.response_text) == 0);
  assert(assistantd_llm_response_queue_size(&queue) == 1);

  memset(&out, 0, sizeof(out));
  assert(assistantd_llm_response_queue_dequeue(&queue, &out) == 1);
  assert(out.sequence_id == second.sequence_id);
  assert(out.used_fallback == second.used_fallback);
  assert(strcmp(out.response_text, second.response_text) == 0);
  assert(assistantd_llm_response_queue_size(&queue) == 0);

  assert(assistantd_llm_response_queue_dequeue(&queue, &out) == 0);
}

static void test_capacity_limit_drop_contract(void) {
  assistantd_llm_response_queue_t queue;
  assistantd_llm_response_queue_init(&queue);

  for (size_t i = 0; i < ASSISTANTD_SUPERVISOR_LLM_RESPONSE_QUEUE_CAPACITY; ++i) {
    assistantd_supervisor_llm_response_t response = {
        .sequence_id = (uint64_t)i,
        .used_fallback = (int)(i % 2),
        .response_text = {0},
    };
    snprintf(response.response_text, sizeof(response.response_text), "response-%zu", i);
    assert(assistantd_llm_response_queue_enqueue(&queue, &response) == 1);
  }

  assert(assistantd_llm_response_queue_size(&queue) == ASSISTANTD_SUPERVISOR_LLM_RESPONSE_QUEUE_CAPACITY);

  assistantd_supervisor_llm_response_t overflow_response = {
      .sequence_id = 9999,
      .used_fallback = 0,
      .response_text = "overflow",
  };
  assert(assistantd_llm_response_queue_enqueue(&queue, &overflow_response) == 0);
  assert(assistantd_llm_response_queue_size(&queue) == ASSISTANTD_SUPERVISOR_LLM_RESPONSE_QUEUE_CAPACITY);

  assistantd_supervisor_llm_response_t first_out = {0};
  assert(assistantd_llm_response_queue_dequeue(&queue, &first_out) == 1);
  assert(first_out.sequence_id == 0);
}

int main(void) {
  test_init_and_invalid_args();
  test_fifo_and_size_accounting();
  test_capacity_limit_drop_contract();
  return 0;
}
