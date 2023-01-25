#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "soc.h"
#include "bug.h"

int fuzzing_soc(const void* data, size_t size) {
  if (size < sizeof(size_t) + sizeof(uint8_t) + sizeof(uint8_t))
    return 0;
  
  const void* dataEnd = data + size;
  
  size_t objSize = (*(const size_t*) data) % SOC_MAX_OBJECT_SIZE;
  data += sizeof(objSize);
  int reserveCount = *(const uint8_t*) data;
  data += 1;
  int pattern = *(const uint8_t*) data;
  data += 1;
  
  struct small_object_cache* cache = soc_new(objSize, reserveCount);
  
  void* pointers[1 << 16] = {};
  while (data + 2 < dataEnd) {
    uint16_t id = *(const uint8_t*) data + (*(const uint8_t*) data << 8);
    if (pointers[id]) {
      soc_dealloc(cache, pointers[id]);
      pointers[id] = NULL;
    } else {
      pointers[id] = soc_alloc(cache);
      memset(pointers[id], pattern, objSize);
    }
    data += 2;
  }
  
  soc_free(cache);
  return 0;
}