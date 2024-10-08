#include <errno.h>
#include <stddef.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <time.h>

#include <flup/bug.h>
#include <flup/thread/thread.h>
#include <flup/concurrency/cond.h>
#include <flup/concurrency/mutex.h>
#include <flup/container_of.h>
#include <flup/data_structs/list_head.h>
#include <flup/core/panic.h>
#include <flup/core/logger.h>
#include <flup/data_structs/buffer/circular_buffer.h>
#include <flup/data_structs/dyn_array.h>
#include <flup/data_structs/buffer.h>

#include "gc/driver.h"
#include "gc/gc_lock.h"
#include "heap/heap.h"
#include "heap/thread.h"
#include "memory/alloc_tracker.h"
#include "heap/generation.h"
#include "object/descriptor.h"
#include "util/moving_window.h"

#include "gc.h"

#undef FLUP_LOG_CATEGORY
#define FLUP_LOG_CATEGORY "GC"

void gc_on_allocate(struct alloc_unit* block, struct generation* gen) {
  block->gcMetadata.markBit = !gen->gcState->mutatorMarkedBitValue;
  block->gcMetadata.owningGeneration = gen;
}

void gc_need_remark(struct alloc_unit* obj) {
  if (!obj)
    return;
  
  struct gc_block_metadata* metadata = &obj->gcMetadata;
  struct gc_per_generation_state* gcState = metadata->owningGeneration->gcState;
  
  // Add to queue if marking in progress
  if (!atomic_load_explicit(&gcState->markingInProgress, memory_order_acquire))
    return;
  
  bool prevMarkBit = atomic_exchange_explicit(&metadata->markBit, gcState->GCMarkedBitValue, memory_order_relaxed);
  if (prevMarkBit == gcState->GCMarkedBitValue)
    return;
  
  // Enqueue an pointer
  struct thread* currentThread = heap_get_current_thread(obj->gcMetadata.owningGeneration->ownerHeap);
  currentThread->localRemarkBuffer[currentThread->localRemarkBufferUsage] = obj;
  currentThread->localRemarkBufferUsage++;
  
  // Local remark buffer is full, flush it to main queue in one go
  if (currentThread->localRemarkBufferUsage == THREAD_LOCAL_REMARK_BUFFER_SIZE) {
    flup_buffer_write_no_fail(gcState->needRemarkQueue, currentThread->localRemarkBuffer, sizeof(void*) * THREAD_LOCAL_REMARK_BUFFER_SIZE);
    currentThread->localRemarkBufferUsage = 0;
  }
}

static void gcThread(void* _self);
struct gc_per_generation_state* gc_per_generation_state_new(struct generation* gen) {
  struct gc_per_generation_state* self = malloc(sizeof(*self));
  if (!self)
    return NULL;
  
  *self = (struct gc_per_generation_state) {
    .ownerGen = gen
  };
  
  if (!(self->cycleTimeSamples = moving_window_new(sizeof(double), GC_CYCLE_TIME_SAMPLE_COUNT)))
    goto failure;
  if (!(self->gcLock = gc_lock_new()))
    goto failure;
  if (!(self->needRemarkQueue = flup_buffer_new(GC_MUTATOR_MARK_QUEUE_SIZE)))
    goto failure;
  if (!(self->cycleStatusLock = flup_mutex_new()))
    goto failure;
  if (!(self->invokeCycleDoneEvent = flup_cond_new()))
    goto failure;
  if (!(self->gcRequestLock = flup_mutex_new()))
    goto failure;
  if (!(self->gcRequestedCond = flup_cond_new()))
    goto failure;
  if (!(self->statsLock = flup_mutex_new()))
    goto failure;
  if (!(self->gcMarkQueueUwU = flup_circular_buffer_new(GC_MARK_QUEUE_SIZE)))
    goto failure;
  if (!(self->deferredMarkQueue = flup_circular_buffer_new(GC_DEFERRED_MARK_QUEUE_SIZE)))
    goto failure;
  if (!(self->thread = flup_thread_new(gcThread, self)))
    goto failure;
  if (!(self->driver = gc_driver_new(self)))
    goto failure;
  return self;

failure:
  gc_per_generation_state_free(self);
  return NULL;
}

static void callGCAsync(struct gc_per_generation_state* self, enum gc_request request) {
  flup_mutex_lock(self->gcRequestLock);
  self->gcRequest = request;
  flup_cond_wake_one(self->gcRequestedCond);
  flup_mutex_unlock(self->gcRequestLock);
}

void gc_perform_shutdown(struct gc_per_generation_state* self) {
  gc_driver_perform_shutdown(self->driver);
  
  if (self->thread) {
    callGCAsync(self, GC_SHUTDOWN);
    flup_thread_wait(self->thread);
  }
}

void gc_per_generation_state_free(struct gc_per_generation_state* self) {
  if (!self)
    return;
  
  gc_driver_free(self->driver);
  if (self->thread)
    flup_thread_free(self->thread);
  flup_circular_buffer_free(self->gcMarkQueueUwU);
  flup_circular_buffer_free(self->deferredMarkQueue);
  flup_mutex_free(self->statsLock);
  flup_cond_free(self->gcRequestedCond);
  flup_mutex_free(self->gcRequestLock);
  flup_cond_free(self->invokeCycleDoneEvent);
  flup_mutex_free(self->cycleStatusLock);
  gc_lock_free(self->gcLock);
  free(self->snapshotOfRootSet);
  flup_buffer_free(self->needRemarkQueue);
  moving_window_free(self->cycleTimeSamples);
  free(self);
}

static bool markOneItem(struct gc_per_generation_state* state, struct alloc_unit* parent, size_t parentIndex, struct alloc_unit* fieldContent) {
  if (!fieldContent)
    return true;
  
  int ret;
  if ((ret = flup_circular_buffer_write(state->gcMarkQueueUwU, &fieldContent, sizeof(void*))) < 0) {
    struct gc_mark_state savedState = {
      .block = parent,
      .fieldIndex = parentIndex
    };
    
    // If mark queue can't fit just put it in deferred mark queue
    if ((ret = flup_circular_buffer_write(state->deferredMarkQueue, &savedState, sizeof(savedState))) < 0) {
      size_t numOfFieldsToTriggerMarkQueueOverflow = (GC_MARK_QUEUE_SIZE / sizeof(void*)) + 1;
      size_t numOfObjectsToTriggerRemarkQueueOverflow = (GC_DEFERRED_MARK_QUEUE_SIZE / sizeof(struct gc_mark_state)) + 1;
      size_t perObjectBytesToTriggerMarkQueueOverflow = numOfFieldsToTriggerMarkQueueOverflow * sizeof(void*);
      size_t totalBytesToTriggerRemarkQueueOverflow = numOfObjectsToTriggerRemarkQueueOverflow * perObjectBytesToTriggerMarkQueueOverflow;
      flup_panic("!!Congrat!! You found very absurb condition with ~%lf TiB worth of bytes composed from %zu objects sized %zu bytes each (or %zu fields/array entries each) UwU", ((double) totalBytesToTriggerRemarkQueueOverflow) / 1024.0f / 1024.0f / 1024.0f / 1024.0f, numOfObjectsToTriggerRemarkQueueOverflow, perObjectBytesToTriggerMarkQueueOverflow, numOfFieldsToTriggerMarkQueueOverflow);
    }
    
    return false;
  }
  return true;
}

static void doMarkInner(struct gc_per_generation_state* state, struct gc_mark_state* markState) {
  struct alloc_unit* block = markState->block;
  bool markBit = atomic_exchange_explicit(&block->gcMetadata.markBit, state->GCMarkedBitValue, memory_order_relaxed);
  // Current item is already marked skip and fieldIndex equals zero
  // mean its not a continuation from previous state
  if (markState->fieldIndex == 0 && markBit == state->GCMarkedBitValue)
    return;
  
  struct descriptor* desc = atomic_load_explicit(&block->desc, memory_order_acquire);
  // Object have no GC-able references
  if (!desc)
    return;
  
  // Uses breadth first search but if failed
  // queue current state to process later
  size_t fieldIndex;
  for (fieldIndex = markState->fieldIndex; fieldIndex < desc->fieldCount; fieldIndex++) {
    size_t offset = desc->fields[fieldIndex].offset;
    _Atomic(struct alloc_unit*)* fieldPtr = (_Atomic(struct alloc_unit*)*) ((void*) (((char*) block->data) + offset));
    if (!markOneItem(state, block, fieldIndex, atomic_load_explicit(fieldPtr, memory_order_relaxed)))
      return;
  }
  
  if (!desc->hasFlexArrayField)
    return;
  
  size_t flexArrayCount = (block->size - desc->objectSize) / sizeof(void*);
  for (size_t i = 0; i < flexArrayCount; i++) {
    _Atomic(struct alloc_unit*)* fieldPtr = (_Atomic(struct alloc_unit*)*) ((void*) (((char*) block->data) + desc->objectSize + i * sizeof(void*)));
    if (!markOneItem(state, block, fieldIndex + i, atomic_load_explicit(fieldPtr, memory_order_relaxed)))
      return;
  }
}

static void processMarkQueue(struct gc_per_generation_state* state) {
  int ret;
  struct alloc_unit* current;
  while ((ret = flup_circular_buffer_read(state->gcMarkQueueUwU, &current, sizeof(void*))) == 0) {
    struct gc_mark_state markState = {
      .block = current,
      .fieldIndex = 0
    };
    doMarkInner(state, &markState);
  }
  
  if (ret != -ENODATA)
    flup_panic("Error reading GC mark queue: %d", ret);
}

static void doMark(struct gc_per_generation_state* state, struct alloc_unit* block) {
  int ret;
  if ((ret = flup_circular_buffer_write(state->gcMarkQueueUwU, &block, sizeof(void*))) < 0)
    flup_panic("Cannot enqueue to GC mark queue (configured queue size was %zu bytes): %d", (size_t) GC_MARK_QUEUE_SIZE, ret);
  
  processMarkQueue(state);
  
  struct gc_mark_state current;
  while ((ret = flup_circular_buffer_read(state->deferredMarkQueue, &current, sizeof(current))) == 0) {
    doMarkInner(state, &current);
    processMarkQueue(state);
  }
  
  if (ret != -ENODATA)
    flup_panic("Error reading GC deferred mark queue: %d", ret);
}

struct cycle_state {
  struct gc_per_generation_state* self;
  struct alloc_tracker* arena;
  struct heap* heap;
  
  // Temporary stats stored here before finally copied to per generation state
  struct gc_stats stats;
  
  struct timespec pauseBegin, pauseEnd;
  
  struct alloc_tracker_snapshot objectsListSnapshot;
};

static void takeRootSnapshotPhase(struct cycle_state* state) {
  __block size_t totalRootRefs = 0;
  heap_iterate_threads(state->heap, ^(struct thread* thrd) {
    totalRootRefs += thrd->rootSize;
  });
  
  struct alloc_unit** rootSnapshot = realloc(state->self->snapshotOfRootSet, totalRootRefs * sizeof(void*));
  if (!rootSnapshot)
    flup_panic("Error reserving memory for root set snapshot");
  state->self->snapshotOfRootSet = rootSnapshot;
  state->self->snapshotOfRootSetSize = totalRootRefs;
  
  __block size_t index = 0;
  heap_iterate_threads(state->heap, ^(struct thread* thrd) {
    flup_list_head* current;
    flup_list_for_each(&thrd->rootEntries, current) {
      // Root set still has more items
      BUG_ON(index >= totalRootRefs);
      
      rootSnapshot[index] = container_of(current, struct root_ref, node)->obj;
      index++;
    }
  });
  
  // Root set somehow reduced in size
  // or counted wrongly
  BUG_ON(index != totalRootRefs);
}

static void markingPhase(struct cycle_state* state) {
  for (size_t i = 0; i < state->self->snapshotOfRootSetSize; i++)
    doMark(state->self, state->self->snapshotOfRootSet[i]);
}

static void processMutatorMarkQueuePhase(struct cycle_state* state) {
  auto processAChunk = ^void (struct alloc_unit** chunk, unsigned int chunkSize) {
    for (unsigned int i = 0; i < chunkSize; i++) {
      struct alloc_unit* current = chunk[i];
      atomic_store_explicit(&current->gcMetadata.markBit, !state->self->GCMarkedBitValue, memory_order_relaxed);
      doMark(state->self, current);
    }
  };
  
  // This is safe as mutator will only ever pushes THREAD_LOCAL_REMARK_BUFFER_SIZE
  // items therefore the buffer content always multiple of this items
  static thread_local struct alloc_unit* chunk[THREAD_LOCAL_REMARK_BUFFER_SIZE];
  int ret;
  while ((ret = flup_buffer_read2(state->self->needRemarkQueue, &chunk, sizeof(void*) * THREAD_LOCAL_REMARK_BUFFER_SIZE, FLUP_BUFFER_READ2_DONT_WAIT_FOR_DATA)) >= 0)
    processAChunk(chunk, THREAD_LOCAL_REMARK_BUFFER_SIZE);
  BUG_ON(ret == -EMSGSIZE);
  
  // Process each thread's local buffer
  // for remaining unqueued entries
  // it must be safe as mutator already
  // stopped writing into its local buffer
  // ensuring no race by this time
  heap_iterate_threads(state->heap, ^(struct thread* thrd) {
    processAChunk(thrd->localRemarkBuffer, thrd->localRemarkBufferUsage);
    thrd->localRemarkBufferUsage = 0;
  });
}

// Returns bytes free'd
static size_t sweepPhase(struct cycle_state* state) {
  __block uint64_t count = 0;
  __block size_t totalSize = 0;
  
  __block uint64_t sweepedCount = 0;
  __block size_t sweepSize = 0;
  
  __block uint64_t liveObjectCount = 0;
  __block size_t liveObjectSize = 0;
  
  alloc_tracker_filter_snapshot_and_delete_snapshot(state->arena, &state->objectsListSnapshot, ^bool (struct alloc_unit* block) {
    count++;
    totalSize += block->size;
    // Object is alive continuing
    if (atomic_load_explicit(&block->gcMetadata.markBit, memory_order_relaxed) == state->self->GCMarkedBitValue) {
      liveObjectCount++;
      liveObjectSize += block->size;
      return true;
    }
    
    sweepedCount++;
    sweepSize += block->size;
    return false;
  });
  
  state->stats.lifetimeTotalSweepedObjectCount += sweepedCount;
  state->stats.lifetimeTotalSweepedObjectSize += sweepSize;
  
  state->stats.lifetimeTotalObjectCount += count;
  state->stats.lifetimeTotalObjectSize += totalSize;
  
  state->stats.lifetimeTotalLiveObjectCount += liveObjectCount;
  state->stats.lifetimeLiveObjectSize += liveObjectSize;
  
  return sweepSize;
}

static void pauseAppThreads(struct cycle_state* state) {
  gc_lock_enter_gc_exclusive(state->self->gcLock);
  clock_gettime(CLOCK_REALTIME, &state->pauseBegin);
}

static void unpauseAppThreads(struct cycle_state* state) {
  clock_gettime(CLOCK_REALTIME, &state->pauseEnd);
  gc_lock_exit_gc_exclusive(state->self->gcLock);
  
  double duration = 
    ((double) state->pauseEnd.tv_sec + ((double) state->pauseEnd.tv_nsec/ 1'000'000'000.0f)) -
    ((double) state->pauseBegin.tv_sec + ((double) state->pauseBegin.tv_nsec/ 1'000'000'000.0f));
  state->stats.lifetimeSTWTime += duration;
}

static void cycleRunner(struct gc_per_generation_state* self) {
  struct cycle_state state = {
    .arena = self->ownerGen->allocTracker,
    .self = self,
    .heap = self->ownerGen->ownerHeap
  };
  
  // pr_info("Before cycle mem usage: %f MiB", (float) atomic_load(&state.arena->currentUsage) / 1024.0f / 1024.0f);
  flup_mutex_lock(self->statsLock);
  self->stats.lifetimeCyclesStartCount++;
  state.stats = self->stats;
  size_t prev = state.stats.lifetimeLiveObjectSize;
  flup_mutex_unlock(self->statsLock);
  
  struct timespec start, end;
  clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start);
  
  pauseAppThreads(&state);
  self->mutatorMarkedBitValue = self->GCMarkedBitValue;
  atomic_store_explicit(&self->cycleInProgress, true, memory_order_release);
  
  atomic_store_explicit(&self->markingInProgress, true, memory_order_release);
  takeRootSnapshotPhase(&state);
  alloc_tracker_take_snapshot(state.arena, &state.objectsListSnapshot);
  unpauseAppThreads(&state);
  
  markingPhase(&state);
  atomic_store_explicit(&self->markingInProgress, false, memory_order_release);
  processMutatorMarkQueuePhase(&state);
  size_t freedBytes = sweepPhase(&state);
  
  pauseAppThreads(&state);
  self->GCMarkedBitValue = !self->GCMarkedBitValue;
  atomic_store_explicit(&self->cycleInProgress, false, memory_order_release);
  unpauseAppThreads(&state);
  clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end);
  
  flup_mutex_lock(self->cycleStatusLock);
  self->cycleID++;
  self->cycleWasInvoked = false;
  flup_cond_wake_all(self->invokeCycleDoneEvent);
  flup_mutex_unlock(self->cycleStatusLock);
  
  double duration =
    ((double) end.tv_sec + ((double) end.tv_nsec) / 1'000'000'000.0f) -
    ((double) start.tv_sec + ((double) start.tv_nsec) / 1'000'000'000.0f);
  state.stats.lifetimeCycleTime += duration;
  state.stats.lifetimeCyclesCompletedCount++;
  
  flup_mutex_lock(self->statsLock);
  self->stats = state.stats;
  flup_mutex_unlock(self->statsLock);
  
  size_t usage = atomic_load_explicit(&self->ownerGen->allocTracker->currentUsage, memory_order_relaxed);
  atomic_store_explicit(&self->bytesUsedRightBeforeSweeping, usage + freedBytes, memory_order_relaxed);
  atomic_store_explicit(&self->liveSetSize, state.stats.lifetimeLiveObjectSize - prev, memory_order_relaxed);
  moving_window_append(self->cycleTimeSamples, &duration);
  
  struct moving_window_iterator iterator = {};
  double total = 0;
  while (moving_window_next(self->cycleTimeSamples, &iterator))
    total += *((double*) iterator.current);
  
  atomic_store_explicit(&self->averageCycleTime, total / (double) self->cycleTimeSamples->entryCount, memory_order_relaxed);
  // pr_info("After cycle mem usage: %f MiB", (float) atomic_load(&state.arena->currentUsage) / 1024.0f / 1024.0f);
}

static void gcThread(void* _self) {
  struct gc_per_generation_state* self = _self;
  
  pr_info("GC thread started!");
  while (1) {
    flup_mutex_lock(self->gcRequestLock);
    while (self->gcRequest == GC_NOOP)
      flup_cond_wait(self->gcRequestedCond, self->gcRequestLock, NULL);
    enum gc_request request = self->gcRequest;
    self->gcRequest = GC_NOOP;
    flup_mutex_unlock(self->gcRequestLock);
    
    switch (request) {
      case GC_NOOP:
        flup_panic("Unreachable");
      case GC_SHUTDOWN:
        pr_info("Shutting down GC thread");
        goto shutdown_gc_thread;
      case GC_START_CYCLE:
        // pr_info("Starting GC cycle!");
        cycleRunner(self);
        break;
    }
  }
shutdown_gc_thread:
}

uint64_t gc_start_cycle_async(struct gc_per_generation_state* self) {
  // It was already started lets wait
  flup_mutex_lock(self->cycleStatusLock);
  uint64_t lastCycleID = self->cycleID;
  if (self->cycleWasInvoked) {
    flup_mutex_unlock(self->cycleStatusLock);
    goto no_need_to_wake_gc;
  }
  
  self->cycleWasInvoked = true;
  flup_mutex_unlock(self->cycleStatusLock);
  
  // Wake GC thread
  callGCAsync(self, GC_START_CYCLE);
no_need_to_wake_gc:
  return lastCycleID;
}

int gc_wait_cycle(struct gc_per_generation_state* self, uint64_t cycleID, struct timespec* absTimeout) {
  flup_mutex_lock(self->cycleStatusLock);
  // Waiting loop
  while (self->cycleID == cycleID) {
    int ret = flup_cond_wait(self->invokeCycleDoneEvent, self->cycleStatusLock, absTimeout);
    if (ret == -ETIMEDOUT) {
      flup_mutex_unlock(self->cycleStatusLock);
      return -ETIMEDOUT;
    }
  }
  
  flup_mutex_unlock(self->cycleStatusLock);
  return 0;
}

void gc_start_cycle(struct gc_per_generation_state* self) {
  gc_wait_cycle(self, gc_start_cycle_async(self), NULL);
}

void gc_block(struct gc_per_generation_state* self, struct thread* blockingThread) {
  gc_lock_block_gc(self->gcLock, blockingThread->gcLockPerThread);
}

void gc_unblock(struct gc_per_generation_state* self, struct thread* blockingThread) {
  gc_lock_unblock_gc(self->gcLock, blockingThread->gcLockPerThread);
}

void gc_get_stats(struct gc_per_generation_state* self, struct gc_stats* stats) {
  flup_mutex_lock(self->statsLock);
  *stats = self->stats;
  flup_mutex_unlock(self->statsLock);
}

void gc_on_preallocate(struct generation* gen) {
  struct gc_per_generation_state* gcState = gen->gcState;
  unsigned int pacingNanosec = atomic_load_explicit(&gcState->pacingMicrosec, memory_order_relaxed) * 1'000;
  if (pacingNanosec == 0)
    return;
  
  struct timespec sleepTime = {
    .tv_sec = 0,
    .tv_nsec = pacingNanosec
  };
  
  struct timespec timeLeft;
  
  int err;
  while ((err = clock_nanosleep(CLOCK_REALTIME, 0, &sleepTime, &timeLeft)) == EINTR)
    sleepTime = timeLeft;
  BUG_ON(err != 0);
}

