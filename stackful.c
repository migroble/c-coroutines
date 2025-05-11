/* Copyright (c) 2025 Miguel Robledo
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

#include "stackful.h"

#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <ucontext.h>
#include <unistd.h>

// NOTE: These are arbitrary values.
#define STACK_SIZE_BYTES (4 * 1024 * 1024)
#define MIN_STACK_OVERHEAD_BYTES (4 * 1024)

#define PTR_GET_HIGH(p) (int)(((long)(p)) >> (8 * sizeof(int)))
#define PTR_GET_LOW(p) (int)(((long)(p)) & INT_MAX)
#define PTR_FROM_PARTS(p_high, p_low)                                          \
  (void *)(((long)(p_high)) << (8 * sizeof(int)) | (((long)p_low) & INT_MAX))

typedef uint8_t coro_flags_t;

typedef enum coro_flag : coro_flags_t {
  CORO_FLAG_RUNNING = 1U << 0,
  CORO_FLAG_DONE = 1U << 1,
} coro_flag_t;

struct coro {
  ucontext_t ucp;
  // Return value pointer. Used for passing values to and from the coroutine.
  void *value;
  // Coroutine that resumed this one. NULL if resumed from main "thread".
  coro_t *parent;
  coro_flags_t flags;
  // Used for debugging.
  coro_fn_t fn;
  coro_t *next;
  coro_t *prev;
};

static _Thread_local ucontext_t g_main_context;
static _Thread_local coro_t *g_current_coro;

// Linked list of coroutines. Used for debugging.
static _Thread_local coro_t *g_coros = NULL;

static void *allocate_stack(size_t length) {
  void *stack = mmap(NULL, length, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
  if (stack == (void *)-1) {
    abort();
  }

  return stack;
}

static void free_stack(void *stack, size_t length) { munmap(stack, length); }

static void trim_stack_pages(void *stack, size_t length, size_t min_overhead) {
  void *fp = __builtin_frame_address(0);
  int page_size = getpagesize();
  size_t unused_length = fp - stack;
  size_t mask = ~(page_size - 1);

  madvise(stack, (unused_length - min_overhead) & mask, MADV_DONTNEED);
}

static void coro_trampoline(int coro_ptr_high, int coro_ptr_low,
                            int fn_ptr_high, int fn_ptr_low, int arg_ptr_high,
                            int arg_ptr_low) {
  coro_t *coro = PTR_FROM_PARTS(coro_ptr_high, coro_ptr_low);
  coro_fn_t fn = PTR_FROM_PARTS(fn_ptr_high, fn_ptr_low);
  void *arg = PTR_FROM_PARTS(arg_ptr_high, arg_ptr_low);

  fn(coro, arg);

  // We trim the stack instead of immediately destroying it to avoid having to
  // allocate it again if the user wants to reuse this coroutine.
  trim_stack_pages(coro->ucp.uc_stack.ss_sp, coro->ucp.uc_stack.ss_size,
                   MIN_STACK_OVERHEAD_BYTES);

  coro->flags |= CORO_FLAG_DONE;
}

coro_t *coro_create(coro_fn_t fn, void *arg) {
  coro_t *coro = calloc(1, sizeof(coro_t));

  _Static_assert(
      sizeof(void *) == 2 * sizeof(int),
      "This implementation assumes pointers are twice the size of an int.");

  coro_reset(coro, fn, arg);

  return coro;
}

void coro_destroy(coro_t *coro) {
  if (coro == NULL) {
    return;
  }

  // It is not safe to destroy a running coroutine.
  assert(coro_is_done(coro));

  // Unlink from list of coroutines.
  coro->next->prev = coro->prev;
  coro->prev->next = coro->next;

  // Fix list head if needed.
  if (g_coros == coro) {
    if (coro->next == coro->prev) {
      // If this is the last coroutine, there's no head.
      g_coros = NULL;
    } else {
      g_coros = coro->next;
    }
  }

  free(coro);
}

void coro_reset(coro_t *coro, coro_fn_t fn, void *arg) {
  coro->parent = NULL;
  coro->value = NULL;
  coro->flags = 0;
  coro->fn = fn;

  if (getcontext(&coro->ucp) == -1) {
    abort();
  }

  coro->ucp.uc_stack.ss_sp = allocate_stack(STACK_SIZE_BYTES);
  coro->ucp.uc_stack.ss_size = STACK_SIZE_BYTES;
  if (g_current_coro == NULL) {
    coro->ucp.uc_link = &g_main_context;
  } else {
    coro->ucp.uc_link = &g_current_coro->ucp;
  }

  makecontext(&coro->ucp, (void *)coro_trampoline, 6, PTR_GET_HIGH(coro),
              PTR_GET_LOW(coro), PTR_GET_HIGH(fn), PTR_GET_LOW(fn),
              PTR_GET_HIGH(arg), PTR_GET_LOW(arg));

  if (g_coros == NULL) {
    g_coros = coro;
  }

  // Link to list of coroutines.
  coro->next = g_coros;
  coro->prev = g_coros->prev;

  g_coros->prev = coro;
  coro->prev->next = coro;
}

void *coro_value(coro_t *coro) { return coro->value; }

bool coro_is_running(coro_t *coro) {
  return (coro->flags & CORO_FLAG_RUNNING) != 0;
}

bool coro_is_done(coro_t *coro) { return (coro->flags & CORO_FLAG_DONE) != 0; }

// Used for setting breakpoints when the current coroutine is suspended.
static void coro_post_resume(coro_t *coro) { (void)coro; };

void coro_resume(coro_t *coro) {
  assert(!coro_is_done(coro));
  assert(!coro_is_running(coro));

  coro->parent = g_current_coro;
  g_current_coro = coro;

  ucontext_t *prev_ctx =
      coro->parent == NULL ? &g_main_context : &coro->parent->ucp;

  swapcontext(prev_ctx, &coro->ucp);

  g_current_coro = coro->parent;
  coro->parent = NULL;

  coro_post_resume(coro);
}

// Used for setting breakpoints when a coroutine is resumed.
static void coro_post_suspend(coro_t *coro) { (void)coro; };

void coro_suspend(coro_t *coro) {
  assert(coro == g_current_coro);
  assert(!coro_is_done(coro));

  // We do the stack bookkeeping here because coroutines will either suspend or
  // eventually finish.
  trim_stack_pages(coro->ucp.uc_stack.ss_sp, coro->ucp.uc_stack.ss_size,
                   MIN_STACK_OVERHEAD_BYTES);

  ucontext_t *next_ctx =
      coro->parent == NULL ? &g_main_context : &coro->parent->ucp;

  swapcontext(&coro->ucp, next_ctx);

  coro_post_suspend(coro);
}

void coro_await(coro_t *coro, void *value) {
  coro->value = value;
  coro_suspend(coro);
}
