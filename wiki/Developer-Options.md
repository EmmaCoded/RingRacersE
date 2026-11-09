# Developer options

[Home](Home.md) - File: `src/rr_config.h`

These are for testing and measuring. Players do not need them.

## Test options file

`rr_config.h` reads optional test options from `devconfig/rr_config.txt` (folder `RRCFG_DIR`), one per line:

    key = value      # comment

If a key is missing there, the older single-option file `devconfig/rr_<key>` is checked. Without the file, every option
keeps its default, which is the normal case for players. `rr_config.h` is header-only (`static inline`), so it needs no
build changes.

| Key | Default | Effect |
|---|---|---|
| `ring_sleep` | 1 | 0 = no sleeping rings in Low-End Mode |
| `fling_nocollide` | 1 | 0 = flung rings and debt spikes check object collisions again in Low-End Mode |
| `bot_ai_mask` | 1 | 0 = bot AI every tic in Low-End Mode |
| `bot_wp_mask` | 3 | Bots search waypoints 1 tic in (mask + 1) in Low-End Mode; 0 = every tic |
| `bot_effects` | 0 | 1 = bots off screen keep their dust, smoke and speed lines in Low-End Mode |
| `nostatic` | off | Turns off the [static geometry cache](Static-Geometry-Cache.md) |
| `dist` | 12288 | Legacy GL draw distance in Low-End Mode (1024 to 32768), see [Legacy GL changes](Legacy-GL-Changes.md) |

Options are read once (at startup or when a course loads), never per frame.

## Command line

| Option | Effect |
|---|---|
| `-d3d9` | Start with Direct3D 9 if the config has no `gfxapi` yet |
| `-testwindow` | Small window in the top right corner that does not steal focus; the game does not pause without focus; no audio. For automated tests. |
| `-rrmenutest N` | Opens Video > Advanced with the cursor on renderer N (menu test without a keyboard) |
| `-rrswitchtest 9,7,1` | With the menu open, switches the renderer to each value every 6 seconds (live switch stress test) |

## Log tags

Diagnostic lines go only to the log file (`RR_DIAGPRINT` = `I_OutputMsg`), never to the in-game console, so they do not
cover the screen during a race. Main tags:

| Tag | Meaning |
|---|---|
| `[GFXAPI]` | API detection, Mesa/ANGLE setup, live switches |
| `[D3D9]` | Direct3D 9 backend |
| `[LOWEND]` | Low-End Mode on/off when a course loads |
| `[STATIC]` | Static geometry cache statistics |
| `[DIST]` | Legacy GL draw distance in use |
| `[TERMINATE]` | Uncaught C++ exception before aborting |
