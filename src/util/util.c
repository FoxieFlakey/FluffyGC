#include "config.h"
#if !IS_ENABLED(CONFIG_STRICTLY_POSIX)
#define _GNU_SOURCE
#endif

#include <math.h>
#include <time.h>
#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <sys/times.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <regex.h>
#include <stddef.h>

#include "panic.h"
#include "util.h"
#include "bug.h"
#include "macros.h"
#include "concurrency/mutex.h"
#include "logger/logger.h"

DEFINE_LOGGER_STATIC(logger, "Util")
#undef LOGGER_DEFAULT
#define LOGGER_DEFAULT (&logger)

// TODO: In Glibc's <bits/posix_opt.h> it is written that it
// should be checked at runtime and there wasn't any
// indication from the https://pubs.opengroup.org/onlinepubs/9699919799/functions/V2_chap02.html
// that it should be checked

#ifndef _POSIX_TIMERS
# error "POSIX timers unsupported on this platform"
#endif
#ifndef _POSIX_CPUTIME
# error "CLOCK_PROCESS_CPUTIME_ID unsupported on this platform"
#endif
#ifndef _POSIX_THREAD_CPUTIME
# error "CLOCK_THREAD_CPUTIME_ID unsupported on this platform"
#endif
#ifndef _POSIX_MONOTONIC_CLOCK
# error "CLOCK_MONOTONIC unsupported on this platform"
#endif

bool util_atomic_add_if_less_uint(volatile atomic_uint* data, unsigned int n, unsigned int max, unsigned int* result) {
  unsigned int new;
  unsigned int old = atomic_load(data);
  do {
    new = old + n;
     
    if (new >= max)
      return false;
  } while (!atomic_compare_exchange_weak(data, &old, new));

  if (result)
    *result = old;
  return true;
}

bool util_atomic_add_if_less_uintptr(volatile atomic_uintptr_t* data, uintptr_t n, uintptr_t max, uintptr_t* result) {
  uintptr_t new;
  uintptr_t old = atomic_load(data);
  do {
    new = old + n;
    
    if (new >= max)
      return false;
  } while (!atomic_compare_exchange_weak(data, &old, new));

  if (result)
    *result = old;
  return true;
}

bool util_atomic_add_if_less_size_t(volatile atomic_size_t* data, size_t n, size_t max, size_t* result) {
  size_t new;
  size_t old = atomic_load(data);
  do {
    new = old + n;
    
    if (new >= max)
      return false;
  } while (!atomic_compare_exchange_weak(data, &old, new));

  if (result)
    *result = old;
  return true;
}

int util_atomic_min_int(volatile atomic_int* object, int newVal) {
  int new;
  int old = atomic_load(object);
  do {
    if (newVal < old)
      new = newVal;
    else
      return old;
  } while (!atomic_compare_exchange_weak(object, &old, new));

  return old;
}

bool util_atomic_add_if_less_int(volatile atomic_int* data, int n, int max, int* result) {
  int new;
  int old = atomic_load(data);
  do {
    new = old + n;
    
    if (new >= max)
      return false;
  } while (!atomic_compare_exchange_weak(data, &old, new));

  if (result)
    *result = old;
  return true;
}

bool util_vasprintf(char** result, const char* fmt, va_list arg) {
  size_t buflen = vsnprintf(NULL, 0, fmt, arg);
  *result = malloc(buflen + 1);
  if (*result == NULL)
    return false;

  vsnprintf(*result, buflen, fmt, arg);
  return true;
}

static thread_local char currentThreadName[UTIL_MAX_THREAD_NAME_LEN];
void util_set_thread_name(const char* name) {
  strncpy(currentThreadName, name, sizeof(currentThreadName));
}

const char* util_get_thread_name() {
  return currentThreadName;
}

int util_get_core_count() {
  // Casting fine for 99.999% of the time
  // due why would 2 billions cores exists
  return (int) sysconf(_SC_NPROCESSORS_ONLN);
}

size_t util_get_pagesize() {
  long result = sysconf(_SC_PAGESIZE);
  
  if (result == -1)
    return 0;
  
  return result;
}

void* util_mmap_anonymous(size_t size, int protection) {
# if IS_ENABLED(CONFIG_ALLOW_USAGE_OF_ANONYMOUS_MMAP)
  void* addr = mmap(NULL, size, protection, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
  if (addr == MAP_FAILED) {
    switch (errno) {
      case ENOMEM:
        errno = ENOMEM;
        break;
      case EINVAL:
      default:
        errno = EINVAL;
        break;
    }
  }
  return addr == MAP_FAILED ? NULL : addr;
# else
  UNUSED(size);
  UNUSED(protection);
  errno = ENOSYS;
  return NULL;
# endif
}

void util_nearest_power_of_two(size_t* size) {
  size_t x = *size;
  *size = 1;
  while(*size < x && *size < SIZE_MAX)
    *size <<= 1;
}

bool util_is_power_of_two(size_t size) {
  size_t tmp = size;
  util_nearest_power_of_two(&tmp);
  return tmp == size;
}

void* util_malloc(size_t size, unsigned long flags) {
  void* ptr = malloc(size);
  if (!ptr)
    return NULL;
  
  WARN_ON(size == 0);
  if (flags & UTIL_MEM_ZERO)
    memset(ptr, 0, size);
  return ptr;
}

void* util_realloc(void* ptr, size_t oldSize, size_t newSize, unsigned long flags) {
  ptr = realloc(ptr, newSize);
  if (!ptr)
    return NULL;
  
  if ((flags & UTIL_MEM_ZERO) && newSize > oldSize)
    memset((char*) ptr + oldSize, 0, newSize - oldSize);
  
  return ptr;
}

// Less than equal binary search
// https://stackoverflow.com/a/28309550/13447666
const uintptr_t* util_find_smallest_but_larger_or_equal_than(const uintptr_t* array, size_t count, uintptr_t search) {
  const uintptr_t* min =  array;
  const uintptr_t* max =  &array[count - 1];
  const uintptr_t* arrayEnd =  &array[count - 1];
  
  if (count <= 0)
    return NULL;
  if (search <= array[0])
    return array;
  
  while (min <= max) {
    const uintptr_t* mid = min + ((max - min) / 2);
    if (*mid < search)
      min = mid + 1;
    else if (*mid > search)
      max = mid - 1;
    else
      return mid;
  }
  
  if (min > arrayEnd)
    return NULL; // Not found
  return min < max ? min : max + 1;
}

void util_shift_array(void* start, size_t offset, size_t size) {
  memmove((char*) start + (offset * size), start, size);
}

void* util_aligned_alloc(size_t alignment, size_t size) {
  void* res = NULL;
  int ret = 0;
  if ((ret = posix_memalign(&res, alignment, size)) < 0)
    res = NULL;
  return res;
}

void util_nanosleep(unsigned long nanosecs) {
  struct timespec time = {
    .tv_sec = nanosecs / 1'000'000'000L,
    .tv_nsec = nanosecs % 1'000'000'000L
  };
  
  while (nanosleep(&time, &time) == -1 && errno == EINTR)
    ;
  
  BUG_ON(errno == -EINVAL);
}

void util_usleep(unsigned long microsecs) {
  util_nanosleep(microsecs * 1000L);
}

void util_msleep(unsigned int milisecs) {
  util_usleep(milisecs * 1000L);
}

void util_sleep(unsigned int secs) {
  util_msleep(secs * 1000);
}

// Choose either CLOCK_MONOTONIC or CLOCK_MONOTONIC_RAW
static clockid_t getMonotonicClock() {
  // Lets conditionally use CLOCK_MONOTONIC_RAW to cope with
  // system with broken CLOCK_MONOTONIC, like it cost barely
  // anything to add this ifdef statement
  // https://stackoverflow.com/questions/3657289/linux-clock-gettimeclock-monotonic-strange-non-monotonic-behavior
  clockid_t clk = CLOCK_MONOTONIC;
  
  if (IS_ENABLED(CONFIG_STRICTLY_POSIX))
    return clk;

// If strictly POSIX just alias monotonic raw
// with CLOCK_MONOTONIC so next code cause no
// syntax error but will never ran
#if IS_ENABLED(CONFIG_STRICTLY_POSIX)
#ifndef CLOCK_MONOTONIC_RAW
# define CLOCK_MONOTONIC_RAW CLOCK_MONOTONIC
#endif
#endif
  static atomic_bool monotonicRawWorking = false;
  static atomic_bool monotonicRawChecked = false;
  static struct mutex checkMutex = MUTEX_INITIALIZER;

  if (atomic_load(&monotonicRawChecked) == false) {
    mutex_lock(&checkMutex);
    if (atomic_load(&monotonicRawChecked) == true)
      goto already_checked;
    
    struct timespec ts = {};
    // Check if monotonic raw clock available
    int ret;
    while ((ret = clock_nanosleep(CLOCK_MONOTONIC_RAW, TIMER_ABSTIME, &ts, NULL)) == EINTR)
      ;

    if (ret == 0) {
      atomic_store(&monotonicRawWorking, true);
      pr_info("CLOCK_MONOTONIC_RAW available using it");
    } else if (ret == ENOTSUP) {
      pr_info("CLOCK_MONOTONIC_RAW unavailable using CLOCK_MONOTONIC");
    } else {
      pr_alert("clock_nanosleep implementation returns %d please report this", ret);
      pr_alert("treating as if CLOCK_MONOTONIC_RAW unavailable");
    }
    atomic_store(&monotonicRawChecked, true);
    mutex_unlock(&checkMutex);
  }
  
already_checked:
  // If monotonic raw working at runtime use it
  if (atomic_load(&monotonicRawWorking) == true)
    clk = CLOCK_MONOTONIC_RAW;

  return clk;
}

static void commonGetTimespecFromClock(clockid_t clock, struct timespec* ts) {
  if (clock_gettime(clock, ts) < 0)
    panic("Unknown clock");
}

static double commonGetTimeFromClock(clockid_t clock) {
  struct timespec ts = {};
  commonGetTimespecFromClock(clock, &ts);
  return util_timespec_to_double(&ts);
}

double util_get_realtime_time() {
  return commonGetTimeFromClock(CLOCK_REALTIME);
}

double util_get_total_cpu_time() {
  return commonGetTimeFromClock(CLOCK_PROCESS_CPUTIME_ID);
}

double util_get_thread_cpu_time() {
  return commonGetTimeFromClock(CLOCK_THREAD_CPUTIME_ID);
}

double util_get_monotonic_time() {
  return commonGetTimeFromClock(getMonotonicClock());
}

void util_get_realtime_timespec(struct timespec* ts) {
  commonGetTimespecFromClock(CLOCK_REALTIME, ts);
}

void util_get_total_cpu_timespec(struct timespec* ts) {
  commonGetTimespecFromClock(CLOCK_PROCESS_CPUTIME_ID, ts);
}

void util_get_thread_cpu_timespec(struct timespec* ts) {
  commonGetTimespecFromClock(CLOCK_THREAD_CPUTIME_ID, ts);
}

void util_get_monotonic_timespec(struct timespec* ts) {
  commonGetTimespecFromClock(getMonotonicClock(), ts);
}

int util_sleep_until(struct timespec* ts) {
  int ret;
  while ((ret = clock_nanosleep(getMonotonicClock(), TIMER_ABSTIME, ts, NULL)) == EINTR)
    ;
  
  if (ret == ENOTSUP)
    panic("No suitable clock to use");
  
  if (ret > 0)
    ret = -ret;
  return ret;
}

void util_add_timespec(struct timespec* ts, double offset) {
  time_t secsDelta = floor(offset + 0.5f);
  long nanosecsDelta = fmod(offset, 1) * 1'000'000'000;
  
  ts->tv_nsec += nanosecsDelta;
  ts->tv_sec += secsDelta;
  
  // Handling overflows from [0, 1'000'000'000] range
  if (ts->tv_nsec < 0) {
    ts->tv_sec--;
    ts->tv_nsec = 1'000'000'000 + ts->tv_nsec;
  } else if (ts->tv_nsec >= 1'000'000'000) {
    ts->tv_sec++;
    ts->tv_nsec -= 1'000'000'000;
  }
}

double util_timespec_to_double(struct timespec* ts) {
  return (double) ts->tv_sec + (double) ts->tv_nsec / (double) 1'000'000'000;
}

struct timespec util_relative_to_abs(clockid_t clock, double offset) {
  struct timespec abs;
  clock_gettime(clock, &abs);
  util_add_timespec(&abs, offset);
  return abs;
}

// Regex match
bool util_is_matched(const char* regex, const char* string) {
  regex_t compiledRegex;
  int ret;

  if ((ret = regcomp(&compiledRegex, regex, 0)) != 0)
    goto regex_error;

  if ((ret = regexec(&compiledRegex, string, 0, NULL, 0)) != 0) {
    if (ret == REG_NOMATCH)
      return false;
    goto regex_error;
  }
  return true;

// TODO better way than panic
regex_error:;
  char regexErrorBuf[1024];
  regerror(ret, &compiledRegex, regexErrorBuf, sizeof(regexErrorBuf));
  regfree(&compiledRegex);
  panic("Regex error: %s", regexErrorBuf);
}


