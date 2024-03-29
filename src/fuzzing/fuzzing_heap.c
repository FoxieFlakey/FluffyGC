#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>

#include "context.h"
#include "memory/heap.h"
#include "gc/gc.h"

#define MB(n) ((n) * 1024 * 1024)

#define printf(...)

int fuzzing_heap(const void* _data, size_t size);
int fuzzing_heap(const void* _data, size_t size) {
  if (size < sizeof(size_t) + sizeof(uint16_t) + sizeof(uint8_t))
    return 0;
  
  const char* data = _data;
  const void* dataEnd = data + size;
  
  size_t heapSize = (*(const size_t*) data) % MB(64);
  data += sizeof(heapSize);
  size_t localHeapSize = (*(const uint16_t*) data);
  data += 2;
  uint8_t pattern = *(const uint8_t*) data;
  data += 1;
  
  struct heap* heap = heap_new(heapSize);
  heap_param_set_local_heap_size(heap, localHeapSize);
  heap_init(heap);
   
  // printf("Heap size: %zu\n", heapSize);
  // printf("Local heap size: %zu\n", localHeapSize);
  // printf("Pattern: %d\n", pattern);
  // printf("Start alloc pattern: %d\n", (int) (data - dataStart));
  
  static struct heap_block* pointers[1 << 16] = {};
  memset(pointers, 0, sizeof(pointers));
  
  while (data + 5 < dataEnd) {
    uint16_t id = *(const uint8_t*) data + (*(const uint8_t*) data << 8);
    if (pointers[id]) {
      printf("Ptr[%d]: Dealloc (%p)\n", id, pointers[id]);
      heap_dealloc(heap, pointers[id]);
      pointers[id] = NULL;
    } else {
      size_t objectSize = *(const uint8_t*) data + (*(const uint8_t*) data << 8) + (*(const uint8_t*) data << 16);
      // fprintf(stderr, "Ptr[%d]: Alloc %zu or 0x%lx bytes\n", id, objectSize, objectSize);
      pointers[id] = heap_alloc(heap, alignof(struct object), objectSize);
      // fprintf(stderr, "Ptr[%d]: Result %p\n", id, pointers[id]);
      if (pointers[id])
        memset(pointers[id]->dataPtr.ptr, pattern, objectSize);
      data += 3;
    }
    data += 2;
  }
  
  heap_merge_free_blocks(heap);
  heap_free(heap);
  return 0;
}
