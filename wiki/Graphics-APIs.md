# Graphics APIs and live switching

[Home](Home.md)

## What changed

The official game has two renderers: Software (drawn by the CPU, shown through the RHI with OpenGL)
and Legacy GL (OpenGL 1.x/2.x style). Switching between them already works without a restart.

Ring Racers E adds a list of graphics APIs in Video > Advanced > **Rendering Backend...**
(console variable `gfxapi`):

| API | Renderer | Backend |
|---|---|---|
| OpenGL | Software and Legacy GL | Official code, system `opengl32.dll` |
| Direct3D 9 / Direct3DEx 9 | Software only | [Direct3D 9 backend](Direct3D-9-Backend.md) |
| DirectX 11 / Vulkan | Legacy GL (*) | [ANGLE](ANGLE-DirectX-11-and-Vulkan.md), embedded in the exe |
| DirectX 12 | Legacy GL (*) | [Mesa GLOn12](Mesa-DirectX-12.md), embedded in the exe, 64-bit only |

(*) Software through ANGLE or Mesa still works if it is set in the config file, but the menu only lists
these APIs for Legacy GL.

**Why:** many PCs (old laptops, Intel graphics, some Windows 7 machines) have a poor or broken OpenGL
driver but a good Direct3D driver. Translating OpenGL to Direct3D or Vulkan lets those machines run the
game with their better driver, without rewriting Legacy GL.

## Detection

`src/hardware/rr_gfxapi.cpp` probes each API once ("detected" = the hardware and driver support it,
"implemented" = the game has a backend for it). Only APIs that are both detected and implemented are
shown in the menu. Every graphics DLL (d3d9, d3d11, d3d12, vulkan-1, Mesa, ANGLE) is loaded with
`LoadLibrary`, so the executable starts on any Windows version even if some of them are missing.

`rr_gfxapi.cpp` does not include game headers: `windows.h` and the Direct3D headers clash with
some game macros, so only `CONS_Printf` is declared by hand.

## Live switching

**Why:** restarting the game to try another API is slow and loses the current state. The official
game could only switch Software <-> Legacy GL live, because both use the same OpenGL window.

How each switch works (`src/sdl/i_video.cpp`, "Live graphics API switch"):

- **DX9 <-> DX9Ex:** the window is kept; only the Direct3D device is recreated.
- **GL -> D3D9:** the GL window is **hidden, not destroyed**, together with its context and RHI.
  Coming back to the same GL implementation just shows it again, so Legacy GL keeps its textures,
  shaders and models.
- **One GL implementation -> another** (plain OpenGL, Mesa, ANGLE): the GL windows are destroyed,
  SDL unloads its OpenGL DLL (`SDL_GL_UnloadLibrary`), the next DLL is prepared and a new window is created.
  Before the old context is destroyed, `RR_LegacyGLContextLost` runs with that context current and
  releases everything Legacy GL created in it (textures, shaders, sky and static cache VBOs, models).
  Everything is rebuilt in the new context.
- The new window opens where the old one was.
- After the switch, the hwr2 state that pointed at the old RHI is rebuilt immediately, because
  `SDLSetMode` draws a frame right away.

Exceptions that need a restart:
- Mesa chooses its driver (`d3d12` or `zink`) only once per process. Today only DirectX 12 uses Mesa
  (Vulkan uses ANGLE), so this only matters if a Mesa Zink path is enabled again.
- Hidden "safe switch" option: `rrapirestart "On"` in `ringconfigE.cfg` always restarts the game when the
  API changes (decided in `cvars.cpp`). It exists for drivers that misbehave when a second device or
  context is created in the same process.
- On Linux and macOS there is no live API switch (only OpenGL exists there).

## Fallbacks

- If Direct3D 9 fails to start, a new OpenGL window is created instead.
- If Mesa or ANGLE cannot create a context, the config goes back to OpenGL and the game restarts itself.
- OpenGL versions are tried from highest to lowest, always with a **compatibility** profile, because
  Legacy GL uses old functions (`glVertexPointer`, fixed matrices) that the core profile does not have.
- An uncaught C++ exception (for example from a backend on an unusual GPU) is written to the log
  before the game aborts, so the cause is not lost.

## Window focus

See [Small fixes](Small-Fixes.md#window-focus): focus is polled every frame instead of relying on window
events, because a live switch hides one window and creates another.

## Embedded ANGLE and Mesa

Files: `src/hardware/rr_gfxpack.cpp`, `src/hardware/rr_gfxapi.cpp`, `src/hardware/CMakeLists.txt`, `CMakeLists.txt`

**Why:** players should only need one file. Before, DirectX 11, Vulkan and DirectX 12 needed `angle/` and `mesa/`
folders next to the executable, and the official game's DLLs (`SDL2.dll`, `libcurl-4.dll`...) have the same names
as the ones Ring Racers E was built with, so copying them could replace the official ones.

How the release executable is built:

1. `-DRRE_STATIC_DEPS=ON` (MinGW) links SDL2, curl, libpng, zlib and Opus statically, with the extra libraries that
   `pkg-config --static` lists. The executable only needs Windows DLLs.
2. If `RRE_EMBED_DIR/angle/` and `RRE_EMBED_DIR/mesa/` have the DLLs, CMake gzips each one at configure time
   (`file(ARCHIVE_CREATE ... FORMAT raw COMPRESSION GZip)`) and generates `rre_pack.inc`, which includes them in the
   executable with `.incbin` (the same way as `d3dcompiler_47.dll`, see
   [Windows 7 and the 32-bit build](Windows-7-and-32-bit.md)). About 34 MB compressed.
3. When DirectX 11, Vulkan or DirectX 12 is started, `RRPack_Extract` inflates that group's DLLs with zlib to
   `%LOCALAPPDATA%\RingRacersE\<group>-<id>\` (only the missing ones; each file is written as `.tmp` and renamed, so a
   half-written DLL is never used). `<id>` comes from the CRC32 of the files, so a new version uses a new folder and
   never fights a copy that another running game has loaded.
4. Detection (`RRGfx_AngleAvailable`, `RRGfx_MesaAvailable`) only checks that the files are embedded: nothing is
   written until the API is actually used.

An `angle/` or `mesa/` folder next to the executable still wins over the embedded copy (useful to try another ANGLE
or Mesa version).
