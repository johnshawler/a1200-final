"""ntm.py - what the tools need to know about Neo Turf Masters (SNK 1999, Neo Geo Pocket Color) that they cannot
find out by themselves. Loaded by tools/recomp900.py (--game ntm). PLAYBOOK-NGPC.md says how to find each entry.
"""
EXTRA_ENTRIES = set()   # functions no trace reached (task and state routines stored as pointers)
NOT_WAITS = set()       # branches that look like a wait loop to the translator and are not
