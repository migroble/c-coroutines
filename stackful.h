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

/*
 * stackful.h
 *
 * Stackful coroutines (resumable functions) for GNU C.
 */

#include <stdbool.h>

typedef struct coro coro_t;
typedef void (*coro_fn_t)(coro_t *coro, void *arg);

// Create a coroutine.
//
// NOTE: This method does not start the coroutine.
// NOTE: Coroutines must be created and destroyed in the same thread.
coro_t *coro_create(coro_fn_t fn, void *arg);

// Destroy a coroutine.
//
// SAFETY: The coroutine must have finished running.
// NOTE: Coroutines must be created and destroyed in the same thread.
void coro_destroy(coro_t *coro);

// Reset the coroutine. Useful for reusing coroutine objects.
void coro_reset(coro_t *coro, coro_fn_t fn, void *arg);

// Gets the return value pointer from a coroutine.
void *coro_value(coro_t *coro);

// Returns whether the coroutine is currently running.
bool coro_is_running(coro_t *coro);

// Returns whether the coroutine has finished running.
bool coro_is_done(coro_t *coro);

// Resumes a coroutine.
//
// SAFETY: The coroutine must not be currently running and must not have
// finished running.
// NOTE: May be called from a different thread or context as the one that
// created it.
void coro_resume(coro_t *coro);

// Stops the currently executing coroutine.
//
// SAFETY: Must only be called within a the coroutine that is being suspended.
void coro_suspend(coro_t *coro);

// Stops the currently executing coroutine and sets a return value pointer.
//
// SAFETY: Must only be called within a the coroutine that is being suspended.
void coro_await(coro_t *coro, void *value);
