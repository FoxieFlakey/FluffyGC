#include <stdbool.h>
#include <stddef.h>

#include "unmakeable.h"
#include "object/descriptor.h"
#include "object/object.h"
#include "panic.h"
#include "macros.h"
#include "util/util.h"

static int impl_forEachOffset(struct descriptor* super, struct object* object, int (^iterator)(size_t offset)) {
  UNUSED(super);
  UNUSED(iterator);
  UNUSED(object);
  panic("Operation inappropriate for unmakeable!");
}

static size_t impl_getObjectSize(struct descriptor* super) {
  UNUSED(super);
  panic("Operation inappropriate for unmakeable!");
}

static const char* impl_getName(struct descriptor* super) {
  struct unmakeable_descriptor* self = container_of(super, struct unmakeable_descriptor, super);
  return self->name;
}

static void impl_runFinalizer(struct descriptor* super, struct object* obj) {
  UNUSED(super);
  UNUSED(obj);
}

static void impl_free(struct descriptor* super) {
  UNUSED(super);
}

static bool impl_isCompatible(struct descriptor* a, struct descriptor* b) {
  UNUSED(b);
  struct unmakeable_descriptor* self = container_of(a, struct unmakeable_descriptor, super);
  switch (self->type) {
    case DESCRIPTOR_UNMAKEABLE_ANY_MARKER:
      return true;
  }
  panic();
}

static struct descriptor* impl_getDescriptorAt(struct descriptor* super, size_t offset) {
  UNUSED(super);
  UNUSED(offset);
  panic("Operation inappropriate for unmakeable!");
}

static void impl_postInitObject(struct descriptor* super, struct object* obj) {
  UNUSED(super);
  UNUSED(obj);
  panic("Operation inappropriate for unmakeable!");
}

static ssize_t impl_calcOffset(struct descriptor* super, size_t index) {
  UNUSED(super);
  UNUSED(index);
  panic("Operation inappropriate for unmakeable!");
}

struct descriptor_ops unmakeable_ops = {
  .forEachOffset = impl_forEachOffset,
  .free = impl_free,
  .getObjectSize = impl_getObjectSize,
  .getName = impl_getName,
  .isCompatible = impl_isCompatible,
  .getDescriptorAt = impl_getDescriptorAt,
  .runFinalizer = impl_runFinalizer,
  .postInitObject = impl_postInitObject,
  .calcOffset = impl_calcOffset
};



