; prof68k.s - debug builds only (main.c with -DLOGIC_TRACE): a 1 kHz sampling profiler on the CIA-B timer A
; (level 6 interrupt). Each sample is the interrupted program counter, from the exception frame.
        section code,code
        xdef    _prof_install,_prof_remove,_prof_buf,_prof_cnt
        xref    _sys_vbr
PROF_MAX equ    65536

_prof_install:
        movem.l a0-a1,-(sp)
        move.l  _sys_vbr,a0
        move.l  $78(a0),old6
        lea     prof_irq(pc),a1
        move.l  a1,$78(a0)
        lea     $bfd000,a0
        move.b  #$7f,$d00(a0)           ; CIA-B ICR: all off
        move.b  #$c5,$400(a0)           ; timer A = 709 E clocks = 1 ms
        move.b  #$02,$500(a0)
        move.b  #$11,$e00(a0)           ; start, continuous, load
        move.b  #$81,$d00(a0)           ; timer A interrupt on
        move.w  #$a000,$dff09a          ; INTENA: set EXTER (level 6)
        movem.l (sp)+,a0-a1
        rts

_prof_remove:
        move.l  a0,-(sp)
        move.w  #$2000,$dff09a          ; EXTER off
        move.b  #$7f,$bfdd00
        move.b  #$00,$bfde00
        move.l  _sys_vbr,a0
        move.l  old6,$78(a0)
        move.l  (sp)+,a0
        rts

prof_irq:
        movem.l d0/a0,-(sp)
        move.b  $bfdd00,d0              ; acknowledge the CIA
        move.l  _prof_cnt,d0
        cmp.l   #PROF_MAX,d0
        bcc.s   .full
        lea     _prof_buf,a0
        lsl.l   #2,d0
        move.l  8+2(sp),(a0,d0.l)       ; PC: after the two saved registers and the SR
        addq.l  #1,_prof_cnt
.full   move.w  #$2000,$dff09c          ; INTREQ: clear EXTER
        move.w  #$2000,$dff09c
        movem.l (sp)+,d0/a0
        rte

        section bss,bss
old6:       ds.l    1
_prof_cnt:  ds.l    1
_prof_buf:  ds.l    PROF_MAX
