; nes68k.s - the run-time support of the translated 68k code (src/gen68k.s, made by tools/recomp.py).
; Register use is described in tools/back68k.py: d0 A, d1 X, d2 Y, d5 V, a2 6502 stack, a3 bank at $8000,
; a4 bank at $C000, a5 NES RAM+$80; 6502 N Z = 68k N Z, 6502 C = 68k X.
; Between the C side and the translated code the 6502 state goes through the cpu_* globals of src/nes.c.

; This file is included by src/gen68k.s, in its code section, so that the many bsr to these helpers stay
; inside one section. It is not assembled on its own.
        xdef    _asm_run,_asm_dispatch
        xdef    _dispatch,_unwind,_io_rd,_io_wr,_rd_ea,_wr_ea_d0,_php,_plp,_set_prg,_wait   ; for the profiler only
        xref    _cpu_a,_cpu_s,_cpu_x,_cpu_y,_cpu_c,_cpu_z,_cpu_n,_cpu_v,_cpu_i,_cpu_d
;       _cpu_x,_cpu_y,_cpu_s,_cpu_c,_cpu_z,_cpu_n,_cpu_v,_cpu_i,_cpu_d
        xref    _nes_ram,_nes_prg_lo,_nes_prg_hi,_nes_prg_data,_ppu_v,_ppu_ctrl,_nes_vram,_nes_dirty,_nes_dirty_flag,_nes_ndirty,_nes_redraw_all,_nes_bank,_mmc_shift,_mmc_chr0,_mmc_chr1,_mmc_ctrl,_mmc_prg,_pad_shift,_pad_strobe,_nes_pad
        xref    _nes_io_read,_nes_read_s0,_nes_write_slow,_nes_wait,_nes_fatal,_nes_mmc_prg,_nes_mmc_ctrl

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
_unwind_x:                              ; the same, when no caller uses N and Z (X is still intact)
        move.w  ccr,d6
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
        moveq   #0,d0
        move.w  d7,d0                   ; address
        moveq   #7,d1                   ; bank: 7 = the fixed one
        cmp.l   #$c000,d0
        bcc.s   .h
        cmp.l   #$8000,d0
        bcs.s   .nf
        move.b  _nes_bank,d1
.h      move.w  d1,d4                   ; slot = (address + bank * 311) & 2047, as tools/back68k.py
        mulu.w  #311,d4
        add.w   d0,d4
        and.w   #2047,d4
        lea     _disp_hash,a0
.probe  move.w  d4,d3
        lsl.w   #3,d3
        move.w  (a0,d3.w),d2
        beq.s   .nf                     ; empty slot: unknown
        cmp.w   d2,d0
        bne.s   .next
        cmp.w   2(a0,d3.w),d1
        beq.s   .found
.next   addq.w  #1,d4
        and.w   #2047,d4
        bra.s   .probe
.found  movea.l 4(a0,d3.w),a1
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
        cmpa.w  #$4016,a0               ; the controller: done here, as nes_io_read() does it
        bne.s   .slow
        move.b  _pad_shift,d4
        tst.b   _pad_strobe
        bne.s   .1
        move.b  d4,d3
        lsr.b   #1,d3
        or.b    #$80,d3
        move.b  d3,_pad_shift
.1      and.b   #1,d4
        or.b    #$40,d4
        move.w  (sp)+,ccr
        rts
.slow   movem.l d0-d1/a0-a1,-(sp)
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
        bra     _io_wr
_io_wr_d1:
        move.w  ccr,-(sp)
        movem.l d0-d1/a0-a1,-(sp)
        moveq   #0,d4
        move.b  d1,d4
        bra     _io_wr
_io_wr_d2:
        move.w  ccr,-(sp)
        movem.l d0-d1/a0-a1,-(sp)
        moveq   #0,d4
        move.b  d2,d4
_set_prg:                               ; d4 = new MMC1 PRG register: as five serial writes would leave it
        move.w  ccr,-(sp)
        move.b  _mmc_ctrl,d3
        and.b   #$0c,d3
        cmp.b   #$0c,d3                 ; mode 3 (the game's): 16 KB at $8000 switched, the last bank fixed
        bne.s   .c
        move.b  d4,_mmc_prg
        move.b  d4,d3
        and.l   #7,d3
        move.b  d3,_nes_bank
        moveq   #14,d4
        lsl.l   d4,d3                   ; bank * $4000
        lea     _nes_prg_data,a3
        adda.l  d3,a3
        move.l  a3,_nes_prg_lo
        move.l  #_nes_prg_data+$1c000,_nes_prg_hi
        move.b  #$10,_mmc_shift
        move.w  (sp)+,ccr
        rts
.c
        movem.l d0-d1/a0-a1,-(sp)
        move.l  d4,-(sp)
        jsr     _nes_mmc_prg
        addq.l  #4,sp
        move.l  _nes_prg_lo,a3
        movem.l (sp)+,d0-d1/a0-a1
        move.w  (sp)+,ccr
        rts
_set_ctrl:                              ; d4 = new MMC1 control register
        move.w  ccr,-(sp)
        movem.l d0-d1/a0-a1,-(sp)
        move.l  d4,-(sp)
        jsr     _nes_mmc_ctrl
        addq.l  #4,sp
        move.l  _nes_prg_lo,a3
        movem.l (sp)+,d0-d1/a0-a1
        move.w  (sp)+,ccr
        rts
_io_wr_d4:
        move.w  ccr,-(sp)
        movem.l d0-d1/a0-a1,-(sp)
        and.l   #$ff,d4
_io_wr: cmpa.w  #$2007,a0               ; PPU data port: nametable writes done here, as vram_write() in nes.c
        bne     .nt9
        move.b  _mmc_ctrl,d3
        and.b   #3,d3
        cmp.b   #2,d3                   ; vertical mirroring only (the game's)
        bne     .call
        move.w  _ppu_v,d3
        and.w   #$3fff,d3
        cmp.w   #$2000,d3
        bcs     .call
        cmp.w   #$3f00,d3
        bcc     .call
        and.w   #$7ff,d3                ; nametable cell
        lea     _nes_vram,a0
        cmp.b   (a0,d3.w),d4
        beq.s   .inc
        move.b  d4,(a0,d3.w)
        lea     _nes_dirty_flag,a0
        tst.b   (a0,d3.w)
        bne.s   .inc
        move.w  _nes_ndirty,d4
        cmp.w   #1024,d4                ; NES_DIRTY_MAX
        bcs.s   .mk
        move.b  #1,_nes_redraw_all
        bra.s   .inc
.mk     move.b  #1,(a0,d3.w)
        lea     _nes_dirty,a0
        add.w   d4,d4
        move.w  d3,(a0,d4.w)
        addq.w  #1,_nes_ndirty
.inc    moveq   #1,d3
        btst    #2,_ppu_ctrl
        beq.s   .i1
        moveq   #32,d3
.i1     add.w   d3,_ppu_v
        bra     .done
.nt9    cmpa.l  #$8000,a0               ; MMC1: four of the five serial writes only shift
        bcs     .pad
        btst    #7,d4
        bne     .call
        move.b  _mmc_shift,d3
        btst    #0,d3
        bne     .call                   ; the fifth: nes_write_slow() loads the register
        lsr.b   #1,d3
        btst    #0,d4
        beq.s   .m1
        or.b    #$10,d3
.m1     move.b  d3,_mmc_shift
        bra     .done
.pad    cmpa.w  #$4016,a0               ; controller strobe
        bne     .call
        move.b  d4,d3
        and.b   #1,d3
        move.b  d3,_pad_strobe
        beq     .done
        move.b  _nes_pad,_pad_shift
        bra     .done
.call   move.l  d4,-(sp)
        move.l  a0,-(sp)
        jsr     _nes_write_slow
        addq.l  #8,sp
        move.l  _nes_prg_lo,a3          ; a mapper write may have switched the bank
.done   movem.l (sp)+,d0-d1/a0-a1
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
_revtab:                                ; bit-reversed bytes
N       set     0
        rept    256
        dc.b    ((N&1)<<7)|((N&2)<<5)|((N&4)<<3)|((N&8)<<1)|((N&16)>>1)|((N&32)>>3)|((N&64)>>5)|((N&128)>>7)
N       set     N+1
        endr
_dectab:
N       set     0
        rept    256
        dc.b    (N-1)&255
N       set     N+1
        endr
