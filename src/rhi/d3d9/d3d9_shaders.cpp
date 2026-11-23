// DR. ROBOTNIK'S RING RACERS - Ring Racers E
//-----------------------------------------------------------------------------
// HLSL (SM3) shaders for the Direct3D 9 backend, hand-translated from the game's
// GLSL (shaders.pk3: rhi_glsl_*.glsl). Each program has vs_main and ps_main.
//
// Conventions (see d3d9_rhi.cpp, kUniforms table):
//  * Matrices: glm COLUMNS are uploaded as-is (c0..c3); the product is done by hand (MUL4).
//  * The game's "int" uniforms arrive as a float in .x of their register.
//  * GL clip space (z in [-w,w]) -> D3D (z in [0,w]), half-pixel offset and Y flip when drawing
//    to textures (so GL and D3D match): see fix_clip().
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------

#ifdef _WIN32

#include <string>

#include "d3d9_shaders.hpp"

namespace srb2::rhi
{

static const char* kPrelude = R"HLSL(
float4 u_proj[4]   : register(c0);
float4 u_mv[4]     : register(c4);
float4 u_tc0[3]    : register(c8);
float4 u_tc0_min   : register(c11);
float4 u_tc0_max   : register(c12);
float4 u_s0size    : register(c13);
float4 u_time      : register(c14);
float4 u_s0idx     : register(c15);
float4 u_wipe_mode : register(c16);
float4 u_wipe_swz  : register(c17);
float4 u_pi_water  : register(c18);
float4 u_pi_heat   : register(c19);
float4 u_d3d       : register(c20); // x = 1/viewport width, y = 1/height, z = Y flip (1 or -1)

sampler2D s_sampler0 : register(s0);
sampler2D s_sampler1 : register(s1);
sampler2D s_sampler2 : register(s2);

#define MUL4(c, v) ((c)[0] * (v).x + (c)[1] * (v).y + (c)[2] * (v).z + (c)[3] * (v).w)

float4 fix_clip(float4 p)
{
	p.z = (p.z + p.w) * 0.5;
	p.y = p.y * u_d3d.z;
	p.xy += float2(-u_d3d.x, u_d3d.y) * p.w;
	return p;
}

float glmod(float x, float y)
{
	return x - y * floor(x / y);
}

struct VSIN
{
	float3 pos : POSITION0;
#ifdef ENABLE_VA_TEXCOORD0
	float2 tc : TEXCOORD0;
#endif
#ifdef ENABLE_VA_COLOR
	float4 col : COLOR0;
#endif
};

struct VSOUT
{
	float4 pos : POSITION;
	float2 tc : TEXCOORD0;
	float4 col : TEXCOORD1;
};

struct PSIN
{
	float2 tc : TEXCOORD0;
	float4 col : TEXCOORD1;
};

// Shared vertex shader (unshaded, unshadedpaletted, sharpbilinear, crt, crtsharp, postimg)
VSOUT vs_main(VSIN i)
{
	VSOUT o;
	float4 p = MUL4(u_proj, MUL4(u_mv, float4(i.pos, 1.0)));
	o.pos = fix_clip(p);

#ifdef ENABLE_VA_COLOR
	o.col = i.col;
#else
	o.col = float4(1.0, 1.0, 1.0, 1.0);
#endif

	float3 t;
#ifdef ENABLE_VA_TEXCOORD0
	t = float3(i.tc, 1.0);
#else
	t = float3(0.0, 0.0, 1.0);
#endif
	t = u_tc0[0].xyz * t.x + u_tc0[1].xyz * t.y + u_tc0[2].xyz * t.z;
	o.tc = t.xy / t.z;
	return o;
}
)HLSL";

static const char* kUnshaded = R"HLSL(
float4 ps_main(PSIN i) : COLOR0
{
	float4 c = tex2D(s_sampler0, i.tc) * i.col;
	if (c.a < 0.01)
		discard;
	return c;
}
)HLSL";

static const char* kUnshadedPaletted = R"HLSL(
float4 ps_main(PSIN i) : COLOR0
{
	float4 index_color = tex2D(s_sampler0, i.tc);

	float index = 0.0;
#ifdef ENABLE_S_SAMPLER2
	index = tex2D(s_sampler2, float2(index_color.r, i.tc.y)).r;
#else
	index = index_color.r;
#endif
	index = floor((index * 255.0) + 0.5);

	float4 c = tex2D(s_sampler1, float2(index / 255.0, 0.0));

#ifdef ENABLE_U_SAMPLER0_IS_INDEXED_ALPHA
	if (u_s0idx.x != 0.0)
		c = c * float4(1.0, 1.0, 1.0, index_color.a) * i.col;
	else
		c = c * i.col;
#else
	c = c * i.col;
#endif

	if (c.a < 0.01)
		discard;
	return c;
}
)HLSL";

static const char* kSharpBilinear = R"HLSL(
#define SCALE 4.0
float4 ps_main(PSIN i) : COLOR0
{
	float2 texel = i.tc * u_s0size.xy;
	float2 texel_floored = floor(texel);
	float2 s = frac(texel);
	float region_range = 0.5 - 0.5 / SCALE;
	float2 center_dist = s - float2(0.5, 0.5);
	float2 f = (center_dist - clamp(center_dist, -region_range, region_range)) * SCALE + 0.5;
	float2 mod_texel = texel_floored + f;

	float4 c = tex2D(s_sampler0, mod_texel.xy / u_s0size.xy) * i.col;
	if (c.a < 0.01)
		discard;
	return c;
}
)HLSL";

static const char* kPostimg = R"HLSL(
float4 ps_main(PSIN i) : COLOR0
{
	float2 texcoord0 = i.tc;
	float2 uvsize = u_tc0_max.xy - u_tc0_min.xy;
	float time = u_time.x;

	float reclamp = 0.0;
	if (u_pi_water.x > 0.0)
	{
		texcoord0.x += (sin((time / 35.0) * 2.0 + ((texcoord0.y - u_tc0_min.y) / uvsize.y) * 20.0) * 0.01);
		reclamp = 1.0;
	}
	if (u_pi_heat.x > 0.0)
	{
		texcoord0.x += (max(sin((time / 35.0) * 1.2 - ((texcoord0.y - u_tc0_min.y) / uvsize.y) * 5.0) - 0.995, 0.0));
		texcoord0.x -= (max(sin(((time + 24.0) / 35.0) * 0.2 - ((texcoord0.y - u_tc0_min.y) / uvsize.y) * 1.5) - 0.990, 0.0));
		reclamp = 1.0;
	}
	if (reclamp > 0.0)
		texcoord0 = max(min(texcoord0, u_tc0_max.xy - float2(0.0001, 0.0001)), u_tc0_min.xy);

#ifdef ENABLE_S_SAMPLER1
	float4 index_color = tex2D(s_sampler0, texcoord0);
	return tex2D(s_sampler1, float2(floor((index_color.r * 255.0) + 0.5) / 255.0, 0.0));
#else
	return tex2D(s_sampler0, texcoord0);
#endif
}
)HLSL";

// The wipe uses its own vertex shader (projection only, no modelview or texcoord transform).
static const char* kWipeVs = R"HLSL(
struct WVSIN
{
	float3 pos : POSITION0;
	float2 tc : TEXCOORD0;
};
VSOUT wipe_vs_main(WVSIN i)
{
	VSOUT o;
	o.pos = fix_clip(MUL4(u_proj, float4(i.pos, 1.0)));
	o.tc = i.tc;
	o.col = float4(1.0, 1.0, 1.0, 1.0);
	return o;
}
)HLSL";

static const char* kWipePs = R"HLSL(
float4 wipe_modulate(float4 start, float4 endc, float mask_value)
{
	return lerp(start, endc, mask_value);
}

float4 wipe_invert(float4 start, float mask_value)
{
	return lerp(start, float4(float3(1.0, 1.0, 1.0) - start.rgb, 1.0), mask_value);
}

float4 wipe_megadrive_to_black(float4 start, float mask_value)
{
	float fade_alpha = clamp(mask_value, 0.0, 1.0);
	float rsub = clamp(fade_alpha * 3.0, 0.0, 3.0);
	float r = start.r - rsub;
	float gsub = clamp(-r, 0.0, 3.0);
	float g = start.g - gsub;
	float bsub = clamp(-g, 0.0, 3.0);
	float b = start.b - bsub;
	return float4(clamp(r, 0.0, 1.0), clamp(g, 0.0, 1.0), clamp(b, 0.0, 1.0), 1.0);
}

float4 wipe_megadrive_to_white(float4 start, float mask_value)
{
	float fade_alpha = clamp(mask_value, 0.0, 1.0);
	float radd = clamp(fade_alpha * 3.0, 0.0, 3.0);
	float r = min(start.r + radd, 1.0);
	float gadd = max(radd - 1.0, 0.0);
	float g = min(start.g + gadd, 1.0);
	float badd = max(gadd - 1.0, 0.0);
	float b = min(start.b + badd, 1.0);
	return float4(clamp(r, 0.0, 1.0), clamp(g, 0.0, 1.0), clamp(b, 0.0, 1.0), 1.0);
}

float4 ps_main(PSIN i) : COLOR0
{
	float2 coord = i.tc;
	float wipe_encore_swizzle = u_wipe_swz.x;
	float y_baseres = floor(coord.y * 200.0);
	float x_phase = wipe_encore_swizzle * 128.0 + (y_baseres * wipe_encore_swizzle * 8.0);
	float screen_y_fac = glmod(-floor(y_baseres), 2.0) * 2.0 - 1.0;
	coord.x = coord.x + (screen_y_fac * sin(x_phase) * wipe_encore_swizzle * 8.0) / 320.0;

	float4 start = tex2D(s_sampler0, coord);
	float4 endc = tex2D(s_sampler1, coord);

	// Wipe mask: luminance values 0..32 (31 = full starting image)
	float fade_mask = 1.0 - (tex2D(s_sampler2, i.tc).r * 255.0 / 31.0);

	float4 final_color = float4(0.0, 0.0, 0.0, 1.0);
	float mode = u_wipe_mode.x;
	if (mode < 0.5)
		final_color = wipe_modulate(start, endc, fade_mask);
	else if (mode < 1.5)
		final_color = wipe_invert(start, fade_mask);
	else if (mode < 2.5)
		final_color = wipe_megadrive_to_black(start, fade_mask);
	else
		final_color = wipe_megadrive_to_white(start, fade_mask);
	return final_color;
}
)HLSL";

static const char* kCrtPs = R"HLSL(
#define CRT_WIDTH 320.0
#define CRT_HEIGHT 200.0
#ifdef CRTSHARP
#define CRT_DOTSCALE (CRT_WIDTH * 4.0)
#define BLUR_DIST 0.5
#define SMUDGE_DIST 0.35
#else
#define CRT_DOTSCALE (CRT_WIDTH * 2.0)
#define BLUR_DIST 1.5
#define SMUDGE_DIST 0.75
#endif
#define CRT_BRIGHT 1.0
#define CRT_CONTRAST 70.0
#define BLUR_PI 6.28318530718
#define BLUR_DIRS 9.0
#define BLUR_INC (BLUR_PI / BLUR_DIRS)
#define BLUR_DIR_SCALAR (9.0 / BLUR_DIRS)
#define BLUR_QUALITY 2.0
#define BLUR_QUALITY_INC (1.0 / BLUR_QUALITY)
#define SMUDGE_CONTRAST 0.65
#define SCANLINE_INTENSITY 0.8

float getColorBrightness(float3 color)
{
	return (0.299 * color.r) + (0.587 * color.g) + (0.114 * color.b);
}

float3 getImgColor(float2 inputUV, float2 imgSize)
{
	float2 imageUV = (floor((inputUV * imgSize) + 0.5)) / imgSize;
	float3 imageColor = tex2D(s_sampler0, imageUV).rgb;

	float contrastMod = clamp(getColorBrightness(imageColor) * CRT_CONTRAST, 0.0, 1.0);
	imageColor *= contrastMod;

	float2 scaleUV = float2(u_s0size.x / u_s0size.y, 1.0); // dot pattern with the correct aspect ratio
	float2 patternUV = inputUV * scaleUV * CRT_DOTSCALE;
	float3 patternColor = tex2D(s_sampler1, patternUV).rgb;

	return imageColor * patternColor * CRT_BRIGHT;
}

float3 blur(float2 uv, float2 imgSize, float blurScale)
{
	float3 acc = float3(0.0, 0.0, 0.0);
	[unroll] for (int di = 0; di < 9; di++)
	{
		float d = (float)di * BLUR_INC;
		[unroll] for (int ii = 1; ii <= 2; ii++)
		{
			float q = (float)ii * BLUR_QUALITY_INC;
			float2 blurOffset = float2(cos(d), sin(d) * 0.5) * (BLUR_DIST * blurScale) * q;
			float2 blurUV = uv + (blurOffset / imgSize.x);
			acc += getImgColor(blurUV, imgSize) * BLUR_QUALITY_INC * BLUR_DIR_SCALAR;
		}
	}
	return acc;
}

float4 ps_main(PSIN i) : COLOR0
{
	float2 resolution = u_s0size.xy;
	float2 imgSize = float2(((resolution.x * 400.0) / resolution.y), 400.0);
	float blurScale = 1920.0 / 1080.0;

	float2 uv = i.tc;

	// Base layer + blur
	float3 finalColor = getImgColor(uv, imgSize) + blur(uv, imgSize, blurScale);

	// "Smear" layer
	float2 overlayOffset = float2(-(SMUDGE_DIST * blurScale) / imgSize.x, -(SMUDGE_DIST * blurScale) / imgSize.y);
	float2 overlayUV = uv + overlayOffset;
	float3 overlayColor = getImgColor(overlayUV, imgSize) + blur(overlayUV, imgSize, blurScale);
	overlayColor *= (overlayColor * SMUDGE_CONTRAST);

	finalColor = lerp(finalColor, overlayColor, 0.5);

	// Scanlines
	float scanPos = glmod(uv.y * CRT_HEIGHT, 1.0);
	scanPos = clamp(scanPos, 0.0, 1.0);
	float scanline = abs(scanPos - 0.5) * 2.0;
	scanline *= 1.0 - getColorBrightness(finalColor);
	scanline = clamp(scanline, 0.0, 1.0);
	finalColor = lerp(finalColor, float3(0.0, 0.0, 0.0), scanline * SCANLINE_INTENSITY);

	return float4(finalColor, 1.0);
}
)HLSL";

bool d3d9_hlsl_source(const std::string& name, std::string& out_source, std::string& out_vs_entry, std::string& out_extra_define)
{
	out_extra_define.clear();
	out_vs_entry = "vs_main";
	out_source = kPrelude;
	if (name == "unshaded")
		out_source += kUnshaded;
	else if (name == "unshadedpaletted")
		out_source += kUnshadedPaletted;
	else if (name == "sharpbilinear")
		out_source += kSharpBilinear;
	else if (name == "postimg")
	{
		out_extra_define = "ENABLE_VA_TEXCOORD0"; // its GLSL always reads a_texcoord0 (the game doesn't pass that define)
		out_source += kPostimg;
	}
	else if (name == "postprocesswipe")
	{
		out_source += kWipeVs;
		out_source += kWipePs;
		out_vs_entry = "wipe_vs_main";
	}
	else if (name == "crt")
		out_source += kCrtPs;
	else if (name == "crtsharp")
	{
		out_extra_define = "CRTSHARP";
		out_source += kCrtPs;
	}
	else
		return false;
	return true;
}

} // namespace srb2::rhi

#endif // _WIN32
