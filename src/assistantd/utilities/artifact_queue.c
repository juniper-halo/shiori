#include "assistantd/utilities/artifact_queue.h"

#include <string.h>

void assistantd_artifact_queue_init(assistantd_artifact_queue_t *queue) {
  if (queue == NULL) {
    return;
  }

  memset(queue, 0, sizeof(*queue));
}

int assistantd_artifact_queue_enqueue(
    assistantd_artifact_queue_t *queue,
    const assistantd_supervisor_artifact_t *artifact) {
  if (queue == NULL || artifact == NULL) {
    return 0;
  }
  if (queue->size >= ASSISTANTD_SUPERVISOR_ARTIFACT_QUEUE_CAPACITY) {
    return 0;
  }

  queue->items[queue->tail] = *artifact;
  queue->tail = (queue->tail + 1) % ASSISTANTD_SUPERVISOR_ARTIFACT_QUEUE_CAPACITY;
  queue->size++;
  return 1;
}

int assistantd_artifact_queue_dequeue(
    assistantd_artifact_queue_t *queue,
    assistantd_supervisor_artifact_t *artifact) {
  if (queue == NULL || artifact == NULL || queue->size == 0) {
    return 0;
  }

  *artifact = queue->items[queue->head];
  memset(&queue->items[queue->head], 0, sizeof(queue->items[queue->head]));
  queue->head = (queue->head + 1) % ASSISTANTD_SUPERVISOR_ARTIFACT_QUEUE_CAPACITY;
  queue->size--;
  return 1;
}

size_t assistantd_artifact_queue_size(const assistantd_artifact_queue_t *queue) {
  if (queue == NULL) {
    return 0;
  }

  return queue->size;
}
