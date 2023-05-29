#ifndef _headers_1674802032_FluffyGC_event
#define _headers_1674802032_FluffyGC_event

#include <pthread.h>
#include <stdbool.h>
#include <stdatomic.h>

#include "mutex.h"
#include "condition.h"

// For waiting on specific event
// without hassle of while loop
// checking condition

enum event_fire_type {
  EVENT_FIRE_NONE,
  EVENT_FIRE_ONE,
  EVENT_FIRE_ALL
};

struct event {
  struct mutex lock;
  struct condition cond;
  enum event_fire_type fireState;
};

int event_init(struct event* self);
void event_cleanup(struct event* self);
void event_reset(struct event* self);

void event__fire_all(struct event* self);
void event__fire(struct event* self);

void event__fire_all_locked(struct event* self);
void event__fire_locked(struct event* self);

// self->lock must held
void event__wait(struct event* self);

#define event_wait(self) do { \
  atomic_thread_fence(memory_order_acquire); \
  event__wait((self)); \
  atomic_thread_fence(memory_order_release); \
} while (0)

#define event_fire(self) do { \
  event__fire((self)); \
  atomic_thread_fence(memory_order_release); \
} while (0)

#define event_fire_locked(self) do { \
  event__fire_locked((self)); \
  atomic_thread_fence(memory_order_release); \
} while (0)

#define event_fire_all(self) do { \
  event__fire_all((self)); \
  atomic_thread_fence(memory_order_release); \
} while (0)

#define event_fire_all_locked(self) do { \
  event__fire_all_locked((self)); \
  atomic_thread_fence(memory_order_release); \
} while (0)

#endif
