#ifndef _headers_1674646850_FluffyGC_managed_heap
#define _headers_1674646850_FluffyGC_managed_heap

// The global state
// Tracking various things

#include "util/list_head.h"
#include "vec.h"
#include "context.h"

struct descriptor;
struct context;
struct heap;
struct gc_struct;
struct object;
enum gc_algorithm;

struct generation_params {
  size_t size;
  size_t earlyPromoteSize;
  int promotionAge;
  
  // Young uses
  // 1. Only atomic pointer (no freelist traversal)
  // 2. Semispace heap scheme
  // 3. No compaction phase
  // Old uses
  // 1. Both atomic and freelist used
  // 2. Has compaction phase
};

struct generation {
  struct heap* fromHeap;
  struct heap* toHeap;
  struct generation_params param;
  
  struct list_head rememberedSet;
  bool useFastOnly;
};

struct managed_heap {
  struct gc_struct* gcState;
  
  struct list_head contextStates[CONTEXT_STATE_COUNT];
  
  vec_t(struct context*) threads;
  
  int generationCount;
  struct generation generations[];
};

struct managed_heap* managed_heap_new(enum gc_algorithm algo, int genCount, struct generation_params* generationParams, int gcFlags);
void managed_heap_free(struct managed_heap* self);

struct root_ref* managed_heap_alloc_object(struct descriptor* desc);

int managed_heap_attach_context(struct managed_heap* self);
void managed_heap_detach_context(struct managed_heap* self);

struct context* managed_heap_new_context(struct managed_heap* self);
void managed_heap_free_context(struct managed_heap* self, struct context* ctx);

// Context swapping
struct context* managed_heap_swap_context(struct context* new);
struct context* managed_heap_switch_context_out();
void managed_heap_switch_context_in(struct context* new);

#endif
