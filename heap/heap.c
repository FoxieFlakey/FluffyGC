#include <flup/container_of.h>
#include <stdlib.h>
#include <stddef.h>

#include <flup/core/logger.h>
#include <flup/data_structs/list_head.h>

#include "heap.h"
#include "gc/gc.h"
#include "heap/generation.h"
#include "memory/arena.h"

#define HEAP_ALLOC_RETRY_COUNT 5

struct heap* heap_new(size_t size) {
  struct heap* self = malloc(sizeof(*self));
  if (!self)
    return NULL;
  
  *self = (struct heap) {
    .root = FLUP_LIST_HEAD_INIT(self->root),
    .rootEntryCount = 0
  };
  
  if (!(self->gen = generation_new(size)))
    goto failure;
  return self;

failure:
  heap_free(self);
  return NULL;
}

void heap_free(struct heap* self) {
  flup_list_head* current;
  flup_list_head* next;
  flup_list_for_each_safe(&self->root, current, next)
    heap_root_unref(self, container_of(current, struct root_ref, node));
  generation_free(self->gen);
  free(self);
}

struct root_ref* heap_root_dup(struct heap* self, struct root_ref* ref) {
  struct root_ref* newRef = malloc(sizeof(*self));
  if (!newRef)
    return NULL;
  
  flup_list_add_tail(&self->root, &newRef->node);
  newRef->obj = ref->obj;
  self->rootEntryCount++;
  return newRef;
}

void heap_root_unref(struct heap* self, struct root_ref* ref) {
  self->rootEntryCount--;
  flup_list_del(&ref->node);
  free(ref);
}

struct root_ref* heap_alloc(struct heap* self, size_t size) {
  struct root_ref* ref = malloc(sizeof(*ref));
  if (!ref)
    return NULL;
  
  struct arena_block* newObj = generation_alloc(self->gen, size);
  for (int i = 0; i < HEAP_ALLOC_RETRY_COUNT && newObj == NULL; i++) {
    pr_info("Allocation failed trying calling GC #%d", i);
    gc_start_cycle(self->gen->gcState);
    newObj = generation_alloc(self->gen, size);
  }
  
  // Heap is actually OOM-ed
  if (!newObj) {
    free(ref);
    return NULL;
  }
  
  flup_list_add_tail(&self->root, &ref->node);
  ref->obj = newObj;
  self->rootEntryCount++;
  return ref;
}

