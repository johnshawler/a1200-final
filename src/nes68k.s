; nes68k.s - the run-time support of the translated 68k code (src/gen68k.s, made by tools/recomp.py).
; Register use is described in tools/back68k.py: d0 A, d1 X, d2 Y, d5 V, a2 6502 stack, a3 bank at $8000,
; a4 bank at $C000, a5 NES RAM+$80; 6502 N Z = 68k N Z, 6502 C = 68k X.
; Between the C side and the translated code the 6502 state goes through the cpu_* globals of src/nes.c.

; This file is included by src/gen68k.s, in its code section, so that the many bsr to these helpers stay
; inside one section. It is not assembled on its own.
        xdef    _asm_run,_asm_dispatch
        xref    _cpu_a,_cpu_s,_cpu_x,_cpu_y,_cpu_c,_cpu_z,_cpu_n,_cpu_v,_cpu_i,_cpu_d
;       _cpu_x,_cpu_y,_cpu_s,_cpu_c,_cpu_z,_cpu_n,_cpu_v,_cpu_i,_cpu_d
        xref    _nes_ram,_nes_prg_lo,_nes_prg_data,_nes_bank
        xref    _nes_io_read,_nes_read_s0,_nes_write_slow,_nes_wait,_nes_fatal

; ---- moving the 6502 state between the globals and the registers
_load_state:
        moveq   #0,d0
        moveq   #0,d1
        moveq   #0,d2
        move.b  _cpu_a,d0
        move.b  _cpu_x,d1
        move.b  _cpu_y,d2
        lea     _nes_ram+$80,a5
        moveq   #0,d4
        move.b  _cpu_s,d4
        lea     ($81,a5,d4.w),a2
        move.l  _nes_prg_lo,a3
        lea     _nes_prg_data+$1c000,a4
        move.b  _cpu_v,d5
        moveq   #0,d4
        tst.b   _cpu_c
        beq.s   .1
        ori.b   #$10,d4
.1      tst.b   _cpu_n
        bpl.s   .2
        ori.b   #$08,d4
.2      tst.b   _cpu_z
        bne.s   .3
        ori.b   #$04,d4
.3      move.w  d4,ccr
        rts

_store_state:
        move.w  ccr,d6
        move.b  d0,_cpu_a
        move.b  d1,_cpu_x
        move.b  d2,_cpu_y
        move.b  d5,_cpu_v
        move.l  a2,d4
        sub.l   a5,d4
        sub.w   #$81,d4
        move.b  d4,_cpu_s
        btst    #4,d6
        sne     d4
        and.b   #1,d4
        move.b  d4,_cpu_c
        btst    #3,d6
        sne     d4
        and.b   #$80,d4
        move.b  d4,_cpu_n
        btst    #2,d6
        seq     d4
        and.b   #1,d4
        move.b  d4,_cpu_z
        move.w  d6,ccr
        rts

; ---- entering translated code from C. Both return the next 6502 address (an RTS nobody called from)
; uint16_t asm_run(void *fn)
_asm_run:
        movem.l d2-d7/a2-a6,-(sp)
        move.l  48(sp),a1
        bsr     _load_state
        pea     _tramp_ret
        jmp     (a1)
; uint16_t asm_dispatch(unsigned pc)
_asm_dispatch:
        movem.l d2-d7/a2-a6,-(sp)
        move.l  48(sp),d7
        bsr     _load_state
        move.w  ccr,d6
        pea     _tramp_ret
        jmp     _dispatch
_tramp_ret:
        dc.w    0                       ; matches no 6502 return: an RTS that reaches here is delivered
        bsr     _store_state
        moveq   #0,d0
        move.w  d7,d0
        movem.l (sp)+,d2-d7/a2-a6
        rts

; ---- RTS found other bytes on the 6502 stack than its caller pushed: find the 68k frame that expects them.
; In: a0 = the frame that did not match + 2, a2 past the two popped bytes, d6 = the flags.
_unwind:
        move.w  -2(a2),d7
        subq.l  #2,a0
.next   cmpa.l  #_tramp_ret,a0
        beq.s   .deliver
        movea.l (sp)+,a0
        cmp.w   (a0),d7
        bne.s   .next
        move.w  d6,ccr
        jmp     2(a0)
.deliver:
        rol.w   #8,d7
        addq.w  #1,d7
        move.w  d6,ccr
        jmp     2(a0)

; RTI of the NMI handler: d7 = the popped address, flags already set. The trampoline frame is on top.
_rti:
        movea.l (sp)+,a0
        jmp     2(a0)

; ---- jump to the translated function for d7.w in the mapped bank; d6 = the flags to restore
_dispatch:
        movem.l d0-d2,-(sp)
        lea     _disp_tabs,a0
        moveq   #0,d2
        move.w  d7,d2
        cmp.l   #$c000,d2
        bcs.s   .banked
        movea.l 28(a0),a0
        bra.s   .search
.banked cmp.l   #$8000,d2
        bcs.s   .nf
        moveq   #0,d4
        move.b  _nes_bank,d4
        lsl.w   #2,d4
        movea.l (a0,d4.w),a0
.search move.l  (a0)+,d4                ; hi = count
        moveq   #0,d3                   ; lo
.loop   cmp.l   d4,d3
        bge.s   .nf
        move.l  d3,d0
        add.l   d4,d0
        lsr.l   #1,d0                   ; mid
        move.l  d0,d1
        lsl.l   #3,d1
        cmp.l   (a0,d1.l),d2
        beq.s   .found
        bhi.s   .up
        move.l  d0,d4
        bra.s   .loop
.up     addq.l  #1,d0
        move.l  d0,d3
        bra.s   .loop
.found  movea.l 4(a0,d1.l),a1
        movem.l (sp)+,d0-d2
        move.w  d6,ccr
        jmp     (a1)
.nf     movem.l (sp)+,d0-d2
        moveq   #3,d4
        bra.s   _fatal
_fatal_op:
        moveq   #1,d4
_fatal:                                 ; d7.w = address, d4 = reason: tell C, then back to the trampoline
        moveq   #0,d3
        move.w  d7,d3
        move.l  d4,-(sp)
        move.l  d3,-(sp)
        jsr     _nes_fatal
        addq.l  #8,sp
.f1     movea.l (sp)+,a0
        cmpa.l  #_tramp_ret,a0
        bne.s   .f1
        moveq   #0,d7
        jmp     2(a0)

; ---- frame boundary: a1 = address of the wait loop (an address register: loading it leaves the flags)
_wait_ram:
        pea     0.w
        bra.s   _wait
_wait_vbl:
        pea     1.w
_wait:  bsr     _store_state
        move.l  (sp)+,d3
        move.l  a1,d4
        and.l   #$ffff,d4
        move.l  d4,-(sp)
        move.l  d3,-(sp)
        jsr     _nes_wait
        addq.l  #8,sp
        bsr     _load_state
        rts

; ---- memory that is not plain RAM or ROM. All keep d0-d3, d5-d7, a0-a6 and the flags.
_io_rd:                                 ; a0 = address -> d4
        move.w  ccr,-(sp)
        movem.l d0-d1/a0-a1,-(sp)
        move.l  a0,-(sp)
        jsr     _nes_io_read
        addq.l  #4,sp
        move.b  d0,d4
        movem.l (sp)+,d0-d1/a0-a1
        move.w  (sp)+,ccr
        rts
_rd_s0:                                 ; $2002 read in a sprite 0 wait -> d4
        move.w  ccr,-(sp)
        movem.l d0-d1/a0-a1,-(sp)
        jsr     _nes_read_s0
        move.b  d0,d4
        movem.l (sp)+,d0-d1/a0-a1
        move.w  (sp)+,ccr
        rts
_io_wr_d0:                              ; a0 = address, value in d0 / d1 / d2
        move.w  ccr,-(sp)
        movem.l d0-d1/a0-a1,-(sp)
        moveq   #0,d4
        move.b  d0,d4
        bra.s   _io_wr
_io_wr_d1:
        move.w  ccr,-(sp)
        movem.l d0-d1/a0-a1,-(sp)
        moveq   #0,d4
        move.b  d1,d4
        bra.s   _io_wr
_io_wr_d2:
        move.w  ccr,-(sp)
        movem.l d0-d1/a0-a1,-(sp)
        moveq   #0,d4
        move.b  d2,d4
_io_wr_d4:
        move.w  ccr,-(sp)
        movem.l d0-d1/a0-a1,-(sp)
        and.l   #$ff,d4
_io_wr: move.l  d4,-(sp)
        move.l  a0,-(sp)
        jsr     _nes_write_slow
        addq.l  #8,sp
        move.l  _nes_prg_lo,a3          ; a mapper write may have switched the bank
        movem.l (sp)+,d0-d1/a0-a1
        move.w  (sp)+,ccr
        rts

; any address in a0 (0..$ffff): -> d4. Changes N Z (not X).
_rd_ea: cmpa.l  #$c000,a0
        bcc.s   .fix
        cmpa.l  #$8000,a0
        bcc.s   .sw
        cmpa.l  #$2000,a0
        bcc     _io_rd
        move.l  a0,d4
        andi.w  #$7ff,d4
        move.b  -$80(a5,d4.w),d4
        rts
.fix    move.b  (-$c000,a4,a0.l),d4
        rts
.sw     move.b  (-$8000,a3,a0.l),d4
        rts
_wr_ea_d4:                              ; read-modify-write: a0 = address, d4 = value, flags already set
        move.w  ccr,-(sp)
        cmpa.l  #$2000,a0
        bcc.s   .io
        move.l  a0,d3
        andi.w  #$7ff,d3
        move.b  d4,-$80(a5,d3.w)
        move.w  (sp)+,ccr
        rts
.io     move.w  (sp)+,ccr
        bra     _io_wr_d4
_wr_ea_d0:
        cmpa.l  #$2000,a0
        bcc     _io_wr_d0
        move.l  a0,d4
        andi.w  #$7ff,d4
        move.b  d0,-$80(a5,d4.w)
        rts
_wr_ea_d1:
        cmpa.l  #$2000,a0
        bcc     _io_wr_d1
        move.l  a0,d4
        andi.w  #$7ff,d4
        move.b  d1,-$80(a5,d4.w)
        rts
_wr_ea_d2:
        cmpa.l  #$2000,a0
        bcc     _io_wr_d2
        move.l  a0,d4
        andi.w  #$7ff,d4
        move.b  d2,-$80(a5,d4.w)
        rts

; ---- the status register
_php:   move.w  ccr,-(sp)
        move.w  (sp),d7
        moveq   #$30,d4
        btst    #4,d7
        beq.s   .1
        or.b    #$01,d4
.1      btst    #3,d7
        beq.s   .2
        or.b    #$80,d4
.2      btst    #2,d7
        beq.s   .3
        or.b    #$02,d4
.3      tst.b   d5
        beq.s   .4
        or.b    #$40,d4
.4      tst.b   _cpu_i
        beq.s   .5
        or.b    #$04,d4
.5      tst.b   _cpu_d
        beq.s   .6
        or.b    #$08,d4
.6      move.b  d4,-(a2)
        move.w  (sp)+,ccr
        rts
_plp:   move.b  (a2)+,d4
_setp:                                  ; d4 = 6502 P -> flags, d5, cpu_i, cpu_d
        move.b  d4,d5
        andi.b  #$40,d5
        btst    #2,d4
        sne     _cpu_i
        btst    #3,d4
        sne     _cpu_d
        moveq   #0,d3
        btst    #0,d4
        beq.s   .1
        ori.b   #$10,d3
.1      btst    #7,d4
        beq.s   .2
        ori.b   #$08,d3
.2      btst    #1,d4
        beq.s   .3
        ori.b   #$04,d3
.3      move.w  d3,ccr
        rts

        section gendata,data
_inctab:
N       set     0
        rept    256
        dc.b    (N+1)&255
N       set     N+1
        endr
_dectab:
N       set     0
        rept    256
        dc.b    (N-1)&255
N       set     N+1
        endr
