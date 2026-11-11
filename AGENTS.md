# Amiga 1200 port

Your instructions are in `BRIEF.md`. Read all of it before you do anything else, then follow it exactly.

- The game to port is in `original/`. Your code goes in `src/`, converted data in `assets/`, your own host
  scripts in `tools/`. Copy `kit/main.c` to `src/main.c` and edit the copy.
- Never change `kit/` or the kit's tools (`tools/build.sh`, `bench.sh`, `run_uae.sh`, `gfx.py`).
  `tools/check_kit.sh` and CI fail if you do.
- Run `tools/setup.sh` once. Describe your port in `port.conf`. Then `tools/port.sh` builds it, benches it on
  the emulated stock A1200, runs your host test and checks the gates. The port is done only when it prints
  `PASS`. Never claim a frame rate that `tools/port.sh` did not print.
- Keep `NOTES.md`: one line for every place the brief was unclear, wrong or missing something.
- When you finish, or are stuck at a gate after real attempts, write the PORT REPORT in the brief's format
  to `REPORT.txt`.
