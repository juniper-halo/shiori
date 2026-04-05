#include "assistantd/ring_buffer.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

/**
 * @todo Implementation Playbook
 * @brief Production SPSC ring buffer core is implemented with C11 atomics.
 * @ownership Audio pipeline and real-time systems.
 * @inputs
 *   - PCM frames from capture worker thread.
 * @outputs
 *   - Deterministic frame windows for VAD worker without dropped ordering.
 * @state
 *   - Single producer writes and single consumer reads with wraparound semantics.
 * @concurrency
 *   - Uses atomic head/tail counters with acquire/release ordering for SPSC safety.
 * @errors
 *   - Tracks overflow/underflow counters for backpressure visibility.
 * @child_process_contracts
 *   - Backpressure policy must align with `arecord` process read cadence.
 * @acceptance
 *   - Functional and threaded SPSC stress tests validate ordering and wraparound behavior.
 *   - Partial write/read accounting is exposed through overflow/underflow counters.
 * @remaining
 *   - TSAN-specific CI job and throughput benchmarking remain follow-up tasks.
 */

static size_t assistantd_min_size(size_t a, size_t b) { return (a < b) ? a : b; }

static size_t assistantd_ring_buffer_used_snapshot(size_t head, size_t tail, size_t capacity) {
  if (head < tail) {
    return 0;
  }

  size_t used = head - tail;
  if (used > capacity) {
    return capacity;
  }
  return used;
}

assistantd_status_t assistantd_ring_buffer_init(assistantd_ring_buffer_t *buffer, size_t capacity) {
  if (buffer == NULL || capacity == 0) {
    return ASSISTANTD_ERR_INVALID_ARGUMENT;
  }

  buffer->data = (uint8_t *)calloc(capacity, sizeof(uint8_t));
  if (buffer->data == NULL) {
    return ASSISTANTD_ERR_IO;
  }

  buffer->capacity = capacity;
  atomic_init(&buffer->head, 0);
  atomic_init(&buffer->tail, 0);
  atomic_init(&buffer->overflow, 0);
  atomic_init(&buffer->underflow, 0);
  return ASSISTANTD_OK;
}

void assistantd_ring_buffer_free(assistantd_ring_buffer_t *buffer) {
  if (buffer == NULL) {
    return;
  }

  free(buffer->data);
  buffer->data = NULL;
  buffer->capacity = 0;
  atomic_store_explicit(&buffer->head, 0, memory_order_relaxed);
  atomic_store_explicit(&buffer->tail, 0, memory_order_relaxed);
  atomic_store_explicit(&buffer->overflow, 0, memory_order_relaxed);
  atomic_store_explicit(&buffer->underflow, 0, memory_order_relaxed);
}

size_t assistantd_ring_buffer_write(
    assistantd_ring_buffer_t *buffer,
    const uint8_t *input,
    size_t length) {
  if (buffer == NULL || input == NULL || buffer->data == NULL) {
    return 0;
  }

  size_t head = atomic_load_explicit(&buffer->head, memory_order_relaxed);
  size_t tail = atomic_load_explicit(&buffer->tail, memory_order_acquire);
  size_t used = assistantd_ring_buffer_used_snapshot(head, tail, buffer->capacity);
  size_t space = buffer->capacity - used;
  size_t to_write = assistantd_min_size(length, space);

  if (to_write > 0) {
    size_t write_pos = head % buffer->capacity;
    size_t first_chunk = assistantd_min_size(to_write, buffer->capacity - write_pos);
    memcpy(buffer->data + write_pos, input, first_chunk);
    if (to_write > first_chunk) {
      memcpy(buffer->data, input + first_chunk, to_write - first_chunk);
    }

    atomic_store_explicit(&buffer->head, head + to_write, memory_order_release);
  }

  if (to_write < length) {
    atomic_fetch_add_explicit(&buffer->overflow, length - to_write, memory_order_relaxed);
  }

  return to_write;
}

size_t assistantd_ring_buffer_read(
    assistantd_ring_buffer_t *buffer,
    uint8_t *output,
    size_t length) {
  if (buffer == NULL || output == NULL || buffer->data == NULL) {
    return 0;
  }

  size_t tail = atomic_load_explicit(&buffer->tail, memory_order_relaxed);
  size_t head = atomic_load_explicit(&buffer->head, memory_order_acquire);
  size_t available = assistantd_ring_buffer_used_snapshot(head, tail, buffer->capacity);
  size_t to_read = assistantd_min_size(length, available);

  if (to_read > 0) {
    size_t read_pos = tail % buffer->capacity;
    size_t first_chunk = assistantd_min_size(to_read, buffer->capacity - read_pos);
    memcpy(output, buffer->data + read_pos, first_chunk);
    if (to_read > first_chunk) {
      memcpy(output + first_chunk, buffer->data, to_read - first_chunk);
    }

    atomic_store_explicit(&buffer->tail, tail + to_read, memory_order_release);
  }

  if (to_read < length) {
    atomic_fetch_add_explicit(&buffer->underflow, length - to_read, memory_order_relaxed);
  }

  return to_read;
}

size_t assistantd_ring_buffer_available(const assistantd_ring_buffer_t *buffer) {
  if (buffer == NULL || buffer->data == NULL) {
    return 0;
  }

  size_t head = atomic_load_explicit(&buffer->head, memory_order_acquire);
  size_t tail = atomic_load_explicit(&buffer->tail, memory_order_acquire);
  return assistantd_ring_buffer_used_snapshot(head, tail, buffer->capacity);
}

size_t assistantd_ring_buffer_space(const assistantd_ring_buffer_t *buffer) {
  if (buffer == NULL || buffer->data == NULL) {
    return 0;
  }

  size_t available = assistantd_ring_buffer_available(buffer);
  if (available > buffer->capacity) {
    return 0;
  }
  return buffer->capacity - available;
}
