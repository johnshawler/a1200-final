# Amiga 1200 port

Your instructions are in `BRIEF.md`. Read all of it before you do anything else, then follow it exactly.

- The game to port is in `original/`. Your code goes in `src/`, converted data in `assets/`, your own host
  scripts in `tools/`. Copy `kit/main.c` to `src/main.c` and edit the copy.
- Never change `kit/` or the kit's tools (`tools/build.sh`, `bench.sh`, `run_uae.sh`, `gfx.py`).
  `tools/check_kit.sh` and CI fail if you do.
- Run `tools/setup.sh` once. Describe your port in `port.conf`. Then `tools/port.sh` builds it, benches it on
  the emulated stock A1200, runs your host test and checks the gates. The port is done only when it prints
  `PASS`. Never claim a frame rate that `tools/port.sh` did not print.
- NES (or other 6502) ports: read `PLAYBOOK-NES.md` first and reuse its tools. Knowledge about one game goes in
  `tools/games/<game>.py`; before finishing, add an entry to the playbook's lessons log and fold general
  lessons into its sections. That file is how the next port starts better than this one.
- Work in the order of `PIPELINE.md` (milestones reprioritised from what this port found; its backlog lists the
  next items for this port). Read `LEARNINGS.md` before changing the translator or the display.
- Keep `NOTES.md`: one line for every place the brief was unclear, wrong or missing something.
- When you finish, or are stuck at a gate after real attempts, write the PORT REPORT in the brief's format
  to `REPORT.txt`.

@BRIEF.md
