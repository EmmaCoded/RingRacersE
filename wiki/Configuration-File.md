# Configuration file

[Home](Home.md) - File: `src/hardware/rr_cfgsync.cpp`

## Own file: `ringconfigE.cfg`

Ring Racers E saves its settings in `ringconfigE.cfg` instead of the official `ringconfig.cfg`.

**Why:** E has options the official game does not know (`gfxapi`, `rrlowend`, `rrapirestart`...). If both games shared
one file, the official game could choke on them. Separate files let both games be installed side by side.

## Sync with the official config

Having two files would mean setting controls, names and options twice. So when E starts, the **non-graphics** options
are made equal in both files:

- **First run** (no `ringconfigE.cfg` yet): E starts with the official game's non-graphics options.
- **The official file was saved last:** its non-graphics options are copied into E.
- **E's file was saved last:** its non-graphics options are copied into the official file, but **only the options the
  official file already has**. E-only options are never added to the official file.

Rule: the file that was modified last wins.

Options are compared by their first word; for `setcontrol`, also by the second word, because there is one line per control.

### Not synced: graphics options

Renderer, `gfxapi`, resolution, fullscreen and screen color (gamma, hue, saturation...) are never synced.
**Why:** E is often used with a different API or lower settings than the official game on the same PC (that is the point
of E), and copying them could make the official game start with a renderer it does not have.
