/* sys.c - take over a stock Amiga 1200 from AmigaOS 3.x and give it back. */
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/dos.h>
#include <exec/memory.h>
#include <exec/execbase.h>
#include <graphics/gfxbase.h>
#include <devices/trackdisk.h>
#include "sys.h"

extern struct ExecBase *SysBase;
struct GfxBase *GfxBase;

UWORD sys_lines, sys_is_pal, sys_is_aga, sys_force_pal;
static UWORD forced_pal;
volatile ULONG sys_frame;
void (*volatile sys_vbl_hook)(void);
volatile UBYTE sys_keys[128];

static struct View *old_view;
static UWORD old_dmacon, old_intena, old_adkcon;
ULONG *sys_vbr;                                     /* CPU vector base (0 on a 68000) */
#define vbr sys_vbr
static ULONG old_lev3;

/* movec vbr,d0 ; rte  - must run in supervisor mode */
static const UWORD getvbr_code[] = { 0x4e7a, 0x0801, 0x4e73 };

/* Level 3 interrupt: only the vertical blank source is enabled. */
static __attribute__((interrupt)) void lev3(void)
{
    if (custom->intreqr & INTF_VERTB) {
        sys_frame++;
        if (sys_vbl_hook) sys_vbl_hook();
        custom->intreq = INTF_VERTB;
        custom->intreq = INTF_VERTB;    /* twice: needed on 68040/060 */
    }
}

struct Block { struct Block *next; ULONG size; };           /* 8 bytes, so the user area stays 8-byte aligned */
static struct Block *blocks;

void *sys_alloc(ULONG size, ULONG memflags)
{
    struct Block *b = AllocMem(size + sizeof *b, memflags);
    if (!b) return 0;
    b->next = blocks; b->size = size + sizeof *b; blocks = b;
    return b + 1;
}

void *sys_chip_copy(const void *src, ULONG size)        /* own, 8-byte aligned chip RAM copy of a block; BEFORE sys_take() */
{
    void *p = sys_alloc(size, MEMF_CHIP);
    if (p) CopyMem((APTR)src, p, size);
    return p;
}

void sys_free_all(void)
{
    while (blocks) { struct Block *b = blocks; blocks = b->next; FreeMem(b, b->size); }
}

/* Read a whole file. memflags: MEMF_CHIP for graphics and samples, MEMF_ANY for everything else. */
void *sys_load(const char *name, ULONG memflags, ULONG *size)
{
    BPTR fh = Open((CONST_STRPTR)name, MODE_OLDFILE);
    LONG len;
    void *p = 0;
    if (!fh) return 0;
    Seek(fh, 0, OFFSET_END);
    len = Seek(fh, 0, OFFSET_BEGINNING);                /* Seek returns the previous position = file length */
    if (len > 0 && (p = sys_alloc(len, memflags)) && Read(fh, p, len) != len) p = 0;   /* sys_free_all() frees it */
    Close(fh);
    if (size) *size = len;
    return p;
}

/* Tell trackdisk.device to stop the floppy motors: nobody can do it once the interrupts are off. */
static void floppy_motors_off(void)
{
    struct MsgPort *port = CreateMsgPort();
    struct IOExtTD *io = port ? (struct IOExtTD *)CreateIORequest(port, sizeof *io) : 0;
    ULONG unit;
    if (io)
        for (unit = 0; unit < 4; unit++)
            if (!OpenDevice((CONST_STRPTR)"trackdisk.device", unit, (struct IORequest *)io, 0)) {
                io->iotd_Req.io_Command = TD_MOTOR; io->iotd_Req.io_Length = 0;
                DoIO((struct IORequest *)io);
                CloseDevice((struct IORequest *)io);
            }
    if (io) DeleteIORequest((struct IORequest *)io);
    if (port) DeleteMsgPort(port);
}

int sys_take(void)
{
    GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library", 39);
    if (!GfxBase) return 0;                         /* needs Kickstart 3.0+ */

    sys_is_pal = (GfxBase->DisplayFlags & PAL) ? 1 : 0;
    sys_lines  = sys_is_pal ? 313 : 263;
    sys_is_aga = ((custom->deniseid & 0xff) == 0xf8); /* Lisa chip ID: AGA */
    if (!sys_is_aga) { CloseLibrary((struct Library *)GfxBase); return 0; }   /* this kit needs the AGA chipset */

    Delay(25);                                      /* half a second: the key that started us comes up */
    floppy_motors_off();
    old_view = GfxBase->ActiView;
    OwnBlitter();
    WaitBlit();
    LoadView(NULL);                                 /* blank, default display */
    WaitTOF();
    WaitTOF();
    Forbid();                                       /* no other task runs from here */

    old_adkcon = custom->adkconr;
    old_dmacon = custom->dmaconr;
    old_intena = custom->intenar;
    custom->intena = 0x7fff;                        /* all interrupts off */
    custom->intreq = 0x7fff;
    custom->dmacon = 0x7fff;                        /* all DMA off */
    custom->dmacon = DMAF_SETCLR | DMAF_MASTER | DMAF_BLITTER | DMAF_BLITHOG;   /* drawing calls work from here on */
    custom->potgo  = 0xff00;                        /* second fire buttons readable */
    forced_pal = 0;
    if (!sys_is_pal && sys_force_pal) {             /* NTSC machine: run at 50 Hz, 313 lines */
        custom->beamcon0 = 0x0020;
        forced_pal = 1; sys_is_pal = 1; sys_lines = 313;
    }

    vbr = 0;
    if (SysBase->AttnFlags & AFF_68010)
        vbr = (ULONG *)Supervisor((ULONG (*)())getvbr_code);
    old_lev3 = vbr[0x6c / 4];
    vbr[0x6c / 4] = (ULONG)lev3;

    sys_frame = 0;
    custom->intena = INTF_SETCLR | INTF_INTEN | INTF_VERTB;
    return 1;
}

void sys_give(void)
{
    sys_wait_frame();
    custom->intena = 0x7fff;
    custom->intreq = 0x7fff;
    custom->dmacon = 0x7fff;
    vbr[0x6c / 4] = old_lev3;
    if (forced_pal) custom->beamcon0 = 0x0000;      /* back to 60 Hz */
    custom->fmode = 0;                              /* AGA registers back to their reset values: the OS copper */
    custom->bplcon3 = 0x0c00;                       /* lists only rewrite them when graphics.library runs in  */
    custom->bplcon4 = 0x0011;                       /* AGA mode (after SetPatch)                               */

    custom->cop1lc = (ULONG)GfxBase->copinit;       /* the system copper list */
    custom->copjmp1 = 0;
    custom->adkcon = 0x7fff;
    custom->adkcon = old_adkcon | 0x8000;
    custom->dmacon = old_dmacon | 0x8000;
    custom->intena = old_intena | 0x8000;

    LoadView(old_view);
    WaitTOF();
    WaitTOF();
    WaitBlit();
    DisownBlitter();
    Permit();
    CloseLibrary((struct Library *)GfxBase);
}

void sys_wait_frame(void)
{
    ULONG f = sys_frame;
    while (sys_frame == f) ;
}

ULONG sys_beam(void)
{
    ULONG a, b, h;
    do {                                            /* VPOSR:VHPOSR is two 16-bit reads: */
        a = *(volatile ULONG *)0xdff004;            /* take the first, and retry if the  */
        b = *(volatile ULONG *)0xdff004;            /* high line bits changed after it   */
    } while ((a ^ b) & 0x00070000);
    h = a & 0xff;                                   /* the line counter ticks at h = 2, not 0 */
    h = (h >= 2) ? h - 2 : h + SYS_CCK_PER_LINE - 2;
    return ((a >> 8) & 0x7ff) * SYS_CCK_PER_LINE + h;
}

ULONG sys_now(void)
{
    ULONG f, b;
    do {
        f = sys_frame;
        b = sys_beam();
    } while (f != sys_frame || (b < 2 * SYS_CCK_PER_LINE && (custom->intreqr & INTF_VERTB)));
    return f * ((ULONG)sys_lines * SYS_CCK_PER_LINE) + b;
}

UWORD sys_joy(int port)
{
    UWORD d = port ? custom->joy1dat : custom->joy0dat;
    UWORD r = 0;
    if (d & 0x0002) r |= JOY_RIGHT;
    if (d & 0x0200) r |= JOY_LEFT;
    if ((d ^ (d >> 1)) & 0x0001) r |= JOY_DOWN;
    if ((d ^ (d >> 1)) & 0x0100) r |= JOY_UP;
    if (!(ciaa->ciapra & (port ? 0x80 : 0x40))) r |= JOY_FIRE;
    if (!(custom->potinp & (port ? 0x4000 : 0x0400))) r |= JOY_FIRE2;
    return r;
}

int sys_mouse_button(void)
{
    return !(ciaa->ciapra & 0x40);
}

/* Keyboard without an interrupt: poll the CIA-A serial flag once per frame. */
void sys_key_poll(void)
{
    UBYTE k, line;
    int i;
    if (!(ciaa->ciaicr & CIAICRF_SP)) return;       /* reading ICR clears it */
    k = ~ciaa->ciasdr;
    k = (UBYTE)((k >> 1) | (k << 7));               /* bit 7 = key up, bits 0-6 = raw code */
    ciaa->ciacra |= CIACRAF_SPMODE;                 /* handshake: pull KDAT low ... */
    for (i = 0; i < 3; i++) {                       /* ... for at least 85 us = 3 scanlines */
        line = custom->vhposr >> 8;
        while ((UBYTE)(custom->vhposr >> 8) == line) ;
    }
    ciaa->ciacra &= ~CIACRAF_SPMODE;
    sys_keys[k & 0x7f] = (k & 0x80) ? 0 : 1;
}
