#include <stdint.h>
#include <errno.h>
#include <threads.h>

#include "FluffyHeap.h"
#include "bug.h"
#include "hook/hook.h"
#include "api/mods/debug/debug.h"

static thread_local uint32_t prevDebugFlags;
HOOK_FUNCTION(, __FLUFFYHEAP_NULLABLE(fluffyheap*), debug_hook_fh_new_head, __FLUFFYHEAP_NONNULL(fh_param*), incomingParams) {
  prevDebugFlags = fh_get_flags(FH_MOD_DEBUG);
  
  int ret = fh_enable_mod(FH_MOD_DEBUG, 0);
  BUG_ON(ret == -EINVAL);
  debug_info("Always enabled activated!");
  
  ci->action = HOOK_CONTINUE;
  return;
}

HOOK_FUNCTION(, __FLUFFYHEAP_NULLABLE(fluffyheap*), debug_hook_fh_new_tail, __FLUFFYHEAP_NONNULL(fh_param*), incomingParams) {
  fh_disable_mod(FH_MOD_DEBUG);
  
  // Restore flags if the debug mod was enabled
  if ((prevDebugFlags & FH_MOD_WAS_ENABLED) != 0)
    fh_enable_mod(FH_MOD_DEBUG, prevDebugFlags & (~FH_MOD_WAS_ENABLED));
  
  ci->action = HOOK_CONTINUE;
  return;
}