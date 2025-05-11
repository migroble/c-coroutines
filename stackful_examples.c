#include "stackful.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

void coroutine(coro_t *coro, void *arg) {
  (void)arg;

  for (int i = 0; i < 10; ++i) {
    coro_await(coro, &i);
  }
}

void nesting_coroutine(coro_t *coro, void *arg) {
  coroutine(coro, arg);
  coroutine(coro, arg);
}

void spawning_coroutine(coro_t *coro, void *arg) {
  coro_t *inner = coro_create(coroutine, NULL);
  coro_t *inner2 = coro_create(coroutine, NULL);

  while (1) {
    coro_resume(inner);
    if (coro_is_done(inner)) {
      break;
    }

    int *n = coro_value(inner);
    printf("spawning_coroutine: inner: %d\n", *n);
  }

  while (1) {
    coro_resume(inner2);
    if (coro_is_done(inner2)) {
      break;
    }

    int *n = coro_value(inner2);
    printf("spawning_coroutine: inner2: %d\n", *n);
  }
}

typedef void (*cb_t)(int value, void *arg);

cb_t CB = NULL;
void *ARG = NULL;

void do_something_with_callback(cb_t cb, void *arg) {
  CB = cb;
  ARG = arg;
}

void callback(int value, void *arg) {
  coro_t *coro = arg;

  int *ret = coro_value(coro);
  *ret = value;

  coro_resume(coro);
}

void coroutine_with_callback(coro_t *coro, void *arg) {
  int x = 0;
  int y = 0;

  do_something_with_callback(callback, coro);
  coro_await(coro, &x);

  do_something_with_callback(callback, coro);
  coro_await(coro, &y);

  int *n = arg;
  *n = x + y;
}

int main(void) {
  // Coroutines can't access the main stack, the result has to be on the heap.
  int *n = calloc(1, sizeof(int));

  coro_t *coro = coro_create(coroutine, NULL);
  coro_t *nesting_coro = coro_create(nesting_coroutine, NULL);
  coro_t *spawning_coro = coro_create(spawning_coroutine, NULL);
  coro_t *coro_cb = coro_create(coroutine_with_callback, n);

  while (1) {
    coro_resume(coro);
    if (coro_is_done(coro)) {
      break;
    }

    int *n = coro_value(coro);
    printf("coroutine: %d\n", *n);
  }

  while (1) {
    coro_resume(nesting_coro);
    if (coro_is_done(nesting_coro)) {
      break;
    }

    int *n = coro_value(nesting_coro);
    printf("nesting_coroutine: %d\n", *n);
  }

  coro_resume(spawning_coro);
  assert(coro_is_done(spawning_coro));

  while (1) {
    if (CB == NULL) {
      coro_resume(coro_cb);
    } else {
      cb_t cb = CB;
      void *arg = ARG;
      CB = NULL;
      ARG = NULL;

      cb(21, arg);
    }

    if (coro_is_done(coro_cb)) {
      break;
    }
  }

  printf("coroutine_with_callback: %d\n", *n);

  coro_reset(coro, coroutine, NULL);

  while (1) {
    coro_resume(coro);
    if (coro_is_done(coro)) {
      break;
    }

    int *n = coro_value(coro);
    printf("coroutine: %d\n", *n);
  }

  coro_destroy(coro);
  coro_destroy(nesting_coro);
  coro_destroy(spawning_coro);
  coro_destroy(coro_cb);

  free(n);
}
