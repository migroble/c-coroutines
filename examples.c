#include <stdio.h>
#include <sys/ucontext.h>

#include "coro.h"

int coroutine(co_ctx_param) {
  // Declare the coroutine's context
  co_ctx {
    int i;
  };

  // Initialize the coroutine
  co_init;

  for (ctx->i = 0; ctx->i < 10; ++ctx->i) {
    // Yield values to the caller
    co_yield (ctx->i);
  }

  // Finish the coroutine and return a value
  co_return (-1);
}

void void_coroutine(co_ctx_param) {
  co_ctx { int i; };

  co_init;

  for (ctx->i = 0; ctx->i < 10; ++ctx->i) {
    printf("void_coroutine: %d\n", ctx->i);

    // Yield to the caller
    co_yield ();
  }

  // Finish the coroutine
  co_return ();
}

int parametrized_coroutine(co_ctx_param, int offset) {
  co_ctx { int i; };

  co_init;

  for (ctx->i = 0; ctx->i < 10; ++ctx->i) {
    co_yield (ctx->i + offset);
  }

  co_return (-1);
}

void custom_dtor(co_ctx_dtor_param) {
  // Declare the destructor's context. It must be the same definition as the
  // coroutine's context
  co_ctx { void *ptr; };

  // Initialize the destructor's context
  co_init_dtor;

  printf("Running custom destructor\n");

  free(ctx->ptr);
}

void custom_dtor_coroutine(co_ctx_param) {
  co_ctx { void *ptr; };

  co_init;

  ctx->ptr = malloc(1);

  // Set destructor function
  co_destructor(custom_dtor);

  co_return ();
}

void nesting_coroutine(co_ctx_param) {
  co_ctx{};

  co_init;

  printf("First nested coroutine\n");
  do {
    void_coroutine(&subctx);
    co_yield ();
  } while (co_is_running(&subctx));

  printf("Second nested coroutine\n");
  do {
    void_coroutine(&subctx);
    co_yield ();
  } while (co_is_running(&subctx));

  co_return ();
}

// Standard state machine implementation
void state_machine(int *state, int event) {
  switch (*state) {
  case 0:
    printf("State 0\n");
    *state = 1;
    break;
  case 1:
    printf("State 1\n");
    *state = 2;
    break;
  case 2:
    printf("State 2; event: %d\n", event);
    if (event == 0) {
      *state = 1;
    } else {
      *state = 3;
    }
    break;
  case 3:
    printf("State 3\n");
    *state = 0;
    break;
  }
}

// Coroutine-based state machine implementation
void state_machine_coroutine(co_ctx_param, int event) {
  co_ctx{};

  co_init;

  printf("State 0\n");
  co_yield ();

  for (;;) {
    printf("State 1\n");
    co_yield ();

    printf("State 2; event: %d\n", event);
    if (event != 0) {
      co_yield ();
      break;
    } else {
      co_yield ();
    }
  }

  printf("State 3\n");
  co_return ();
}

int main(void) {
  co_ctx_t ctx = co_new_ctx;

  // Simplest way to drive a coroutine - in practice you'd likely drive it with
  // an event loop of some sort
  do {
    int n = coroutine(&ctx);
    printf("coroutine: %d\n", n);
  } while (co_is_running(&ctx));

  do {
    void_coroutine(&ctx);
  } while (co_is_running(&ctx));

  // Coroutines can be "cancelled" by simply freeing their state
  void_coroutine(&ctx);
  co_free(&ctx);

  // Freeing the context resets the coroutine to its initial state
  void_coroutine(&ctx);

  // It is safe to call co_free multiple times. The second time it just won't do
  // anything
  co_free(&ctx);
  co_free(&ctx);

  // Coroutines can have custom destructors that get executed whenever they
  // finish running
  do {
    custom_dtor_coroutine(&ctx);
  } while (co_is_running(&ctx));

  // The custom destructor is also executed when the coroutine is cancelled
  custom_dtor_coroutine(&ctx);
  co_free(&ctx);

  do {
    int n = parametrized_coroutine(&ctx, 10);
    printf("parametrized_coroutine: %d\n", n);
  } while (co_is_running(&ctx));

  do {
    nesting_coroutine(&ctx);
  } while (co_is_running(&ctx));

  int events[] = {-1, -1, 0, -1, 0, -1, 1, -1};
  int event_count = sizeof(events) / sizeof(int);

  printf("Standard state machine:\n");
  int state = 0;
  for (int i = 0; i < event_count; ++i) {
    state_machine(&state, events[i]);
  }

  printf("Coroutine state machine:\n");
  for (int i = 0; i < event_count; ++i) {
    state_machine_coroutine(&ctx, events[i]);
  }

  return 0;
}