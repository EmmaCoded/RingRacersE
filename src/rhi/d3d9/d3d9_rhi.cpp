// DR. ROBOTNIK'S RING RACERS - Ring Racers E
//-----------------------------------------------------------------------------
// Direct3D 9 / 9Ex RHI backend. See d3d9_rhi.hpp.
//
// GL -> D3D9 translation notes:
//  * GL coordinates (bottom-left origin) are converted in to_d3d_rect() for the backbuffer.
//    Textures are treated as in GL: row 0 is the first row uploaded. When drawing to a
//    texture, Y is flipped in the vertex shader (u_d3d.z = -1) so they keep matching.
//  * Every RGB/RGBA texture is a render target (D3DPOOL_DEFAULT); each texture has a
//    system-memory surface (stage) that data is uploaded from with UpdateSurface.
//    That copy also allows recreating everything after a Reset.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------

#ifdef _WIN32

#include "d3d9_rhi.hpp"
#include "d3d9_factory.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>
#include <type_traits>

#include <d3dcommon.h>

#include "../../cxxutil.hpp"
#include "d3d9_shaders.hpp"

extern "C" void CONS_Printf(const char* fmt, ...);

// Slab's iterator only yields const references; the resources need to be modified here.
#define D3D9_SLAB_EACH(var, slab) \
	for (auto var##_it = (slab).cbegin(); var##_it != (slab).cend(); ++var##_it) \
		if (auto& var = const_cast<std::remove_const_t<std::remove_reference_t<decltype(*var##_it)>>&>(*var##_it); true)


namespace srb2::rhi
{

namespace
{

// ------------------------------------------------------------------ HLSL compiler (loaded dynamically)
typedef HRESULT(WINAPI* PFN_D3DCOMPILE_)(
	LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**
);

extern "C" const unsigned char* rr_d3dc_embedded(size_t* size); // d3dcompiler_embed.cpp

PFN_D3DCOMPILE_ get_d3dcompile()
{
	static PFN_D3DCOMPILE_ fn = nullptr;
	static bool tried = false;
	if (!tried)
	{
		tried = true;
		static const char* kDlls[] = {"d3dcompiler_47.dll", "d3dcompiler_46.dll", "d3dcompiler_43.dll"};
		for (const char* dll : kDlls)
		{
			HMODULE m = LoadLibraryA(dll);
			if (m)
			{
				fn = reinterpret_cast<PFN_D3DCOMPILE_>(GetProcAddress(m, "D3DCompile"));
				if (fn)
				{
					CONS_Printf("[D3D9] HLSL compiler: %s\n", dll);
					break;
				}
			}
		}
		if (!fn)
		{
			// Windows without the compiler (7): write the copy embedded in the .exe to the temp folder and load it from there.
			size_t n = 0;
			const unsigned char* data = rr_d3dc_embedded(&n);
			char path[MAX_PATH + 64];
			const DWORD len = (data && n) ? GetTempPathA(MAX_PATH, path) : 0;
			if (len > 0 && len < MAX_PATH)
			{
				snprintf(path + len, 64, "rr_d3dcompiler_47_%u.dll", static_cast<unsigned>(n));
				bool ok = false;
				HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
				if (h != INVALID_HANDLE_VALUE)
				{
					ok = (GetFileSize(h, NULL) == static_cast<DWORD>(n)); // already written earlier
					CloseHandle(h);
				}
				if (!ok)
				{
					FILE* f = fopen(path, "wb");
					if (f)
					{
						ok = (fwrite(data, 1, n, f) == n);
						fclose(f);
					}
				}
				HMODULE m = ok ? LoadLibraryA(path) : NULL;
				if (m)
				{
					fn = reinterpret_cast<PFN_D3DCOMPILE_>(GetProcAddress(m, "D3DCompile"));
					if (fn)
						CONS_Printf("[D3D9] HLSL compiler: embedded in the exe (%s)\n", path);
				}
			}
		}
	}
	return fn;
}

// ------------------------------------------------------------------ uniforms -> constant registers
struct UniformSlot
{
	const char* name;
	int reg;
	int count; // registers used
};

constexpr UniformSlot kUniforms[] = {
	{"u_projection", 0, 4},
	{"u_modelview", 4, 4},
	{"u_texcoord0_transform", 8, 3},
	{"u_texcoord0_min", 11, 1},
	{"u_texcoord0_max", 12, 1},
	{"u_sampler0_size", 13, 1},
	{"u_time", 14, 1},
	{"u_sampler0_is_indexed_alpha", 15, 1},
	{"u_wipe_colorize_mode", 16, 1},
	{"u_wipe_encore_swizzle", 17, 1},
	{"u_postimg_water", 18, 1},
	{"u_postimg_heat", 19, 1},
};
constexpr int kD3dReg = 20; // u_d3d: 1/width, 1/height, Y flip

const UniformSlot* find_uniform(const char* name)
{
	for (const auto& u : kUniforms)
	{
		if (std::strcmp(u.name, name) == 0)
			return &u;
	}
	return nullptr;
}

// ------------------------------------------------------------------ enum mappings
D3DFORMAT d3d_format(TextureFormat f)
{
	switch (f)
	{
	case TextureFormat::kLuminance:
		return D3DFMT_L8;
	case TextureFormat::kLuminanceAlpha:
		return D3DFMT_A8L8;
	case TextureFormat::kRGB:
		return D3DFMT_X8R8G8B8;
	case TextureFormat::kRGBA:
		return D3DFMT_A8R8G8B8;
	}
	return D3DFMT_UNKNOWN;
}

bool is_render_target_format(TextureFormat f)
{
	return f == TextureFormat::kRGB || f == TextureFormat::kRGBA;
}

uint32_t pixel_format_size(PixelFormat f)
{
	switch (f)
	{
	case PixelFormat::kR8:
		return 1;
	case PixelFormat::kRG8:
		return 2;
	case PixelFormat::kRGB8:
		return 3;
	case PixelFormat::kRGBA8:
		return 4;
	case PixelFormat::kDepth16:
		return 2;
	case PixelFormat::kStencil8:
		return 1;
	}
	return 0;
}

TextureFormat texture_format_for_pixels(PixelFormat f)
{
	switch (f)
	{
	case PixelFormat::kR8:
		return TextureFormat::kLuminance;
	case PixelFormat::kRG8:
		return TextureFormat::kLuminanceAlpha;
	case PixelFormat::kRGB8:
		return TextureFormat::kRGB;
	default:
		return TextureFormat::kRGBA;
	}
}

D3DBLEND d3d_blend(BlendFactor f)
{
	switch (f)
	{
	case BlendFactor::kZero:
		return D3DBLEND_ZERO;
	case BlendFactor::kOne:
		return D3DBLEND_ONE;
	case BlendFactor::kSource:
		return D3DBLEND_SRCCOLOR;
	case BlendFactor::kOneMinusSource:
		return D3DBLEND_INVSRCCOLOR;
	case BlendFactor::kSourceAlpha:
		return D3DBLEND_SRCALPHA;
	case BlendFactor::kOneMinusSourceAlpha:
		return D3DBLEND_INVSRCALPHA;
	case BlendFactor::kDest:
		return D3DBLEND_DESTCOLOR;
	case BlendFactor::kOneMinusDest:
		return D3DBLEND_INVDESTCOLOR;
	case BlendFactor::kDestAlpha:
		return D3DBLEND_DESTALPHA;
	case BlendFactor::kOneMinusDestAlpha:
		return D3DBLEND_INVDESTALPHA;
	case BlendFactor::kConstant:
	case BlendFactor::kConstantAlpha:
		return D3DBLEND_BLENDFACTOR;
	case BlendFactor::kOneMinusConstant:
	case BlendFactor::kOneMinusConstantAlpha:
		return D3DBLEND_INVBLENDFACTOR;
	case BlendFactor::kSourceAlphaSaturated:
		return D3DBLEND_SRCALPHASAT;
	}
	return D3DBLEND_ONE;
}

D3DBLENDOP d3d_blend_op(BlendFunction f)
{
	switch (f)
	{
	case BlendFunction::kAdd:
		return D3DBLENDOP_ADD;
	case BlendFunction::kSubtract:
		return D3DBLENDOP_SUBTRACT;
	case BlendFunction::kReverseSubtract:
		return D3DBLENDOP_REVSUBTRACT;
	}
	return D3DBLENDOP_ADD;
}

D3DCMPFUNC d3d_cmp(CompareFunc f)
{
	switch (f)
	{
	case CompareFunc::kNever:
		return D3DCMP_NEVER;
	case CompareFunc::kLess:
		return D3DCMP_LESS;
	case CompareFunc::kEqual:
		return D3DCMP_EQUAL;
	case CompareFunc::kLessEqual:
		return D3DCMP_LESSEQUAL;
	case CompareFunc::kGreater:
		return D3DCMP_GREATER;
	case CompareFunc::kNotEqual:
		return D3DCMP_NOTEQUAL;
	case CompareFunc::kGreaterEqual:
		return D3DCMP_GREATEREQUAL;
	case CompareFunc::kAlways:
		return D3DCMP_ALWAYS;
	}
	return D3DCMP_ALWAYS;
}

D3DSTENCILOP d3d_stencil_op(StencilOp o)
{
	switch (o)
	{
	case StencilOp::kKeep:
		return D3DSTENCILOP_KEEP;
	case StencilOp::kZero:
		return D3DSTENCILOP_ZERO;
	case StencilOp::kReplace:
		return D3DSTENCILOP_REPLACE;
	case StencilOp::kIncrementClamp:
		return D3DSTENCILOP_INCRSAT;
	case StencilOp::kDecrementClamp:
		return D3DSTENCILOP_DECRSAT;
	case StencilOp::kInvert:
		return D3DSTENCILOP_INVERT;
	case StencilOp::kIncrementWrap:
		return D3DSTENCILOP_INCR;
	case StencilOp::kDecrementWrap:
		return D3DSTENCILOP_DECR;
	}
	return D3DSTENCILOP_KEEP;
}

D3DTEXTUREADDRESS d3d_wrap(TextureWrapMode m)
{
	switch (m)
	{
	case TextureWrapMode::kRepeat:
		return D3DTADDRESS_WRAP;
	case TextureWrapMode::kMirroredRepeat:
		return D3DTADDRESS_MIRROR;
	case TextureWrapMode::kClamp:
		return D3DTADDRESS_CLAMP;
	}
	return D3DTADDRESS_CLAMP;
}

D3DTEXTUREFILTERTYPE d3d_filter(TextureFilterMode m)
{
	return m == TextureFilterMode::kNearest ? D3DTEXF_POINT : D3DTEXF_LINEAR;
}

D3DPRIMITIVETYPE d3d_primitive(PrimitiveType t)
{
	switch (t)
	{
	case PrimitiveType::kPoints:
		return D3DPT_POINTLIST;
	case PrimitiveType::kLines:
		return D3DPT_LINELIST;
	case PrimitiveType::kLineStrip:
		return D3DPT_LINESTRIP;
	case PrimitiveType::kTriangles:
		return D3DPT_TRIANGLELIST;
	case PrimitiveType::kTriangleStrip:
		return D3DPT_TRIANGLESTRIP;
	case PrimitiveType::kTriangleFan:
		return D3DPT_TRIANGLEFAN;
	}
	return D3DPT_TRIANGLELIST;
}

UINT primitive_count(PrimitiveType t, uint32_t n)
{
	switch (t)
	{
	case PrimitiveType::kPoints:
		return n;
	case PrimitiveType::kLines:
		return n / 2;
	case PrimitiveType::kLineStrip:
		return n >= 1 ? n - 1 : 0;
	case PrimitiveType::kTriangles:
		return n / 3;
	case PrimitiveType::kTriangleStrip:
	case PrimitiveType::kTriangleFan:
		return n >= 2 ? n - 2 : 0;
	}
	return 0;
}

D3DDECLTYPE d3d_decl_type(VertexAttributeFormat f)
{
	switch (f)
	{
	case VertexAttributeFormat::kFloat:
		return D3DDECLTYPE_FLOAT1;
	case VertexAttributeFormat::kFloat2:
		return D3DDECLTYPE_FLOAT2;
	case VertexAttributeFormat::kFloat3:
		return D3DDECLTYPE_FLOAT3;
	case VertexAttributeFormat::kFloat4:
		return D3DDECLTYPE_FLOAT4;
	}
	return D3DDECLTYPE_FLOAT4;
}

bool decl_usage_for(const std::string& name, BYTE& usage, BYTE& index)
{
	if (name == "a_position")
	{
		usage = D3DDECLUSAGE_POSITION;
		index = 0;
		return true;
	}
	if (name == "a_normal")
	{
		usage = D3DDECLUSAGE_NORMAL;
		index = 0;
		return true;
	}
	if (name == "a_texcoord0")
	{
		usage = D3DDECLUSAGE_TEXCOORD;
		index = 0;
		return true;
	}
	if (name == "a_texcoord1")
	{
		usage = D3DDECLUSAGE_TEXCOORD;
		index = 1;
		return true;
	}
	if (name == "a_color")
	{
		usage = D3DDECLUSAGE_COLOR;
		index = 0;
		return true;
	}
	return false;
}

template <typename T>
void safe_release(T*& p)
{
	if (p)
	{
		p->Release();
		p = nullptr;
	}
}

} // namespace

// ================================================================== device
namespace
{
D3d9Rhi* g_instance = nullptr;
D3d9ResetHook g_hook_before = nullptr;
D3d9ResetHook g_hook_after = nullptr;
} // namespace

D3d9Rhi::D3d9Rhi(HWND hwnd, bool vsync, bool allow_ex) : hwnd_(hwnd), vsync_(vsync), allow_ex_(allow_ex)
{
	// No HLSL compiler means no shaders: fail HERE (before creating the device) so the game falls back to
	// OpenGL, instead of breaking later in create_program (Windows 7 doesn't ship d3dcompiler_47.dll).
	if (!get_d3dcompile())
		throw std::runtime_error("D3D9: d3dcompiler_47.dll not found (put it next to the .exe)");
	create_device();
	g_instance = this;
}

D3d9Rhi::~D3d9Rhi()
{
	if (g_instance == this)
		g_instance = nullptr;
	for (auto& decl : decl_cache_)
		safe_release(decl.second);
	D3D9_SLAB_EACH(t, texture_slab_)
	{
		safe_release(t.gpu);
		safe_release(t.stage);
		safe_release(t.stage_tex);
	}
	D3D9_SLAB_EACH(b, buffer_slab_)
	{
		safe_release(b.vb);
		safe_release(b.ib);
	}
	D3D9_SLAB_EACH(r, renderbuffer_slab_)
		safe_release(r.surface);
	D3D9_SLAB_EACH(p, program_slab_)
	{
		safe_release(p.vs);
		safe_release(p.ps);
	}
	safe_release(auto_ds_);
	safe_release(dev_);
	safe_release(d3d_);
}

void D3d9Rhi::sync_backbuffer_size()
{
	RECT rc = {};
	GetClientRect(hwnd_, &rc);
	back_w_ = static_cast<uint32_t>(std::max<LONG>(rc.right - rc.left, 1));
	back_h_ = static_cast<uint32_t>(std::max<LONG>(rc.bottom - rc.top, 1));
}

void D3d9Rhi::create_device()
{
	// Direct3D 9Ex (Vista+) when available; otherwise classic Direct3D 9 (XP).
	typedef HRESULT(WINAPI * Create9Ex_t)(UINT, IDirect3D9Ex**);
	HMODULE m = LoadLibraryA("d3d9.dll");
	if (!m)
		throw std::runtime_error("D3D9: d3d9.dll not found");
	Create9Ex_t create_ex = reinterpret_cast<Create9Ex_t>(GetProcAddress(m, "Direct3DCreate9Ex"));
	IDirect3D9Ex* d3dex = nullptr;
	if (allow_ex_ && create_ex && SUCCEEDED(create_ex(D3D_SDK_VERSION, &d3dex)) && d3dex)
	{
		d3d_ = d3dex;
		ex_ = true;
	}
	else
	{
		typedef IDirect3D9*(WINAPI * Create9_t)(UINT);
		Create9_t create = reinterpret_cast<Create9_t>(GetProcAddress(m, "Direct3DCreate9"));
		d3d_ = create ? create(D3D_SDK_VERSION) : nullptr;
		ex_ = false;
	}
	if (!d3d_)
		throw std::runtime_error("D3D9: could not create IDirect3D9");

	sync_backbuffer_size();

	std::memset(&pp_, 0, sizeof(pp_));
	pp_.Windowed = TRUE;
	pp_.BackBufferWidth = back_w_;
	pp_.BackBufferHeight = back_h_;
	pp_.BackBufferFormat = D3DFMT_X8R8G8B8;
	pp_.BackBufferCount = 1;
	pp_.SwapEffect = D3DSWAPEFFECT_DISCARD;
	pp_.hDeviceWindow = hwnd_;
	pp_.EnableAutoDepthStencil = TRUE;
	pp_.AutoDepthStencilFormat = D3DFMT_D24S8;
	pp_.PresentationInterval = vsync_ ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_IMMEDIATE;

	static const DWORD kBehaviors[] = {
		D3DCREATE_HARDWARE_VERTEXPROCESSING,
		D3DCREATE_MIXED_VERTEXPROCESSING,
		D3DCREATE_SOFTWARE_VERTEXPROCESSING};
	HRESULT hr = E_FAIL;
	for (DWORD beh : kBehaviors)
	{
		if (ex_)
		{
			IDirect3DDevice9Ex* dex = nullptr;
			hr = static_cast<IDirect3D9Ex*>(d3d_)->CreateDeviceEx(
				D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd_, beh | D3DCREATE_FPU_PRESERVE, &pp_, nullptr, &dex
			);
			if (SUCCEEDED(hr))
				dev_ = dex;
		}
		else
		{
			hr = d3d_->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd_, beh | D3DCREATE_FPU_PRESERVE, &pp_, &dev_);
		}
		if (SUCCEEDED(hr) && dev_)
			break;
	}
	if (FAILED(hr) || !dev_)
	{
		char msg[160];
		snprintf(msg, sizeof msg, "D3D9: could not create the device (CreateDevice failed, hr=0x%08lX, %ux%u, ex=%d)",
			static_cast<unsigned long>(hr), static_cast<unsigned>(back_w_), static_cast<unsigned>(back_h_), ex_ ? 1 : 0);
		throw std::runtime_error(msg);
	}

	D3DCAPS9 caps = {};
	dev_->GetDeviceCaps(&caps);
	if (D3DSHADER_VERSION_MAJOR(caps.PixelShaderVersion) < 3 || D3DSHADER_VERSION_MAJOR(caps.VertexShaderVersion) < 3)
	{
		safe_release(dev_);
		throw std::runtime_error("D3D9: the GPU doesn't support shader model 3.0");
	}

	dev_->GetDepthStencilSurface(&auto_ds_);
	CONS_Printf(
		"[D3D9] device created: %s | backbuffer %ux%u | vsync %d | PS %d.%d\n",
		ex_ ? "Direct3D 9Ex" : "Direct3D 9 classic",
		back_w_,
		back_h_,
		vsync_ ? 1 : 0,
		static_cast<int>(D3DSHADER_VERSION_MAJOR(caps.PixelShaderVersion)),
		static_cast<int>(D3DSHADER_VERSION_MINOR(caps.PixelShaderVersion))
	);
	init_state();
}

void D3d9Rhi::init_state()
{
	dev_->SetRenderState(D3DRS_LIGHTING, FALSE);
	dev_->SetRenderState(D3DRS_FOGENABLE, FALSE);
	dev_->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
	dev_->SetRenderState(D3DRS_SPECULARENABLE, FALSE);
	dev_->SetRenderState(D3DRS_CLIPPING, TRUE);
	raster_valid_ = false;
}

void D3d9Rhi::set_vsync(bool vsync)
{
	if (vsync_ == vsync)
		return;
	vsync_ = vsync;
	pp_.PresentationInterval = vsync_ ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_IMMEDIATE;
	// No Reset here: this can arrive mid-frame (cvar) with render passes open, which would empty their stack.
	reset_pending_ = true;
}

// ------------------------------------------------------------------ resource loss / recreation
void D3d9Rhi::release_all_gpu()
{
	safe_release(auto_ds_);
	D3D9_SLAB_EACH(t, texture_slab_)
		safe_release(t.gpu);
	D3D9_SLAB_EACH(b, buffer_slab_)
	{
		safe_release(b.vb);
		safe_release(b.ib);
	}
	D3D9_SLAB_EACH(r, renderbuffer_slab_)
		safe_release(r.surface);
}

bool D3d9Rhi::create_texture_gpu(D3d9Texture& t)
{
	HRESULT hr = dev_->CreateTexture(
		t.desc.width,
		t.desc.height,
		1,
		t.render_target ? D3DUSAGE_RENDERTARGET : 0,
		t.format,
		D3DPOOL_DEFAULT,
		&t.gpu,
		nullptr
	);
	return SUCCEEDED(hr) && t.gpu;
}

void D3d9Rhi::restore_all_gpu()
{
	dev_->GetDepthStencilSurface(&auto_ds_);

	D3D9_SLAB_EACH(t, texture_slab_)
	{
		if (t.desc.width == 0 || t.desc.height == 0)
			continue; // empty slab slot
		if (!create_texture_gpu(t))
		{
			// Don't crash the game: that texture draws empty until the next Reset
			CONS_Printf("[D3D9] warning: could not recreate a %ux%u texture after Reset\n", (unsigned)t.desc.width, (unsigned)t.desc.height);
			continue;
		}
		IDirect3DSurface9* dst = nullptr;
		if (SUCCEEDED(t.gpu->GetSurfaceLevel(0, &dst)))
		{
			dev_->UpdateSurface(t.stage, nullptr, dst, nullptr);
			dst->Release();
		}
	}
	D3D9_SLAB_EACH(b, buffer_slab_)
	{
		const bool is_vertex = (b.desc.type == BufferType::kVertexBuffer);
		HRESULT hr = is_vertex ? dev_->CreateVertexBuffer(
							 b.desc.size, D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT, &b.vb, nullptr
						 )
							   : dev_->CreateIndexBuffer(
									 b.desc.size,
									 D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY,
									 D3DFMT_INDEX16,
									 D3DPOOL_DEFAULT,
									 &b.ib,
									 nullptr
								 );
		if (FAILED(hr))
		{
			CONS_Printf("[D3D9] warning: could not recreate a buffer after Reset\n");
			continue;
		}
		void* ptr = nullptr;
		if (is_vertex ? SUCCEEDED(b.vb->Lock(0, 0, &ptr, D3DLOCK_DISCARD)) : SUCCEEDED(b.ib->Lock(0, 0, &ptr, D3DLOCK_DISCARD)))
		{
			std::memcpy(ptr, b.shadow.data(), b.shadow.size());
			if (is_vertex)
				b.vb->Unlock();
			else
				b.ib->Unlock();
		}
	}
	D3D9_SLAB_EACH(r, renderbuffer_slab_)
	{
		if (FAILED(dev_->CreateDepthStencilSurface(
				r.desc.width, r.desc.height, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, TRUE, &r.surface, nullptr
			)))
		{
			CONS_Printf("[D3D9] warning: could not recreate a renderbuffer after Reset\n");
			continue;
		}
	}
}

void D3d9Rhi::reset_device()
{
	end_scene();
	if (g_hook_before)
		g_hook_before();
	// Unbind everything from the device: otherwise Reset can't release those textures/surfaces
	for (DWORD i = 0; i < 4; i++)
		dev_->SetTexture(i, nullptr);
	for (UINT i = 0; i < 4; i++)
		dev_->SetStreamSource(i, nullptr, 0, 0);
	dev_->SetIndices(nullptr);
	{
		IDirect3DSurface9* bb = nullptr;
		if (SUCCEEDED(dev_->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)))
		{
			dev_->SetRenderTarget(0, bb);
			bb->Release();
		}
		dev_->SetDepthStencilSurface(auto_ds_);
	}
	release_all_gpu();
	render_pass_stack_.clear();
	current_program_ = std::nullopt;

	sync_backbuffer_size();
	pp_.BackBufferWidth = back_w_;
	pp_.BackBufferHeight = back_h_;

	HRESULT hr;
	if (ex_)
		hr = static_cast<IDirect3DDevice9Ex*>(dev_)->ResetEx(&pp_, nullptr);
	else
		hr = dev_->Reset(&pp_);
	if (FAILED(hr))
	{
		lost_ = true; // retried on the next present
		return;
	}
	lost_ = false;
	restore_all_gpu();
	init_state();
	if (g_hook_after)
		g_hook_after();
	CONS_Printf("[D3D9] device reset (%ux%u)\n", back_w_, back_h_);
}

// ================================================================== resources
Handle<Program> D3d9Rhi::create_program(const ProgramDesc& desc)
{
	std::string src, vs_entry, extra_define;
	if (!d3d9_hlsl_source(desc.name, src, vs_entry, extra_define))
		throw std::runtime_error(std::string("D3D9: unknown shader program: ") + desc.name);

	PFN_D3DCOMPILE_ compile = get_d3dcompile();
	if (!compile)
		throw std::runtime_error("D3D9: d3dcompiler_47.dll (HLSL compiler) not found");

	std::vector<std::string> define_names;
	for (const char* d : desc.defines)
		define_names.emplace_back(d);
	define_names.emplace_back("USES_LUMINANCE");
	if (!extra_define.empty())
		define_names.push_back(extra_define);
	std::vector<D3D_SHADER_MACRO> macros;
	for (const auto& d : define_names)
		macros.push_back({d.c_str(), "1"});
	macros.push_back({nullptr, nullptr});

	auto build = [&](const char* entry, const char* target) -> ID3DBlob* {
		ID3DBlob* code = nullptr;
		ID3DBlob* errors = nullptr;
		HRESULT hr = compile(src.data(), src.size(), desc.name, macros.data(), nullptr, entry, target, 0, 0, &code, &errors);
		if (FAILED(hr) || !code)
		{
			std::string msg = std::string("D3D9: failed to compile ") + desc.name + " (" + entry + "): ";
			if (errors)
				msg += std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize());
			safe_release(errors);
			safe_release(code);
			throw std::runtime_error(msg);
		}
		safe_release(errors);
		return code;
	};

	D3d9Program program;
	ID3DBlob* vs_code = build(vs_entry.c_str(), "vs_3_0");
	ID3DBlob* ps_code = nullptr;
	try
	{
		ps_code = build("ps_main", "ps_3_0");
	}
	catch (...)
	{
		safe_release(vs_code);
		throw;
	}
	HRESULT hr1 = dev_->CreateVertexShader(static_cast<const DWORD*>(vs_code->GetBufferPointer()), &program.vs);
	HRESULT hr2 = dev_->CreatePixelShader(static_cast<const DWORD*>(ps_code->GetBufferPointer()), &program.ps);
	vs_code->Release();
	ps_code->Release();
	if (FAILED(hr1) || FAILED(hr2))
	{
		safe_release(program.vs);
		safe_release(program.ps);
		throw std::runtime_error(std::string("D3D9: CreateShader failed for ") + desc.name);
	}
	return program_slab_.insert(std::move(program));
}

void D3d9Rhi::destroy_program(Handle<Program> handle)
{
	SRB2_ASSERT(program_slab_.is_valid(handle) == true);
	D3d9Program p = program_slab_.remove(handle);
	safe_release(p.vs);
	safe_release(p.ps);
	if (current_program_ && *current_program_ == handle)
		current_program_ = std::nullopt;
}

Handle<Texture> D3d9Rhi::create_texture(const TextureDesc& desc)
{
	D3d9Texture t;
	t.desc = desc;
	t.format = d3d_format(desc.format);
	t.render_target = is_render_target_format(desc.format);
	SRB2_ASSERT(t.format != D3DFMT_UNKNOWN);

	if (!create_texture_gpu(t))
		throw std::runtime_error("D3D9: could not create the texture (unsupported format or size)");
	const HRESULT hr_stage = dev_->CreateOffscreenPlainSurface(desc.width, desc.height, t.format, D3DPOOL_SYSTEMMEM, &t.stage, nullptr);
	if (FAILED(hr_stage))
	{
		// Some drivers (old GPUs, Windows 7) can't create plain L8/A8L8 system-memory surfaces, but can create a
		// texture in that pool: its level 0 is used as the upload surface.
		IDirect3DTexture9* st = nullptr;
		if (SUCCEEDED(dev_->CreateTexture(desc.width, desc.height, 1, 0, t.format, D3DPOOL_SYSTEMMEM, &st, nullptr)) && st
		    && SUCCEEDED(st->GetSurfaceLevel(0, &t.stage)))
		{
			t.stage_tex = st;
		}
		else
		{
			safe_release(st);
			safe_release(t.gpu);
			char msg[160];
			snprintf(msg, sizeof msg, "D3D9: could not create the texture upload surface (format %d, %ux%u, hr=0x%08lX)",
			         static_cast<int>(t.format), static_cast<unsigned>(desc.width), static_cast<unsigned>(desc.height),
			         static_cast<unsigned long>(hr_stage));
			throw std::runtime_error(msg);
		}
	}
	// initial contents zeroed (like a new texture without data)
	D3DLOCKED_RECT lr;
	if (SUCCEEDED(t.stage->LockRect(&lr, nullptr, 0)))
	{
		const uint32_t bpp = (t.format == D3DFMT_L8) ? 1 : (t.format == D3DFMT_A8L8 ? 2 : 4);
		for (uint32_t y = 0; y < desc.height; y++)
			std::memset(static_cast<uint8_t*>(lr.pBits) + y * lr.Pitch, 0, desc.width * bpp);
		t.stage->UnlockRect();
	}
	return texture_slab_.insert(std::move(t));
}

void D3d9Rhi::destroy_texture(Handle<Texture> handle)
{
	SRB2_ASSERT(texture_slab_.is_valid(handle) == true);
	D3d9Texture t = texture_slab_.remove(handle);
	safe_release(t.gpu);
	safe_release(t.stage);
	safe_release(t.stage_tex);
}

Handle<Buffer> D3d9Rhi::create_buffer(const BufferDesc& desc)
{
	D3d9Buffer b;
	b.desc = desc;
	b.shadow.assign(desc.size, 0);
	HRESULT hr;
	if (desc.type == BufferType::kVertexBuffer)
		hr = dev_->CreateVertexBuffer(desc.size, D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT, &b.vb, nullptr);
	else
		hr = dev_->CreateIndexBuffer(
			desc.size, D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_DEFAULT, &b.ib, nullptr
		);
	if (FAILED(hr))
		throw std::runtime_error("D3D9: could not create the buffer");
	return buffer_slab_.insert(std::move(b));
}

void D3d9Rhi::destroy_buffer(Handle<Buffer> handle)
{
	SRB2_ASSERT(buffer_slab_.is_valid(handle) == true);
	D3d9Buffer b = buffer_slab_.remove(handle);
	safe_release(b.vb);
	safe_release(b.ib);
	if (current_index_buffer_ == handle)
		current_index_buffer_ = kNullHandle;
}

Handle<Renderbuffer> D3d9Rhi::create_renderbuffer(const RenderbufferDesc& desc)
{
	D3d9Renderbuffer r;
	r.desc = desc;
	if (FAILED(dev_->CreateDepthStencilSurface(
			desc.width, desc.height, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, TRUE, &r.surface, nullptr
		)))
		throw std::runtime_error("D3D9: could not create the renderbuffer (depth)");
	return renderbuffer_slab_.insert(std::move(r));
}

void D3d9Rhi::destroy_renderbuffer(Handle<Renderbuffer> handle)
{
	SRB2_ASSERT(renderbuffer_slab_.is_valid(handle) == true);
	D3d9Renderbuffer r = renderbuffer_slab_.remove(handle);
	safe_release(r.surface);
}

TextureDetails D3d9Rhi::get_texture_details(Handle<Texture> texture)
{
	SRB2_ASSERT(texture_slab_.is_valid(texture) == true);
	auto& t = texture_slab_[texture];
	return {t.desc.width, t.desc.height, t.desc.format};
}

Rect D3d9Rhi::get_renderbuffer_size(Handle<Renderbuffer> renderbuffer)
{
	SRB2_ASSERT(renderbuffer_slab_.is_valid(renderbuffer) == true);
	auto& r = renderbuffer_slab_[renderbuffer];
	return {0, 0, r.desc.width, r.desc.height};
}

uint32_t D3d9Rhi::get_buffer_size(Handle<Buffer> buffer)
{
	SRB2_ASSERT(buffer_slab_.is_valid(buffer) == true);
	return buffer_slab_[buffer].desc.size;
}

void D3d9Rhi::update_buffer(Handle<Buffer> buffer, uint32_t offset, tcb::span<const std::byte> data)
{
	if (data.empty())
		return;
	SRB2_ASSERT(buffer_slab_.is_valid(buffer) == true);
	auto& b = buffer_slab_[buffer];
	SRB2_ASSERT(offset + data.size_bytes() <= b.desc.size);

	std::memcpy(b.shadow.data() + offset, data.data(), data.size_bytes());

	const bool is_vertex = (b.desc.type == BufferType::kVertexBuffer);
	const DWORD flags = (offset == 0 && data.size_bytes() == b.desc.size) ? D3DLOCK_DISCARD : 0;
	void* ptr = nullptr;
	HRESULT hr = is_vertex ? b.vb->Lock(offset, static_cast<UINT>(data.size_bytes()), &ptr, flags)
						   : b.ib->Lock(offset, static_cast<UINT>(data.size_bytes()), &ptr, flags);
	if (FAILED(hr) || !ptr)
		return; // device lost: the CPU copy is restored on Reset
	std::memcpy(ptr, data.data(), data.size_bytes());
	if (is_vertex)
		b.vb->Unlock();
	else
		b.ib->Unlock();
}

void D3d9Rhi::update_texture(
	Handle<Texture> texture,
	Rect region,
	srb2::rhi::PixelFormat data_format,
	tcb::span<const std::byte> data
)
{
	if (data.empty())
		return;
	SRB2_ASSERT(texture_slab_.is_valid(texture) == true);
	auto& t = texture_slab_[texture];
	SRB2_ASSERT(texture_format_for_pixels(data_format) == t.desc.format);
	SRB2_ASSERT(region.x + region.w <= t.desc.width && region.y + region.h <= t.desc.height);

	const uint32_t src_bpp = pixel_format_size(data_format);
	const uint32_t src_stride = ((src_bpp * region.w) + kPixelRowUnpackAlignment - 1) / kPixelRowUnpackAlignment *
								kPixelRowUnpackAlignment;
	SRB2_ASSERT(src_stride * region.h == data.size_bytes());

	RECT r = {
		static_cast<LONG>(region.x),
		static_cast<LONG>(region.y),
		static_cast<LONG>(region.x + region.w),
		static_cast<LONG>(region.y + region.h)};
	D3DLOCKED_RECT lr;
	if (FAILED(t.stage->LockRect(&lr, &r, 0)))
		return;

	const uint8_t* src = reinterpret_cast<const uint8_t*>(data.data());
	for (uint32_t y = 0; y < region.h; y++)
	{
		const uint8_t* s = src + y * src_stride;
		uint8_t* d = static_cast<uint8_t*>(lr.pBits) + y * lr.Pitch;
		switch (t.desc.format)
		{
		case TextureFormat::kLuminance:
			std::memcpy(d, s, region.w);
			break;
		case TextureFormat::kLuminanceAlpha:
			std::memcpy(d, s, region.w * 2); // memory: [L, A] = A8L8 little-endian
			break;
		case TextureFormat::kRGB: // RGB -> B,G,R,X
			for (uint32_t x = 0; x < region.w; x++)
			{
				d[x * 4 + 0] = s[x * 3 + 2];
				d[x * 4 + 1] = s[x * 3 + 1];
				d[x * 4 + 2] = s[x * 3 + 0];
				d[x * 4 + 3] = 255;
			}
			break;
		case TextureFormat::kRGBA: // RGBA -> B,G,R,A
			for (uint32_t x = 0; x < region.w; x++)
			{
				d[x * 4 + 0] = s[x * 4 + 2];
				d[x * 4 + 1] = s[x * 4 + 1];
				d[x * 4 + 2] = s[x * 4 + 0];
				d[x * 4 + 3] = s[x * 4 + 3];
			}
			break;
		}
	}
	t.stage->UnlockRect();

	if (!t.gpu)
		return;
	IDirect3DSurface9* dst = nullptr;
	if (SUCCEEDED(t.gpu->GetSurfaceLevel(0, &dst)))
	{
		POINT pt = {static_cast<LONG>(region.x), static_cast<LONG>(region.y)};
		dev_->UpdateSurface(t.stage, &r, dst, &pt);
		dst->Release();
	}
}

void D3d9Rhi::update_texture_settings(
	Handle<Texture> texture,
	TextureWrapMode u_wrap,
	TextureWrapMode v_wrap,
	TextureFilterMode min,
	TextureFilterMode mag
)
{
	SRB2_ASSERT(texture_slab_.is_valid(texture) == true);
	auto& t = texture_slab_[texture];
	t.desc.u_wrap = u_wrap;
	t.desc.v_wrap = v_wrap;
	t.desc.min = min;
	t.desc.mag = mag;
}

// ================================================================== render passes
void D3d9Rhi::begin_scene()
{
	if (!in_scene_ && !lost_)
	{
		if (SUCCEEDED(dev_->BeginScene()))
			in_scene_ = true;
	}
}

void D3d9Rhi::end_scene()
{
	if (in_scene_)
	{
		dev_->EndScene();
		in_scene_ = false;
	}
}

Rect D3d9Rhi::to_d3d_rect(const Rect& gl_rect, uint32_t target_h) const
{
	// The backbuffer uses a bottom-left origin in GL; textures are treated "like GL" (row 0 = first).
	if (target_is_texture_)
		return gl_rect;
	Rect r = gl_rect;
	r.y = static_cast<int32_t>(target_h) - (gl_rect.y + static_cast<int32_t>(gl_rect.h));
	return r;
}

void D3d9Rhi::apply_viewport(const Rect& gl_rect)
{
	viewport_ = gl_rect;
	uint32_t target_h = back_h_;
	if (!render_pass_stack_.empty() && std::holds_alternative<RenderPassBeginInfo>(render_pass_stack_.back()))
	{
		auto& info = std::get<RenderPassBeginInfo>(render_pass_stack_.back());
		target_h = texture_slab_[info.color_attachment].desc.height;
	}
	Rect r = to_d3d_rect(gl_rect, target_h);
	D3DVIEWPORT9 vp;
	vp.X = static_cast<DWORD>(std::max<int32_t>(r.x, 0));
	vp.Y = static_cast<DWORD>(std::max<int32_t>(r.y, 0));
	vp.Width = std::max<uint32_t>(r.w, 1);
	vp.Height = std::max<uint32_t>(r.h, 1);
	vp.MinZ = 0.0f;
	vp.MaxZ = 1.0f;
	dev_->SetViewport(&vp);

	float d3d[4] = {1.0f / static_cast<float>(vp.Width), 1.0f / static_cast<float>(vp.Height), target_is_texture_ ? -1.0f : 1.0f, 0.0f};
	set_const(kD3dReg, 1, d3d);
}

void D3d9Rhi::apply_target(const RenderPassState& state, bool clear)
{
	begin_scene();
	if (std::holds_alternative<DefaultRenderPassState>(state))
	{
		IDirect3DSurface9* bb = nullptr;
		if (SUCCEEDED(dev_->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)))
		{
			dev_->SetRenderTarget(0, bb);
			bb->Release();
		}
		dev_->SetDepthStencilSurface(auto_ds_);
		target_is_texture_ = false;
		apply_viewport({0, 0, back_w_, back_h_});
		dev_->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
		if (clear)
			dev_->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, D3DCOLOR_ARGB(255, 0, 0, 0), 1.0f, 0);
	}
	else
	{
		const RenderPassBeginInfo& info = std::get<RenderPassBeginInfo>(state);
		SRB2_ASSERT(texture_slab_.is_valid(info.color_attachment));
		auto& tex = texture_slab_[info.color_attachment];
		IDirect3DSurface9* surf = nullptr;
		if (tex.gpu && SUCCEEDED(tex.gpu->GetSurfaceLevel(0, &surf)))
		{
			dev_->SetRenderTarget(0, surf);
			surf->Release();
		}
		if (info.depth_stencil_attachment.has_value())
		{
			SRB2_ASSERT(renderbuffer_slab_.is_valid(*info.depth_stencil_attachment));
			dev_->SetDepthStencilSurface(renderbuffer_slab_[*info.depth_stencil_attachment].surface);
		}
		else
		{
			dev_->SetDepthStencilSurface(nullptr);
		}
		target_is_texture_ = true;
		apply_viewport({0, 0, tex.desc.width, tex.desc.height});
		dev_->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
		// Like the GL2 backend, a clear in push_render_pass isn't executed (whoever needs it does it).
	}
	raster_valid_ = false;
	if (raster_set_)
		apply_rasterizer();
}

void D3d9Rhi::push_default_render_pass(bool clear)
{
	render_pass_stack_.emplace_back(DefaultRenderPassState {clear});
	apply_target(render_pass_stack_.back(), clear);
}

void D3d9Rhi::push_render_pass(const RenderPassBeginInfo& info)
{
	render_pass_stack_.push_back(info);
	apply_target(render_pass_stack_.back(), false);
}

void D3d9Rhi::pop_render_pass()
{
	SRB2_ASSERT(render_pass_stack_.empty() == false);
	current_program_ = std::nullopt;
	render_pass_stack_.pop_back();
	if (!render_pass_stack_.empty())
	{
		// Must not clear when returning to a previous pass.
		apply_target(render_pass_stack_.back(), false);
	}
}

// ================================================================== state and drawing
void D3d9Rhi::set_const(int reg, int count, const float* data)
{
	std::memcpy(consts_[reg], data, sizeof(float) * 4 * count);
	dev_->SetVertexShaderConstantF(reg, data, count);
	dev_->SetPixelShaderConstantF(reg, data, count);
}

void D3d9Rhi::bind_program(Handle<Program> program)
{
	SRB2_ASSERT(render_pass_stack_.empty() == false);
	SRB2_ASSERT(program_slab_.is_valid(program));
	D3d9Program& p = program_slab_[program];
	dev_->SetVertexShader(p.vs);
	dev_->SetPixelShader(p.ps);
	current_program_ = program;
	attribs_.clear();
	// constants persist across programs: restore the D3D ones
	apply_viewport(viewport_);
}

void D3d9Rhi::bind_vertex_attrib(
	const char* name,
	Handle<Buffer> buffer,
	VertexAttributeFormat format,
	uint32_t offset,
	uint32_t stride
)
{
	SRB2_ASSERT(current_program_.has_value());
	SRB2_ASSERT(buffer_slab_.is_valid(buffer));
	SRB2_ASSERT(buffer_slab_[buffer].desc.type == BufferType::kVertexBuffer);
	for (auto& a : attribs_)
	{
		if (a.name == name)
		{
			a = BoundAttrib {name, buffer, format, offset, stride};
			return;
		}
	}
	attribs_.push_back(BoundAttrib {name, buffer, format, offset, stride});
}

void D3d9Rhi::bind_index_buffer(Handle<Buffer> buffer)
{
	SRB2_ASSERT(current_program_.has_value());
	SRB2_ASSERT(buffer_slab_.is_valid(buffer));
	SRB2_ASSERT(buffer_slab_[buffer].desc.type == BufferType::kIndexBuffer);
	current_index_buffer_ = buffer;
}

void D3d9Rhi::set_uniform(const char* name, float value)
{
	const UniformSlot* u = find_uniform(name);
	if (!u)
		return;
	const float v[4] = {value, 0, 0, 0};
	set_const(u->reg, 1, v);
}

void D3d9Rhi::set_uniform(const char* name, int value)
{
	set_uniform(name, static_cast<float>(value));
}

void D3d9Rhi::set_uniform(const char* name, glm::vec2 value)
{
	const UniformSlot* u = find_uniform(name);
	if (!u)
		return;
	const float v[4] = {value.x, value.y, 0, 0};
	set_const(u->reg, 1, v);
}

void D3d9Rhi::set_uniform(const char* name, glm::vec3 value)
{
	const UniformSlot* u = find_uniform(name);
	if (!u)
		return;
	const float v[4] = {value.x, value.y, value.z, 0};
	set_const(u->reg, 1, v);
}

void D3d9Rhi::set_uniform(const char* name, glm::vec4 value)
{
	const UniformSlot* u = find_uniform(name);
	if (!u)
		return;
	const float v[4] = {value.x, value.y, value.z, value.w};
	set_const(u->reg, 1, v);
}

void D3d9Rhi::set_uniform(const char* name, glm::ivec2 value)
{
	set_uniform(name, glm::vec2(value));
}

void D3d9Rhi::set_uniform(const char* name, glm::ivec3 value)
{
	set_uniform(name, glm::vec3(value));
}

void D3d9Rhi::set_uniform(const char* name, glm::ivec4 value)
{
	set_uniform(name, glm::vec4(value));
}

void D3d9Rhi::set_uniform(const char* name, glm::mat2 value)
{
	(void)name;
	(void)value; // no game shader uses mat2
}

void D3d9Rhi::set_uniform(const char* name, glm::mat3 value)
{
	const UniformSlot* u = find_uniform(name);
	if (!u)
		return;
	float v[3][4];
	for (int c = 0; c < 3; c++)
	{
		v[c][0] = value[c][0];
		v[c][1] = value[c][1];
		v[c][2] = value[c][2];
		v[c][3] = 0.0f;
	}
	set_const(u->reg, 3, &v[0][0]);
}

void D3d9Rhi::set_uniform(const char* name, glm::mat4 value)
{
	const UniformSlot* u = find_uniform(name);
	if (!u)
		return;
	set_const(u->reg, 4, &value[0][0]); // glm columns, one per register
}

void D3d9Rhi::set_sampler(const char* name, uint32_t slot, Handle<Texture> texture)
{
	(void)name;
	SRB2_ASSERT(slot < kMaxSamplers);
	SRB2_ASSERT(texture_slab_.is_valid(texture));
	D3d9Texture& t = texture_slab_[texture];
	dev_->SetTexture(slot, t.gpu);
	dev_->SetSamplerState(slot, D3DSAMP_ADDRESSU, d3d_wrap(t.desc.u_wrap));
	dev_->SetSamplerState(slot, D3DSAMP_ADDRESSV, d3d_wrap(t.desc.v_wrap));
	dev_->SetSamplerState(slot, D3DSAMP_MINFILTER, d3d_filter(t.desc.min));
	dev_->SetSamplerState(slot, D3DSAMP_MAGFILTER, d3d_filter(t.desc.mag));
	dev_->SetSamplerState(slot, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
}

void D3d9Rhi::set_rasterizer_state(const RasterizerStateDesc& desc)
{
	raster_ = desc;
	raster_set_ = true;
	primitive_ = desc.primitive;
	apply_rasterizer();
}

void D3d9Rhi::apply_rasterizer()
{
	const RasterizerStateDesc& d = raster_;

	// Winding: GL "front=CCW" = D3D "front=CW" (D3D screen Y points down). Drawing to a texture flips Y: inverted.
	DWORD cull = D3DCULL_NONE;
	if (d.cull != CullMode::kNone)
	{
		bool cull_ccw = (d.cull == CullMode::kBack); // GL back-face culling
		if (d.winding == FaceWinding::kClockwise)
			cull_ccw = !cull_ccw;
		if (target_is_texture_)
			cull_ccw = !cull_ccw;
		cull = cull_ccw ? D3DCULL_CCW : D3DCULL_CW;
	}
	dev_->SetRenderState(D3DRS_CULLMODE, cull);

	DWORD mask = 0;
	if (d.color_mask.r) mask |= D3DCOLORWRITEENABLE_RED;
	if (d.color_mask.g) mask |= D3DCOLORWRITEENABLE_GREEN;
	if (d.color_mask.b) mask |= D3DCOLORWRITEENABLE_BLUE;
	if (d.color_mask.a) mask |= D3DCOLORWRITEENABLE_ALPHA;
	dev_->SetRenderState(D3DRS_COLORWRITEENABLE, mask);

	dev_->SetRenderState(D3DRS_ALPHABLENDENABLE, d.blend_enabled ? TRUE : FALSE);
	if (d.blend_enabled)
	{
		dev_->SetRenderState(D3DRS_SRCBLEND, d3d_blend(d.blend_source_factor_color));
		dev_->SetRenderState(D3DRS_DESTBLEND, d3d_blend(d.blend_dest_factor_color));
		dev_->SetRenderState(D3DRS_BLENDOP, d3d_blend_op(d.blend_color_function));
		dev_->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, TRUE);
		dev_->SetRenderState(D3DRS_SRCBLENDALPHA, d3d_blend(d.blend_source_factor_alpha));
		dev_->SetRenderState(D3DRS_DESTBLENDALPHA, d3d_blend(d.blend_dest_factor_alpha));
		dev_->SetRenderState(D3DRS_BLENDOPALPHA, d3d_blend_op(d.blend_alpha_function));
		dev_->SetRenderState(
			D3DRS_BLENDFACTOR,
			D3DCOLOR_COLORVALUE(d.blend_color.r, d.blend_color.g, d.blend_color.b, d.blend_color.a)
		);
	}

	dev_->SetRenderState(D3DRS_ZENABLE, d.depth_test ? D3DZB_TRUE : D3DZB_FALSE);
	dev_->SetRenderState(D3DRS_ZWRITEENABLE, d.depth_write ? TRUE : FALSE);
	dev_->SetRenderState(D3DRS_ZFUNC, d3d_cmp(d.depth_func));

	dev_->SetRenderState(D3DRS_STENCILENABLE, d.stencil_test ? TRUE : FALSE);
	if (d.stencil_test)
	{
		// GL front (CCW) = D3D "clockwise"; swapped when Y is flipped
		const bool swap = target_is_texture_;
		const StencilOp fp = swap ? d.back_pass : d.front_pass;
		const StencilOp ff = swap ? d.back_fail : d.front_fail;
		const StencilOp fz = swap ? d.back_depth_fail : d.front_depth_fail;
		const CompareFunc fc = swap ? d.back_stencil_compare : d.front_stencil_compare;
		const StencilOp bp = swap ? d.front_pass : d.back_pass;
		const StencilOp bf = swap ? d.front_fail : d.back_fail;
		const StencilOp bz = swap ? d.front_depth_fail : d.back_depth_fail;
		const CompareFunc bc = swap ? d.front_stencil_compare : d.back_stencil_compare;
		dev_->SetRenderState(D3DRS_TWOSIDEDSTENCILMODE, TRUE);
		dev_->SetRenderState(D3DRS_STENCILPASS, d3d_stencil_op(fp));
		dev_->SetRenderState(D3DRS_STENCILFAIL, d3d_stencil_op(ff));
		dev_->SetRenderState(D3DRS_STENCILZFAIL, d3d_stencil_op(fz));
		dev_->SetRenderState(D3DRS_STENCILFUNC, d3d_cmp(fc));
		dev_->SetRenderState(D3DRS_CCW_STENCILPASS, d3d_stencil_op(bp));
		dev_->SetRenderState(D3DRS_CCW_STENCILFAIL, d3d_stencil_op(bf));
		dev_->SetRenderState(D3DRS_CCW_STENCILZFAIL, d3d_stencil_op(bz));
		dev_->SetRenderState(D3DRS_CCW_STENCILFUNC, d3d_cmp(bc));
		dev_->SetRenderState(D3DRS_STENCILREF, stencil_ref_[swap ? 1 : 0]);
		dev_->SetRenderState(D3DRS_STENCILMASK, stencil_cmask_[swap ? 1 : 0]);
		dev_->SetRenderState(D3DRS_STENCILWRITEMASK, stencil_wmask_[swap ? 1 : 0]);
	}

	dev_->SetRenderState(D3DRS_SCISSORTESTENABLE, d.scissor_test ? TRUE : FALSE);
	if (d.scissor_test)
	{
		uint32_t target_h = back_h_;
		if (!render_pass_stack_.empty() && std::holds_alternative<RenderPassBeginInfo>(render_pass_stack_.back()))
			target_h = texture_slab_[std::get<RenderPassBeginInfo>(render_pass_stack_.back()).color_attachment].desc.height;
		Rect r = to_d3d_rect(d.scissor, target_h);
		RECT rc = {r.x, r.y, r.x + static_cast<LONG>(r.w), r.y + static_cast<LONG>(r.h)};
		dev_->SetScissorRect(&rc);
	}
	raster_valid_ = true;
}

void D3d9Rhi::set_viewport(const Rect& rect)
{
	SRB2_ASSERT(render_pass_stack_.empty() == false);
	apply_viewport(rect);
}

IDirect3DVertexDeclaration9* D3d9Rhi::get_declaration(const std::vector<D3DVERTEXELEMENT9>& elements)
{
	std::string key(reinterpret_cast<const char*>(elements.data()), elements.size() * sizeof(D3DVERTEXELEMENT9));
	auto it = decl_cache_.find(key);
	if (it != decl_cache_.end())
		return it->second;
	IDirect3DVertexDeclaration9* decl = nullptr;
	if (FAILED(dev_->CreateVertexDeclaration(elements.data(), &decl)))
		return nullptr;
	decl_cache_[key] = decl;
	return decl;
}

bool D3d9Rhi::bind_streams(uint32_t* out_first_stream_vertices)
{
	std::vector<D3DVERTEXELEMENT9> elements;
	struct Stream
	{
		Handle<Buffer> buffer;
		uint32_t stride;
	};
	std::vector<Stream> streams;
	for (const auto& a : attribs_)
	{
		BYTE usage = 0, index = 0;
		if (!decl_usage_for(a.name, usage, index))
			continue;
		size_t s = 0;
		for (; s < streams.size(); s++)
		{
			if (streams[s].buffer == a.buffer && streams[s].stride == a.stride)
				break;
		}
		if (s == streams.size())
			streams.push_back({a.buffer, a.stride});
		D3DVERTEXELEMENT9 e = {};
		e.Stream = static_cast<WORD>(s);
		e.Offset = static_cast<WORD>(a.offset);
		e.Type = static_cast<BYTE>(d3d_decl_type(a.format));
		e.Method = D3DDECLMETHOD_DEFAULT;
		e.Usage = usage;
		e.UsageIndex = index;
		elements.push_back(e);
	}
	if (elements.empty() || streams.empty())
		return false;
	const D3DVERTEXELEMENT9 end = D3DDECL_END();
	elements.push_back(end);

	IDirect3DVertexDeclaration9* decl = get_declaration(elements);
	if (!decl)
		return false;
	dev_->SetVertexDeclaration(decl);
	for (size_t s = 0; s < streams.size(); s++)
		dev_->SetStreamSource(static_cast<UINT>(s), buffer_slab_[streams[s].buffer].vb, 0, streams[s].stride);
	if (out_first_stream_vertices)
	{
		const uint32_t stride = std::max<uint32_t>(streams[0].stride, 1);
		*out_first_stream_vertices = buffer_slab_[streams[0].buffer].desc.size / stride;
	}
	return true;
}

void D3d9Rhi::draw(uint32_t vertex_count, uint32_t first_vertex)
{
	SRB2_ASSERT(render_pass_stack_.empty() == false);
	if (lost_ || vertex_count == 0)
		return;
	if (!bind_streams(nullptr))
		return;
	const UINT prims = primitive_count(primitive_, vertex_count);
	if (prims == 0)
		return;
	dev_->DrawPrimitive(d3d_primitive(primitive_), first_vertex, prims);
}

void D3d9Rhi::draw_indexed(uint32_t index_count, uint32_t first_index)
{
	SRB2_ASSERT(current_index_buffer_ != kNullHandle);
	if (lost_ || index_count == 0)
		return;
	uint32_t num_vertices = 0;
	if (!bind_streams(&num_vertices))
		return;
	const UINT prims = primitive_count(primitive_, index_count);
	if (prims == 0)
		return;
	dev_->SetIndices(buffer_slab_[current_index_buffer_].ib);
	dev_->DrawIndexedPrimitive(d3d_primitive(primitive_), 0, 0, std::max<uint32_t>(num_vertices, 1), first_index, prims);
}

void D3d9Rhi::set_stencil_reference(CullMode face, uint8_t reference)
{
	if (face != CullMode::kBack)
		stencil_ref_[0] = reference;
	if (face != CullMode::kFront)
		stencil_ref_[1] = reference;
	if (raster_set_)
		apply_rasterizer();
}

void D3d9Rhi::set_stencil_compare_mask(CullMode face, uint8_t mask)
{
	if (face != CullMode::kBack)
		stencil_cmask_[0] = mask;
	if (face != CullMode::kFront)
		stencil_cmask_[1] = mask;
	if (raster_set_)
		apply_rasterizer();
}

void D3d9Rhi::set_stencil_write_mask(CullMode face, uint8_t mask)
{
	if (face != CullMode::kBack)
		stencil_wmask_[0] = mask;
	if (face != CullMode::kFront)
		stencil_wmask_[1] = mask;
	if (raster_set_)
		apply_rasterizer();
}

// ================================================================== framebuffer readback
bool D3d9Rhi::download_target(std::vector<uint8_t>& out_bgra, uint32_t& out_w, uint32_t& out_h, bool& out_is_back)
{
	SRB2_ASSERT(render_pass_stack_.empty() == false);
	IDirect3DSurface9* rt = nullptr;
	if (FAILED(dev_->GetRenderTarget(0, &rt)) || !rt)
		return false;
	D3DSURFACE_DESC sd = {};
	rt->GetDesc(&sd);
	IDirect3DSurface9* sys = nullptr;
	bool ok = false;
	if (SUCCEEDED(dev_->CreateOffscreenPlainSurface(sd.Width, sd.Height, sd.Format, D3DPOOL_SYSTEMMEM, &sys, nullptr)) &&
		SUCCEEDED(dev_->GetRenderTargetData(rt, sys)))
	{
		D3DLOCKED_RECT lr;
		if (SUCCEEDED(sys->LockRect(&lr, nullptr, D3DLOCK_READONLY)))
		{
			out_w = sd.Width;
			out_h = sd.Height;
			out_bgra.resize(static_cast<size_t>(sd.Width) * sd.Height * 4);
			for (uint32_t y = 0; y < sd.Height; y++)
				std::memcpy(out_bgra.data() + static_cast<size_t>(y) * sd.Width * 4, static_cast<uint8_t*>(lr.pBits) + y * lr.Pitch, sd.Width * 4);
			sys->UnlockRect();
			ok = true;
		}
	}
	safe_release(sys);
	rt->Release();
	out_is_back = !target_is_texture_;
	return ok;
}

void D3d9Rhi::read_pixels(const Rect& rect, PixelFormat format, tcb::span<std::byte> out)
{
	std::vector<uint8_t> img;
	uint32_t w = 0, h = 0;
	bool is_back = false;
	if (!download_target(img, w, h, is_back))
		return;

	const uint32_t bpp = pixel_format_size(format);
	const uint32_t stride = (rect.w * bpp + (kPixelRowPackAlignment - 1)) & ~(kPixelRowPackAlignment - 1);
	SRB2_ASSERT(out.size_bytes() == stride * rect.h);
	SRB2_ASSERT(rect.x >= 0 && rect.y >= 0 && rect.x + rect.w <= w && rect.y + rect.h <= h);

	uint8_t* dst = reinterpret_cast<uint8_t*>(out.data());
	for (uint32_t j = 0; j < rect.h; j++)
	{
		// GL: output row 0 = lowest y. On the backbuffer that is the bottom row; on a texture, row 0.
		const uint32_t src_row = is_back ? (h - 1 - (rect.y + j)) : (rect.y + j);
		const uint8_t* s = img.data() + (static_cast<size_t>(src_row) * w + rect.x) * 4;
		uint8_t* d = dst + j * stride;
		for (uint32_t i = 0; i < rect.w; i++)
		{
			const uint8_t b = s[i * 4 + 0], g = s[i * 4 + 1], r = s[i * 4 + 2], a = s[i * 4 + 3];
			switch (format)
			{
			case PixelFormat::kR8:
				d[i] = r;
				break;
			case PixelFormat::kRG8:
				d[i * 2 + 0] = r;
				d[i * 2 + 1] = g;
				break;
			case PixelFormat::kRGB8:
				d[i * 3 + 0] = r;
				d[i * 3 + 1] = g;
				d[i * 3 + 2] = b;
				break;
			case PixelFormat::kRGBA8:
				d[i * 4 + 0] = r;
				d[i * 4 + 1] = g;
				d[i * 4 + 2] = b;
				d[i * 4 + 3] = a;
				break;
			default:
				break;
			}
		}
	}
}

void D3d9Rhi::copy_framebuffer_to_texture(Handle<Texture> dst_tex, const Rect& dst_region, const Rect& src_region)
{
	SRB2_ASSERT(texture_slab_.is_valid(dst_tex));
	D3d9Texture& tex = texture_slab_[dst_tex];
	SRB2_ASSERT(dst_region.w == src_region.w && dst_region.h == src_region.h);

	std::vector<uint8_t> img;
	uint32_t w = 0, h = 0;
	bool is_back = false;
	if (!download_target(img, w, h, is_back))
		return;

	RECT r = {
		static_cast<LONG>(dst_region.x),
		static_cast<LONG>(dst_region.y),
		static_cast<LONG>(dst_region.x + dst_region.w),
		static_cast<LONG>(dst_region.y + dst_region.h)};
	D3DLOCKED_RECT lr;
	if (FAILED(tex.stage->LockRect(&lr, &r, 0)))
		return;
	for (uint32_t j = 0; j < dst_region.h; j++)
	{
		const uint32_t src_row = is_back ? (h - 1 - (src_region.y + j)) : (src_region.y + j);
		const uint8_t* s = img.data() + (static_cast<size_t>(src_row) * w + src_region.x) * 4;
		uint8_t* d = static_cast<uint8_t*>(lr.pBits) + j * lr.Pitch;
		for (uint32_t i = 0; i < dst_region.w; i++)
		{
			switch (tex.desc.format)
			{
			case TextureFormat::kRGB:
			case TextureFormat::kRGBA:
				d[i * 4 + 0] = s[i * 4 + 0];
				d[i * 4 + 1] = s[i * 4 + 1];
				d[i * 4 + 2] = s[i * 4 + 2];
				d[i * 4 + 3] = (tex.desc.format == TextureFormat::kRGB) ? 255 : s[i * 4 + 3];
				break;
			case TextureFormat::kLuminance:
				d[i] = s[i * 4 + 2];
				break;
			case TextureFormat::kLuminanceAlpha:
				d[i * 2 + 0] = s[i * 4 + 2];
				d[i * 2 + 1] = s[i * 4 + 3];
				break;
			}
		}
	}
	tex.stage->UnlockRect();

	IDirect3DSurface9* dst = nullptr;
	if (tex.gpu && SUCCEEDED(tex.gpu->GetSurfaceLevel(0, &dst)))
	{
		POINT pt = {static_cast<LONG>(dst_region.x), static_cast<LONG>(dst_region.y)};
		dev_->UpdateSurface(tex.stage, &r, dst, &pt);
		dst->Release();
	}
}

// ================================================================== present
void D3d9Rhi::present()
{
	end_scene();

	if (lost_)
	{
		// device lost (classic D3D9 / fullscreen): retry
		HRESULT coop = dev_->TestCooperativeLevel();
		if (coop == D3DERR_DEVICENOTRESET || SUCCEEDED(coop))
			reset_device();
		return;
	}

	HRESULT hr = dev_->Present(nullptr, nullptr, nullptr, nullptr);
	if (hr == D3DERR_DEVICELOST)
	{
		lost_ = true;
		return;
	}

	// window size changed (mode, fullscreen): reset the backbuffer
	RECT rc = {};
	GetClientRect(hwnd_, &rc);
	const uint32_t w = static_cast<uint32_t>(std::max<LONG>(rc.right - rc.left, 1));
	const uint32_t h = static_cast<uint32_t>(std::max<LONG>(rc.bottom - rc.top, 1));
	if (w != back_w_ || h != back_h_ || reset_pending_)
	{
		reset_pending_ = false;
		reset_device();
	}
}

void D3d9Rhi::finish()
{
	// Nothing to release per frame (no FBO to recreate).
}

std::unique_ptr<Rhi> create_d3d9_rhi(void* hwnd, bool vsync, bool allow_ex)
{
	return std::make_unique<D3d9Rhi>(static_cast<HWND>(hwnd), vsync, allow_ex);
}

void D3d9Rhi::prepare_external_draw()
{
	begin_scene();
	if (render_pass_stack_.empty() || std::holds_alternative<DefaultRenderPassState>(render_pass_stack_.back()))
	{
		IDirect3DSurface9* bb = nullptr;
		if (SUCCEEDED(dev_->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)))
		{
			dev_->SetRenderTarget(0, bb);
			bb->Release();
		}
		dev_->SetDepthStencilSurface(auto_ds_);
		target_is_texture_ = false;
	}
}

void* d3d9_current_device()
{
	return g_instance ? static_cast<void*>(g_instance->device()) : nullptr;
}

bool d3d9_current_is_ex()
{
	return g_instance && g_instance->is_ex();
}

void d3d9_prepare_external_draw()
{
	if (g_instance)
		g_instance->prepare_external_draw();
}

void d3d9_backbuffer_size(unsigned* w, unsigned* h)
{
	const Rect r = g_instance ? g_instance->default_framebuffer_dimensions() : Rect {0, 0, 0, 0};
	if (w)
		*w = r.w;
	if (h)
		*h = r.h;
}

void d3d9_set_reset_hooks(D3d9ResetHook before, D3d9ResetHook after)
{
	g_hook_before = before;
	g_hook_after = after;
}

bool d3d9_set_vsync(Rhi* rhi, bool vsync)
{
	D3d9Rhi* d3d9 = dynamic_cast<D3d9Rhi*>(rhi);
	if (!d3d9)
		return false;
	d3d9->set_vsync(vsync);
	return true;
}

} // namespace srb2::rhi

#endif // _WIN32
