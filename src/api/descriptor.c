#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include "api/api.h"
#include "bug.h"
#include "concurrency/mutex.h"
#include "object/object.h"
#include "logger/logger.h"
#include "object/descriptor/object.h"
#include "concurrency/rwlock.h"
#include "context.h"
#include "api/type_registry.h"
#include "object/descriptor.h"
#include "managed_heap.h"
#include "FluffyHeap/FluffyHeap.h"
#include "util/list_head.h"
#include "util/util.h"
#include "vec.h"

typedef vec_t(struct object_descriptor*) descriptor_stack;

DEFINE_LOGGER(LOGGER, "Descriptor Loader");
#undef LOGGER_DEFAULT
#define LOGGER_DEFAULT (&LOGGER)

struct descriptor_loader_context {
  struct list_head stack;
  struct list_head newlyAddedDescriptor;
  fh_descriptor_loader appLoader;
};

static thread_local int exclusiveCount = 0;
static void enterClassLoaderExclusive() {
  if (exclusiveCount == 0)
    mutex_lock(&managed_heap_current->api.descriptorLoaderSerializationLock);
  exclusiveCount++;
}

static void exitClassLoaderExclusive() {
  if (exclusiveCount == 1)
    mutex_unlock(&managed_heap_current->api.descriptorLoaderSerializationLock);
  exclusiveCount--;
}

static bool isValidObjectName(const char* name) {
  if (util_prefixed_by("fox.fluffyheap.marker.", name))
    return false;
  
  if (!util_is_matched("^[a-zA-Z_$][a-zA-Z0-9_$.]*$", name))
    return false;
  return true;
}

static int loadDescriptor(struct descriptor_loader_context* loader, const char* name, struct object_descriptor** result) {
  if (!isValidObjectName(name))
    return -EINVAL;

  int ret = 0;
  struct object_descriptor* new = NULL;
  // Descriptor for the field haven't created invoke loader
  if (!(new = object_descriptor_new())) { 
    ret = -ENOMEM;
    goto failure;
  }
  
  if ((new->name = strdup(name)) < 0) {
    ret = -ENOMEM;
    goto failure;
  }
  
  // Program may invokes GC and invokes loader
  // again recursively
  rwlock_unlock(&managed_heap_current->api.registry->lock);
  context_unblock_gc();
  if (loader->appLoader) {
    pr_info("Calling Application's loader for '%s'", name);
    ret = loader->appLoader(name, managed_heap_current->api.udata, &new->api.param);
    pr_info("Loaded '%s' result: %d", name, ret);
  } else {
    pr_info("Loader not present or disabled during loading of '%s'", name);
    ret = -ESRCH;
  }
  context_block_gc();
  rwlock_wrlock(&managed_heap_current->api.registry->lock);
  
  if (ret < 0)
    goto failure;
  
  list_add(&new->api.list, &loader->stack);
failure:
  if (ret < 0)
    object_descriptor_free(new);
  
  *result = ret >= 0 ? new : NULL;
  return ret;
}

// Type registry must be locked  when calling this
static int process(struct descriptor_loader_context* loader, struct object_descriptor* current) {
  fh_descriptor_param* param = &current->api.param;
  int ret = 0;
  
  if ((ret = type_registry_add_nolock(managed_heap_current->api.registry, &current->super)) < 0)
    return ret;
  
  current->objectSize = param->size;
  
  static enum reference_strength strengthMapping[FH_REF_COUNT] = {
    [FH_REF_STRONG] = REF_STRONG,
    [FH_REF_PHANTOM] = REF_PHANTOM,
    [FH_REF_WEAK] = REF_WEAK,
    [FH_REF_SOFT] = REF_SOFT,
  };
  
  for (int i = 0; param->fields[i].name; i++) {
    fh_descriptor_field* field = &param->fields[i];
    struct object_descriptor_field convertedField = (struct object_descriptor_field) {
      .offset = field->offset,
      .strength = strengthMapping[field->strength]
    };
    
    struct descriptor* dataDescriptor = type_registry_get_nolock(managed_heap_current->api.registry, field->dataType);
    if (dataDescriptor)
      goto descriptor_found;
    
    struct object_descriptor* newlyLoadedDesriptor;
    if ((ret = loadDescriptor(loader, field->dataType, &newlyLoadedDesriptor)) < 0)
      goto failure;
    
    dataDescriptor = &newlyLoadedDesriptor->super;
descriptor_found:
    convertedField.name = descriptor_get_name(dataDescriptor);
    convertedField.dataType = dataDescriptor;
    
    if (vec_push(&current->fields, convertedField) < 0) {
      ret = -ENOMEM;
      goto failure;
    }
  }
  
failure:
  return ret;
}

API_FUNCTION_DEFINE(int, fh_define_descriptor, __FLUFFYHEAP_NONNULL(__FLUFFYHEAP_NULLABLE(fh_descriptor*)*), resultDescriptor, __FLUFFYHEAP_NONNULL(const char*), name, __FLUFFYHEAP_NONNULL(fh_descriptor_param*), parameter, bool, dontInvokeLoader) {
  if (!isValidObjectName(name))
    return -EINVAL;

  enterClassLoaderExclusive();
  
  descriptor_stack stack = {};
  struct descriptor_loader_context loader = {
    .appLoader = !dontInvokeLoader ? managed_heap_current->api.descriptorLoader : NULL
  };
  list_head_init(&loader.newlyAddedDescriptor);
  list_head_init(&loader.stack);
  
  int ret = 0;
  struct object_descriptor* newDescriptor = object_descriptor_new();
  if (!newDescriptor)
    return -ENOMEM;
  descriptor_acquire(&newDescriptor->super);
  
  // GC is blocked to prevent it trying to access descriptors
  context_block_gc();
  rwlock_wrlock(&managed_heap_current->api.registry->lock);
  vec_init(&stack);
  
  newDescriptor->api.param = *parameter;
  if (!(newDescriptor->name = strdup(name))) {
    ret = -ENOMEM;
    goto failure;
  }
  
  list_add(&newDescriptor->api.list, &loader.stack);
  while (!list_is_empty(&loader.stack)) {
    struct object_descriptor* current = list_first_entry(&loader.stack, struct object_descriptor, api.list);
    
    // Add so later can find it
    list_del(&current->api.list);
    list_add(&current->api.list, &loader.newlyAddedDescriptor);
    
    if ((ret = process(&loader, current)) < 0)
      goto failure;
  }
  
failure:;
  struct list_head* currentEntry;
  struct list_head* next;
  list_for_each_safe(currentEntry, next, &loader.newlyAddedDescriptor) {
    struct object_descriptor* current = list_entry(currentEntry, struct object_descriptor, api.list);
    list_del(currentEntry);
    
    if (ret < 0) {
      type_registry_remove_nolock(managed_heap_current->api.registry, &current->super);
      object_descriptor_free(current);
    } else {
      object_descriptor_init(current);
      list_add(&current->super.list, &managed_heap_current->descriptorList);
      pr_info("Descriptor '%s' was loaded", current->name);
    }
  }
  
  vec_deinit(&stack);
  
  rwlock_unlock(&managed_heap_current->api.registry->lock);
  context_unblock_gc();
  
  exitClassLoaderExclusive();
  
  if (ret < 0)
    pr_info("Failed loading '%s'", name);
  if (ret >= 0)
    *resultDescriptor = EXTERN(&newDescriptor->super);
  return ret;
}

static struct descriptor* getDescriptor(const char* name) {
  if (!isValidObjectName(name))
    return NULL;
  
  context_block_gc();
  rwlock_rdlock(&managed_heap_current->api.registry->lock);
  
  struct descriptor* desc = type_registry_get_nolock(managed_heap_current->api.registry, name);
  
  // Skip acquire if its first get to the descriptor
  if (desc)
    descriptor_acquire(desc);
  
  rwlock_unlock(&managed_heap_current->api.registry->lock);
  context_unblock_gc();
  
  BUG_ON(desc->type != OBJECT_NORMAL);
  return desc;
}

API_FUNCTION_DEFINE(__FLUFFYHEAP_NULLABLE(fh_descriptor*), fh_get_descriptor, __FLUFFYHEAP_NONNULL(const char*), name, bool, dontInvokeLoader) {
  if (!isValidObjectName(name))
    return NULL;

  struct descriptor* desc = getDescriptor(name);
  
  if (desc || dontInvokeLoader)
    return desc ? EXTERN(desc) : NULL;

  enterClassLoaderExclusive();
 
  struct descriptor* loadedDesc = NULL;
  // This thread may have been blocked by another loader which potentially
  // loads what this thread needs so recheck
  if ((loadedDesc = getDescriptor(name)) != NULL)
    goto false_negative;
  
  fh_descriptor_param param;
  int ret = managed_heap_current->api.descriptorLoader(name, managed_heap_current->api.udata, &param);
  if (ret >= 0) {
    fh_descriptor* tmp = NULL;
    ret = fh_define_descriptor(&tmp, name, &param, dontInvokeLoader);
    if (ret < 0)
      loadedDesc = INTERN(tmp);
  }
  
  if (ret < 0)
    goto failure;
  
false_negative:
failure:
  exitClassLoaderExclusive();
  return EXTERN(loadedDesc);
}

API_FUNCTION_DEFINE_VOID(fh_release_descriptor, __FLUFFYHEAP_NULLABLE(fh_descriptor*), desc) {
  descriptor_release(INTERN(desc));
}

API_FUNCTION_DEFINE(__FLUFFYHEAP_NONNULL(const fh_descriptor_param*), fh_descriptor_get_param, __FLUFFYHEAP_NONNULL(fh_descriptor*), self) {
  struct descriptor* desc = INTERN(self);
  BUG_ON(desc->type != OBJECT_NORMAL);
  return &container_of(desc, struct object_descriptor, super)->api.param;
}



