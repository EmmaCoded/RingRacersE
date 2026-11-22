// rr_gfxapi.h - Ring Racers E: graphics API selection (Windows).
// "Detected" = the hardware/driver supports it (probed once). "Implemented" = the game has a backend for it.
// The API is chosen in Video Options (cv_gfxapi) and can be switched live (see sdl/i_video.cpp).
#ifndef __RR_GFXAPI_H__
#define __RR_GFXAPI_H__

#ifdef __cplusplus
extern "C" {
#endif

enum { RRGFX_OPENGL, RRGFX_DX9, RRGFX_DX9EX, RRGFX_DX11, RRGFX_DX12, RRGFX_VULKAN, RRGFX_COUNT };

int RRGfx_Detected(int api);     // 1 = this PC supports it (probed once, logged as [GFXAPI])
int RRGfx_Implemented(int api);  // 1 = the game has a backend for it
const char *RRGfx_Name(int api);
int RRGfx_Active(void);          // API this process started with
void RRGfx_SetActive(int api);
void RRGfx_SpawnSelf(void);      // starts another copy of the game with the same command line (restart)
int RRGfx_LiveOK(int api, int renderer); // 1 = can switch to that API without restarting (defined in sdl/i_video.cpp)
int RRGfx_GLStale(void);         // 1 = Legacy GL must be rebuilt (defined in sdl/i_video.cpp; currently always 0)
void RRGfx_LiveSwitch(int api, int renderer); // requests a live API switch (renderer 1 = Software, 2 = Legacy GL); done at the start of the next frame

// DirectX 12 = the game's OpenGL translated by Mesa (GLOn12), from the mesa\ folder next to the exe.
int RRGfx_IsMesa(int api);           // 1 = that API comes from Mesa (DX12)
int RRGfx_MesaAvailable(void);       // 1 = mesa\opengl32.dll and libgallium_wgl.dll exist (64-bit only)
const char *RRGfx_MesaPrepare(int api); // gets Mesa ready for that API; returns the path of mesa\opengl32.dll (NULL = none)
int RRGfx_MesaHookSDL(const char *mesa_gl); // redirects SDL2's pixel format/SwapBuffers calls (gdi32) to Mesa; returns how many
// DirectX 11 and Vulkan = OpenGL ES from ANGLE (angle\ folder next to the exe).
int RRGfx_IsAngle(int api);          // 1 = that API comes from ANGLE (DX11, Vulkan)
int RRGfx_AngleAvailable(void);      // 1 = angle\libEGL.dll and angle\libGLESv2.dll exist (64-bit only)
int RRGfx_AnglePrepare(int api);     // DLL folder + environment so SDL loads ANGLE with its D3D11 or Vulkan backend
void RRGfx_VkHideUserSettings(int hide); // hides vkconfig settings while Vulkan starts (1) / restores them (0)
int RRGfx_AngleVulkanAvailable(void); // 1 = the angle folder also has vulkan-1.dll (Chrome's ANGLE): Vulkan through ANGLE
// Live API switch: undo the Mesa / ANGLE setup (after their window and context are destroyed)
void RRGfx_MesaRelease(void);
int RRGfx_MesaLoadedApi(void);       // driver Mesa was started with (-1 = never loaded); a different one needs a restart
void RRGfx_AngleRelease(void);

#ifdef __cplusplus
}
#endif

#endif
