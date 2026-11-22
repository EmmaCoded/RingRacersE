# DirectX 12 (Mesa)

[Home](Home.md) - Files: `src/hardware/rr_gfxapi.cpp`, `src/sdl/i_video.cpp`, `src/hardware/hw_md2.cpp`

## What it is

[Mesa](https://www.mesa3d.org/) can expose desktop OpenGL on top of Direct3D 12 ("GLOn12", Gallium driver
`d3d12`). Ring Racers E loads Mesa's `opengl32.dll` from the copy embedded in the executable (written to
`%LOCALAPPDATA%\RingRacersE\mesa-<id>\` the first time, see [Embedded DLLs](Graphics-APIs.md#embedded-angle-and-mesa))
or from a `mesa/` folder next to the executable if there is one
(with `libgallium_wgl.dll` and `dxil.dll`, from [mesa-dist-win](https://github.com/pal1000/mesa-dist-win)).

Unlike ANGLE, Mesa gives full desktop OpenGL, so Legacy GL runs on it without the ES emulation.

**Why:** a Direct3D 12 path for PCs where the vendor OpenGL driver is the weak point. 64-bit only, because
mesa-dist-win has no 32-bit OpenGL build.

## Making SDL use Mesa's OpenGL

Loading Mesa's `opengl32.dll` is not enough: SDL2 chooses the pixel format and calls `SwapBuffers` through
`gdi32.dll`, and gdi32 always forwards to the **system** OpenGL in `system32`, never to Mesa. The result
was "No matching GL pixel format".

Fix (`rr_gfxapi.cpp`): after Mesa is loaded, the 5 gdi32 imports of `SDL2.dll` that do this are patched to
point at Mesa's `wgl*` functions (same arguments). When leaving Mesa during a live switch, the patch is
undone and SDL2 goes back to gdi32.

Mesa's `opengl32.dll` loads `libgallium_wgl.dll` and `dxil.dll` by name, so the `mesa/` folder is added to the
DLL search path, and `GALLIUM_DRIVER=d3d12` is set (both with `SetEnvironmentVariable` and `_putenv_s`,
because Mesa reads the C runtime copy with `getenv`).

## Mesa is never unloaded

Unloading Mesa left Direct3D 12 cleanup threads running inside the unloaded DLL, and the game crashed a few
seconds later. So once Mesa has been loaded it stays loaded until the game exits; switching away from DirectX 12
only destroys its window and context. Mesa also picks its Gallium driver only once per process.

When the game restarts itself (for example after a failed API start), the DLL folder and `GALLIUM_DRIVER` are
cleared first. Without that, the new copy inherits them and loads Mesa's `opengl32.dll` even when it asked for
regular OpenGL.

## 3D models

Legacy GL normally keeps `.md3` model vertices in a compact "tinyframes" format (small integers). Direct3D 12 has
no equivalent vertex format, so Mesa converted them on the CPU on **every draw**. With Mesa, models are loaded
as floats instead.

Because the vertex format depends on the API, and model VBOs die with the old context anyway, a live API switch
unloads every model (`HWR_UnloadAllModels` in `hw_md2.cpp`); they are reloaded in the right format the next time
they are drawn.

Models that use their sprite as texture adjust their UVs with the sprite's hardware patch when they are reloaded. A live
switch frees every hardware patch (`HWR_ClearAllTextures`), so `adjustTextureCoords` now uses `Patch_CreateGL`, which
recreates it if needed (before, it read a NULL pointer and crashed when such a model appeared after a switch).

## Getting the DLLs

Mesa is not part of the repository. See the README section
[ANGLE and Mesa](../README.md#angle-and-mesa-directx-11-vulkan-directx-12). Mesa is MIT licensed; its license file must
be shipped with the DLLs.
