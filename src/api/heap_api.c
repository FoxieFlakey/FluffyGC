#include <stddef.h>
#include <errno.h>
#include <stdint.h>
#include <stddef.h>

#include "api/api.h"
#include "context.h"
#include "gc/gc.h"
#include "gc/gc_flags.h"
#include "managed_heap.h"
#include "FluffyHeap/FluffyHeap.h"
#include "util/util.h"

struct mapping {
  enum gc_algorithm algo;
  uint64_t flags;
};

static struct mapping gcMapping[GC_COUNT] = {
  [FH_GC_BALANCED] = {GC_SERIAL_GC, SERIAL_GC_USE_2_GENERATIONS},
  [FH_GC_LOW_LATENCY] = {GC_SERIAL_GC, SERIAL_GC_USE_3_GENERATIONS},
  [FH_GC_HIGH_THROUGHPUT] = {GC_SERIAL_GC, 0}
};

API_FUNCTION_DEFINE(__FLUFFYHEAP_NULLABLE(fluffyheap*), fh_new, __FLUFFYHEAP_NONNULL(fh_param*), incomingParams) {
  if (incomingParams->hint >= ARRAY_SIZE(gcMapping))
    return NULL;
  
  struct mapping mapping = gcMapping[incomingParams->hint];
  struct generation_params params[incomingParams->generationCount] = {};
  for (size_t i = 0; i < incomingParams->generationCount; i++) {
    params[i].promotionAge = gc_preferred_promotion_age(mapping.algo, mapping.flags, i);
    params[i].earlyPromoteSize = gc_preferred_promotion_size(mapping.algo, mapping.flags, i);
    params[i].size = incomingParams->generationSizes[i];
  }
  return EXTERN(managed_heap_new(mapping.algo, incomingParams->generationCount, params, mapping.flags));
}

API_FUNCTION_DEFINE_VOID(fh_free, __FLUFFYHEAP_NONNULL(fluffyheap*), self) {
  managed_heap_free(INTERN(self));
}

API_FUNCTION_DEFINE(int, fh_attach_thread, __FLUFFYHEAP_NONNULL(fluffyheap*), self) {
  int res = managed_heap_attach_thread(INTERN(self));
  if (res < 0)
    return -ENOMEM;
  return 0;
}

API_FUNCTION_DEFINE_VOID(fh_detach_thread, __FLUFFYHEAP_NONNULL(fluffyheap*), self) {
  managed_heap_detach_thread(INTERN(self));
}

API_FUNCTION_DEFINE(int, fh_get_generation_count, __FLUFFYHEAP_NONNULL(fh_param*), param) {
  if (param->hint >= ARRAY_SIZE(gcMapping))
    return -EINVAL;
  return gc_generation_count(gcMapping[param->hint].algo, gcMapping[param->hint].flags);
}

API_FUNCTION_DEFINE_VOID(fh_set_descriptor_loader, __FLUFFYHEAP_NONNULL(fluffyheap*), self, __FLUFFYHEAP_NULLABLE(fh_descriptor_loader), loader) {
  INTERN(self)->api.descriptorLoader = loader;
}

API_FUNCTION_DEFINE(__FLUFFYHEAP_NULLABLE(fh_descriptor_loader), fh_get_descriptor_loader, __FLUFFYHEAP_NONNULL(fluffyheap*), self) {
  return INTERN(self)->api.descriptorLoader;
}
