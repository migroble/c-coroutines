/* Copyright (c) 2024 Miguel Robledo
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * coro.h
 *
 * Stackless coroutines (resumable functions) for GNU C.
 *
 * Inspired by: https://www.chiark.greenend.org.uk/~sgtatham/coroutines.html
 */

#ifndef _CORO_H_
#define _CORO_H_

#include <stdlib.h>

// Cleanup callback
typedef void (*co_destructor_t)(void *);

struct __co_ctx_s {
  struct __co_ctx_inner_s *__ctx;
  void *__ptr;
  co_destructor_t __dtor;
};

// Opaque pointer to coroutine context
typedef struct __co_ctx_s co_ctx_t;

struct __co_ctx_inner_s {
  co_ctx_t __nested;
  struct {
  } __user;
};

// Macro to generate the coroutine context parameter
#define co_ctx_param co_ctx_t *__ctx

// Macro to generate the coroutine context parameter
#define co_ctx_dtor_param void *__user_ctx

#define co_new_ctx                                                             \
  (co_ctx_t) { 0 }

// Macro to define the coroutine context struct
#define co_ctx struct co_ctx_s

// Macro to initialize the coroutine. It provides the following variables:
//  - ctx: This coroutines persistent context
//  - subctx: Context to be used for nested coroutines
#define co_init                                                                \
  co_ctx *ctx;                                                                 \
  co_ctx_t subctx;                                                             \
                                                                               \
  do {                                                                         \
    if (__ctx == NULL) {                                                       \
      abort();                                                                 \
    }                                                                          \
                                                                               \
    if (__ctx->__ctx == NULL) {                                                \
      __ctx->__ctx = malloc(sizeof(struct __co_ctx_inner_s) + sizeof(co_ctx)); \
      __ctx->__ctx->__nested = co_new_ctx;                                     \
      __ctx->__ptr = &&__begin;                                                \
      __ctx->__dtor = NULL;                                                    \
    }                                                                          \
                                                                               \
    ctx = (co_ctx *)&__ctx->__ctx->__user;                                     \
    subctx = __ctx->__ctx->__nested;                                           \
                                                                               \
    goto * __ctx->__ptr;                                                       \
                                                                               \
  __begin:;                                                                    \
  } while (0)

#define co_init_dtor                                                           \
  co_ctx *ctx = (co_ctx *)__user_ctx;                                          \
                                                                               \
  do {                                                                         \
    if (ctx == NULL) {                                                         \
      return;                                                                  \
    }                                                                          \
  } while (0)

// Macro to set the coroutine's destructor. Must be called after co_init
#define co_destructor(fn)                                                      \
  do {                                                                         \
    __ctx->__dtor = &(fn);                                                     \
  } while (0)

#define __co_concat(a, b) a##b
#define __co_label(label) __co_concat(__, label)

// Yield from the coroutine, returning the given value if the function is
// non-void
#define co_yield(...) __co_yield(__co_label(__COUNTER__), __VA_ARGS__)
#define __co_yield(label, ...)                                                 \
  do {                                                                         \
    __ctx->__ptr = &&label;                                                    \
    return __VA_ARGS__;                                                        \
  label:;                                                                      \
  } while (0)

// Return from the coroutine, returning the given value if the function is
// non-void, and freeing its context
#define co_return(...)                                                         \
  do {                                                                         \
    co_free(__ctx);                                                            \
    return __VA_ARGS__;                                                        \
  } while (0)

// Clean up a coroutine's context
static inline void co_free(co_ctx_t *ctx) {
  if (ctx == NULL || ctx->__ctx == NULL) {
    return;
  }

  co_free(&ctx->__ctx->__nested);

  if (ctx->__dtor != NULL) {
    ctx->__dtor(&ctx->__ctx->__user);
  }

  free(ctx->__ctx);
  ctx->__ctx = NULL;
}

static inline int co_is_running(co_ctx_t *ctx) {
  return ctx != NULL && ctx->__ctx != NULL;
}

#endif // _CORO_H_
