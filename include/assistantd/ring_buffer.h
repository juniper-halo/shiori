#ifndef ASSISTANTD_RING_BUFFER_H
#define ASSISTANTD_RING_BUFFER_H

#include <stddef.h>
#include <stdatomic.h>
#include <stdint.h>

#include "assistantd/status.h"

typedef struct {
  uint8_t *data;
  size_t capacity;
  atomic_size_t head;
  atomic_size_t tail;
  atomic_size_t overflow;
  atomic_size_t underflow;
} assistantd_ring_buffer_t;

assistantd_status_t assistantd_ring_buffer_init(assistantd_ring_buffer_t *buffer, size_t capacity);
void assistantd_ring_buffer_free(assistantd_ring_buffer_t *buffer);
size_t assistantd_ring_buffer_write(
    assistantd_ring_buffer_t *buffer,
    const uint8_t *input,
    size_t length);
size_t assistantd_ring_buffer_read(
    assistantd_ring_buffer_t *buffer,
    uint8_t *output,
    size_t length);
size_t assistantd_ring_buffer_available(const assistantd_ring_buffer_t *buffer);
size_t assistantd_ring_buffer_space(const assistantd_ring_buffer_t *buffer);

#endif  // ASSISTANTD_RING_BUFFER_H
