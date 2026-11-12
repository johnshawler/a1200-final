; coro.s - the coroutine switch for the Amiga build (see coro.h). Saves the registers gcc expects a call to
; keep (d2-d7, a2-a6) on the old stack, switches stacks, restores them from the new one.
        section code,code
        xdef    _co_init,_co_resume,_co_yield

; void co_init(void (*entry)(void), void *stack, unsigned size)
_co_init:
        move.l  4(sp),a0                ; entry
        move.l  8(sp),a1
        add.l   12(sp),a1               ; top of the new stack
        move.l  a1,d0
        and.w   #$fffc,d0
        move.l  d0,a1
        move.l  a0,-(a1)                ; first switch "returns" into the entry function
        sub.w   #11*4,a1                ; 11 saved registers, any values
        move.l  a1,co_sp
        rts

_co_resume:
        movem.l d2-d7/a2-a6,-(sp)
        move.l  sp,main_sp
        move.l  co_sp,sp
        movem.l (sp)+,d2-d7/a2-a6
        rts

_co_yield:
        movem.l d2-d7/a2-a6,-(sp)
        move.l  sp,co_sp
        move.l  main_sp,sp
        movem.l (sp)+,d2-d7/a2-a6
        rts

        section bss,bss
main_sp: ds.l   1
co_sp:   ds.l   1
