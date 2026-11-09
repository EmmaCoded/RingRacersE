// rr_gfxapi.cpp - Ring Racers E: graphics API probing and Mesa/ANGLE setup (Windows). See rr_gfxapi.h.
// No game headers (avoids clashes with windows.h/d3d): only CONS_Printf is declared.
// Every graphics DLL is loaded dynamically.
#include <cstdio> // fopen (vkconfig warning)
#include "rr_gfxapi.h"

extern "C" void CONS_Printf(const char *fmt, ...);

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <d3d11.h>
#include <d3d12.h>
#include <string.h>
#include <stdlib.h>
#endif

static int s_probed = 0;
static int s_active = RRGFX_OPENGL;
static int s_det[RRGFX_COUNT];
static int s_dx9ps = 0, s_dx11fl = 0, s_dx12fl = 0;

const char *RRGfx_Name(int api)
{
	switch (api)
	{
		case RRGFX_OPENGL: return "OpenGL";
		case RRGFX_DX9: return "Direct3D 9";
		case RRGFX_DX9EX: return "Direct3DEx 9";
		case RRGFX_DX11: return "DirectX 11";
		case RRGFX_DX12: return "DirectX 12";
		case RRGFX_VULKAN: return "Vulkan";
	}
	return "?";
}

// APIs with a backend in the game.
int RRGfx_Implemented(int api)
{
	if (RRGfx_IsMesa(api))
		return RRGfx_MesaAvailable();
	if (api == RRGFX_VULKAN) // Vulkan = ANGLE with its Vulkan backend (needs angle\vulkan-1.dll)
		return RRGfx_AngleVulkanAvailable();
	if (RRGfx_IsAngle(api))
		return RRGfx_AngleAvailable();
	return api == RRGFX_OPENGL || api == RRGFX_DX9 || api == RRGFX_DX9EX;
}

int RRGfx_IsMesa(int api)
{
	return api == RRGFX_DX12; // Vulkan goes through ANGLE, not Mesa Zink
}

int RRGfx_IsAngle(int api)
{
	return api == RRGFX_DX11 || api == RRGFX_VULKAN;
}

#if defined(_WIN32) && defined(_WIN64)
// mesa\ folder next to the exe (mesa-dist-win has no 32-bit OpenGL)
static int mesa_dir(char *out, int n)
{
	if (!GetModuleFileNameA(NULL, out, n))
		return 0;
	char *b = strrchr(out, '\\');
	if (!b)
		return 0;
	lstrcpynA(b + 1, "mesa", n - (int)(b + 1 - out));
	return 1;
}

static int file_exists(const char *dir, const char *name)
{
	char p[MAX_PATH + 64];
	wsprintfA(p, "%s\\%s", dir, name);
	DWORD a = GetFileAttributesA(p);
	return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

int RRGfx_MesaAvailable(void)
{
	char d[MAX_PATH];
	return mesa_dir(d, MAX_PATH) && file_exists(d, "opengl32.dll") && file_exists(d, "libgallium_wgl.dll");
}

static int s_mesa_driver = -1; // driver Mesa was started with in this process (RRGFX_DX12 / RRGFX_VULKAN)
const char *RRGfx_MesaPrepare(int api)
{
	static char gl[MAX_PATH + 32];
	char d[MAX_PATH];
	if (!RRGfx_IsMesa(api) || !RRGfx_MesaAvailable() || !mesa_dir(d, MAX_PATH))
		return NULL;
	// Mesa's opengl32.dll loads libgallium_wgl.dll and dxil.dll by name: look for them in the mesa folder
	SetDllDirectoryA(d);
	const char *drv = (api == RRGFX_VULKAN) ? "zink" : "d3d12";
	if (s_mesa_driver < 0)
		s_mesa_driver = api;
	SetEnvironmentVariableA("GALLIUM_DRIVER", drv);
	_putenv_s("GALLIUM_DRIVER", drv); // the CRT (ucrt) copy that Mesa reads with getenv
	wsprintfA(gl, "%s\\opengl32.dll", d);
	CONS_Printf("[GFXAPI] Mesa: %s with GALLIUM_DRIVER=%s (%s)\n", RRGfx_Name(api), drv, gl);
	return gl;
}

// SDL2 chooses/sets the pixel format and calls SwapBuffers through gdi32, which always goes to the Windows
// OpenGL (system32), not Mesa's -> "No matching GL pixel format". Those 5 imports of SDL2.dll are patched
// to Mesa's wgl* functions (same arguments). Call after Mesa's opengl32.dll is loaded.
// What was patched in SDL2.dll (to undo it when leaving Mesa live) and the extra reference to Mesa:
static ULONG_PTR *s_hook_slot[8];
static ULONG_PTR s_hook_orig[8];
static int s_hook_n = 0;
static HMODULE s_mesa_ref = NULL;

// Mesa is NEVER unloaded (unloading it leaves D3D12 cleanup threads behind that crash later).
// It picks its driver (d3d12 / zink) once: switching between Mesa drivers needs a restart.
int RRGfx_MesaLoadedApi(void)
{
	return s_mesa_driver;
}

int RRGfx_MesaHookSDL(const char *mesa_gl)
{
	static const char *const names[5][2] = {
		{"ChoosePixelFormat", "wglChoosePixelFormat"}, {"DescribePixelFormat", "wglDescribePixelFormat"},
		{"SetPixelFormat", "wglSetPixelFormat"}, {"GetPixelFormat", "wglGetPixelFormat"}, {"SwapBuffers", "wglSwapBuffers"},
	};
	HMODULE sdl = GetModuleHandleA("SDL2.dll");
	HMODULE gl = LoadLibraryA(mesa_gl); // already loaded: just gets the handle (Mesa's, not system32's)
	if (!sdl || !gl)
		return 0;
	if (s_mesa_ref)
		FreeLibrary(s_mesa_ref); // don't pile up references (the old one and this one are the same DLL)
	s_mesa_ref = gl;
	BYTE *base = (BYTE *)sdl;
	IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
	IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	if (!dir.VirtualAddress)
		return 0;
	int n = 0;
	for (IMAGE_IMPORT_DESCRIPTOR *d = (IMAGE_IMPORT_DESCRIPTOR *)(base + dir.VirtualAddress); d->Name; d++)
	{
		if (lstrcmpiA((const char *)(base + d->Name), "gdi32.dll") != 0 || !d->OriginalFirstThunk)
			continue;
		IMAGE_THUNK_DATA *names_t = (IMAGE_THUNK_DATA *)(base + d->OriginalFirstThunk);
		IMAGE_THUNK_DATA *addr_t = (IMAGE_THUNK_DATA *)(base + d->FirstThunk);
		for (; names_t->u1.AddressOfData; names_t++, addr_t++)
		{
			if (IMAGE_SNAP_BY_ORDINAL(names_t->u1.Ordinal))
				continue;
			const char *fn = (const char *)((IMAGE_IMPORT_BY_NAME *)(base + names_t->u1.AddressOfData))->Name;
			for (int i = 0; i < 5; i++)
			{
				if (strcmp(fn, names[i][0]) != 0)
					continue;
				FARPROC p = GetProcAddress(gl, names[i][1]);
				DWORD old;
				if (p && VirtualProtect(&addr_t->u1.Function, sizeof addr_t->u1.Function, PAGE_READWRITE, &old))
				{
					if (s_hook_n < 8)
					{
						s_hook_slot[s_hook_n] = (ULONG_PTR *)&addr_t->u1.Function;
						s_hook_orig[s_hook_n] = addr_t->u1.Function;
						s_hook_n++;
					}
					addr_t->u1.Function = (ULONG_PTR)p;
					VirtualProtect(&addr_t->u1.Function, sizeof addr_t->u1.Function, old, &old);
					n++;
				}
			}
		}
	}
	CONS_Printf("[GFXAPI] Mesa: %d SDL2 (gdi32) functions redirected to Mesa\n", n);
	return n;
}

// Leave Mesa live (with Mesa's window and context already destroyed and SDL done with the DLL):
// SDL2 goes back to gdi32 and the environment is cleaned up. Mesa itself stays loaded (see above).
void RRGfx_MesaRelease(void)
{
	for (int i = s_hook_n - 1; i >= 0; i--)
	{
		DWORD old;
		if (VirtualProtect(s_hook_slot[i], sizeof(ULONG_PTR), PAGE_READWRITE, &old))
		{
			*s_hook_slot[i] = s_hook_orig[i];
			VirtualProtect(s_hook_slot[i], sizeof(ULONG_PTR), old, &old);
		}
	}
	s_hook_n = 0;
	// s_mesa_ref is kept: Mesa stays loaded (see RRGfx_MesaLoadedApi)
	SetDllDirectoryA(NULL);
	SetEnvironmentVariableA("GALLIUM_DRIVER", NULL);
	_putenv_s("GALLIUM_DRIVER", "");
	CONS_Printf("[GFXAPI] Mesa paused (still loaded with %s)\n", RRGfx_Name(s_mesa_driver));
}

// DirectX 11 = OpenGL ES from ANGLE (libEGL.dll + libGLESv2.dll, angle folder next to the exe).
static int angle_dir(char *out, int n)
{
	if (!mesa_dir(out, n))
		return 0;
	char *b = strrchr(out, '\\');
	lstrcpynA(b + 1, "angle", n - (int)(b + 1 - out));
	return 1;
}

int RRGfx_AngleAvailable(void)
{
	char d[MAX_PATH];
	return angle_dir(d, MAX_PATH) && file_exists(d, "libEGL.dll") && file_exists(d, "libGLESv2.dll");
}

// Chrome's ANGLE has a Vulkan backend; with the system vulkan-1.dll it failed (Internal Vulkan error -3)
int RRGfx_AngleVulkanAvailable(void)
{
	char d[MAX_PATH];
	return RRGfx_AngleAvailable() && angle_dir(d, MAX_PATH) && file_exists(d, "vulkan-1.dll");
}

static void rr_vk_no_validation(void);
void RRGfx_VkHideUserSettings(int hide); // defined next to probe_vulkan
int RRGfx_AnglePrepare(int api)
{
	char d[MAX_PATH], p[MAX_PATH + 32];
	if (!RRGfx_AngleAvailable() || !angle_dir(d, MAX_PATH))
		return 0;
	// libGLESv2.dll loads its DLLs (vulkan-1.dll...) by name: look for them in the angle folder
	rr_vk_no_validation(); // before ANGLE loads vulkan-1.dll
	if (api == RRGFX_VULKAN)
		RRGfx_VkHideUserSettings(1); // released after the context is created (i_video.cpp)
	SetDllDirectoryA(d);
	// DirectX 11 -> ANGLE's d3d11 backend; Vulkan -> its vulkan backend
	const char *plat = (api == RRGFX_VULKAN) ? "vulkan" : "d3d11";
	SetEnvironmentVariableA("ANGLE_DEFAULT_PLATFORM", plat);
	_putenv_s("ANGLE_DEFAULT_PLATFORM", plat);
	wsprintfA(p, "%s\\libEGL.dll", d);
	SetEnvironmentVariableA("SDL_VIDEO_EGL_DRIVER", p);
	_putenv_s("SDL_VIDEO_EGL_DRIVER", p);
	wsprintfA(p, "%s\\libGLESv2.dll", d);
	SetEnvironmentVariableA("SDL_VIDEO_GL_DRIVER", p);
	_putenv_s("SDL_VIDEO_GL_DRIVER", p);
	CONS_Printf("[GFXAPI] ANGLE: %s (%s)\n", RRGfx_Name(api), d);
	return 1;
}

// Leave ANGLE live: clear the environment and the DLL folder
void RRGfx_AngleRelease(void)
{
	RRGfx_VkHideUserSettings(0);
	static const char *const env[] = {"ANGLE_DEFAULT_PLATFORM", "SDL_VIDEO_EGL_DRIVER", "SDL_VIDEO_GL_DRIVER"};
	for (const char *e : env)
	{
		SetEnvironmentVariableA(e, NULL);
		_putenv_s(e, "");
	}
	SetDllDirectoryA(NULL);
}
#else
int RRGfx_AngleAvailable(void) { return 0; }
int RRGfx_AnglePrepare(int) { return 0; }
int RRGfx_AngleVulkanAvailable(void) { return 0; }
void RRGfx_AngleRelease(void) {}
void RRGfx_MesaRelease(void) {}
int RRGfx_MesaLoadedApi(void) { return -1; }
int RRGfx_MesaAvailable(void) { return 0; }
const char *RRGfx_MesaPrepare(int api) { (void)api; return 0; }
int RRGfx_MesaHookSDL(const char *mesa_gl) { (void)mesa_gl; return 0; }
#endif

#ifdef _WIN32
static int probe_dx9(void)
{
	typedef IDirect3D9 *(WINAPI *Create_t)(UINT);
	HMODULE m = LoadLibraryA("d3d9.dll");
	Create_t c = m ? (Create_t)GetProcAddress(m, "Direct3DCreate9") : NULL;
	IDirect3D9 *d3d = c ? c(D3D_SDK_VERSION) : NULL;
	if (!d3d) return 0;
	D3DCAPS9 caps; memset(&caps, 0, sizeof caps);
	int ok = SUCCEEDED(d3d->GetDeviceCaps(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, &caps));
	if (ok) s_dx9ps = (int)D3DSHADER_VERSION_MAJOR(caps.PixelShaderVersion);
	d3d->Release();
	return ok && s_dx9ps >= 2;
}

// 9Ex: extended runtime from Vista on (no "device lost" when switching windows, PresentEx). Not on XP.
static int probe_dx9ex(void)
{
	typedef HRESULT (WINAPI *CreateEx_t)(UINT, IDirect3D9Ex **);
	HMODULE m = LoadLibraryA("d3d9.dll");
	CreateEx_t c = m ? (CreateEx_t)GetProcAddress(m, "Direct3DCreate9Ex") : NULL;
	IDirect3D9Ex *d3d = NULL;
	if (!c || FAILED(c(D3D_SDK_VERSION, &d3d)) || !d3d) return 0;
	D3DCAPS9 caps; memset(&caps, 0, sizeof caps);
	int ok = SUCCEEDED(d3d->GetDeviceCaps(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, &caps)) && D3DSHADER_VERSION_MAJOR(caps.PixelShaderVersion) >= 2;
	d3d->Release();
	return ok;
}

static int probe_dx11(void)
{
	HMODULE m = LoadLibraryA("d3d11.dll");
	PFN_D3D11_CREATE_DEVICE cd = m ? (PFN_D3D11_CREATE_DEVICE)GetProcAddress(m, "D3D11CreateDevice") : NULL;
	if (!cd) return 0;
	static const D3D_FEATURE_LEVEL lv[] = { D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_11_1,
		D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
	ID3D11Device *dev = NULL; D3D_FEATURE_LEVEL got = D3D_FEATURE_LEVEL_10_0;
	HRESULT hr = cd(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, lv, 6, D3D11_SDK_VERSION, &dev, &got, NULL);
	if (hr == E_INVALIDARG) // Windows 7 without 11.1/12
		hr = cd(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, lv + 3, 3, D3D11_SDK_VERSION, &dev, &got, NULL);
	if (FAILED(hr) || !dev) return 0;
	s_dx11fl = (int)got;
	dev->Release();
	return 1;
}

static int probe_dx12(void)
{
	static const GUID IID_Dev12 = {0x189819f1, 0x1db6, 0x4b57, {0xbe, 0x54, 0x18, 0x21, 0x33, 0x9b, 0x85, 0xf7}};
	HMODULE m = LoadLibraryA("d3d12.dll");
	PFN_D3D12_CREATE_DEVICE cd = m ? (PFN_D3D12_CREATE_DEVICE)GetProcAddress(m, "D3D12CreateDevice") : NULL;
	if (!cd) return 0;
	ID3D12Device *dev = NULL;
	if (SUCCEEDED(cd(NULL, D3D_FEATURE_LEVEL_11_0, IID_Dev12, (void **)&dev)) && dev)
	{
		s_dx12fl = (int)D3D_FEATURE_LEVEL_11_0;
		dev->Release();
		return 1;
	}
	return 0;
}

struct VkAppInfo { int sType; const void *pNext; const char *pAppName; unsigned appVer; const char *pEngName; unsigned engVer; unsigned apiVersion; };
struct VkInstCI { int sType; const void *pNext; unsigned flags; const VkAppInfo *pApp; unsigned nL; const char *const *ppL; unsigned nE; const char *const *ppE; };
// Ring Racers E: the Vulkan VALIDATION layer (a development tool, VERY slow) can be forced for EVERY program by
// Vulkan Configurator (vkconfig: %LOCALAPPDATA%\\LunarG\\vulkan\\vk_loader_settings.json with
// VK_LAYER_KHRONOS_validation "on"). On a test PC it dropped Vulkan from 60 to ~37 fps. Only that layer is disabled
// (RivaTuner, OBS... still work) before Vulkan is touched. -vkvalidation leaves it alone.
extern "C" int M_CheckParm(const char *check);
// Ring Racers E: Vulkan Configurator (vkconfig) can FORCE the validation layer for every program through a loader
// settings file (HKCU\\SOFTWARE\\Khronos\\Vulkan\\LoaderSettings -> vk_loader_settings.json with
// VK_LAYER_KHRONOS_validation "control": "on"). That file wins over VK_LOADER_LAYERS_DISABLE, and the loader ignores it
// for elevated processes. The game can only warn: Vulkan (ANGLE) gets much slower. Fix it in vkconfig.
static int rr_vk_warn_forced_validation(void)
{
	static int printed = 0;
	int forced = 0;
	HKEY key;
	if (RegOpenKeyExA(HKEY_CURRENT_USER, "SOFTWARE\\Khronos\\Vulkan\\LoaderSettings", 0, KEY_READ, &key) != ERROR_SUCCESS)
		return 0;
	char name[MAX_PATH];
	for (DWORD i = 0;; i++)
	{
		DWORD len = sizeof name;
		if (RegEnumValueA(key, i, name, &len, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
			break;
		FILE *f = fopen(name, "rb");
		if (!f)
			continue;
		static char buf[65536];
		const size_t n = fread(buf, 1, sizeof buf - 1, f);
		fclose(f);
		buf[n] = 0;
		// the layer JSON object: between the previous "{" and the next "}"; "control" may come before or after the name
		const char *p = strstr(buf, "\"VK_LAYER_KHRONOS_validation\"");
		if (!p)
			continue;
		const char *start = p;
		while (start > buf && *start != '{')
			start--;
		const char *end = strchr(p, '}');
		const char *on = strstr(start, "\"on\"");
		if (on && end && on < end)
		{
			forced = 1;
			if (!printed++)
				CONS_Printf("[GFXAPI] WARNING: Vulkan Configurator (vkconfig) forces the Vulkan VALIDATION layer in %s: Vulkan/ANGLE is much slower. Set it to auto/off in vkconfig.\n", name);
		}
	}
	RegCloseKey(key);
	return forced;
}

static void rr_vk_no_validation(void)
{
	static int done = 0;
	if (done)
		return;
	done = 1;
	rr_vk_warn_forced_validation();
	if (M_CheckParm("-vkvalidation"))
		return;
	const char *layer = "VK_LAYER_KHRONOS_validation";
	char cur[1024] = "";
	const DWORD n = GetEnvironmentVariableA("VK_LOADER_LAYERS_DISABLE", cur, sizeof cur);
	if (n >= sizeof cur || strstr(cur, "~all~") || strstr(cur, layer))
		return; // already disabled (or the variable is too long: left alone)
	char val[1100];
	if (cur[0])
		wsprintfA(val, "%s,%s", cur, layer);
	else
		lstrcpyA(val, layer);
	SetEnvironmentVariableA("VK_LOADER_LAYERS_DISABLE", val);
	_putenv_s("VK_LOADER_LAYERS_DISABLE", val);
	CONS_Printf("[GFXAPI] Vulkan: validation layer disabled (VK_LOADER_LAYERS_DISABLE=%s)\n", val);
}

// Ring Racers E: the game protects itself (players cannot be asked to fix vkconfig). While ANGLE (or the API probe)
// creates the Vulkan instance, HKEY_CURRENT_USER of THIS process points to HKCU\\Software\\Classes
// (RegOverridePredefKey): the loader does not find SOFTWARE\\Khronos\\Vulkan\\LoaderSettings and does not force the
// validation layer. Nothing is written to the registry. RivaTuner/OBS/Steam layers (HKLM) still load. Only when
// vkconfig forces validation and without -vkvalidation. hide=0 restores it.
static HKEY s_vk_hkcu = NULL;
void RRGfx_VkHideUserSettings(int hide)
{
	if (hide)
	{
		if (s_vk_hkcu || M_CheckParm("-vkvalidation") || !rr_vk_warn_forced_validation())
			return;
		HKEY k;
		if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Classes", 0, KEY_READ, &k) != ERROR_SUCCESS)
			return;
		if (RegOverridePredefKey(HKEY_CURRENT_USER, k) == ERROR_SUCCESS)
		{
			s_vk_hkcu = k;
			CONS_Printf("[GFXAPI] Vulkan: vkconfig settings hidden while Vulkan starts (no validation layer)\n");
		}
		else
			RegCloseKey(k);
	}
	else if (s_vk_hkcu)
	{
		RegOverridePredefKey(HKEY_CURRENT_USER, NULL);
		RegCloseKey(s_vk_hkcu);
		s_vk_hkcu = NULL;
	}
}

static int probe_vulkan_impl(void);
static int probe_vulkan(void)
{
	rr_vk_no_validation();
	RRGfx_VkHideUserSettings(1);
	const int r = probe_vulkan_impl();
	RRGfx_VkHideUserSettings(0);
	return r;
}
static int probe_vulkan_impl(void)
{
	typedef int (__stdcall *vkCI_t)(const VkInstCI *, const void *, void **);
	typedef void *(__stdcall *vkGIPA_t)(void *, const char *);
	typedef int (__stdcall *vkEPD_t)(void *, unsigned *, void **);
	typedef void (__stdcall *vkDI_t)(void *, const void *);
	HMODULE m = LoadLibraryA("vulkan-1.dll");
	if (!m) return 0;
	vkGIPA_t gipa = (vkGIPA_t)GetProcAddress(m, "vkGetInstanceProcAddr");
	vkCI_t ci = (vkCI_t)GetProcAddress(m, "vkCreateInstance");
	if (!gipa || !ci) return 0;
	VkAppInfo app = { 0, NULL, "ringracers", 1, "rr", 1, (1u << 22) };
	VkInstCI ici = { 1, NULL, 0, &app, 0, NULL, 0, NULL };
	void *inst = NULL;
	if (ci(&ici, NULL, &inst) != 0 || !inst) return 0;
	vkEPD_t epd = (vkEPD_t)gipa(inst, "vkEnumeratePhysicalDevices");
	unsigned n = 0;
	int ok = epd && epd(inst, &n, NULL) == 0 && n > 0;
	vkDI_t di = (vkDI_t)gipa(inst, "vkDestroyInstance");
	if (di) di(inst, NULL);
	return ok;
}
#endif

static void probe_all(void)
{
	s_probed = 1;
	s_det[RRGFX_OPENGL] = 1; // the game already knows whether OpenGL loaded (vid.glstate); assumed here
#ifdef _WIN32
	s_det[RRGFX_DX9] = probe_dx9();
	s_det[RRGFX_DX9EX] = probe_dx9ex();
	s_det[RRGFX_DX11] = probe_dx11();
	s_det[RRGFX_DX12] = probe_dx12();
	s_det[RRGFX_VULKAN] = probe_vulkan();
#endif
	CONS_Printf("[GFXAPI] detected: OpenGL=%d DX9=%d (PS %d) DX9Ex=%d DX11=%d (FL 0x%X) DX12=%d Vulkan=%d | with backend: OpenGL, DX9, DX9Ex%s%s\n",
		s_det[RRGFX_OPENGL], s_det[RRGFX_DX9], s_dx9ps, s_det[RRGFX_DX9EX], s_det[RRGFX_DX11], s_dx11fl, s_det[RRGFX_DX12], s_det[RRGFX_VULKAN], RRGfx_MesaAvailable() ? ", DX12 (Mesa)" : " (no mesa folder)",
		RRGfx_AngleVulkanAvailable() ? ", DX11+Vulkan (ANGLE)" : RRGfx_AngleAvailable() ? ", DX11 (ANGLE, no vulkan-1.dll)" : " (no angle folder)");
}

int RRGfx_Active(void)
{
	return s_active;
}

void RRGfx_SetActive(int api)
{
	s_active = api;
}

// Restarts the game: starts another copy with the same command line (the caller closes this one).
void RRGfx_SpawnSelf(void)
{
#ifdef _WIN32
	char exe[MAX_PATH] = {0};
	if (!GetModuleFileNameA(NULL, exe, MAX_PATH))
		return;
	char cmd[4096];
	lstrcpynA(cmd, GetCommandLineA(), (int)sizeof cmd);
	// The DLL folder and GALLIUM_DRIVER are inherited: without this the new copy would load Mesa's opengl32.dll
	// even when asking for regular OpenGL (and fail). If the new copy wants Mesa, it sets them again.
	SetDllDirectoryA(NULL);
	SetEnvironmentVariableA("GALLIUM_DRIVER", NULL);
	_putenv_s("GALLIUM_DRIVER", "");
	static const char *const angle_env[] = {"ANGLE_DEFAULT_PLATFORM", "SDL_VIDEO_EGL_DRIVER", "SDL_VIDEO_GL_DRIVER"};
	for (const char *e : angle_env)
	{
		SetEnvironmentVariableA(e, NULL);
		_putenv_s(e, "");
	}
	STARTUPINFOA si;
	PROCESS_INFORMATION pi;
	memset(&si, 0, sizeof si);
	memset(&pi, 0, sizeof pi);
	si.cb = sizeof si;
	if (CreateProcessA(exe, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
	{
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
	}
#endif
}

int RRGfx_Detected(int api)
{
	if (!s_probed) probe_all();
	return (api >= 0 && api < RRGFX_COUNT) ? s_det[api] : 0;
}
