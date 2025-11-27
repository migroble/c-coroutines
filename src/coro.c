// Copyright © 2025 Miguel Robledo
// SPDX-License-Identifier: MIT

#include "coro/coro.h"

#include <stdio.h>
#include <assert.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#if defined(_MSC_VER)
#include <intrin.h>
#include <windows.h>
#elif defined(__i386__) || defined(__x86_64__)
#if defined(__clang__)
#include <immintrin.h>
#endif
#endif

#if defined(__GNUC__) || defined(_MSC_VER)
#if __SANITIZE_ADDRESS__
#define ASAN_ENABLED
#endif
#if __SANITIZE_THREAD__
#define TSAN_ENABLED
#endif
#elif defined(__clang__)
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define ASAN_ENABLED
#endif
#if __has_feature(thread_sanitizer)
#define TSAN_ENABLED
#endif
#endif
#endif

#if defined(ASAN_ENABLED)
#include <sanitizer/asan_interface.h>
#endif

#if defined(TSAN_ENABLED)
#include <execinfo.h>
#include <sanitizer/tsan_interface.h>
#endif

#include "libco.h"

// NOTE: These are arbitrary values.
static const int STACK_SIZE_BYTES = 4 * 1024 * 1024;
static const int MIN_STACK_OVERHEAD_BYTES = 4 * 1024;

typedef uint8_t coro_flags_t;

typedef enum coro_flag : coro_flags_t {
  CORO_FLAG_INITIALIZED = 1U << 0,
  CORO_FLAG_RUNNING = 1U << 1,
  CORO_FLAG_DONE = 1U << 2,
} coro_flag_t;

typedef struct stack {
  void *base;
  size_t size;
} stack_t;

struct coro {
  cothread_t thread;
  stack_t stack;
  coro_fn_t fn;
  void *arg;
  // Coroutine that resumed this one. nullptr if resumed from main "thread".
  coro_t *parent;
  // Return value pointer. Used for passing values to and from the coroutine.
  void *value;
  _Atomic coro_flags_t flags;
#if defined(ASAN_ENABLED)
  void *asan_fake_stack;
#endif
#if defined(TSAN_ENABLED)
  void *tsan_fiber;
#endif
};

typedef uint8_t coro_promise_flags_t;

typedef enum coro_promise_flag : coro_promise_flags_t {
  CORO_PROMISE_FLAG_AWAITING = 1U << 0,
  CORO_PROMISE_FLAG_FULFILLED = 1U << 1,
} coro_promise_flag_t;

struct coro_promise {
  void *value;
  coro_t *coro;
  _Atomic coro_promise_flags_t flags;
};

// These thread-local global variables are required to represent the main
// context (i.e. the default thread context) and the current thread context.
//
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
static thread_local coro_t g_main_context;
static thread_local coro_t *g_current_coro;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

static inline void yield() {
#if defined(_MSC_VER)
  YieldProcessor();
#elif defined(__i386__) || defined(__x86_64__)
#if defined(__clang__)
  _mm_pause();
#else
  __builtin_ia32_pause();
#endif
#elif defined(__arm__)
  // This intrinsic should fail to be found if YIELD is not supported on the
  // current processor.
  __yield();
#endif
}

static void stack_init(stack_t *stack, size_t length) {
  stack->base = mmap(nullptr, length, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);

  // We should never fail to allocate a stack.
  assert(stack != (void *)-1L);

  stack->size = length;
}

static void stack_fini(stack_t *stack) {
  munmap(stack->base, stack->size);

  stack->base = nullptr;
}

static inline coro_flags_t coro_flags(const coro_t *coro) {
  return atomic_load_explicit(&coro->flags, memory_order_relaxed);
}

static stack_t *coro_stack(coro_t *coro) { return &coro->stack; }

static bool coro_is_initialized(const coro_t *coro) {
  return (coro_flags(coro) & CORO_FLAG_INITIALIZED) != 0;
}

static bool coro_is_running(const coro_t *coro) {
  return (coro_flags(coro) & CORO_FLAG_RUNNING) != 0;
}

static bool coro_is_done(const coro_t *coro) {
  return (coro_flags(coro) & CORO_FLAG_DONE) != 0;
}

#if defined(ASAN_ENABLED)
static void coro_trim_stack_pages(coro_t *coro, size_t min_overhead) {
  (void)coro;
  (void)min_overhead;
}
#else
static void *get_stack_ptr() {
#if defined(__GNUC__)
  return __builtin_frame_address(0);
#elif defined(_MSC_VER)
  return _AddressOfReturnAddress();
#else
  char x = 0;
  // The volatile store here is intended to prevent the compiler from optimizing
  // the char away.
  char *volatile ptr = &x;
  return ptr;
#endif
}

static void coro_trim_stack_pages(coro_t *coro, size_t min_overhead) {
  void *fp = get_stack_ptr();
  unsigned int page_size = getpagesize();

  const stack_t *stack = coro_stack(coro);
  size_t unused_length = (uintptr_t)fp - (uintptr_t)stack->base;
  size_t mask = ~(page_size - 1);

  assert(unused_length <= stack->size);

  madvise(stack->base, (unused_length - min_overhead) & mask, MADV_DONTNEED);
}
#endif

static void coro_swap_context(coro_t *from, coro_t *to) {
  (void)from;

#if defined(ASAN_ENABLED)
  const stack_t *to_stack = coro_stack(to);
  __sanitizer_start_switch_fiber(&from->asan_fake_stack, to_stack->base,
                                 to_stack->size);
#endif

#if defined(TSAN_ENABLED)
  __tsan_switch_to_fiber(to->tsan_fiber, 0);
#endif

  co_switch(to->thread);

#if defined(ASAN_ENABLED)
  // At this point the "from" coroutine has been resumed, so we finish that
  // fiber switch.
  __sanitizer_finish_switch_fiber(to->asan_fake_stack, nullptr, nullptr);
#endif
}

static void coro_trampoline() {
  coro_t *coro = g_current_coro;

#if defined(ASAN_ENABLED)
  stack_t *from_stack = coro_stack(coro->parent);
  __sanitizer_finish_switch_fiber(coro->asan_fake_stack,
                                  (const void **)&from_stack->base,
                                  &from_stack->size);
#endif

  coro->fn(coro->arg);

  // We trim the stack instead of immediately destroying it to avoid having to
  // allocate it again if the user wants to reuse this coroutine.
  coro_trim_stack_pages(coro, MIN_STACK_OVERHEAD_BYTES);

  coro->flags |= CORO_FLAG_DONE;

  coro_suspend();

  // Reaching the end of the trampoline is undefined behaviour since we have
  // nowhere to jump back to.
  abort();
}

coro_t *coro_create(coro_fn_t fn, void *arg) {
  coro_t *coro = calloc(1, sizeof(coro_t));

  if (!coro_is_initialized(&g_main_context)) {
    // Since this is the first coroutine being created in this thread, we must
    // be in the main context.
    g_main_context.thread = co_active();
    atomic_init(&g_main_context.flags,
                CORO_FLAG_INITIALIZED | CORO_FLAG_RUNNING);

    stack_t *stack = coro_stack(&g_main_context);
    stack->base = nullptr;
    stack->size = 0;

#if defined(TSAN_ENABLED)
    g_main_context.tsan_fiber = __tsan_get_current_fiber();
    __tsan_set_fiber_name(g_main_context.tsan_fiber, "main");
#endif

    g_current_coro = &g_main_context;
  }

  coro_reset(coro, fn, arg);

  return coro;
}

void coro_destroy(coro_t *coro) {
  if (coro == nullptr) {
    return;
  }

  // It is not safe to destroy a coroutine while it is running.
  assert(!coro_is_running(coro));

  // It is not safe to destroy a coroutine before it is done as it may lead to
  // resource leaks.
  assert(coro_is_done(coro));

#if defined(TSAN_ENABLED)
  __tsan_destroy_fiber(coro->tsan_fiber);
#endif

  stack_t *stack = coro_stack(coro);
  stack_fini(stack);

  free(coro);
}

void coro_reset(coro_t *coro, coro_fn_t fn, void *arg) {
  assert(!coro_is_initialized(coro) || coro_is_done(coro));

  coro->fn = fn;
  coro->arg = arg;
  coro->parent = nullptr;
  coro->value = nullptr;
  atomic_init(&coro->flags, CORO_FLAG_INITIALIZED);

  stack_t *stack = coro_stack(coro);
  if (stack->base == nullptr) {
    stack_init(stack, STACK_SIZE_BYTES);
  }

  coro->thread = co_derive(stack->base, stack->size, coro_trampoline);

  // We should never fail to create the coroutine context.
  assert(coro->thread != nullptr);

#if defined(TSAN_ENABLED)
  if (coro->tsan_fiber != nullptr) {
    __tsan_destroy_fiber(coro->tsan_fiber);
  }

  coro->tsan_fiber = __tsan_create_fiber(0);

  char **symbols = backtrace_symbols((void *const)&coro->fn, 1);
  __tsan_set_fiber_name(coro->tsan_fiber, symbols[0]);
  free(symbols);
#endif
}

void *coro_value(coro_t *coro) {
  atomic_thread_fence(memory_order_acquire);
  return coro->value;
}

// Used for setting breakpoints when the current coroutine is suspended.
static void coro_post_resume(coro_t *coro) { (void)coro; }

bool coro_resume(coro_t *coro) {
  if (coro == nullptr) {
    return false;
  }

  assert(coro != g_current_coro);
  assert(!coro_is_running(coro));

  if (coro_is_done(coro)) {
    return false;
  }

  coro->parent = g_current_coro;
  g_current_coro = coro;

  atomic_fetch_or_explicit(&coro->flags, CORO_FLAG_RUNNING,
                           memory_order_relaxed);

  coro_swap_context(coro->parent, g_current_coro);

  atomic_fetch_and_explicit(&coro->flags, ~CORO_FLAG_RUNNING,
                            memory_order_relaxed);

  coro_post_resume(coro);

  return !coro_is_done(coro);
}

// Used for setting breakpoints when a coroutine is resumed.
static void coro_post_suspend(coro_t *coro) { (void)coro; }

void coro_suspend() {
  coro_t *coro = g_current_coro;

  assert(coro_is_running(coro));

  // We do the stack bookkeeping here because coroutines will either suspend or
  // eventually finish.
  coro_trim_stack_pages(coro, MIN_STACK_OVERHEAD_BYTES);

  g_current_coro = coro->parent;
  coro->parent = nullptr;

  coro_swap_context(coro, g_current_coro);

  coro_post_suspend(coro);
}

void coro_return(void *value) {
  coro_t *coro = g_current_coro;
  coro->value = value;
  atomic_thread_fence(memory_order_release);

  coro_suspend();
}

void coro_await(coro_promise_t *promise) {
  promise->coro = g_current_coro;
  atomic_thread_fence(memory_order_release);

  const coro_promise_flags_t flags = atomic_fetch_or_explicit(
      &promise->flags, CORO_PROMISE_FLAG_AWAITING, memory_order_relaxed);

  // Promises should only be awaited once.
  assert((flags & CORO_PROMISE_FLAG_AWAITING) == 0);

  if ((flags & CORO_PROMISE_FLAG_FULFILLED) == 0) {
    coro_suspend();
  }
}

coro_promise_t *coro_promise_create() {
  coro_promise_t *promise = calloc(1, sizeof(coro_promise_t));

  promise->value = nullptr;
  promise->coro = nullptr;
  atomic_init(&promise->flags, 0);

  return promise;
}

coro_promise_t *coro_promise_create_with_ptr(void *value) {
  coro_promise_t *promise = coro_promise_create();
  coro_promise_set_ptr(promise, value);

  return promise;
}

void coro_promise_destroy(coro_promise_t *promise) { free(promise); }

coro_t *coro_promise_fulfill(coro_promise_t *promise) {
  // This fence is used for synchronizing the promise's value, which should have
  // already been filled.
  atomic_thread_fence(memory_order_release);

  const coro_promise_flags_t flags = atomic_fetch_or_explicit(
      &promise->flags, CORO_PROMISE_FLAG_FULFILLED, memory_order_relaxed);

  // Promises can only be fulfilled once.
  assert((flags & CORO_PROMISE_FLAG_FULFILLED) == 0);

  if ((flags & CORO_PROMISE_FLAG_AWAITING) != 0) {
    atomic_thread_fence(memory_order_acquire);

    coro_t *coro = promise->coro;

    // If the promise is fulfilled after the coroutine has set the awaiting
    // flag, we need to wait until it is not running anymore because there is a
    // span of time between setting the awaiting flag and actually suspending
    // the coroutine where it would be unsafe to resume it. Note that the
    // coroutine is guaranteed to eventually suspend after setting the awaiting
    // flag.
    while (coro_is_running(coro)) {
      yield();
    }

    assert(coro != nullptr);

    return coro;
  }

  return nullptr;
}

uintptr_t coro_promise_value(coro_promise_t *promise) {
  return (uintptr_t)promise->value;
}

void coro_promise_set_value(coro_promise_t *promise, uintptr_t value) {
  promise->value = (void *)value;
}

void *coro_promise_ptr(coro_promise_t *promise) { return promise->value; }

const void *coro_promise_ptr_const(const coro_promise_t *promise) {
  return promise->value;
}

void coro_promise_set_ptr(coro_promise_t *promise, void *ptr) {
  promise->value = ptr;
}
