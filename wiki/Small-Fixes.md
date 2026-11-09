# Small fixes

[Home](Home.md)

## Floor sprites on slopes

File: `src/hardware/hw_main.cpp` (`HWR_SplatClampY`)

**Problem:** in Legacy GL, floor sprites (sneaker panels and other "splats") were partly hidden under the floor.
The Software renderer draws them without a depth buffer, so they are always visible there; in GL the floor hides any
part of the sprite that ends up slightly below it.

Real case: in `RR_RobotnikCoaster`, a panel on one floor took its slope from the **neighbouring** sector, so part of
the panel was below the real floor.

**Fix:** each corner of the floor sprite is moved up to the real floor at that point (or down to the ceiling for
upside-down objects), only when the difference is smaller than 128 units, so intentional offsets are kept.

Original function:
[`HWR_DrawSprite`](https://github.com/KartKrewDev/RingRacers/blob/49beb6b74bce839f414b6ea9ad655f4a0c72a801/src/hardware/hw_main.cpp#L3670).

## Damaged ban file

File: `src/k_bans.cpp`

**Problem:** a power cut while the game was saving could leave `srvbans.json` full of zeros. The official game then
stopped at startup with "Not a valid ban file"
([original code](https://github.com/KartKrewDev/RingRacers/blob/49beb6b74bce839f414b6ea9ad655f4a0c72a801/src/k_bans.cpp#L127)),
and the player had to find and delete the file by hand.

**Fix:** the damaged file is renamed to `srvbans.json.bad` (the old `.bad` is deleted first, because on Windows
`rename` does not replace an existing file), a warning is printed, and the game continues without bans.
The file is closed before renaming, because Windows cannot rename an open file.

## Window focus

File: `src/sdl/i_video.cpp` (`Impl_PollFocus`)

**Problem:** the game pauses (single player), mutes and releases held keys when it loses focus. The official code does
this from SDL window events. With live API switching, one window is hidden and another created, and with ANGLE, Mesa
or Direct3D the focus events of the old window can arrive **after** the new one is shown, so the game could stay
paused or muted.

**Fix:** focus is polled once per frame from the current window's real focus flag, which works the same with every API.
Only changes are applied, so a game started without focus (for example from a script) keeps running until it gains and
loses focus. Events from any window that is not the current one (such as the hidden OpenGL window while Direct3D 9 is
active) are ignored.

Original event handler:
[`Impl_HandleWindowEvent`](https://github.com/KartKrewDev/RingRacers/blob/49beb6b74bce839f414b6ea9ad655f4a0c72a801/src/sdl/i_video.cpp#L473).

## Display Mode (Window / Fullscreen / Borderless)

Files: `src/cvars.cpp` (`rrdisplaymode`, `RR_ToggleDisplayMode`, `RR_DisplayModeStartup`), `src/sdl/i_video.cpp`
(`RR_ApplyWindowBorder`), `src/k_menufunc.c` (F11), `src/menus/options-video-1.c`

**Problem:** with a window the same size as the screen (for example 1366x768 on a 1366x768 screen), the title bar ends
up above the top of the screen, and Fullscreen (F11) looks almost the same as windowed, so it seems to do nothing. There
was no real borderless window.

**Change:** the Video option "Fullscreen" became **Display Mode**: Window, Fullscreen or Borderless (a window without
borders that covers the screen it is on; the game is scaled to it).

- The option is the saved **preference**. The current state is `fullscreen` + `rrborderless` (not saved).
- **F11 only changes the current state**: with the Borderless preference it toggles Window <-> Borderless; with
  Window or Fullscreen it toggles Window <-> Fullscreen. The option in the menu does not change.
- At startup the preference is applied (`RR_DisplayModeStartup`, after the config is loaded). Configs from older
  versions (without `rrdisplaymode`) take the preference from the saved `fullscreen` value.
- These are graphics options, so they are not copied to the official config file.

## Crash information

If a C++ exception is not caught anywhere (for example inside a graphics backend on an unusual GPU), its message is
written to the log before the game aborts, so the reason is not lost.
