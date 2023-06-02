#include <pthread.h>
#include <errno.h>
#include <stdatomic.h>

#include "bug.h"
#include "condition.h"
#include "event.h"
#include "mutex.h"

int event_init(struct event* self) {
  *self = (struct event) {};
  
  int ret = 0;
  if (mutex_init(&self->lock) < 0 || condition_init(&self->cond) < 0) {
    ret = -ENOMEM;
    event_cleanup(self);
  }
  return ret;
}

void event_cleanup(struct event* self) {
  mutex_cleanup(&self->lock);
  condition_cleanup(&self->cond);
}

void event_reset(struct event* self) {
  self->fireState = EVENT_FIRE_NONE;
}

void event__wait(struct event* self) {
  enum event_fire_type wakeupCause;
  
  while ((wakeupCause = self->fireState) == EVENT_FIRE_NONE)
    condition_wait(&self->cond, &self->lock);
  if (wakeupCause == EVENT_FIRE_ONE)
    self->fireState = EVENT_FIRE_NONE;
}

void event__fire_nolock(struct event* self) {
  self->fireState = EVENT_FIRE_ONE;
  condition_wake(&self->cond);
}

void event__fire_all_nolock(struct event* self) {
  self->fireState = EVENT_FIRE_ALL;
  condition_wake_all(&self->cond);
}
