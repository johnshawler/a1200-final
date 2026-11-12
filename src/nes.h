/* nes.h - the NES machine state the translated game code runs against: RAM, the 6502 registers, the MMC1
 * mapper, and the PPU/APU/controller registers reduced to plain state. Plain C, no platform header: the host
 * test and the Amiga build compile exactly this. Drawing is NOT done here: the Amiga side reads this state
 * once per frame (nes_vram, nes_pal, nes_oam, the scroll values, the dirty lists) and updates its display. */
#ifndef NES_H
#define NES_H
#include <stdint.h>

/* ---- 6502 registers between translated functions (inside a function they live in locals) ---- */
extern uint8_t cpu_a, cpu_x, cpu_y, cpu_s, cpu_c, cpu_z, cpu_n, cpu_v, cpu_i, cpu_d;

/* ---- memory ---- */
extern uint8_t nes_ram[0x800];
extern const uint8_t *nes_prg;          /* whole PRG ROM, 8 x 16 KB */
extern const uint8_t *nes_chr;          /* whole CHR ROM, 128 KB = 32 banks of 4 KB */
extern const uint8_t *nes_prg_lo;       /* the 16 KB bank mapped at $8000 */
extern const uint8_t *nes_prg_hi;       /* the fixed last bank at $C000 */
extern uint8_t nes_bank;                /* number of the bank at $8000 */

uint8_t nes_io_read(uint16_t a);                /* $2000-$7FFF */
uint8_t nes_read_s0(void);                      /* $2002 read by a loop that waits for the sprite 0 hit */
void    nes_write_slow(uint16_t a, uint8_t v);  /* everything at $2000 and above */
void    nes_mmc_prg(uint8_t v);                 /* MMC1 PRG register in one step (src/nes68k.s) */
void    nes_mmc_ctrl(uint8_t v);                /* MMC1 control register in one step */
void    nes_init(const uint8_t *prg, const uint8_t *chr);
void    nes_fatal(uint16_t pc, int why);        /* translated code reached something it cannot do */
extern uint16_t nes_fatal_pc; extern int nes_fatal_why;

static inline uint8_t nes_rd(uint16_t a)
{
    if (a < 0x2000) return nes_ram[a & 0x7ff];
    if (a >= 0xc000) return nes_prg_hi[a - 0xc000];
    if (a >= 0x8000) return nes_prg_lo[a - 0x8000];
    return nes_io_read(a);
}
static inline void nes_wr(uint16_t a, uint8_t v)
{
    if (a < 0x2000) nes_ram[a & 0x7ff] = v; else nes_write_slow(a, v);
}

/* ---- PPU state ---- */
extern uint8_t nes_vram[0x800];         /* two nametables (with attributes), after mirroring */
extern uint8_t nes_pal[32];
extern uint8_t nes_oam[256];
extern uint8_t ppu_ctrl, ppu_mask, ppu_fx;
extern uint16_t ppu_t;
extern uint8_t mmc_ctrl, mmc_chr0, mmc_chr1, mmc_prg;
extern uint8_t ppu_vbl;                 /* vertical blank flag, as read from $2002 */

/* What changed since the display last looked (the display clears these). */
#define NES_DIRTY_MAX 1024
extern uint16_t nes_dirty[NES_DIRTY_MAX];       /* nametable cells (0..0x7ff, attribute cells included) */
extern uint16_t nes_ndirty;
extern uint8_t  nes_dirty_flag[0x800];
extern uint8_t  nes_redraw_all;                 /* too many changes, or the tile set changed: redraw everything */
extern uint8_t  nes_pal_dirty;

/* A raster split: the game waits for sprite 0 and then changes scroll and CHR banks for the rest of the
   picture. These hold the values for the upper part; the ppu_ and mmc_ values are the lower part. */
extern uint8_t  split_on, split_ctrl, split_chr0, split_chr1, split_fx, split_line;
extern uint16_t split_t;
extern uint8_t  split_seen;                     /* a split happened in the NMI that just ran */

/* APU: the register file, and which channels were (re)started this frame. The Amiga side plays it. */
extern uint8_t apu_reg[0x18];
extern uint8_t apu_trig;

/* Controller: buttons in NES order (bit 0 A, 1 B, 2 Select, 3 Start, 4 Up, 5 Down, 6 Left, 7 Right) */
extern uint8_t nes_pad;

/* hooks the translated code calls */
uint16_t nes_dispatch(uint16_t pc);             /* generated: run the code at pc (in the mapped bank), return the next pc */
uint16_t nes_reset_entry(void);
void     nes_wait(int kind, uint16_t pc);       /* logic.c: the game waits for the next frame here */
#define WAIT_RAM 0
#define WAIT_VBL 1

#endif
