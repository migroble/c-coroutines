#ifndef CORO_CORO_H_
#define CORO_CORO_H_

#include <stdint.h>

// A stackful coroutine.
//
// Coroutines can be safely passed between threads, but must only be executed in
// one thread at a time.
//
// Any place where a coroutine is (potentially) suspended (i.e. calls to
// coro_suspend, coro_await, and coro_return) is called a suspension point.
//
// The coroutine's stack is preserved throughout its lifetime, so it is safe to
// store and access data across suspension points.
//
// IMPORTANT: Do *NOT* hold locks across suspension points as it can easily lead
// to deadlocks.
typedef struct coro coro_t;

typedef void (*coro_fn_t)(void *arg);

// A promise object.
//
// When passing or returning this type to and from functions, add an inline
// comment specifying the type of its contents for ease of use. For example:
//
// - When storing a value:
//
//   coro_promise_t*  /* <bool> */
//   do_something();
//
//   void
//   do_something(coro_promise_t* /* <bool> */ promise);
//
// - When storing a pointer:
//
//   coro_promise_t*  /* <buffer_t*> */
//   do_something();
//
//   void
//   do_something(coro_promise_t* /* <buffer_t*> */ promise);
typedef struct coro_promise coro_promise_t;

// Create a coroutine.
//
// NOTE: This method does not start the coroutine.
[[nodiscard]] coro_t *coro_create(coro_fn_t fn, void *arg);

// Destroy a coroutine.
//
// SAFETY: The coroutine must have finished running.
void coro_destroy(coro_t *coro);

// Reset the coroutine. Useful for reusing coroutine objects.
//
// SAFETY: The coroutine must have finished running.
void coro_reset(coro_t *coro, coro_fn_t fn, void *arg);

// Gets the return value pointer from a coroutine.
[[nodiscard]] void *coro_value(coro_t *coro);

// Resumes a coroutine and returns whether it finished or does nothing if coro
// is nullptr.
//
// SAFETY: The coroutine must not be currently running.
// NOTE: May be called from a different thread or context as the one that
// created it.
bool coro_resume(coro_t *coro);

// Stops the currently executing coroutine.
//
// SAFETY: This function is only safe to call from a coroutine context.
void coro_suspend();

// Stops the currently executing coroutine and sets a return value pointer.
//
// SAFETY: This function is only safe to call from a coroutine context.
void coro_return(void *value);

// Stops the currently executing coroutine until a promise is fulfilled. If the
// promise was already fulfilled, the coroutine will not get suspended.
//
// SAFETY: This function is only safe to call from a coroutine context.
void coro_await(coro_promise_t *promise);

[[nodiscard]] coro_promise_t *coro_promise_create();

[[nodiscard]] coro_promise_t *coro_promise_create_with_ptr(void *value);

void coro_promise_destroy(coro_promise_t *promise);

// Marks the promise and fulfilled and returns the coroutine that is awaiting
// this promise or nullptr if none are awaiting it.
//
// IMPORTANT: You should set the promise's value before calling this function.
[[nodiscard]] coro_t *coro_promise_fulfill(coro_promise_t *promise);

[[nodiscard]] uintptr_t coro_promise_value(coro_promise_t *promise);

void coro_promise_set_value(coro_promise_t *promise, uintptr_t value);

[[nodiscard]] void *coro_promise_ptr(coro_promise_t *promise);

[[nodiscard]] const void *coro_promise_ptr_const(const coro_promise_t *promise);

void coro_promise_set_ptr(coro_promise_t *promise, void *ptr);

#endif
