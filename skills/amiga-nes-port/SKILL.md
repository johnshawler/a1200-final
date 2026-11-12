---
name: amiga-nes-port
description: Port an NES game (or any 6502 game) to the Commodore Amiga 1200 by static translation. Use whenever the user asks to port a NES/Famicom ROM or other 6502 game to the Amiga, A1200, AGA, or an Amiga port template, or asks to continue or improve such a port. Brings in the accumulated playbook, tools and lessons from earlier ports in johnshawler/a1200-final, and makes the session add its own lessons back.
---

# Porting a NES game to the Amiga 1200

Earlier ports left a playbook, a working toolchain and a lessons log in the GitHub repository
**johnshawler/a1200-final** (branch `main`). Every new port starts from them and adds to them. Without this,
each port starts from zero.

## Before anything else

1. Get the knowledge into this session. If the repository is not in the session, attach it (read access is
   enough), or fetch the files. Read, in this order:
   - `PLAYBOOK-NES.md`: what is reusable, the first-day checklist, NES behaviours, the idioms that gave the
     big wins, the feasibility table, what is not done yet, and the lessons log (read every entry).
   - `PIPELINE.md` part A: the order of milestones.
   - The latest `LEARNINGS.md` (in the repository root, or `history/<game>/LEARNINGS.md`): details and numbers.
   - `BRIEF.md` and `CLAUDE.md` of the port repository: the rules and the referee still apply.
2. Set up the port repository from a1200-final, not from scratch:
   - Best: the user makes the new repository from a1200-final ("Use this template", or a copy), then run
     `tools/new_port.sh <game> <rom.nes>`. It archives the previous port under `history/`, keeps the tools,
     resets the per-game files and copies the ROM in (never commit ROMs: public repositories).
   - If the new repository came from the plain template, copy in `PLAYBOOK-NES.md`, `PIPELINE.md`,
     `tools/new_port.sh` and the files listed in section 1 of the playbook from a1200-final first.
3. Say to the user, in one line, which earlier lessons apply to this game (for example: "the load measurement
   comes first; earlier fielding scenes needed 11,000 instructions a frame").

## During the port

- Follow the playbook's first-day checklist and `PIPELINE.md`: reference emulator, **measure the load per scene
  and give the verdict before writing code**, trace, translate, prove tick by tick, then Amiga checksum, then
  profile (1 kHz profiler, busiest scene only), then display on the host harness, then sound.
- Put game-specific knowledge in `tools/games/<game>.py`, nowhere else in the tools.
- When something general is learned (a new idiom, a mapper, a pitfall, a better number), write it down at once
  in the playbook section it belongs to; don't wait for the end.
- Never claim a frame rate the referee did not print; keep the checksum equal after every change.

## Before finishing

1. Add an entry to the playbook's lessons log (game, mapper, load numbers, result per machine, what was new,
   what to do differently) and fold general lessons into its sections.
2. Write `REPORT.txt` and a `LEARNINGS.md` for this port.
3. Offer to bring the improved playbook and tools back into **johnshawler/a1200-final** `main` (a pull request),
   so the next port starts from them. Tell the user plainly if you could not.
