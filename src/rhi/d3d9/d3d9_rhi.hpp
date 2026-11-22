// DR. ROBOTNIK'S RING RACERS - Ring Racers E
//-----------------------------------------------------------------------------
// Direct3D 9 / 9Ex RHI backend (Windows only).
// Uses Direct3D9Ex (Vista+, no "device lost" when switching windows) when available and falls
// back to classic Direct3D 9 otherwise (XP). Shaders are embedded HLSL SM3 (d3d9_shaders.cpp).
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------

#ifndef __SRB2_RHI_D3D9_RHI_HPP__
#define __SRB2_RHI_D3D9_RHI_HPP__

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "../rhi.hpp"

namespace srb2::rhi
{

struct D3d9Texture : public rhi::Texture
{
	IDirect3DTexture9* gpu = nullptr; // D3DPOOL_DEFAULT (render target if RGB/RGBA)
	IDirect3DSurface9* stage = nullptr; // D3DPOOL_SYSTEMMEM, same size/format (for uploads)
	IDirect3DTexture9* stage_tex = nullptr; // if the GPU can't create 'stage' as a plain surface, it is level 0 of this system-memory texture
	rhi::TextureDesc desc {};
	D3DFORMAT format = D3DFMT_UNKNOWN;
	bool render_target = false;
};

struct D3d9Buffer : public rhi::Buffer
{
	IDirect3DVertexBuffer9* vb = nullptr;
	IDirect3DIndexBuffer9* ib = nullptr;
	rhi::BufferDesc desc {};
	std::vector<uint8_t> shadow; // CPU copy to recreate the buffer after a Reset
};

struct D3d9Renderbuffer : public rhi::Renderbuffer
{
	IDirect3DSurface9* surface = nullptr;
	rhi::RenderbufferDesc desc {};
};

struct D3d9Program : public rhi::Program
{
	IDirect3DVertexShader9* vs = nullptr;
	IDirect3DPixelShader9* ps = nullptr;
};

class D3d9Rhi final : public Rhi
{
public:
	/// @param hwnd window to present to
	/// @param vsync wait for vertical refresh
	D3d9Rhi(HWND hwnd, bool vsync, bool allow_ex);
	virtual ~D3d9Rhi();

	bool is_ex() const noexcept { return ex_; }
	void set_vsync(bool vsync);
	/// Current backbuffer size.
	Rect default_framebuffer_dimensions() const noexcept { return {0, 0, back_w_, back_h_}; }

	virtual Handle<Program> create_program(const ProgramDesc& desc) override;
	virtual void destroy_program(Handle<Program> handle) override;

	virtual Handle<Texture> create_texture(const TextureDesc& desc) override;
	virtual void destroy_texture(Handle<Texture> handle) override;
	virtual Handle<Buffer> create_buffer(const BufferDesc& desc) override;
	virtual void destroy_buffer(Handle<Buffer> handle) override;
	virtual Handle<Renderbuffer> create_renderbuffer(const RenderbufferDesc& desc) override;
	virtual void destroy_renderbuffer(Handle<Renderbuffer> handle) override;

	virtual TextureDetails get_texture_details(Handle<Texture> texture) override;
	virtual Rect get_renderbuffer_size(Handle<Renderbuffer> renderbuffer) override;
	virtual uint32_t get_buffer_size(Handle<Buffer> buffer) override;

	virtual void update_buffer(Handle<Buffer> buffer, uint32_t offset, tcb::span<const std::byte> data) override;
	virtual void update_texture(
		Handle<Texture> texture,
		Rect region,
		srb2::rhi::PixelFormat data_format,
		tcb::span<const std::byte> data
	) override;
	virtual void update_texture_settings(
		Handle<Texture> texture,
		TextureWrapMode u_wrap,
		TextureWrapMode v_wrap,
		TextureFilterMode min,
		TextureFilterMode mag
	) override;

	virtual void push_default_render_pass(bool clear) override;
	virtual void push_render_pass(const RenderPassBeginInfo& info) override;
	virtual void pop_render_pass() override;
	virtual void bind_program(Handle<Program> program) override;
	virtual void bind_vertex_attrib(
		const char* name,
		Handle<Buffer> buffer,
		VertexAttributeFormat format,
		uint32_t offset,
		uint32_t stride
	) override;
	virtual void bind_index_buffer(Handle<Buffer> buffer) override;
	virtual void set_uniform(const char* name, float value) override;
	virtual void set_uniform(const char* name, int value) override;
	virtual void set_uniform(const char* name, glm::vec2 value) override;
	virtual void set_uniform(const char* name, glm::vec3 value) override;
	virtual void set_uniform(const char* name, glm::vec4 value) override;
	virtual void set_uniform(const char* name, glm::ivec2 value) override;
	virtual void set_uniform(const char* name, glm::ivec3 value) override;
	virtual void set_uniform(const char* name, glm::ivec4 value) override;
	virtual void set_uniform(const char* name, glm::mat2 value) override;
	virtual void set_uniform(const char* name, glm::mat3 value) override;
	virtual void set_uniform(const char* name, glm::mat4 value) override;
	virtual void set_sampler(const char* name, uint32_t slot, Handle<Texture> texture) override;
	virtual void set_rasterizer_state(const RasterizerStateDesc& desc) override;
	virtual void set_viewport(const Rect& rect) override;
	virtual void draw(uint32_t vertex_count, uint32_t first_vertex) override;
	virtual void draw_indexed(uint32_t index_count, uint32_t first_index) override;
	virtual void read_pixels(const Rect& rect, PixelFormat format, tcb::span<std::byte> out) override;
	virtual void copy_framebuffer_to_texture(
		Handle<Texture> dst_tex,
		const Rect& dst_region,
		const Rect& src_region
	) override;
	virtual void set_stencil_reference(CullMode face, uint8_t reference) override;
	virtual void set_stencil_compare_mask(CullMode face, uint8_t mask) override;
	virtual void set_stencil_write_mask(CullMode face, uint8_t mask) override;

	virtual void present() override;
	virtual void finish() override;

private:
	struct DefaultRenderPassState
	{
		bool clear = false;
	};
	using RenderPassState = std::variant<DefaultRenderPassState, RenderPassBeginInfo>;

	struct BoundAttrib
	{
		std::string name;
		Handle<Buffer> buffer;
		VertexAttributeFormat format;
		uint32_t offset;
		uint32_t stride;
	};

	HWND hwnd_ = nullptr;
	bool vsync_ = false;
	bool allow_ex_ = true;
	bool ex_ = false;
	bool lost_ = false;
	bool reset_pending_ = false; // vsync changed: the Reset happens in present(), outside a render pass
	bool in_scene_ = false;
	uint32_t back_w_ = 0;
	uint32_t back_h_ = 0;

	IDirect3D9* d3d_ = nullptr;
	IDirect3DDevice9* dev_ = nullptr;
	IDirect3DSurface9* auto_ds_ = nullptr; // backbuffer's automatic depth buffer
	D3DPRESENT_PARAMETERS pp_ {};

	Slab<D3d9Texture> texture_slab_;
	Slab<D3d9Buffer> buffer_slab_;
	Slab<D3d9Renderbuffer> renderbuffer_slab_;
	Slab<D3d9Program> program_slab_;

	std::vector<RenderPassState> render_pass_stack_;
	std::optional<Handle<Program>> current_program_;
	Handle<Buffer> current_index_buffer_;
	std::vector<BoundAttrib> attribs_;
	std::unordered_map<std::string, IDirect3DVertexDeclaration9*> decl_cache_;

	RasterizerStateDesc raster_ {};
	bool raster_valid_ = false;
	bool raster_set_ = false; // set_rasterizer_state was already called
	PrimitiveType primitive_ = PrimitiveType::kTriangles;
	bool target_is_texture_ = false; // true = drawing to a texture (Y is flipped, as in GL)
	Rect viewport_ {};
	float consts_[40][4] = {};
	uint8_t stencil_ref_[2] = {0, 0};
	uint8_t stencil_cmask_[2] = {0xFF, 0xFF};
	uint8_t stencil_wmask_[2] = {0xFF, 0xFF};

	void create_device();
	void init_state();
	void release_all_gpu();
	void restore_all_gpu();
	void reset_device();
	void sync_backbuffer_size();
public:
	void prepare_external_draw();
	IDirect3DDevice9* device() const noexcept { return dev_; }
private:
	void apply_target(const RenderPassState& state, bool clear);
	void apply_viewport(const Rect& gl_rect);
	Rect to_d3d_rect(const Rect& gl_rect, uint32_t target_h) const;
	void apply_rasterizer();
	void set_const(int reg, int count, const float* data);
	bool create_texture_gpu(D3d9Texture& t);
	void begin_scene();
	void end_scene();
	IDirect3DVertexDeclaration9* get_declaration(const std::vector<D3DVERTEXELEMENT9>& elements);
	bool bind_streams(uint32_t* out_first_stream_vertices);
	bool download_target(std::vector<uint8_t>& out_bgra, uint32_t& out_w, uint32_t& out_h, bool& out_is_back);
};

} // namespace srb2::rhi

#endif // _WIN32

#endif // __SRB2_RHI_D3D9_RHI_HPP__
