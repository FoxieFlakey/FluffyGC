#ifndef _headers_1667482340_CProjectTemplate_bug
#define _headers_1667482340_CProjectTemplate_bug

#include "logger/logger.h"
#include "panic.h"

// Linux kernel style bug 

// Time out needed incase of BUG inside
// logger thread which leads to deadlock
#define BUG() do { \
  pr_fatal("BUG: failure at %s:%d/%s()!", __FILE__, __LINE__, __func__); \
  panic("BUG triggered!"); \
} while(0)

#define BUG_ON(cond) do { \
  if (cond)  \
    BUG(); \
} while(0)

#define WARN() do { \
  pr_warn("WARN: warning at %s:%d/%s()!", __FILE__, __LINE__, __func__); \
} while(0)

#define WARN_ON(cond) do { \
  if (cond)  \
    WARN(); \
} while(0)

#endif

