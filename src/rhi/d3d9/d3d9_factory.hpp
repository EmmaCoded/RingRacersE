// DR. ROBOTNIK'S RING RACERS - Ring Racers E
// Direct3D 9 backend factory, without Windows/DirectX headers (the game redefines "boolean" and
// d3d9.h clashes when included after doomtype.h). Windows only.
#ifndef __SRB2_RHI_D3D9_FACTORY_HPP__
#define __SRB2_RHI_D3D9_FACTORY_HPP__

#include <memory>

#include "../rhi.hpp"

namespace srb2::rhi
{

/// Creates the Direct3D 9 backend on window `hwnd` (HWND). With allow_ex it uses Direct3D 9Ex when available
/// (Vista+); without it, classic Direct3D 9 is forced. Throws std::runtime_error on failure.
std::unique_ptr<Rhi> create_d3d9_rhi(void* hwnd, bool vsync, bool allow_ex);

/// Changes vsync if `rhi` is a Direct3D 9 backend. Returns false if it isn't.
bool d3d9_set_vsync(Rhi* rhi, bool vsync);

// --- For a hardware 3D renderer (HWD) sharing the same device ---

/// IDirect3DDevice9* of the active backend (nullptr if none).
void* d3d9_current_device();
/// true if the device is Direct3D 9Ex.
bool d3d9_current_is_ex();
/// Before 3D drawing: begins the scene and binds the backbuffer if no texture pass is active.
void d3d9_prepare_external_draw();
/// Backbuffer size.
void d3d9_backbuffer_size(unsigned* w, unsigned* h);
/// Reset notifications (resize, vsync, device lost): `before` is called BEFORE GPU resources are released
/// and `after` AFTER they are recreated. The 3D renderer must release and rebuild its textures there.
typedef void (*D3d9ResetHook)(void);
void d3d9_set_reset_hooks(D3d9ResetHook before, D3d9ResetHook after);

} // namespace srb2::rhi

#endif
