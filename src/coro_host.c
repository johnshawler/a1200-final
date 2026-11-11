/* coro_host.c - the coroutine for the host test (see coro.h), with ucontext. */
#include <ucontext.h>
#include "coro.h"

static ucontext_t main_ctx, co_ctx;

void co_init(void (*entry)(void), void *stack, unsigned size)
{
    getcontext(&co_ctx);
    co_ctx.uc_stack.ss_sp = stack;
    co_ctx.uc_stack.ss_size = size;
    co_ctx.uc_link = 0;
    makecontext(&co_ctx, entry, 0);
}
void co_resume(void) { swapcontext(&main_ctx, &co_ctx); }
void co_yield(void) { swapcontext(&co_ctx, &main_ctx); }
