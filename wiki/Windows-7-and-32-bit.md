# Windows 7 and the 32-bit build

[Home](Home.md) - Files: `src/rhi/d3d9/d3dcompiler_embed.cpp`, `src/rhi/d3d9/d3d9_rhi.cpp`, `src/hardware/CMakeLists.txt`

## Why

Many of the PCs that need Ring Racers E most are old: Windows 7, 2 to 4 GB of RAM, sometimes a 32-bit system.
Ring Racers E builds as 32-bit with MSYS2 MINGW32 and runs on Windows 7 32-bit (tested on a weak laptop: it runs, but
slowly). Build commands are in the [README](../README.md#building-from-source-windows).

Not available in the 32-bit build: DirectX 12 (mesa-dist-win has no 32-bit OpenGL build).

## Embedded `d3dcompiler_47.dll`

The [Direct3D 9 backend](Direct3D-9-Backend.md) compiles its HLSL shaders at startup with `d3dcompiler_47.dll`.
Windows 8 and later ship it; **Windows 7 does not**.

So the DLL can be embedded inside the executable:

1. Put `d3dcompiler_47_x64.dll` and/or `d3dcompiler_47_x86.dll` (Windows SDK redistributables) in the `embed/`
   folder, or point `-DRRE_EMBED_DIR=` at another folder.
2. `src/hardware/CMakeLists.txt` passes the path as `-DRR_D3DC_PATH=...`, and `d3dcompiler_embed.cpp` includes the
   file in the executable with an assembler `.incbin`.
3. At startup, if the system has no `d3dcompiler_47.dll`, `d3d9_rhi.cpp` writes the embedded copy to the temp folder
   and loads it from there.

If the file is not there at build time, nothing is embedded and `rr_d3dc_embedded()` returns `NULL`: the game uses the
system copy (`d3dcompiler_47`, `_46` or `_43`). On Windows 7 without any of them, Direct3D 9 fails to start and the game
stays on OpenGL.

`__USER_LABEL_PREFIX__` is `_` on 32-bit and empty on 64-bit, so the assembler symbol is built with it to match the
C name on both.

## 32-bit details

- Direct3D callbacks use `WINAPI`: on 32-bit the calling convention (stdcall) matters; on 64-bit there is only one.
- Every graphics DLL is loaded with `LoadLibrary`, so a missing DLL (Direct3D 12, Vulkan) on an old system only hides
  that API instead of stopping the game from starting.
