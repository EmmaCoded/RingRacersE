# DirectX 11 and Vulkan (ANGLE)

[Home](Home.md) - Files: `src/hardware/r_opengl/r_gles.inl`, `src/hardware/rr_gfxapi.cpp`, `src/sdl/i_video.cpp`

## What it is

[ANGLE](https://chromium.googlesource.com/angle/angle) is the library that browsers use to run WebGL on
top of Direct3D 11 or Vulkan. It exposes **OpenGL ES 3.0** (through EGL), not desktop OpenGL.

Ring Racers E loads ANGLE from the `angle/` folder next to the executable (`libEGL.dll`, `libGLESv2.dll`,
and `vulkan-1.dll` for Vulkan) and tells SDL2 to create an ES context through EGL instead of WGL. Both the
Software renderer (through the RHI) and Legacy GL can run on it.

**Why:** it gives Legacy GL (3D models, shaders, the full hardware renderer) a Direct3D 11 or Vulkan path
without writing a new renderer, on PCs where the OpenGL driver is slow or broken.

## The problem: Legacy GL is not OpenGL ES

Legacy GL ([`src/hardware/r_opengl/r_opengl.cpp`](https://github.com/KartKrewDev/RingRacers/blob/49beb6b74bce839f414b6ea9ad655f4a0c72a801/src/hardware/r_opengl/r_opengl.cpp))
uses features that OpenGL ES 3 removed:

- the fixed matrix stack (`glMatrixMode`, `glLoadMatrixf`, ...)
- client vertex arrays (`glVertexPointer`, `glTexCoordPointer`, `glColorPointer`)
- the fixed-function pipeline (texture environment MODULATE/REPLACE, light 0 for models without shaders)
- alpha test
- `LUMINANCE`, `LUMINANCE_ALPHA`, `ALPHA` textures created from RGBA data
- `glReadPixels` in RGB
- desktop GLSL 1.20 shaders (the game's and those in mods)

## The solution: `r_gles.inl`

`r_opengl.cpp` calls OpenGL through function pointers (`pgl*`). When the context is ES (`rr_gl_es = 1`, set by
`i_video.cpp` for ANGLE), `RRGLES_Install()` swaps those pointers for emulated versions. Plain OpenGL never
goes through this file, so the official path is unchanged.

`r_gles.inl` is included **inside** `r_opengl.cpp` because it needs that file's static pointers.

What it does:

- **Matrices:** keeps its own projection/modelview stacks and uploads them as uniforms before each draw.
  The normal matrix (inverse transpose) is computed on the CPU.
- **Vertex arrays:** `glVertexPointer` and friends become vertex attributes in fixed slots. Like real
  `glVertexPointer`, the buffer bound at call time is latched immediately.
- **Fixed pipeline:** a small built-in shader program emulates color, two textures with MODULATE/REPLACE and
  light 0 for models drawn without shaders.
- **Alpha test:** added at the end of the game's fragment shaders when they are translated.
- **Shaders:** desktop GLSL 1.20 is rewritten to GLSL ES 1.00 (the shader's own `#version` is replaced, built-in
  variables are mapped to the emulation's attributes and uniforms).
- **Textures:** in ES the internal format must match the data format. The game sends RGBA data and asks for
  LUMINANCE_ALPHA/ALPHA/RGB; it is uploaded as RGBA with the same sampling result.
- **Redundant state:** the RHI (Software mode and the 2D layer) always leaves its own program bound. If the
  emulation's program is still bound before a draw, nobody changed the state, so the setup is skipped.

### Driver workaround

ANGLE on Vulkan with an AMD driver leaked about 1.5 MB on every `glCopyTexSubImage2D` into a `GL_RGB` texture.
The final screen is copied every frame, so the game used about 90 MB/s more and crashed after roughly
70 seconds. `GL_RGBA` does not leak, so screen copies use RGBA.

### Screen copies

Screen wipes (`src/f_wipe.cpp`, through the RHI) and Legacy GL screen textures copy the current framebuffer into a
texture with `glCopyTexSubImage2D` / `glCopyTexImage2D`. On ANGLE these calls failed with "Invalid copy texture format
combination", so wipes faded from garbage (solid blocks where text should be). On OpenGL ES the copies are done with
`glBlitFramebuffer` instead, which converts between formats: in `Gl2Rhi::copy_framebuffer_to_texture`
(`src/rhi/gl2/gl2_rhi.cpp`) and in `rrg_CopyTexImage2D` / `rrg_CopyTexSubImage2D` (`r_gles.inl`). Desktop OpenGL keeps
the original calls.

The blit only covers the part of the texture that has screen data. Legacy GL asks for a power-of-two square (2048x2048 at 1366x768), and copying the area outside the screen cost about 14 ms per frame on ANGLE. The temporary framebuffer used for the blit is created once and reused. The screen copy itself is now skipped when nothing uses it, see [Legacy GL changes](Legacy-GL-Changes.md#same-picture-less-work).

### Forced Vulkan validation layer

Vulkan Configurator (vkconfig, part of the Vulkan SDK) can force the **validation layer** for every Vulkan program
through a loader settings file registered in `HKCU\SOFTWARE\Khronos\Vulkan\LoaderSettings`. The validation layer is a
development tool and is very slow: on a test PC Vulkan dropped from 60 to about 37 fps. The loader ignores that file for
elevated (administrator) processes, which is why it did not show up when the game was started from an admin terminal.

Players cannot be expected to fix their vkconfig, so the game protects itself (`src/hardware/rr_gfxapi.cpp`):

- If the settings file forces `VK_LAYER_KHRONOS_validation`, a warning is written to the log.
- While ANGLE (or the API probe) creates the Vulkan instance, `HKEY_CURRENT_USER` of the game process is pointed at
  `HKCU\Software\Classes` with `RegOverridePredefKey`, so the loader does not find the settings file. It is restored
  right after the context is created. Nothing is written to the registry, and implicit layers registered in HKLM
  (RivaTuner, OBS, Steam overlay) still load.
- `VK_LAYER_KHRONOS_validation` is also added to `VK_LOADER_LAYERS_DISABLE`, for when the layer comes from environment
  variables.
- `-vkvalidation` leaves everything as it is, for developers who want the layer.

## Live switching

Before the ES context is destroyed, the emulation forgets its programs, cached state and the real function
pointers, because the next context may come from a different DLL. See [Graphics APIs](Graphics-APIs.md#live-switching).

## Getting the DLLs

ANGLE is not part of the repository. See the README section
[Optional folders](../README.md#optional-folders-angle-and-mesa). ANGLE is BSD-3-Clause and the Vulkan loader
is Apache-2.0; their license files must be shipped with the DLLs.
