// Copyright © 2025 Miguel Robledo
// SPDX-License-Identifier: MIT

#include "coro/coro.h"

#include <assert.h>
#include <emmintrin.h>
#include <stdint.h>
#include <stdio.h>

coro_promise_t *create_promise(int *ret) {
  return coro_promise_create_with_ptr(ret);
}

void coroutine(void *arg) {
  (void)arg;

  for (int i = 0; i < 10; ++i) {
    coro_return(&i);
  }
}

void nesting_coroutine(void *arg) {
  coroutine(arg);
  coroutine(arg);
}

void spawning_coroutine(void *arg) {
  (void)arg;

  coro_t *inner = coro_create(coroutine, nullptr);
  coro_t *inner2 = coro_create(coroutine, nullptr);

  while (coro_resume(inner)) {
    int *n = coro_value(inner);
    printf("spawning_coroutine: inner: %d\n", *n);
  }

  while (coro_resume(inner2)) {
    int *n = coro_value(inner2);
    printf("spawning_coroutine: inner2: %d\n", *n);
  }

  coro_destroy(inner);
  coro_destroy(inner2);
}

typedef void (*cb_t)(int value, void *arg);

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
cb_t g_cb = nullptr;
void *g_arg = nullptr;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

coro_promise_t *do_something_with_callback(cb_t cb, int *ret) {
  coro_promise_t *promise = coro_promise_create_with_ptr(ret);

  g_cb = cb;
  g_arg = promise;

  return promise;
}

void callback(int value, void *arg) {
  coro_promise_t *promise = arg;

  int *ret = coro_promise_ptr(promise);
  *ret = value;

  coro_resume(coro_promise_fulfill(promise));
}

void coroutine_with_callback(void *arg) {
  int x = 0;
  int y = 0;

  coro_promise_t *x_promise = do_something_with_callback(callback, &x);
  coro_await(x_promise);

  coro_promise_t *y_promise = do_something_with_callback(callback, &y);
  coro_await(y_promise);

  int *n = arg;
  *n = x + y;

  coro_promise_destroy(x_promise);
  coro_promise_destroy(y_promise);
}

int main(void) {
  int n = 0;

  coro_t *coro = coro_create(coroutine, nullptr);
  coro_t *nesting_coro = coro_create(nesting_coroutine, nullptr);
  coro_t *spawning_coro = coro_create(spawning_coroutine, nullptr);
  coro_t *coro_cb = coro_create(coroutine_with_callback, &n);

  while (coro_resume(coro)) {
    int *n = coro_value(coro);
    printf("coroutine: %d\n", *n);
  }

  while (coro_resume(nesting_coro)) {
    int *n = coro_value(nesting_coro);
    printf("nesting_coroutine: %d\n", *n);
  }

  assert(!coro_resume(spawning_coro));

  while (1) {
    if (g_cb == nullptr) {
      if (!coro_resume(coro_cb)) {
        break;
      }
    } else {
      cb_t cb = g_cb;
      void *arg = g_arg;
      g_cb = nullptr;
      g_arg = nullptr;

      cb(21, arg);
    }
  }

  printf("coroutine_with_callback: %d\n", n);

  coro_reset(coro, coroutine, nullptr);

  while (coro_resume(coro)) {
    int *n = coro_value(coro);
    printf("coroutine: %d\n", *n);
  }

  coro_destroy(coro);
  coro_destroy(nesting_coro);
  coro_destroy(spawning_coro);
  coro_destroy(coro_cb);
}
