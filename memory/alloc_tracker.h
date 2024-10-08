#ifndef UWU_F15ECD4B_1EE0_482D_8E14_3B523A879538_UWU
#define UWU_F15ECD4B_1EE0_482D_8E14_3B523A879538_UWU

#include <mimalloc.h>
#include <stdatomic.h>
#include <stddef.h>

#include <flup/data_structs/list_head.h>
#include <flup/concurrency/mutex.h>

#include "gc/gc.h"
#include "object/descriptor.h"

#include "alloc_context.h"

// Takes "pre-reserve" the counter, and each context
// check its counter usage. This meant to reduce contention
// on global "currentUsage" atomic variable when multiple
// threads intensively allocating.
//
// But the accounting is "currentUsage" reports 2 * <num threads> MiB
// more used than actual usage 
#define CONTEXT_COUNTER_PRERESERVE_SIZE (2 * 1024 * 1024)

// Minimum size for allocation to skip the "pre-reserve"
// mechanism and straight for slow one
#define CONTEXT_COUNTER_PRERESERVE_SKIP (256 * 1024)

struct alloc_tracker {
  atomic_size_t currentUsage;
  
  // Number of bytes ever allocated
  atomic_size_t lifetimeBytesAllocated;
  size_t maxSize;
  
  // List of "global" blocks which wont
  // be inserted back to per context's list
  // of blocks because there simply no reason
  // to and remove the need of blocking the context
  //
  // this is where unsnapshot put blocks to
  _Atomic(struct alloc_unit*) head;
    
  flup_mutex* listOfContextLock;
  flup_list_head contexts;
  
  mi_arena_id_t arena;
};

struct alloc_unit {
  // If current->next == NULL then its end of detached head :3
  struct alloc_unit* next;
  size_t size;
  // Atomic so that new object would have NULL value
  // and can be atomically set the corresponding descriptor
  // without blocking GC too long if descriptor so happen
  // to have insanely large amount of fields and it takes
  // to initialize those fields
  _Atomic(struct descriptor*) desc;
  struct gc_block_metadata gcMetadata;
  
  char data[];
};

// Snapshot of list of heap objects
// at the time of snapshot for GC traversal
struct alloc_tracker_snapshot {
  struct alloc_unit* head;
};

struct alloc_tracker_statistic {
  // Copied from corresponding size in alloc_tracker struct
  size_t maxSize;
  size_t usedBytes;
  size_t reservedBytes;
  size_t commitedBytes;
};

void alloc_tracker_get_statistics(struct alloc_tracker* self, struct alloc_tracker_statistic* stat);

struct alloc_context* alloc_tracker_new_context(struct alloc_tracker* self);
void alloc_tracker_free_context(struct alloc_tracker* self, struct alloc_context* ctx);

void alloc_tracker_take_snapshot(struct alloc_tracker* self, struct alloc_tracker_snapshot* snapshot);
void alloc_tracker_add_block_to_global_list(struct alloc_tracker* self, struct alloc_unit* block);

// Return true if the block going to be unsnapshotted
// or false if not
typedef bool (^alloc_tracker_snapshot_filter_func)(struct alloc_unit* blk);
void alloc_tracker_filter_snapshot_and_delete_snapshot(struct alloc_tracker* self, struct alloc_tracker_snapshot* snapshot, alloc_tracker_snapshot_filter_func filter);

struct alloc_tracker* alloc_tracker_new(size_t size);
void alloc_tracker_free(struct alloc_tracker* self);

struct alloc_unit* alloc_tracker_alloc(struct alloc_tracker* self, struct alloc_context* ctx, size_t size);

#endif
