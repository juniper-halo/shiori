#include "assistantd/ring_buffer.h"

#include <stdlib.h>
#include <string.h>

/**
 * @todo Implementation Playbook
 * @brief Upgrade ring buffer from scaffold implementation to production SPSC lock-free queue.
 * @ownership Audio pipeline and real-time systems.
 * @inputs
 *   - PCM frames from capture worker thread.
 * @outputs
 *   - Deterministic frame windows for VAD worker without dropped ordering.
 * @state
 *   - Single producer writes and single consumer reads with wraparound semantics.
 * @concurrency
 *   - Replace size/head/tail mutation with atomics and memory barriers for real-time safety.
 * @errors
 *   - Track overflow/underflow counters and emit structured logs when thresholds are hit.
 * @child_process_contracts
 *   - Backpressure policy must align with `arecord` process read cadence.
 * @acceptance
 *   - No data races under TSAN.
 *   - Bounded latency under sustained audio throughput.
 *   - Explicit behavior for overflow events is tested.
 */

assistantd_status_t assistantd_ring_buffer_init(assistantd_ring_buffer_t *buffer, size_t capacity) {
  if (buffer == NULL || capacity == 0) {
    return ASSISTANTD_ERR_INVALID_ARGUMENT;
  }

  buffer->data = (uint8_t *)calloc(capacity, sizeof(uint8_t));
  if (buffer->data == NULL) {
    return ASSISTANTD_ERR_IO;
  }

  buffer->capacity = capacity;
  buffer->head = 0;
  buffer->tail = 0;
  buffer->size = 0;
  return ASSISTANTD_OK;
}

void assistantd_ring_buffer_free(assistantd_ring_buffer_t *buffer) {
  if (buffer == NULL) {
    return;
  }

  free(buffer->data);
  buffer->data = NULL;
  buffer->capacity = 0;
  buffer->head = 0;
  buffer->tail = 0;
  buffer->size = 0;
}

size_t assistantd_ring_buffer_write(
    assistantd_ring_buffer_t *buffer,
    const uint8_t *input,
    size_t length) {
  if (buffer == NULL || input == NULL || buffer->data == NULL) {
    return 0;
  }

  size_t written = 0;
  while (written < length && buffer->size < buffer->capacity) {
    buffer->data[buffer->head] = input[written];
    buffer->head = (buffer->head + 1) % buffer->capacity;
    buffer->size++;
    written++;
  }

  return written;
}

size_t assistantd_ring_buffer_read(
    assistantd_ring_buffer_t *buffer,
    uint8_t *output,
    size_t length) {
  if (buffer == NULL || output == NULL || buffer->data == NULL) {
    return 0;
  }

  size_t read = 0;
  while (read < length && buffer->size > 0) {
    output[read] = buffer->data[buffer->tail];
    buffer->tail = (buffer->tail + 1) % buffer->capacity;
    buffer->size--;
    read++;
  }

  return read;
}

size_t assistantd_ring_buffer_available(const assistantd_ring_buffer_t *buffer) {
  if (buffer == NULL) {
    return 0;
  }
  return buffer->size;
}

size_t assistantd_ring_buffer_space(const assistantd_ring_buffer_t *buffer) {
  if (buffer == NULL) {
    return 0;
  }
  return buffer->capacity - buffer->size;
}
