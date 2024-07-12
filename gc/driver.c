#include <stdatomic.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <stddef.h>

#include <flup/core/panic.h>
#include <flup/core/logger.h>
#include <flup/thread/thread.h>

#include "gc/gc.h"
#include "heap/generation.h"
#include "memory/alloc_tracker.h"
#include "util/moving_window.h"

#include "driver.h"

#undef FLUP_LOG_CATEGORY
#define FLUP_LOG_CATEGORY "GC Driver"

// This file will contain logics to decide when to start GC
// while the gc.c primarily focus on the actual GC process

static void pollHeapState(struct gc_driver* self) {
  // Capture allocation rates
  size_t current = atomic_load(&self->gcState->ownerGen->allocTracker->lifetimeBytesAllocated);
  size_t rate = current - self->prevAllocBytes;
  self->prevAllocBytes = current;
  moving_window_append(self->runningSamplesOfAllocRates, &rate);
  
  struct generation* gen = self->gcState->ownerGen;
  
  size_t usage = atomic_load(&gen->allocTracker->currentUsage);
  size_t softLimit = (size_t) ((float) gen->allocTracker->maxSize * gen->gcState->asyncTriggerThreshold);
  
  // Start GC cycle so memory freed before mutator has to start
  // waiting on GC 
  if (usage > softLimit) {
    struct moving_window_iterator iterator = {};
    size_t totalRates = 0;
    while (moving_window_next(self->runningSamplesOfAllocRates, &iterator))
      totalRates += *((size_t*) iterator.current);
    pr_info("Average allocation rate sampled was %zu MiB per second", totalRates * DRIVER_CHECK_RATE_HZ / self->runningSamplesOfAllocRates->entryCount / 1024 / 1024);
    pr_verbose("Soft limit reached, starting GC");
    gc_start_cycle(gen->gcState);
    return;
  }
}

static void driver(void* _self) {
  struct gc_driver* self = _self;
  
  struct timespec deadline;
  clockid_t clockToUse;
  
  pr_info("Checking for CLOCK_MONOTONIC support");
  if (clock_gettime(CLOCK_MONOTONIC, &deadline) == 0) {
    pr_info("CLOCK_MONOTONIC available using it");
    clockToUse = CLOCK_MONOTONIC;
    goto nice_clock_found;
  }
  pr_info("CLOCK_MONOTONIC support unavailable falling back to CLOCK_REALTIME");
  
  // Fall back to CLOCK_REALTIME which should be available on most implementations
  if (clock_gettime(CLOCK_REALTIME, &deadline) != 0)
    flup_panic("Strange this implementation did not support CLOCK_REALTIME");
  
  clockToUse = CLOCK_REALTIME;
nice_clock_found:
  while (atomic_load(&self->quitRequested) == false) {
    if (atomic_load(&self->paused) == true)
      goto driver_was_paused;
    
    pollHeapState(self);
    
driver_was_paused:
    // Any neater way to deal this??? TwT
    deadline.tv_nsec += 1'000'000'000 / DRIVER_CHECK_RATE_HZ;
    if (deadline.tv_nsec >= 1'000'000'000) {
      deadline.tv_nsec -= 1'000'000'000;
      deadline.tv_sec++;
    }
    
    int ret = 0;
    while ((ret = clock_nanosleep(clockToUse, TIMER_ABSTIME, &deadline, NULL)) == EINTR)
      ;
    
    if (ret != 0)
      flup_panic("clock_nanosleep failed: %d", ret);
  }
  
  pr_info("Requested to quit, quiting!");
}

struct gc_driver* gc_driver_new(struct gc_per_generation_state* gcState) {
  struct gc_driver* self = malloc(sizeof(*self));
  if (!self)
    return NULL;
  
  *self = (struct gc_driver) {
    .gcState = gcState,
    .paused = true,
  };
  
  if (!(self->runningSamplesOfAllocRates = moving_window_new(sizeof(size_t), DRIVER_ALLOC_RATE_SAMPLES)))
    goto failure;
  if (!(self->driverThread = flup_thread_new(driver, self)))
    goto failure;
  return self;
failure:
  gc_driver_free(self);
  return NULL;
}

void gc_driver_unpause(struct gc_driver* self) {
  atomic_store(&self->paused, false);
}

void gc_driver_perform_shutdown(struct gc_driver* self) {
  gc_driver_unpause(self);
  atomic_store(&self->quitRequested, true);
  
  if (self->driverThread)
    flup_thread_wait(self->driverThread);
}

void gc_driver_free(struct gc_driver* self) {
  if (!self)
    return;
  
  if (self->driverThread)
    flup_thread_free(self->driverThread);
  moving_window_free(self->runningSamplesOfAllocRates);
  free(self);
}

