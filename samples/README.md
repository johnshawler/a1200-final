# Sample games for trying a model out

Three small PC games written for testing this template. Each is one C file against the tiny platform layer in
`common/pcfb.h` (8-bit framebuffer, 60 Hz page flip, keys, file loading, sound trigger numbers), written the
way PC games are: floating point, full redraw every frame, little-endian data files.

| Game | Class in the brief | What the trials got on the emulated stock A1200 |
| --- | --- | --- |
| `breakout/` | still screen | 50 Hz held, worst frame 36% (Haiku, second attempt) |
| `scroller/` | 2D scrolling | 50 Hz held, worst frame 30% (Sonnet), 41% (Haiku, with cuts) |
| `raycaster/` | per-pixel | 9.5 pictures a second, 12.3 with fast RAM (Sonnet) |

To try a model: copy `common/` and one game folder into `original/`, add an `original/README.md`, and give
the model the prompt from the top-level README. Build and run a game on the host like this:

    cd samples/breakout
    cc -O2 breakout.c ../common/pcfb_headless.c -I../common -lm -o breakout_host
    FB_FRAMES=600 FB_DUMP=10,300,599 FB_INPUT=input.txt ./breakout_host

`FB_INPUT` is a text file of lines "first_frame last_frame KEY" (KEY = LEFT RIGHT UP DOWN FIRE). Dumped frames
are PPM files. The scroller's data files are made by `scroller/make_assets.py`.
