# Ring Racers E 2.4E

<p align="center">
  <a href="https://www.kartkrew.org">
    <img src="docs/logo.png" width="404" style="image-rendering:pixelated" alt="Dr. Robotnik's Ring Racers logo">
  </a>
</p>

Ring Racers E is an unofficial, performance-oriented edition of [Dr. Robotnik's Ring Racers](https://github.com/KartKrewDev/RingRacers) 2.4 for PC. It adds several graphics APIs that can be switched while the game is running, an optional Low-End Mode for weak computers, and a 32-bit build that runs on Windows 7.

It is based on the official source code at commit [`49beb6b`](https://github.com/KartKrewDev/RingRacers/tree/49beb6b74bce839f414b6ea9ad655f4a0c72a801). Gameplay, content and data files are the same as the official game: you need the official Ring Racers 2.4 data files (`bios.pk3` and the rest) to play.

**This is not an official Kart Krew Dev release.** Please do not report problems with Ring Racers E to Kart Krew Dev.

## Features

### Graphics APIs

Video > Advanced > **Rendering Backend...** shows a list with every renderer your PC can use (the ones it cannot use are hidden), with a short description of each one.

| Renderer | How it works | Notes |
|---|---|---|
| Software | Official software renderer | Always available |
| OpenGL | Official Legacy GL renderer | 3D models and shaders |
| Direct3D 9 | New Direct3D 9 backend | Software mode only |
| Direct3DEx 9 | Same backend, using Direct3D 9Ex | Software mode only |
| DirectX 11 | Legacy GL translated to Direct3D 11 by [ANGLE](https://chromium.googlesource.com/angle/angle) | 3D models and shaders; needs the `angle/` folder |
| Vulkan | Legacy GL translated to Vulkan by ANGLE | 3D models and shaders; needs the `angle/` folder |
| DirectX 12 | Legacy GL translated to Direct3D 12 by [Mesa](https://www.mesa3d.org/) | 3D models and shaders; needs the `mesa/` folder; 64-bit only |

- **Live switching:** changing the renderer does not restart the game. The new window opens where the old one was.
- Mesa is never unloaded once it has been loaded (unloading it caused a crash a few seconds later), so after using DirectX 12 the game may ask for a restart.
- If ANGLE or Mesa fails to start, the game goes back to OpenGL and reopens itself.
- Hidden "safe switch" option: set `rrapirestart "On"` in `ringconfigE.cfg` to always restart the game when the renderer changes.
- To run the Legacy GL renderer on ANGLE (OpenGL ES 3.0), `src/hardware/r_opengl/r_gles.inl` emulates the old fixed-function OpenGL calls that Legacy GL uses (matrix stack, vertex pointers, desktop GLSL shaders).
- With Mesa, `.md3` models are loaded as floats: Direct3D 12 has no equivalent of the compact "tinyframes" vertex formats, and Mesa had to convert them on every draw.

### Low-End Mode

Video > Advanced > **Low-End Mode**. Off by default. When it is off, the game runs the original code.

When it is on, the game uses lighter versions of several systems: rings far from players sleep, bot AI and bot waypoints update less often, distant springs, item boxes and decorations think less often, there are fewer particles and effects, and Legacy GL draws the level geometry from a static cache (`src/hardware/rr_staticgeo.cpp`).

Low-End Mode is applied when a course loads. It is not used while watching replays.

In netgames, the host decides: if the host uses Low-End Mode, every player uses it. A player who joins with Low-End Mode on while the game is running without it spectates until the end of the course, and then the whole game switches to Low-End Mode. Replays recorded with Low-End Mode on are not tested yet.

### Online

- Ring Racers E only plays online with other Ring Racers E players. The official game and Ring Racers E show a clear message instead of trying to connect.
- There is no server browser: use **Direct Join** with an IP address. E games are never listed publicly: the "Make Public" option was removed from Options > Server.
- **My IP Addresses:** press **Y** in the Online menu, in Host Game or in the host's pause menu (hint in the bottom left corner), or use the console command `rrmyip`: shows the addresses other players can use (LAN, Wi-Fi, ZeroTier, Hamachi, Radmin, Tailscale and other VPNs) with the port. It never shows a public IP of a normal network adapter.

### Other changes

- **Display Mode** (Video Options): Window, Fullscreen or Borderless (a window without borders that covers the screen). F11 toggles between Window and the chosen mode (Fullscreen when Window is chosen) without changing the option.
- Own configuration file, `ringconfigE.cfg`, kept in sync with the official `ringconfig.cfg` key by key (the newest value wins; graphics settings are not synced), so both games can be used side by side.
- 32-bit build (MinGW32) that runs on Windows 7. `d3dcompiler_47.dll` can be embedded inside the executable, because Windows 7 does not ship it.
- Legacy GL fix: floor sprites (like sneaker panels) on slopes are no longer hidden under the floor.
- A damaged `srvbans.json` no longer stops the game: it is renamed to `srvbans.json.bad` and the game continues.

## Documentation

The [wiki](wiki/Home.md) explains every change compared to the official code, and why it was made.

## Building from source (Windows)

Ring Racers E is built with [MSYS2](https://www.msys2.org/), CMake and Ninja.

**64-bit** (MSYS2 UCRT64 shell):

    pacman -S --needed git mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,pkgconf,SDL2,libpng,zlib,curl,opus}
    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DSRB2_CONFIG_ENABLE_WEBM_MOVIES=OFF -DSRB2_CONFIG_ENABLE_DISCORDRPC=OFF
    cmake --build build

**32-bit** (MSYS2 MINGW32 shell): the same commands with the `mingw-w64-i686-` packages.

Optional: to embed `d3dcompiler_47.dll` (needed by Direct3D 9 on Windows 7), put `d3dcompiler_47_x64.dll` and/or `d3dcompiler_47_x86.dll` from the Windows SDK redistributables in the `embed/` folder (or point `-DRRE_EMBED_DIR=` to another folder). If the file is missing, nothing is embedded and the game uses the system copy.

Copy the DLLs the executable needs from the MSYS2 `bin` folder next to it (`ldd RingRacersE.exe` lists them).

The official build instructions (vcpkg and CMake presets, Linux libraries) are in the [official README](https://github.com/KartKrewDev/RingRacers/tree/49beb6b74bce839f414b6ea9ad655f4a0c72a801#building-from-source). On Linux, only the Software and OpenGL renderers are available.

### Optional folders: `angle/` and `mesa/`

These DLLs are **not** part of this repository. Put the folders next to the executable:

- `angle/` (DirectX 11 and Vulkan): `libEGL.dll`, `libGLESv2.dll` and `vulkan-1.dll` from a recent ANGLE build with the Vulkan backend (for example, the ones shipped with Google Chrome), plus their license files. The `mingw-w64-ucrt-x86_64-angleproject` MSYS2 package also works, for DirectX 11 only.
- `mesa/` (DirectX 12): `opengl32.dll`, `libgallium_wgl.dll` and `dxil.dll` from [mesa-dist-win](https://github.com/pal1000/mesa-dist-win) (release-mingw, 64-bit).

Licenses: ANGLE is BSD-3-Clause, Mesa is MIT, and the Vulkan loader (`vulkan-1.dll`) is Apache-2.0. Their license files must be shipped together with the DLLs.

## Credits

- **Kart Krew Dev** made Dr. Robotnik's Ring Racers. The game, its code and its content are their work; Ring Racers E only changes part of the code. Links: [website](https://www.kartkrew.org/), [Discord](https://www.kartkrew.org/discord), [official repository](https://github.com/KartKrewDev/RingRacers).
- Ring Racers is based on [Sonic Robo Blast 2](https://srb2.org/), itself based on [Doom Legacy](http://doomlegacy.sourceforge.net/).
- [ANGLE](https://chromium.googlesource.com/angle/angle) (Google and contributors), [Mesa](https://www.mesa3d.org/), [mesa-dist-win](https://github.com/pal1000/mesa-dist-win) (pal1000), Vulkan loader (Khronos Group).
- Claude (Anthropic) was used as a programming assistant.

## License

Like the official game, Ring Racers E's source code is available under the GNU General Public License version 2.0 or higher. See [LICENSE](LICENSE) and [LICENSE-3RD-PARTY.txt](LICENSE-3RD-PARTY.txt).

## Disclaimer

Dr. Robotnik's Ring Racers is a work of fan art made available for free without intent to profit or harm the intellectual property rights of the original works it is based on. Kart Krew Dev is in no way affiliated with SEGA Corporation. We do not claim ownership of any of SEGA's intellectual property used in Dr. Robotnik's Ring Racers.

Ring Racers E is an unofficial modification and is not affiliated with or endorsed by Kart Krew Dev or SEGA.
