# Starting a new NES → Amiga 1200 port

## Before the session

1. On GitHub, make the new repository from **johnshawler/a1200-final** ("Use this template"; tick
   "Template repository" in a1200-final's settings once). Don't work in a1200-final itself: it is the
   knowledge base every port starts from.
2. Start a Claude Code session with the **new** repository attached, and with read access to
   johnshawler/a1200-final if you want the session to bring its lessons back there at the end.
3. Upload the NES ROM. Upload a Kickstart ROM only if you want a final check under the real OS (the bench uses
   the free AROS ROM). Neither ROM may be committed: the repository is public.
4. If the `amiga-nes-port` skill is installed in Claude (`skills/amiga-nes-port/SKILL.md`), it applies by itself;
   the first message below works with or without it.

## The first message (paste, then fill in the two names)

```text
Port the NES ROM I uploaded (<file name>) to the Amiga 1200. The game is <name>; use "<short_name>" as its
name in the tools (lower case, letters and digits).

Before anything else read PLAYBOOK-NES.md (including the lessons log), PIPELINE.md, history/*/LEARNINGS.md,
BRIEF.md and CLAUDE.md. Then run tools/new_port.sh <short_name> <path to the uploaded ROM> and follow the
playbook's first-day checklist.

Tell me early:
- whether the ROM's mapper and sizes are supported by the tools, and what it takes if not;
- whether the game changes scroll at a raster split (a fixed status bar), which the kit cannot show yet;
- the load measured per scene and your verdict for a stock A1200 and for 4 MB fast RAM, before writing code.

Keep the ROMs and everything made from them out of git. Commit and push as you go. Before finishing, write
REPORT.txt and LEARNINGS.md, add an entry to the playbook's lessons log, fold general lessons into the
playbook, and open a pull request against johnshawler/a1200-final that brings the improved playbook and
tools back (not this game's files).
```

## If the session stops or restarts

Everything that matters is in the repository: tell the new session "continue the port: read PLAYBOOK-NES.md,
PIPELINE.md, NOTES.md and the git log first, then carry on from the last commit".

## What to expect

- Supported now: MMC1 boards with 128 KB PRG + 128 KB CHR, background tile-set splits, hardware-sprite
  multiplexing, the APU on Paula. Other boards and scroll splits are new work (PLAYBOOK-NES.md section 8).
- 50 Hz depends on how busy the original keeps the NES CPU; the session measures it on day one.
- A port takes many hours, possibly several sessions.
