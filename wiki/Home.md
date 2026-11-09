# Ring Racers E wiki

This wiki explains what Ring Racers E changes compared to the official
[Dr. Robotnik's Ring Racers](https://github.com/KartKrewDev/RingRacers) 2.4 source code
(commit [`49beb6b`](https://github.com/KartKrewDev/RingRacers/tree/49beb6b74bce839f414b6ea9ad655f4a0c72a801)),
and **why** each change was made. It does not document the rest of the game: for that, read the official
source code (links to the official code always point to commit `49beb6b`, so line numbers stay valid).

For a short feature list and build instructions, see the [README](../README.md).

## Pages

- [Installing](Installing.md): Windows and Linux, game data, minimum requirements.

Graphics:
- [Graphics APIs and live switching](Graphics-APIs.md): how renderers are detected, chosen and switched without a restart.
- [Direct3D 9 backend](Direct3D-9-Backend.md): a new RHI backend for the Software renderer.
- [DirectX 11 and Vulkan (ANGLE)](ANGLE-DirectX-11-and-Vulkan.md): Legacy GL on top of OpenGL ES 3.
- [DirectX 12 (Mesa)](Mesa-DirectX-12.md): Legacy GL translated by Mesa GLOn12.

Performance:
- [Low-End Mode](Low-End-Mode.md): every lighter system, and why it is safe.
- [Static geometry cache](Static-Geometry-Cache.md): how Legacy GL draws the level in Low-End Mode.
- [Legacy GL changes](Legacy-GL-Changes.md): renderer changes (some always active, some only in Low-End Mode: sky, draw distance...).

Online and configuration:
- [Online](Online.md): Ring Racers E only, Direct Join, My IP Addresses, Low-End Mode in netgames.
- [Configuration file](Configuration-File.md): `ringconfigE.cfg` and how it is kept in sync with the official config.

Other:
- [Windows 7 and the 32-bit build](Windows-7-and-32-bit.md)
- [Small fixes](Small-Fixes.md): floor sprites on slopes, damaged ban file, window focus.
- [Developer options](Developer-Options.md): test options, command line switches and log tags.

## Where the new code is

| File | What it does |
|---|---|
| `src/hardware/rr_gfxapi.cpp/.h` | Graphics API detection, Mesa and ANGLE setup |
| `src/hardware/rr_gfxpack.cpp/.h` | ANGLE and Mesa DLLs embedded in the executable |
| `src/sdl/i_video.cpp` | Window creation, live API switching, focus handling |
| `src/rhi/d3d9/` | Direct3D 9 / 9Ex RHI backend and its HLSL shaders |
| `src/hardware/r_opengl/r_gles.inl` | Old OpenGL features emulated on OpenGL ES 3 (ANGLE) |
| `src/hardware/rr_staticgeo.cpp` | Static geometry vertex pool (Low-End Mode, Legacy GL) |
| `src/rr_config.h` | Low-End Mode switch and optional test options |
| `src/rr_nete.c/.h` | Ring Racers E only netgames, Low-End Mode in netgames |
| `src/rr_netip.cpp` | Local IP address list for "My IP Addresses" |
| `src/hardware/rr_cfgsync.cpp/.h` | `ringconfigE.cfg` sync with `ringconfig.cfg` |

Changes inside official files are marked with comments, and most Low-End code is inside
`#ifdef RR_LOWEND_CODE` blocks that only run when `RR_LOWEND` is true.
