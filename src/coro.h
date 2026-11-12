/* coro.h - one coroutine: the NES program's main thread runs on its own stack and gives control back at
 * every frame boundary. Amiga: src/coro.s. Host test: src/coro_host.c (ucontext). */
#ifndef CORO_H
#define CORO_H
void co_init(void (*entry)(void), void *stack, unsigned size);   /* entry must never return */
void co_resume(void);           /* run the coroutine until it calls co_yield() */
void co_yield(void);            /* inside the coroutine: back to whoever called co_resume() */
#endif
