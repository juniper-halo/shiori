#include <assert.h>
#include <stdint.h>

#include "assistantd/ring_buffer.h"

/**
 * @todo Expand into concurrency and overflow policy tests once lock-free implementation lands.
 */
int main(void) {
  assistantd_ring_buffer_t buffer;
  assistantd_status_t status = assistantd_ring_buffer_init(&buffer, 8);
  assert(status == ASSISTANTD_OK);
  assert(assistantd_ring_buffer_available(&buffer) == 0);

  const uint8_t input[4] = {1, 2, 3, 4};
  size_t written = assistantd_ring_buffer_write(&buffer, input, 4);
  assert(written == 4);
  assert(assistantd_ring_buffer_available(&buffer) == 4);

  uint8_t output[4] = {0};
  size_t read = assistantd_ring_buffer_read(&buffer, output, 4);
  assert(read == 4);
  assert(output[0] == 1 && output[1] == 2 && output[2] == 3 && output[3] == 4);

  assistantd_ring_buffer_free(&buffer);
  return 0;
}
