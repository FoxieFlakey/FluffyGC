#include <stdbool.h>
#include <stdlib.h>
#include <threads.h>
#include <pthread.h>

#include "list.h"
#include "soc.h"
#include "thread.h"
#include "bug.h"
#include "util.h"

static thread_local struct thread* volatile currentThread = NULL;

struct thread* thread_get_current() {
  return currentThread;
}

struct thread* thread_set_current(struct thread* new) {
  swap(currentThread, new);
  return new;
}

struct thread* thread_new() {
  struct thread* self = malloc(sizeof(*self)); 
  if (!self)
    return NULL;
  *self = (struct thread) {};
  
  self->listNodeCache = soc_new(sizeof(list_node_t), 0);
  if (!self->listNodeCache)
    goto failure;
  
  self->pinnedObjects = list_new();
  self->root = list_new();
  if (!self->pinnedObjects || !self->root)
    goto failure;
  
  return self;
  
failure:
  thread_free(self);
  return NULL;
}

void thread_free(struct thread* self) {
  list_destroy2(self->root);
  list_destroy2(self->pinnedObjects);
  soc_free(self->listNodeCache);
  free(self);
}

void thread_block_gc() {
}

void thread_unblock_gc() {
}

static list_node_t* allocListNodeWithContent(void* data) {
  list_node_t* tmp = soc_alloc(thread_get_current()->listNodeCache);
  *tmp = (list_node_t) {
    .val = data
  };
  return tmp;
}

bool thread_add_pinned_object(struct object* obj) {
  thread_block_gc();
  bool res = true;
  list_node_t* node = allocListNodeWithContent(obj);
  if (!node) {
    res = false;
    goto node_alloc_failure;
  }
  
  list_rpush(currentThread->pinnedObjects, node);
node_alloc_failure:
  thread_unblock_gc();
  return res;
}

void thread_remove_pinned_object(struct object* obj) {
  thread_block_gc();
  // Search backward as it more likely caller removing recently
  // added object. In long run minimizing time spent searching
  // object to be removed UwU
  list_node_t* node = currentThread->pinnedObjects->tail;
  while (node && (node = node->prev)) {
    if (node->val == obj) {
      list_remove2(currentThread->pinnedObjects, node);
      soc_dealloc(thread_get_current()->listNodeCache, node);
      break;
    }
  }
  
  BUG_ON(!node);
  thread_unblock_gc();
}

bool thread_add_root_object(struct object* obj) {
  thread_block_gc();
  bool res = true;
  list_node_t* node = allocListNodeWithContent(obj);
  if (!node) {
    res = false;
    goto node_alloc_failure;
  }
  
  list_rpush(currentThread->root, node);
node_alloc_failure:
  thread_unblock_gc();
  return res;
}

void thread_remove_root_object(struct object* obj) {
  thread_block_gc();
  // Search backward as it more likely caller removing recently
  // added object. In long run minimizing time spent searching
  // object to be removed UwU
  list_node_t* node = currentThread->root->tail;
  while (node && (node = node->prev)) {
    if (node->val == obj) {
      list_remove2(currentThread->root, node);
      soc_dealloc(thread_get_current()->listNodeCache, node);
      break;
    }
  }
  
  BUG_ON(!node);
  thread_unblock_gc();
}

