// DR. ROBOTNIK'S RING RACERS - Ring Racers E
// HLSL shaders for the Direct3D 9 backend. Windows only.
#ifndef __SRB2_RHI_D3D9_SHADERS_HPP__
#define __SRB2_RHI_D3D9_SHADERS_HPP__

#ifdef _WIN32

#include <string>

namespace srb2::rhi
{

/// Returns the full HLSL (vs + ps) of program `name` ("unshaded", "unshadedpaletted", "sharpbilinear",
/// "postimg", "postprocesswipe", "crt", "crtsharp"). false if it doesn't exist.
bool d3d9_hlsl_source(const std::string& name, std::string& out_source, std::string& out_vs_entry, std::string& out_extra_define);

} // namespace srb2::rhi

#endif // _WIN32

#endif
