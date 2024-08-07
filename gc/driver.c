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
#include "gc/stat_collector.h"
#include "heap/generation.h"
#include "memory/alloc_tracker.h"
#include "util/moving_window.h"

#include "driver.h"

#undef FLUP_LOG_CATEGORY
#define FLUP_LOG_CATEGORY "GC Driver"

// This file will contain logics to decide when to start GC
// while the gc.c primarily focus on the actual GC process

static size_t calcAverageTargetThreshold(struct gc_driver* self) {
  if (self->triggerThresholdSamples->entryCount == 0)
    return 0;
  
  struct moving_window_iterator iterator = {};
  size_t total = 0;
  while (moving_window_next(self->triggerThresholdSamples, &iterator))
    total += *((size_t*) iterator.current);
  
  return total / self->triggerThresholdSamples->entryCount;
}

static void doCollection(struct gc_driver* self) {
  gc_start_cycle(self->gcState);
  
  size_t threshold = atomic_load(&self->gcState->bytesUsedRightBeforeSweeping);
  moving_window_append(self->triggerThresholdSamples, &threshold);
  
  atomic_store(&self->averageTriggerThreshold, calcAverageTargetThreshold(self));
}

static bool lowMemoryRule(struct gc_driver* self) {
  struct generation* gen = self->gcState->ownerGen;
  
  size_t usage = atomic_load(&gen->allocTracker->currentUsage);
  size_t softLimit = (size_t) ((float) gen->allocTracker->maxSize * 0.95f);
  
  // Start GC cycle so memory freed before mutator has to start
  // waiting on GC 
  if (usage > softLimit) {
    pr_verbose("Low memory rule: starting GC");
    doCollection(self);
    return true;
  }
  return false;
}

// Runs GC at 10%, 20%, 30%, 40%, and 50% to warm up statistics
static bool warmUpRule(struct gc_driver* self) {
  static thread_local int warmUpCurrentCount = 0;
  if (warmUpCurrentCount >= 5)
    return false;
  
  struct generation* gen = self->gcState->ownerGen;
  
  float warmPercent = 0.10f + (float) warmUpCurrentCount * 0.10f;
  size_t usage = atomic_load(&gen->allocTracker->currentUsage);
  size_t warmTrigger = (size_t) ((float) gen->allocTracker->maxSize * warmPercent);
  
  if (usage > warmTrigger) {
    pr_verbose("Warming GC at %.00f percent", warmPercent * 100);
    doCollection(self);
    warmUpCurrentCount++;
    return true;
  }
  return false;
}

static bool matchingRateRule(struct gc_driver* self) {
  size_t bytesLimitSizeT = atomic_load(&self->averageTriggerThreshold);
  
  double cycleTime = atomic_load(&self->gcState->averageCycleTime);
  double allocRate = (double) (atomic_load(&self->statCollector->averageAllocRatePerSecond) + 1);
  double bytesLimit = (double) bytesLimitSizeT;
  if (bytesLimit > (double) self->gcState->ownerGen->allocTracker->maxSize)
    bytesLimit = (double) self->gcState->ownerGen->allocTracker->maxSize;
  
  double bytesToOOM = bytesLimit - (double) atomic_load(&self->gcState->ownerGen->allocTracker->currentUsage);
  if (bytesToOOM < 0)
    bytesToOOM = 0;
  double secondsToOOM = (double) bytesToOOM / allocRate;
  
  double panicFactor = 1.70f;
  double adjustedCycleTime = cycleTime * panicFactor;
  
  if (secondsToOOM < 1.0f / DRIVER_CHECK_RATE_HZ) {
    doCollection(self);
    return true;
  }
  
  if (secondsToOOM < adjustedCycleTime) {
    doCollection(self);
    return true;
  }
  return false;
}

static void pollHeapState(struct gc_driver* self) {
  if (lowMemoryRule(self))
    return;
  
  if (warmUpRule(self))
    return;
  
  if (matchingRateRule(self))
    return;
}

static void driver(void* _self) {
  struct gc_driver* self = _self;
  
  struct timespec deadline;
  
  if (clock_gettime(CLOCK_REALTIME, &deadline) != 0)
    flup_panic("Strange this implementation did not support CLOCK_REALTIME");
  
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
    while ((ret = clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &deadline, NULL)) == EINTR)
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
  
  if (!(self->triggerThresholdSamples = moving_window_new(sizeof(size_t), DRIVER_TRIGGER_THRESHOLD_SAMPLES)))
    goto failure;
  if (!(self->statCollector = stat_collector_new(gcState)))
    goto failure;
  if (!(self->driverThread = flup_thread_new(driver, self)))
    goto failure;
  return self;
failure:
  gc_driver_free(self);
  return NULL;
}

void gc_driver_unpause(struct gc_driver* self) {
  stat_collector_unpause(self->statCollector);
  atomic_store(&self->paused, false);
}

void gc_driver_perform_shutdown(struct gc_driver* self) {
  gc_driver_unpause(self);
  atomic_store(&self->quitRequested, true);
  
  if (self->driverThread)
    flup_thread_wait(self->driverThread);
  
  stat_collector_perform_shutdown(self->statCollector);
}

void gc_driver_free(struct gc_driver* self) {
  if (!self)
    return;
  
  if (self->driverThread)
    flup_thread_free(self->driverThread);
  stat_collector_free(self->statCollector);
  moving_window_free(self->triggerThresholdSamples);
  free(self);
}

