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
typedef void (*co_destructor_t)(char (*)[]);

typedef struct __co_ctx_s {
  void *__pc;
  co_destructor_t __dtor;
  struct __co_ctx_s *__nested_ctx;
  char __user_ctx[];
} __co_ctx_t;

// Opaque pointer to coroutine context
typedef __co_ctx_t *co_ctx_t;

// Macro to generate the coroutine context parameter
#define co_ctx_param co_ctx_t *__ctx_p

// Macro to generate the coroutine destructor context parameter
#define co_ctx_dtor_param char(*__user_ctx)[]

// Macro to initialize the coroutine. It provides the following variables:
//  - ctx: This coroutines persistent context
//  - subctx: Context to be used for nested coroutines
#define co_init(ctx_type)                                                      \
  __co_ctx_t *__ctx;                                                           \
  ctx_type *ctx;                                                               \
  co_ctx_t subctx;                                                             \
                                                                               \
  do {                                                                         \
    if (__ctx_p == NULL) {                                                     \
      abort();                                                                 \
    }                                                                          \
                                                                               \
    __ctx = *__ctx_p;                                                          \
    if (__ctx == NULL) {                                                       \
      __ctx = malloc(sizeof(__co_ctx_t) + sizeof(ctx_type));                   \
      __ctx->__pc = &&__begin;                                                 \
      __ctx->__dtor = NULL;                                                    \
      __ctx->__nested_ctx = NULL;                                              \
      *__ctx_p = __ctx;                                                        \
    }                                                                          \
                                                                               \
    ctx = (void *)&__ctx->__user_ctx;                                          \
    subctx = __ctx->__nested_ctx;                                              \
                                                                               \
    goto * __ctx->__pc;                                                        \
                                                                               \
  __begin:;                                                                    \
  } while (0)

#define co_init_dtor(ctx_type)                                                 \
  ctx_type *ctx;                                                               \
                                                                               \
  do {                                                                         \
    ctx = (void *)__user_ctx;                                                  \
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
    __ctx->__pc = &&label;                                                     \
    return __VA_ARGS__;                                                        \
  label:;                                                                      \
  } while (0)

// Return from the coroutine, returning the given value if the function is
// non-void, and freeing its context
#define co_return(...)                                                         \
  do {                                                                         \
    co_free(__ctx_p);                                                          \
    return __VA_ARGS__;                                                        \
  } while (0)

// Clean up a coroutine's context
static inline void co_free(co_ctx_t *ctx_p) {
  if (ctx_p == NULL || *ctx_p == NULL) {
    return;
  }

  __co_ctx_t *ctx = *ctx_p;

  co_free(&ctx->__nested_ctx);

  if (ctx->__dtor != NULL) {
    ctx->__dtor(&ctx->__user_ctx);
  }

  free(ctx);
  *ctx_p = NULL;
}

static inline int co_is_running(co_ctx_t *ctx_p) {
  return ctx_p != NULL && *ctx_p != NULL;
}

#endif // _CORO_H_
