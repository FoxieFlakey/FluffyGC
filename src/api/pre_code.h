#ifndef _headers_1686662453_FluffyGC_pre_code
#define _headers_1686662453_FluffyGC_pre_code

#include "attributes.h"
#include "api.h"

#define __FLUFFYHEAP_EXPORT ATTRIBUTE((used)) ATTRIBUTE((visibility("default"))) extern

#define INTERN(c) API_INTERN(c)

#define EXTERN(c) API_EXTERN(c)

// Arrays need explicit cast (and also checks whether
// its appropriately fh_object* to ensure safe cast)
#define CAST_TO_ARRAY(c) _Generic ((c), \
  fh_object*: (fh_array*) (c) \
)

#endif
