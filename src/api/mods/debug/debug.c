#include <stdarg.h>
#include <stdio.h>
#include <threads.h>

#include "attributes.h"
#include "panic.h"
#include "api/api.h"
#include "api/mods/debug/debug.h"
#include "config.h"
#include "context.h"
#include "FluffyHeap.h"
#include "managed_heap.h"
#include "panic.h"

// Debug mode

int debug_check_flags(uint32_t flags) {
  return 0;
}

bool debug_can_do_check() {
  return managed_heap_current->api.state->modManager.modStates[FH_MOD_DEBUG].data.debugMod.verbosity > MOD_DEBUG_SILENT;
}

bool debug_can_do_api_verbose() {
  return managed_heap_current->api.state->modManager.modStates[FH_MOD_DEBUG].data.debugMod.verbosity >= MOD_DEBUG_ULTRA_VERBOSE;
}

int debug_init(struct api_mod_state* self) {
  self->data.debugMod.panicOnWarn = IS_ENABLED(CONFIG_MOD_DEBUG_PANIC);
  self->data.debugMod.verbosity = MOD_DEBUG_ULTRA_VERBOSE;
  return 0;
}

void debug_cleanup(struct api_mod_state* self) {
  return;
}

ATTRIBUTE_PRINTF(1, 2)
static void writeDebugOutputf(const char* fmt, ...) {
  va_list args;
  va_start(args);
  vfprintf(stderr, fmt, args);
  va_end(args);
}

void debug_info(const char* msg, ...) {
  va_list args;
  va_start(args, msg);
  
  static thread_local char buffer[512 * 1024];
  vsnprintf(buffer, sizeof(buffer), msg, args);
  
  writeDebugOutputf("[DEBUG] [INFO] %s\n", buffer);
  va_end(args);
}

void debug_warn(const char* msg, ...) {
  va_list args;
  va_start(args, msg);
  
  static thread_local char buffer[512 * 1024];
  vsnprintf(buffer, sizeof(buffer), msg, args);
  
  if (managed_heap_current->api.state->modManager.modStates[FH_MOD_DEBUG].data.debugMod.panicOnWarn)
    panic("Ouch -w-: %s", buffer);
  else
    writeDebugOutputf("[DEBUG] [WARN] %s\n", buffer);
  va_end(args);
}