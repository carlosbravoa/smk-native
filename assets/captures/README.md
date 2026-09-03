# Live captures the labs read (LOCAL ONLY - never committed)

CGRAM / VRAM / OAM snapshots taken out of the running game, read by the art
generators and by `tools/labs/vramrender.py` and `leanpix.py`.

Unlike the sheets next door these ARE regenerable, by the labs that write
them:

    tools/labs/vramdump.py      vram<TAG>.bin, cgram<TAG>.bin, oam<TAG>.bin
    tools/labs/coursedump.py    the same, per course
    tools/labs/headlean.py      lean_<left|neutral|right>_*.bin

They live here rather than in `tmp/` because the readers need them to
survive a scratch clean.
