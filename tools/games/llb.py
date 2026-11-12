"""llb.py - what the translator needs to know about Little League Baseball: Championship Series (NES, MMC1)
that it cannot find out by itself. Loaded by tools/recomp.py (--game llb).

  NORETURN       subroutines that pop their own return address (jump-table dispatchers: a table of words
                 follows the JSR). Find them in the traces: 'I' targets reached from one JMP (ind) after PLA PLA.
  HLE            hand-written 68k equivalents of hot 6502 routines, keyed (bank, address), 7 = the fixed bank.
                 Each must leave RAM, the stack page, registers and flags exactly as the 6502 code does; the
                 host test runs the original, so equal checksums prove them. Code ends where the generator
                 appends a normal RTS sequence.
  HLE_FALLBACK   those whose text may branch to .orig: the translation of the original follows it.
See LEARNINGS.md and PLAYBOOK-NES.md for how these were found (1 kHz profiler, field-play window).
"""

NORETURN = {0xc232}             # jump-table dispatcher: pops its return address, a table of words follows the JSR

# Hand-written equivalents of hot 6502 routines (exact: same RAM, same stack bytes left below S, same
# registers and flags on return). The host test runs the original code, so equal checksums prove them.
HLE = {
    # $CEBD: read both controllers twice until equal, keep the edges and per-button hold counters.
    # Port 2 reads $4017, which is always 0 here. Effects worked out from the code at $CEBD-$CF25.
    (7, 0xcebd): """
    moveq #0,d4
    move.b _nes_pad,d4
    move.b (_revtab,d4.w),d4
    move.b #$ce,-1(a2)
    move.b #$ce,-2(a2)
    move.b #$ce,-3(a2)
    move.b #$e3,-4(a2)
    clr.b $18-$80(a5)
    clr.b $19-$80(a5)
    move.b d4,$8f-$80(a5)
    clr.b $90-$80(a5)
    lea $97-$80(a5),a0
    move.b d4,d7
    moveq #7,d3
.c  lsr.b #1,d7
    bcc.s .z
    addq.b #1,(a0)
    bra.s .n
.z  clr.b (a0)
.n  clr.b 1(a0)
    addq.l #2,a0
    dbf d3,.c
    move.b $8d-$80(a5),d7
    eor.b d4,d7
    and.b d4,d7
    move.b d7,$91-$80(a5)
    move.b d4,$8d-$80(a5)
    clr.b $92-$80(a5)
    clr.b $8e-$80(a5)
    move.b d7,$31-$80(a5)
    move.b d4,$30-$80(a5)
    move.b _nes_pad,_pad_shift
    clr.b _pad_strobe
    moveq #$11,d1
    moveq #0,d2
    move.b d4,d0
    ori.b #$10,ccr""",
}

# The game's MMC1 routines: "STA store (optional); INC $2D; five times STA reg, LSR; DEC $2D; RTS". Done in one
# step when the shift register is idle and A has no reset bit (always, in this game); else the translation (.orig).
# Effects: the register gets A & $1F, A ends shifted right 4 (carry = bit 3), N and Z come from $2D.
def _mmc_hle(load, store, setreg):
    pre = ['    move.b $%x-$80(a5),d0' % load] if load else []
    st = ['    move.b d0,$%x-$80(a5)' % store] if store else []
    return '\n'.join(pre + ['    btst #7,d0', '    bne .orig', '    cmp.b #$10,_mmc_shift', '    bne .orig'] + st +
                     ['    moveq #$1f,d4', '    and.b d0,d4'] + setreg + ['    lsr.b #4,d0', '    tst.b $2d-$80(a5)'])
for _k, _ld, _st, _set in [
        ((7, 0xc14e), None, 0x27, ['    bsr _set_prg']),                       # PRG bank
        ((7, 0xc14c), 0x27, 0x27, ['    bsr _set_prg']),                       # the same, bank from $27
        ((7, 0xc150), None, None, ['    bsr _set_prg']),                       # the same without the store (music code)
        ((7, 0xc102), None, 0x22, ['    bsr _set_ctrl']),                      # control (mirroring)
        ((7, 0xc11c), None, None, ['    move.b d4,_mmc_chr0', '    move.b #$10,_mmc_shift']),   # tile set 0
        ((7, 0xc134), None, None, ['    move.b d4,_mmc_chr1', '    move.b #$10,_mmc_shift'])]:  # tile set 1
    HLE[_k] = _mmc_hle(_ld, _st, _set)
# $C378: $05:$04 (signed) times $02 (unsigned), shifted right 8 with rounding, back into $05:$04. The original
# shifts and adds 16 times with PHP/PLP around it; this computes the product with mulu and replays only the low
# byte to know which of its subroutine calls ran last (their return addresses stay on the stack page).
HLE[(7, 0xc378)] = '''
    move.b $05-$80(a5),d0
    bsr _php
    moveq #0,d3
    move.b $05-$80(a5),d3
    lsl.w #8,d3
    move.b $04-$80(a5),d3
    moveq #0,d1
    moveq #0,d0
    tst.w d3
    bpl.s .pos
    neg.w d3
    moveq #1,d1
    moveq #1,d0
.pos moveq #0,d6
    move.b $02-$80(a5),d6
    moveq #0,d4
    moveq #15,d7
.l  add.b d4,d4
    btst d7,d3
    beq.s .nb
    add.b d6,d4
    bcc.s .nb
    moveq #2,d1
.nb dbf d7,.l
    mulu.w d6,d3
    tst.b d4
    bpl.s .nr
    moveq #3,d1
    add.l #$100,d3
.nr lsr.l #8,d3
    move.b d3,$04-$80(a5)
    lsr.w #8,d3
    move.b d3,$05-$80(a5)
    tst.b d1
    beq.s .ng
    move.b #$c3,-1(a2)
    move.b .ret(pc,d1.w),-2(a2)
.ng move.b d0,d7
    move.b d4,d0
    andi.b #$80,d0
    moveq #0,d1
    tst.b d7
    bne.s .neg
    bsr _plp
    bra.s .out
.ret dc.b 0,$7f,$92,$9c
.neg bsr _plp
    moveq #0,d0
    sub.b $04-$80(a5),d0
    move.b d0,$04-$80(a5)
    moveq #0,d0
    move.b $05-$80(a5),d4
    ori.b #$04,ccr
    subx.b d4,d0
    svs d5
    eori.b #$10,ccr
    move.b d0,$05-$80(a5)
.out'''
# $E2DE: one entry of a metasprite table at ($12),Y into OAM entry X: hide it, then tile, attribute (EOR $02),
# x = $05 + offset (negated when $03 says mirrored, sign-extended into $00, carried into $07), y = $04 + offset.
# A $FF tile or attribute ends the list; off-screen positions hide the entry. Every exit sets A, Y, the flags
# and $00/$10 exactly as the 6502 code does. The table must be in ROM and Y must not wrap; else .orig.
HLE[(7, 0xe2de)] = '''
    cmp.b #$fb,d2
    bhi .orig
    moveq #0,d7
    move.w $12-$80(a5),d7
    rol.w #8,d7
    add.l d2,d7
    cmp.l #$c000,d7
    bcs.s .lo
    lea (-$c000,a4,d7.l),a0
    bra.s .go
.lo cmp.l #$8000,d7
    bcs .orig
    cmp.l #$bffc,d7
    bhi .orig
    lea (-$8000,a3,d7.l),a0
.go move.b #$f0,($180,a5,d1.w)
    move.b (a0)+,d0
    addq.b #1,d2
    cmp.b #$ff,d0
    bne.s .e2ed
    move.b d1,$10-$80(a5)
    cmp.b #$ff,d0
    ori.b #$10,ccr
    bra .rts
.e2ed move.b $02-$80(a5),d4
    eor.b d4,d0
    move.b d0,($182,a5,d1.w)
    move.b (a0)+,d0
    move.b d0,($181,a5,d1.w)
    addq.b #1,d2
    cmp.b #$ff,d0
    bne.s .c1
    ori.b #$10,ccr
    bra.s .e337
.c1 clr.b $00-$80(a5)
    move.b (a0),d0
    tst.b $03-$80(a5)
    beq.s .nm
    neg.b d0
.nm tst.b d0
    bpl.s .p1
    subq.b #1,$00-$80(a5)
.p1 add.b $05-$80(a5),d0
    move.b d0,($183,a5,d1.w)
    move.b $07-$80(a5),d4
    move.b $00-$80(a5),d3
    ori.b #$04,ccr
    addx.b d3,d4
    move.b d4,d0
    bne.s .e337
    addq.l #1,a0
    addq.b #1,d2
    clr.b $00-$80(a5)
    move.b (a0),d0
    bpl.s .p2
    subq.b #1,$00-$80(a5)
.p2 add.b $04-$80(a5),d0
    move.b d0,($180,a5,d1.w)
    moveq #0,d4
    move.b $00-$80(a5),d3
    ori.b #$04,ccr
    addx.b d3,d4
    move.b d4,d0
    bne.s .e338
    move.b (_inctab,d2.w),d2
    bra.s .rts
.e337 move.b (_inctab,d2.w),d2
.e338 move.b #$f0,d0
    move.b d0,($180,a5,d1.w)
    move.b d1,$10-$80(a5)
    move.b (_inctab,d2.w),d2
.rts'''
HLE_FALLBACK = {(7, 0xe2de), (7, 0xc14e), (7, 0xc14c), (7, 0xc150), (7, 0xc102), (7, 0xc11c), (7, 0xc134)}
