# Direct3D 9 backend

[Home](Home.md) - Files: `src/rhi/d3d9/`

## What it is

The official game draws everything through its RHI (render hardware interface, `src/rhi/`), which only
has an OpenGL backend. The Software renderer draws the frame with the CPU and the RHI shows it, together
with the 2D layer (HUD, menus, fonts) and post-processing.

Ring Racers E adds a second RHI backend, `src/rhi/d3d9/`, that implements the same interface with
Direct3D 9. It is used with the **Software renderer only**: Legacy GL calls OpenGL directly and does not
go through the RHI.

**Why Direct3D 9:** it is the API with the widest driver support on old Windows PCs (including Windows 7
and integrated graphics with bad OpenGL drivers), and it is enough for what the RHI needs.

## Direct3D 9Ex

When available (Windows Vista and later), the backend uses `Direct3D9Ex`. **Why:** 9Ex has no "device lost"
state when the window loses focus or changes mode, so Alt+Tab and fullscreen changes are much simpler.
On systems without 9Ex it falls back to classic Direct3D 9 and handles `Reset` itself.

Both appear in the menu as "Direct3D 9" and "Direct3DEx 9". Switching between them keeps the window and
only recreates the device.

## Translating GL conventions to D3D9

The game's renderer code was written for OpenGL, so the backend converts at the edges instead of changing
the game (notes at the top of `d3d9_rhi.cpp`):

- **Origin:** GL rectangles start at the bottom-left; `to_d3d_rect()` converts them for the backbuffer.
- **Render-to-texture:** textures keep GL row order (row 0 = first row uploaded). When drawing into a
  texture, the vertex shader flips Y (`u_d3d.z = -1`), so textures drawn by the game and textures uploaded
  by the game still match.
- **Clip space:** GL uses z in [-w, w], D3D uses [0, w]. `fix_clip()` in the shaders converts it and also
  applies the Direct3D 9 half-pixel offset.
- **Textures:** every RGB/RGBA texture is a render target (`D3DPOOL_DEFAULT`) with a system-memory copy
  ("stage") that data is uploaded from with `UpdateSurface`. **Why:** `D3DPOOL_DEFAULT` resources are lost on
  `Reset`; the stage copy allows recreating everything afterwards.

## Shaders

`d3d9_shaders.cpp` contains HLSL Shader Model 3 versions of the game's GLSL shaders
(`rhi_glsl_*.glsl` in `shaders.pk3`), translated by hand. Conventions:

- glm matrices are uploaded column by column as they are, and multiplied by hand in the shader (`MUL4`),
  so no transposing is needed on the CPU.
- The game's integer uniforms arrive as a float in `.x` of their register.

The HLSL is compiled at startup with `d3dcompiler_47.dll`. Windows 7 does not ship that DLL, so it can
be embedded in the executable: see [Windows 7 and the 32-bit build](Windows-7-and-32-bit.md).

## OpenGL window kept alive

When switching from a GL API to Direct3D 9, the GL window is hidden instead of destroyed, so switching
back is instant and Legacy GL keeps its state. Window events from that hidden window are ignored.
See [Graphics APIs](Graphics-APIs.md#live-switching).
