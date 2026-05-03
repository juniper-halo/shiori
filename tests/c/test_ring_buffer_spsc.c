#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <time.h>

#include "assistantd/ring_buffer.h"

typedef struct {
  assistantd_ring_buffer_t *buffer;
  size_t total_items;
  atomic_int *consumer_enabled;
} spsc_context_t;

static void assert_invariant(const assistantd_ring_buffer_t *buffer) {
  size_t available = assistantd_ring_buffer_available(buffer);
  size_t space = assistantd_ring_buffer_space(buffer);
  assert(available <= buffer->capacity);
  assert(space <= buffer->capacity);
  assert(available + space == buffer->capacity);
}

static void *producer_thread(void *arg) {
  spsc_context_t *ctx = (spsc_context_t *)arg;
  for (size_t i = 0; i < ctx->total_items; i++) {
    uint8_t value = (uint8_t)(i & 0xFFu);
    while (assistantd_ring_buffer_write(ctx->buffer, &value, 1) != 1) {
      sched_yield();
    }
  }
  return NULL;
}

static void *consumer_thread(void *arg) {
  spsc_context_t *ctx = (spsc_context_t *)arg;
  while (atomic_load_explicit(ctx->consumer_enabled, memory_order_acquire) == 0) {
    sched_yield();
  }

  for (size_t i = 0; i < ctx->total_items; i++) {
    uint8_t value = 0;
    while (assistantd_ring_buffer_read(ctx->buffer, &value, 1) != 1) {
      sched_yield();
    }
    assert(value == (uint8_t)(i & 0xFFu));
  }
  return NULL;
}

static void run_spsc_round(size_t capacity, size_t total_items) {
  assistantd_ring_buffer_t buffer;
  assert(assistantd_ring_buffer_init(&buffer, capacity) == ASSISTANTD_OK);
  assert_invariant(&buffer);

  atomic_int consumer_enabled;
  atomic_init(&consumer_enabled, 0);

  spsc_context_t ctx = {
      .buffer = &buffer,
      .total_items = total_items,
      .consumer_enabled = &consumer_enabled,
  };

  pthread_t producer;
  pthread_t consumer;
  assert(pthread_create(&producer, NULL, producer_thread, &ctx) == 0);
  assert(pthread_create(&consumer, NULL, consumer_thread, &ctx) == 0);

  struct timespec delay = {
      .tv_sec = 0,
      .tv_nsec = 5 * 1000 * 1000,
  };
  nanosleep(&delay, NULL);
  atomic_store_explicit(&consumer_enabled, 1, memory_order_release);

  assert(pthread_join(producer, NULL) == 0);
  assert(pthread_join(consumer, NULL) == 0);

  assert(assistantd_ring_buffer_available(&buffer) == 0);
  assert(assistantd_ring_buffer_space(&buffer) == capacity);
  assert_invariant(&buffer);

  size_t overflow = atomic_load_explicit(&buffer.overflow, memory_order_relaxed);
  assert(overflow > 0);

  assistantd_ring_buffer_free(&buffer);
}

int main(void) {
  run_spsc_round(64, 250000);
  run_spsc_round(127, 200000);
  run_spsc_round(256, 300000);
  return 0;
}
