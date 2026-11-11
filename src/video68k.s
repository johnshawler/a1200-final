; video68k.s - the two inner loops of src/video.c, in assembly (they run every frame and stay in the cache).
        section code,code
        xdef    _tile8,_spr_put
        xref    _video_plane,_video_flip

; void tile8(UBYTE *d __asm("a0"), const UBYTE *chr __asm("a1"), ULONG attr __asm("d0"))
; One 8x8 NES tile into 4 bitplanes, 64 bytes a line: planes 0-1 from the CHR tile (8 bytes plane 0,
; 8 bytes plane 1), planes 2-3 filled with the attribute bytes in attr bits 0-7 and 8-15.
_tile8: movem.l d2-d3/a2-a4,-(sp)
        move.l  _video_plane,d2
        lea     (a0,d2.l),a2
        lea     (a2,d2.l),a3
        lea     (a3,d2.l),a4
        move.w  d0,d1
        lsr.w   #8,d1                   ; plane 3 byte
        moveq   #7,d3
.l      move.b  8(a1),(a2)
        move.b  (a1)+,(a0)
        move.b  d0,(a3)
        move.b  d1,(a4)
        lea     64(a0),a0
        lea     64(a2),a2
        lea     64(a3),a3
        lea     64(a4),a4
        dbf     d3,.l
        movem.l (sp)+,d2-d3/a2-a4
        rts

; void spr_put(UBYTE *e __asm("a0"), UBYTE *o __asm("a1"), const UBYTE *chr __asm("a2"), ULONG ctl __asm("d0"))
; One 8x8 NES sprite into the lines of a 64-pixel attached sprite pair, over what is there.
; e, o: the sprite's first line in the even and odd sprite's data (16 bytes a line: 8 plane A, 8 plane B).
; ctl: bits 0-5 x in the 64-pixel window, 8 h-flip, 9 v-flip, 10-11 NES sprite palette, 12 o is valid.
_spr_put:
        movem.l d2-d7/a3,-(sp)
        move.l  d0,d7
        moveq   #0,d1
        move.b  d7,d1
        move.w  d1,d2
        lsr.w   #3,d2
        adda.w  d2,a0
        adda.w  d2,a1
        and.w   #7,d1
        moveq   #8,d6
        sub.w   d1,d6                   ; shift: 8 - x mod 8
        lea     _video_flip,a3
        moveq   #7,d5
        moveq   #1,d4
        btst    #9,d7
        beq.s   .row
        addq.l  #7,a2
        moveq   #-1,d4
.row    moveq   #0,d1
        move.b  (a2),d1
        moveq   #0,d2
        move.b  8(a2),d2
        btst    #8,d7
        beq.s   .nh
        move.b  (a3,d1.w),d1
        move.b  (a3,d2.w),d2
.nh     move.w  d1,d3
        or.w    d2,d3
        beq.s   .next
        lsl.w   d6,d1
        lsl.w   d6,d2
        lsl.w   d6,d3                   ; opaque pixels
        move.w  d3,d0
        not.w   d0                      ; keep mask
        and.w   d0,(a0)
        or.w    d1,(a0)
        and.w   d0,8(a0)
        or.w    d2,8(a0)
        btst    #12,d7
        beq.s   .next
        and.w   d0,(a1)
        btst    #10,d7
        beq.s   .p2
        or.w    d3,(a1)
.p2     and.w   d0,8(a1)
        btst    #11,d7
        beq.s   .next
        or.w    d3,8(a1)
.next   lea     16(a0),a0
        lea     16(a1),a1
        adda.w  d4,a2
        dbf     d5,.row
        movem.l (sp)+,d2-d7/a3
        rts
