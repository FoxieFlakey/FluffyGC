#ifndef _headers_1686393086_FluffyGC_address_spaces
#define _headers_1686393086_FluffyGC_address_spaces

#include "attributes.h"

// Address into heap which must not be accessed
// by derefenrencing
#define address_heap ATTRIBUTE_ADDRESS_SPACE(0x0001)

#endif

