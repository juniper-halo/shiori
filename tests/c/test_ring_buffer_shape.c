#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>

#include "assistantd/ring_buffer.h"

/**
 * @todo Add TSAN and throughput-focused performance validation in dedicated CI jobs.
 */
static void assert_invariant(const assistantd_ring_buffer_t *buffer) {
  size_t available = assistantd_ring_buffer_available(buffer);
  size_t space = assistantd_ring_buffer_space(buffer);
  assert(available <= buffer->capacity);
  assert(space <= buffer->capacity);
  assert(available + space == buffer->capacity);
}

static void test_init_and_invalid_args(void) {
  assert(assistantd_ring_buffer_init(NULL, 8) == ASSISTANTD_ERR_INVALID_ARGUMENT);

  assistantd_ring_buffer_t buffer;
  assert(assistantd_ring_buffer_init(&buffer, 0) == ASSISTANTD_ERR_INVALID_ARGUMENT);

  assert(assistantd_ring_buffer_init(&buffer, 8) == ASSISTANTD_OK);
  assert(assistantd_ring_buffer_available(&buffer) == 0);
  assert(assistantd_ring_buffer_space(&buffer) == 8);
  assert(atomic_load_explicit(&buffer.overflow, memory_order_relaxed) == 0);
  assert(atomic_load_explicit(&buffer.underflow, memory_order_relaxed) == 0);
  assert_invariant(&buffer);
  assistantd_ring_buffer_free(&buffer);
}

static void test_fifo_wraparound(void) {
  assistantd_ring_buffer_t buffer;
  assistantd_status_t status = assistantd_ring_buffer_init(&buffer, 8);
  assert(status == ASSISTANTD_OK);
  assert_invariant(&buffer);

  const uint8_t first_input[6] = {1, 2, 3, 4, 5, 6};
  size_t written = assistantd_ring_buffer_write(&buffer, first_input, 6);
  assert(written == 6);
  assert_invariant(&buffer);

  uint8_t first_output[4] = {0};
  size_t read = assistantd_ring_buffer_read(&buffer, first_output, 4);
  assert(read == 4);
  assert(first_output[0] == 1 && first_output[1] == 2 && first_output[2] == 3 &&
         first_output[3] == 4);
  assert_invariant(&buffer);

  const uint8_t second_input[6] = {7, 8, 9, 10, 11, 12};
  written = assistantd_ring_buffer_write(&buffer, second_input, 6);
  assert(written == 6);
  assert_invariant(&buffer);

  uint8_t second_output[8] = {0};
  read = assistantd_ring_buffer_read(&buffer, second_output, 8);
  assert(read == 8);
  const uint8_t expected[8] = {5, 6, 7, 8, 9, 10, 11, 12};
  for (size_t i = 0; i < 8; i++) {
    assert(second_output[i] == expected[i]);
  }
  assert_invariant(&buffer);

  assistantd_ring_buffer_free(&buffer);
}

static void test_overflow_accounting(void) {
  assistantd_ring_buffer_t buffer;
  assert(assistantd_ring_buffer_init(&buffer, 4) == ASSISTANTD_OK);

  const uint8_t first[4] = {1, 2, 3, 4};
  assert(assistantd_ring_buffer_write(&buffer, first, 4) == 4);
  assert(assistantd_ring_buffer_write(&buffer, first, 3) == 0);
  assert(atomic_load_explicit(&buffer.overflow, memory_order_relaxed) == 3);
  assert_invariant(&buffer);

  uint8_t out2[2] = {0};
  assert(assistantd_ring_buffer_read(&buffer, out2, 2) == 2);
  assert(out2[0] == 1 && out2[1] == 2);

  const uint8_t second[3] = {8, 9, 10};
  assert(assistantd_ring_buffer_write(&buffer, second, 3) == 2);
  assert(atomic_load_explicit(&buffer.overflow, memory_order_relaxed) == 4);
  assert_invariant(&buffer);

  uint8_t out4[4] = {0};
  assert(assistantd_ring_buffer_read(&buffer, out4, 4) == 4);
  assert(out4[0] == 3 && out4[1] == 4 && out4[2] == 8 && out4[3] == 9);
  assert_invariant(&buffer);

  assistantd_ring_buffer_free(&buffer);
}

static void test_underflow_accounting(void) {
  assistantd_ring_buffer_t buffer;
  assert(assistantd_ring_buffer_init(&buffer, 4) == ASSISTANTD_OK);

  uint8_t output[8] = {0};
  assert(assistantd_ring_buffer_read(&buffer, output, 5) == 0);
  assert(atomic_load_explicit(&buffer.underflow, memory_order_relaxed) == 5);

  const uint8_t input[2] = {42, 43};
  assert(assistantd_ring_buffer_write(&buffer, input, 2) == 2);
  assert(assistantd_ring_buffer_read(&buffer, output, 4) == 2);
  assert(output[0] == 42 && output[1] == 43);
  assert(atomic_load_explicit(&buffer.underflow, memory_order_relaxed) == 7);
  assert_invariant(&buffer);

  assistantd_ring_buffer_free(&buffer);
}

static void test_fail_soft_helpers(void) {
  uint8_t byte = 0;
  assert(assistantd_ring_buffer_write(NULL, &byte, 1) == 0);
  assert(assistantd_ring_buffer_read(NULL, &byte, 1) == 0);
  assert(assistantd_ring_buffer_available(NULL) == 0);
  assert(assistantd_ring_buffer_space(NULL) == 0);

  assistantd_ring_buffer_t buffer;
  assert(assistantd_ring_buffer_init(&buffer, 4) == ASSISTANTD_OK);
  assistantd_ring_buffer_free(&buffer);
  assert(assistantd_ring_buffer_write(&buffer, &byte, 1) == 0);
  assert(assistantd_ring_buffer_read(&buffer, &byte, 1) == 0);
  assert(assistantd_ring_buffer_available(&buffer) == 0);
  assert(assistantd_ring_buffer_space(&buffer) == 0);
}

int main(void) {
  test_init_and_invalid_args();
  test_fifo_wraparound();
  test_overflow_accounting();
  test_underflow_accounting();
  test_fail_soft_helpers();
  return 0;
}
