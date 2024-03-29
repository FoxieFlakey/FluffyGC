#include <stdio.h>
#include <pthread.h>

#include "logger/logger.h"
#include "specials.h"
#include "config.h"
#include "bug.h"
#include "public.h"
#include "premain.h" // IWYU pragma: keep
#include "main.h"

struct worker_args {
  int* ret;
  int argc;
  const char** argv;
};

static void* testWorker(void* _args) {
  struct worker_args* args = _args;
  *args->ret = main2(args->argc, args->argv);
  return NULL;
}

PUBLIC int fluffygc_main(int argc, const char** argv) {
  special_premain(argc, argv);

  int res = 0;
  struct worker_args args = {
    .ret = &res,
    .argc = argc,
    .argv = argv
  };
  
  if (IS_ENABLED(CONFIG_DONT_START_SEPERATE_MAIN_THREAD)) {
    testWorker(&args);
  } else {
    pthread_t tmp;
    res = pthread_create(&tmp, NULL, testWorker, &args);
    BUG_ON(res != 0);
    pthread_join(tmp, NULL);
  }

  pr_info("Exiting :3");
  return res;
}



