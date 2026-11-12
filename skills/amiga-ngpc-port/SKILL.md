---
name: amiga-ngpc-port
description: Port a Neo Geo Pocket or Neo Geo Pocket Color game (TLCS-900/H code, .ngc or .ngp cartridge image) to the Commodore Amiga 1200 by static translation. Use whenever the user asks to port an NGPC or NGP game to the Amiga, A1200 or AGA, or asks to continue or improve such a port. Brings in the playbook, tools and lessons kept in johnshawler/a1200-final, and makes the session add its own lessons back.
---

# Porting a Neo Geo Pocket Color game to the Amiga 1200

Earlier work left a playbook, a tool chain and a lessons log in the GitHub repository
**johnshawler/a1200-final** (branch `main`). Every new port starts from them and adds to them. Without this,
each port starts from zero.

## Before anything else

1. Get the knowledge into this session. If the repository is not in the session, attach it (read access is
   enough), or fetch the files. Read, in this order:
   - `PLAYBOOK-NGPC.md`, all of it: what is reusable, the first-day checklist, the machine, what the frame
     model must get right, the translator, the verdict, what exists of display and sound, **what is not done
     yet**, and the lessons log (every entry).
   - `PIPELINE.md` part A: the order of milestones for a translated binary.
   - `BRIEF.md` and `CLAUDE.md` of the port repository: the rules and the referee still apply.
   - `PLAYBOOK-NES.md` sections 3-5 and 7: the same method on another machine, with a 68k back end and a
     sprite display that this playbook points to.
2. Set up the port repository from a1200-final, not from scratch: the user makes the new repository from it
   ("Use this template", or a copy), then run `tools/new_port_ngpc.sh <game> <rom.ngc> [bios.ngp]`. Never
   commit the cartridge or BIOS image, or anything made from them.
3. Say to the user, in a few lines and before writing code: whether the cartridge's data fits a stock A1200,
   what the game uses that the tools do not model, the load of both processors per scene, and your verdict.

## During the port

- Follow the playbook's first-day checklist and `PIPELINE.md`: reference machine, memory check, **load of the
  main CPU and of the sound program per scene, verdict before code**, traces of every scene, translation,
  proof tick by tick (`tools/verify_ngpc.sh`), then the Amiga checksum, then speed, display and sound.
- The sound program is part of the game's logic: games wait for its answers. Check that it is alive in the port's
  frame model (the playbook says how) before trusting any number.
- Put game-specific knowledge in `tools/games/<game>.py`, nowhere else in the tools.
- When something general is learned (an instruction form made native, a hardware feature modelled, a pitfall,
  a better number), write it into the playbook section it belongs to at once.
- Never claim a frame rate the referee did not print; keep the checksum equal after every change.

## Before finishing

1. Add an entry to the playbook's lessons log (game, cartridge size and data size, load numbers of both
   processors, result per machine, what was new, what to do differently) and fold general lessons into its
   sections. Move what you built out of section 8 ("not done yet").
2. Write `REPORT.txt` in the brief's format, with the failing output if a gate fails.
3. Offer to bring the improved playbook and tools back into **johnshawler/a1200-final** `main` (a pull
   request), so the next port starts from them. Tell the user plainly if you could not.
