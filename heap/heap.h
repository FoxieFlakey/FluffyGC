#ifndef UWU_A20CD05E_D0C0_425B_B0C9_876974A0CA1B_UWU
#define UWU_A20CD05E_D0C0_425B_B0C9_876974A0CA1B_UWU

#include <stddef.h>
#include <flup/data_structs/list_head.h>

#include "heap/generation.h"
#include "memory/arena.h"

struct heap {
  struct generation* gen;
  
  flup_list_head root;
  size_t rootEntryCount;
};

struct root_ref {
  flup_list_head node;
  struct arena_block* obj;
};

struct heap* heap_new(size_t size);
void heap_free(struct heap* self);

struct root_ref* heap_alloc(struct heap* self, size_t size);

struct root_ref* heap_root_dup(struct heap* self, struct root_ref* ref);
void heap_root_unref(struct heap* self, struct root_ref* ref);

#endif