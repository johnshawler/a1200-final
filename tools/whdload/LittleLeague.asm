;*---------------------------------------------------------------------------
;  :Modul.	LittleLeague.asm
;  :Contents.	WHDLoad slave for the Amiga 1200 port of Little League Baseball (NES)
;  :Original.	adapted from the public-domain kick31.asm example of the WHDLoad developer package (Wepl)
;  :Requires.	whdload/kick31.s (WHDLoad developer package), Kickstart 40.068 in DEVS:Kickstarts
;  :Copyright.	Public Domain
;  :Language.	68000 Assembler (vasm, -phxass)
;  Boots Kickstart 3.1 under WHDLoad and runs data/llb, the port's executable; quits WHDLoad when it ends.
;---------------------------------------------------------------------------*

	INCLUDE	whdload.i
	INCLUDE	whdmacros.i
	INCLUDE	lvo/dos.i

;============================================================================

CHIPMEMSIZE	= $100000	;1 MB chip: display, sprite lists, sound waveforms (about 0.3 MB used)
FASTMEMSIZE	= $200000	;2 MB fast: the program and its data (about 0.8 MB used)
NUMDRIVES	= 1		;the program switches the floppy motors off at start
WPDRIVES	= %0001

BOOTDOS				;run the program ourselves (no startup-sequence)
CACHECHIP			;instruction cache on
HDINIT				;filesystem for the data directory
INITAGA				;the program needs the AGA chipset
IOCACHE		= 1024
SETKEYBOARD
STACKSIZE	= 8000

;============================================================================

slv_Version	= 17
slv_Flags	= WHDLF_NoError|WHDLF_Examine
slv_keyexit	= $59	;F10 (the game itself quits with Esc)

;============================================================================

	INCLUDE	whdload/kick31.s

;============================================================================

slv_CurrentDir	dc.b	"data",0
slv_name	dc.b	"Little League Baseball (NES port, AGA)",0
slv_copy	dc.b	"1990 SNK",0
slv_info	dc.b	"Amiga 1200 port by static translation",10
		dc.b	"Needs a 68020, AGA and fast memory",0
slv_config	= slv_base
	EVEN

;============================================================================

_bootdos	move.l	(_resload,pc),a2	;A2 = resload

		moveq	#OLTAG_DOS,d0
		move.l	(4),a6
		jsr	(_LVOTaggedOpenLibrary,a6)
		lea	(_dosbase,pc),a0
		move.l	d0,(a0)
		move.l	d0,a6			;A6 = dosbase

		lea	(_program,pc),a0
		move.l	a0,d1
		jsr	(_LVOLoadSeg,a6)
		move.l	d0,d7			;D7 = segment
		beq	.program_err

		move.l	d7,a1
		add.l	a1,a1
		add.l	a1,a1
		moveq	#_args_end-_args,d0
		lea	(_args,pc),a0
		jsr	(4,a1)

		pea	TDREASON_OK		;the game ended (Esc): leave WHDLoad
		move.l	(_resload,pc),a2
		jmp	(resload_Abort,a2)

.program_err	jsr	(_LVOIoErr,a6)
		pea	(_program,pc)
		move.l	d0,-(a7)
		pea	TDREASON_DOSREAD
		jmp	(resload_Abort,a2)

_program	dc.b	"llb",0
_args		dc.b	10
_args_end
	EVEN

_dosbase	dc.l	0
