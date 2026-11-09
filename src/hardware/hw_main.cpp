// DR. ROBOTNIK'S RING RACERS
//-----------------------------------------------------------------------------
// Copyright (C) 2025 by Kart Krew.
// Copyright (C) 2020 by Sonic Team Junior.
// Copyright (C) 2000 by DooM Legacy Team.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file hw_main.c
/// \brief hardware renderer, using the standard HardWareRender driver DLL for SRB2

#include <algorithm>
#include "../rr_config.h"  // test options in a single file
#include <math.h>

#include "../doomstat.h"

#ifdef HWRENDER
#include "hw_glob.h"
#include "hw_light.h"
#include "hw_drv.h"
#include "hw_batching.h"

#include "../i_video.h" // for rendermode == render_glide
#include "../v_video.h"
#include "../p_local.h"
#include "../p_setup.h"
#include "../r_fps.h"
#include "../r_local.h"
#include "../r_patch.h"
#include "../r_picformats.h"
#include "../r_bsp.h"
#include "../d_clisrv.h"
#include "../d_netcmd.h" // D_MapChange
#include "../w_wad.h"
#include "../z_zone.h"
#include "../r_splats.h"
#include "../g_game.h"
#include "../st_stuff.h"
#include "../i_system.h"
#include "../m_cheat.h"
#include "../f_finale.h"
#include "../r_things.h" // R_GetShadowZ
#include "../d_main.h"
#include "../p_slopes.h"
#include "hw_md2.h"

// SRB2Kart
#include "../k_hitlag.h" // HITLAGJITTERS
#include "../r_fps.h"
#include "../r_plane.h" // R_FlatDimensionsFromLumpSize

/// FINALLY some real clipping that doesn't make walls dissappear AND speeds the game up
/// (that was the original comment from SRB2CB, sadly it is a lie and actually slows game down)
/// on the bright side it fixes some weird issues with translucent walls
/// \note	SRB2CB port.
///      	SRB2CB itself ported this from PrBoom+
#include "hw_clip.h"

#define R_FAKEFLOORS
#define HWPRECIP
//#define POLYSKY

// ==========================================================================
// the hardware driver object
// ==========================================================================
struct hwdriver_s hwdriver;

// ==========================================================================
//                                                                     PROTOS
// ==========================================================================


static void HWR_AddSprites(sector_t *sec);
static void HWR_ProjectSprite(mobj_t *thing);
#ifdef HWPRECIP
static void HWR_AddPrecipitationSprites(void);
static void HWR_ProjectPrecipitationSprite(precipmobj_t *thing);
#endif
static void HWR_ProjectBoundingBox(mobj_t *thing);
static void HWR_RollTransform(FTransform *tr, angle_t roll);

void HWR_AddTransparentFloor(levelflat_t *levelflat, extrasubsector_t *xsub, boolean isceiling, fixed_t fixedheight, INT32 lightlevel, INT32 alpha, sector_t *FOFSector, FBITFIELD blend, boolean fogplane, extracolormap_t *planecolormap);
void HWR_AddTransparentPolyobjectFloor(levelflat_t *levelflat, polyobj_t *polysector, boolean isceiling, fixed_t fixedheight,
                             INT32 lightlevel, INT32 alpha, sector_t *FOFSector, FBITFIELD blend, extracolormap_t *planecolormap);

boolean drawsky = true;

// ==========================================================================
//                                                               VIEW GLOBALS
// ==========================================================================
// Fineangles in the SCREENWIDTH wide window.
#define FIELDOFVIEW ANGLE_90
#define ABS(x) ((x) < 0 ? -(x) : (x))

static angle_t gl_clipangle;

// The viewangletox[viewangle + FINEANGLES/4] lookup
// maps the visible view angles to screen X coordinates,
// flattening the arc to a flat projection plane.
// There will be many angles mapped to the same X.
static INT32 gl_viewangletox[FINEANGLES/2];

// The xtoviewangleangle[] table maps a screen pixel
// to the lowest viewangle that maps back to x ranges
// from clipangle to -clipangle.
static angle_t gl_xtoviewangle[MAXVIDWIDTH+1];

// ==========================================================================
//                                                                    GLOBALS
// ==========================================================================

// uncomment to remove the plane rendering
#define DOPLANES
//#define DOWALLS

// test of drawing sky by polygons like in software with visplane, unfortunately
// this doesn't work since we must have z for pixel and z for texture (not like now with z = oow)
//#define POLYSKY

// test change fov when looking up/down but bsp projection messup :(
//#define NOCRAPPYMLOOK

// base values set at SetViewSize
static float gl_basecentery;
static float gl_basecenterx;

float gl_baseviewwindowy, gl_basewindowcentery;
float gl_baseviewwindowx, gl_basewindowcenterx;
float gl_viewwidth, gl_viewheight; // viewport clipping boundaries (screen coords)
float gl_viewwindowx;

static float gl_centerx, gl_centery;
static float gl_viewwindowy; // top left corner of view window
static float gl_windowcenterx; // center of view window, for projection
static float gl_windowcentery;

static float gl_pspritexscale, gl_pspriteyscale;

static seg_t *gl_curline;
static side_t *gl_sidedef;
static line_t *gl_linedef;
static sector_t *gl_frontsector;
static sector_t *gl_backsector;

// --------------------------------------------------------------------------
//                                              STUFF FOR THE PROJECTION CODE
// --------------------------------------------------------------------------

FTransform atransform;
// duplicates of the main code, set after R_SetupFrame() passed them into sharedstruct,
// copied here for local use
static fixed_t dup_viewx, dup_viewy, dup_viewz;
static angle_t dup_viewangle;

static float gl_viewx, gl_viewy, gl_viewz;
float gl_viewsin, gl_viewcos;

// Maybe not necessary with the new T&L code (needs to be checked!)
static float gl_viewludsin, gl_viewludcos; // look up down kik test
static float gl_fovlud;

static angle_t gl_aimingangle;
static void HWR_SetTransformAiming(FTransform *trans, player_t *player, boolean skybox);

// Render stats
precise_t ps_hw_skyboxtime = 0;
precise_t ps_hw_nodesorttime = 0;
precise_t ps_hw_nodedrawtime = 0;
precise_t ps_hw_spritesorttime = 0;
precise_t ps_hw_spritedrawtime = 0;

// Render stats for batching
int ps_hw_numpolys = 0;
int ps_hw_numverts = 0;
int ps_hw_numcalls = 0;
int ps_hw_numshaders = 0;
int ps_hw_numtextures = 0;
int ps_hw_numpolyflags = 0;
int ps_hw_numcolors = 0;
precise_t ps_hw_batchsorttime = 0;
precise_t ps_hw_batchdrawtime = 0;

boolean gl_init = false;
boolean gl_maploaded = false;
boolean gl_sessioncommandsadded = false;
boolean gl_shadersavailable = true;

// ==========================================================================
// Lighting
// ==========================================================================


// Legacy GL changes that change the picture (12288 draw distance, flat sky, no horizon lines,
// half sprite contrast without shaders) are only used in Low-End Mode. Drawing only (not the
// simulation), so they also apply in replays and as soon as the option is changed.
#define RRE_GLLOWEND (rr_lowend || cv_rrlowend.value)

static boolean HWR_UseShader(void)
{
	return (cv_glshaders.value && gl_shadersavailable);
}
extern "C" boolean HWR_UseShaderPC(void) { return HWR_UseShader(); } // used by rr_staticgeo.cpp

boolean HWR_OverrideObjectLightLevel(mobj_t *thing, INT32 *lightlevel)
{
	if (R_ThingIsFullBright(thing))
		*lightlevel = 255;
	else if (R_ThingIsFullDark(thing))
		*lightlevel = 0;
	else if (thing->renderflags & RF_ABSOLUTELIGHTLEVEL)
		*lightlevel = R_ThingLightLevel(thing);
	else
		return false;

	return true;
}

void HWR_ObjectLightLevelPost(gl_vissprite_t *spr, const sector_t *sector, INT32 *lightlevel, boolean model)
{
	const boolean semibright = R_ThingIsSemiBright(spr->mobj);
	const boolean papersprite = R_ThingIsPaperSprite(spr->mobj);

	*lightlevel += R_ThingLightLevel(spr->mobj);

	if (maplighting.directional == true && P_SectorUsesDirectionalLighting(sector))
	{
		if (model == false) // this is implemented by shader
		{
			fixed_t extralight = R_GetSpriteDirectionalLighting(
				papersprite
				? spr->angle + (spr->flip ? -ANGLE_90 : ANGLE_90)
				: R_PointToAngle(spr->mobj->x, spr->mobj->y) // fixme
			);

			// Less change in contrast in dark sectors
			extralight = FixedMul(extralight, std::min<INT32>(std::max<INT32>(0, *lightlevel), 255) * FRACUNIT / 255);

			if (papersprite)
			{
				// Papersprite contrast should match walls
				*lightlevel += FixedFloor(extralight + (FRACUNIT / 2)) / FRACUNIT;
			}
			else
			{
				// simple OGL approximation
				fixed_t tr = R_PointToDist(spr->mobj->x, spr->mobj->y);
				fixed_t xscale = FixedDiv((vid.width / 2) << FRACBITS, tr);

				// Less change in contrast at further distances, to counteract DOOM diminished light
				fixed_t n = FixedDiv(FixedMul(xscale, LIGHTRESOLUTIONFIX), ((MAXLIGHTSCALE-1) << LIGHTSCALESHIFT));
				extralight = FixedMul(extralight, std::min<INT32>(n, FRACUNIT));

				// Contrast is stronger for normal sprites, stronger than wall lighting is at the same distance
				// Without shaders, low light (e.g. 200 -> 75) looks almost
				// black with the fallback formula (same issue the rings had).
				// Measured in Balconies: Sonic facing away from the light = 75.
				// Half the contrast: 200 -> ~138, shading still visible but not black.
				if (RRE_GLLOWEND)
					*lightlevel += FixedFloor(extralight + (FRACUNIT / 2)) / FRACUNIT;
				else
					*lightlevel += FixedFloor((extralight * 2) + (FRACUNIT / 2)) / FRACUNIT;
			}
		}

		// Semibright objects will be made slightly brighter to compensate contrast
		if (semibright)
		{
			*lightlevel += 16;
		}
	}

	if (semibright)
	{
		*lightlevel = 192 + (*lightlevel >> 1);
	}
}

void HWR_Lighting(FSurfaceInfo *Surface, INT32 light_level, extracolormap_t *colormap, const boolean directional)
{
	RGBA_t poly_color, tint_color, fade_color;

	poly_color.rgba = 0xFFFFFFFF;
	tint_color.rgba = (colormap != NULL) ? (UINT32)colormap->rgba : GL_DEFAULTMIX;
	fade_color.rgba = (colormap != NULL) ? (UINT32)colormap->fadergba : GL_DEFAULTFOG;

	// Crappy backup coloring if you can't do shaders
	if (!HWR_UseShader())
	{
		// be careful, this may get negative for high lightlevel values.
		float tint_alpha, fade_alpha;
		float red, green, blue;

		red = (float)poly_color.s.red;
		green = (float)poly_color.s.green;
		blue = (float)poly_color.s.blue;

		// 48 is just an arbritrary value that looked relatively okay.
		tint_alpha = (float)(sqrt(tint_color.s.alpha) * 48) / 255.0f;

		// 8 is roughly the brightness of the "close" color in Software, and 16 the brightness of the "far" color.
		// 8 is too bright for dark levels, and 16 is too dark for bright levels.
		// 12 is the compromise value. It doesn't look especially good anywhere, but it's the most balanced.
		// (Also, as far as I can tell, fade_color's alpha is actually not used in Software, so we only use light level.)
		// (clamp: with light > 255 the square root of a negative gave NaN -> color 0)
		fade_alpha = (float)(sqrt(std::max<INT32>(0, 255-light_level)) * 12) / 255.0f;

		// Clamp the alpha values
		tint_alpha = std::min(std::max(tint_alpha, 0.0f), 1.0f);
		fade_alpha = std::min(std::max(fade_alpha, 0.0f), 1.0f);

		red = (tint_color.s.red * tint_alpha) + (red * (1.0f - tint_alpha));
		green = (tint_color.s.green * tint_alpha) + (green * (1.0f - tint_alpha));
		blue = (tint_color.s.blue * tint_alpha) + (blue * (1.0f - tint_alpha));

		red = (fade_color.s.red * fade_alpha) + (red * (1.0f - fade_alpha));
		green = (fade_color.s.green * fade_alpha) + (green * (1.0f - fade_alpha));
		blue = (fade_color.s.blue * fade_alpha) + (blue * (1.0f - fade_alpha));

		poly_color.s.red = (UINT8)red;
		poly_color.s.green = (UINT8)green;
		poly_color.s.blue = (UINT8)blue;
	}

	// Clamp the light level, since it can sometimes go out of the 0-255 range from animations
	light_level = std::min<INT32>(std::max<INT32>(light_level, 0), 255);

	Surface->PolyColor.rgba = poly_color.rgba;
	Surface->TintColor.rgba = tint_color.rgba;
	Surface->FadeColor.rgba = fade_color.rgba;

	Surface->LightInfo.light_level = light_level;
	Surface->LightInfo.fade_start = (colormap != NULL) ? colormap->fadestart : 0;
	Surface->LightInfo.fade_end = (colormap != NULL) ? colormap->fadeend : 31;

	Surface->LightInfo.directional = (maplighting.directional == true && directional == true);
}

UINT8 HWR_FogBlockAlpha(INT32 light, extracolormap_t *colormap) // Let's see if this can work
{
	RGBA_t realcolor, surfcolor;
	INT32 alpha;

	realcolor.rgba = (colormap != NULL) ? colormap->rgba : GL_DEFAULTMIX;

	if (cv_glshaders.value && gl_shadersavailable)
	{
		surfcolor.s.alpha = (255 - light);
	}
	else
	{
		light = light - (255 - light);

		// Don't go out of bounds
		if (light < 0)
			light = 0;
		else if (light > 255)
			light = 255;

		alpha = (realcolor.s.alpha*255)/25;

		// at 255 brightness, alpha is between 0 and 127, at 0 brightness alpha will always be 255
		surfcolor.s.alpha = (alpha*light) / (2*256) + 255-light;
	}

	return surfcolor.s.alpha;
}

static FUINT HWR_CalcWallLight(FUINT lightnum, seg_t *seg)
{
	INT16 finallight = lightnum;

	if (seg != NULL && P_ApplyLightOffsetFine(lightnum, seg->frontsector))
	{
		finallight += seg->hwLightOffset;

		if (finallight > 255) finallight = 255;
		if (finallight < 0) finallight = 0;
	}

	return (FUINT)finallight;
}

static FUINT HWR_CalcSlopeLight(FUINT lightnum, pslope_t *slope, const sector_t *sector, const boolean fof)
{
	INT16 finallight = lightnum;

	if (slope != NULL && sector != NULL && P_ApplyLightOffsetFine(lightnum, sector))
	{
		finallight += (fof ? -slope->hwLightOffset : slope->hwLightOffset);

		if (finallight > 255) finallight = 255;
		if (finallight < 0) finallight = 0;
	}

	return (FUINT)finallight;
}

// ==========================================================================
//                                   FLOOR/CEILING GENERATION FROM SUBSECTORS
// ==========================================================================

#ifdef DOPLANES

// -----------------+
// HWR_RenderPlane  : Render a floor or ceiling convex polygon
// -----------------+
// DEBUG mode 7 (g_rre_dbgflat = 7): tints every polygon by where it
// comes from. red=floor, cyan=ceiling, magenta=FOF floor/ceiling,
// green=wall, dark green=translucent/FOF wall, blue=sprite,
// yellow=polyobject, orange=shadow.
extern "C" int g_rre_dbgflat;
static inline void dbg_tint(FSurfaceInfo *s, FBITFIELD *f, UINT8 r, UINT8 g, UINT8 b)
{
	if (g_rre_dbgflat != 7 || !s) return;
	s->PolyColor.s.red = r; s->PolyColor.s.green = g; s->PolyColor.s.blue = b;
	if (f) *f |= PF_Modulated;
}
#define DBG_TINT(s,f,r,g,b) dbg_tint(s,f,r,g,b)

// ==========================================================================
// STATIC GEOMETRY - world cache in GPU-side memory
// --------------------------------------------------------------------------
// The first time a subsector floor/ceiling or a wall (seg) is drawn it is
// "captured": the polygons sent by the normal path are copied into a persistent
// vertex pool (with lighting baked in as a per-vertex color).
// On the following frames, if the "signature" (heights, textures, light, offsets...)
// did not change, all of the HWR work is skipped and only indices are added.
// At the end of the BSP pass everything is drawn grouped by (texture, flags).
// Things that change often (>= 4 times) or are translucent stay on the normal path.
// Disable: "nostatic" test option (rr_config.h).
// ==========================================================================
#include <vector>
#include <string>
#include <csignal>
#include <unistd.h> // _exit
#ifdef _WIN32
// No windows.h here (it clashes with min/max/boolean): only what is needed from kernel32
#ifndef _WINDOWS_
extern "C" __declspec(dllimport) unsigned short __stdcall RtlCaptureStackBackTrace(unsigned long, unsigned long, void **, unsigned long *);
extern "C" __declspec(dllimport) void *__stdcall GetModuleHandleA(const char *);
#endif
#endif
#include <unordered_map>
#include <new>
extern "C" {
int  RRE_StaticAdd(const FOutVector* v, int n, unsigned int rgba);
int  RRE_StaticUsed(void);
void RRE_StaticTruncate(int used);
int  RRE_StaticReset(void);
int  RRE_StaticDraw(GLMipmap_t* mip, GLMipmap_t* bright, FBITFIELD flags, const UINT16* idx, int n);
void RRE_StaticEnd(void);
void RRE_StaticShiftST(int first, int n, float ds, float dt);
void RRE_StaticSetYT(int first, int n, const float *y, const float *t);
void RRE_StaticSetY(int first, int n, float y);
}
// Sky-to-texture cache: not implemented by this renderer (stubs).
static inline int  RRE_SkyTexOn(void) { return 0; }
static inline int  RRE_SkyTexDrawing(void) { return 0; }
static inline int  RRE_SkyTexNeedRedraw(void) { return 1; }
static inline int  RRE_SkyTexBegin(void) { return 0; }
static inline void RRE_SkyTexEnd(void) {}
static inline void RRE_SkyTexDraw(void) {}
static inline void RRE_SkyTexInvalidate(void) {}
static inline void RRE_SkyTexWhy(int* a, int* b, int* c, float* d, float* e) { *a = *b = *c = 0; *d = *e = 0; }
static boolean rre_skyQuad = false; // this frame the sky comes from the texture
static int rre_skyDrawn = 0, rre_skyReused = 0;
extern GLMapTexture_t *rre_lastgrtex; // hw_cache.cpp
GLMipmap_t *rre_lastmip = NULL; // set by HWR_SetCurrentTexture (hw_batching.cpp)
GLMipmap_t *rre_lastbright = NULL; // brightmap of rre_lastmip (or NULL)
struct rre_texreq_t { int kind; INT32 a, b; void *p; }; // same as in hw_cache.cpp
extern rre_texreq_t rre_texreq; // hw_cache.cpp
extern mobj_t *rre_sprmobj; // hw_cache.cpp

static int RRE_ViewNow(void);
namespace rre_static
{
enum { ST_NONE = 0, ST_VALID = 1, ST_NEVER = 2 };
struct SItem  { UINT32 sig; UINT32 gen; UINT32 firstPoly; UINT16 numPolys; UINT8 state; UINT8 changes; UINT8 why; UINT8 hasOff; fixed_t to, ro;
	UINT32 sigH; UINT8 kmask; UINT8 patchOk; UINT32 fofMask;
	UINT16 stable; UINT8 strikes; }; // heights kept apart (simple walls); "never" items are retried if their signature holds still
struct SPoly  { UINT16 base; UINT8 n; UINT8 pad; UINT16 group; float sx, sy; };
struct SGroup { GLMipmap_t *mip; FBITFIELD flags; std::vector<UINT16> idx; rre_texreq_t req; GLMipmap_t *bright; };

static std::vector<SItem>  items;     // [numsubsectors*2 (floor/ceiling) + numsegs]
enum { RRE_MAXPOLYS = 16384, RRE_MAXGROUPS = 512 };
static std::vector<SPoly>  polys;
static std::vector<SGroup> groups;
static std::unordered_map<UINT64, UINT16> groupMap;
static std::vector<UINT32> secSig, secStamp;
static UINT32 gen = 1, stamp = 1;
// Per wall, what was already decided (type + signature). Reused if the FULL signature
// of its sectors (computed once per sector and pass) and its side/line data did not change.
// Gives EXACTLY the same sig/sigH/curSimple as recomputing (the full signature covers everything).
struct SegMemo { UINT32 fSig, bSig, sKey, sig, sigH; UINT32 fs, bs; UINT8 ok, simple; };
static std::vector<SegMemo> segMemo;
static int stMemoHit = 0, stMemoMiss = 0;
static boolean enabled = true, needReset = true;
static size_t segBase = 0;

static boolean capActive = false, capFail = false, capNever = false;
static size_t capItem = 0, capPoly0 = 0;
static int capVert0 = 0;

static int stHits = 0, stCapt = 0, stNever = 0, stResets = 0, stDrawFail = 0, stFrames = 0;
// Why an item is "never": 1 changes a lot, 2 translucent wall, 3 flags (fog/horizon/no depth), 4 no texture, 5 >255 vertices
static int stWhyPlane = 0, stWhyWall = 0;
static int stWhy[6] = {0}; static UINT8 capWhy = 0; static int stAnim = 0;

static inline UINT32 H(UINT32 h, UINT32 v) { h ^= v; return h * 16777619u; }
static inline UINT32 HP(UINT32 h, const void *p) { return H(h, (UINT32)(uintptr_t)p); }

static UINT32 SlopeSig(UINT32 h, const pslope_t *s)
{
	if (!s) return H(h, 0x51u);
	h = H(h, (UINT32)s->o.z);
	h = H(h, (UINT32)s->normal.x); h = H(h, (UINT32)s->normal.y); h = H(h, (UINT32)s->normal.z);
	return h;
}

static UINT32 FlatSig(UINT32 h, INT32 pic)
{
	if (pic < 0 || (size_t)pic >= numlevelflats) return H(h, (UINT32)pic);
	const levelflat_t *lf = &levelflats[pic];
	h = H(h, lf->type);
	// ANIMATED flats (RBTCPR01, RBCSLI02, road edges...): signature uses the BASE frame;
	// the current frame is picked at draw time (RRE_StaticFrameDraw, kind 2). Before, every
	// frame change marked the floor and all neighbouring walls as "never".
	if (lf->speed && lf->type == LEVELFLAT_TEXTURE && lf->u.texture.basenum != -1)
		h = H(h, (UINT32)lf->u.texture.basenum);
	else if (lf->speed && lf->type == LEVELFLAT_FLAT && lf->u.flat.baselumpnum != LUMPERROR)
		h = H(h, (UINT32)lf->u.flat.baselumpnum);
	else
		h = H(h, (UINT32)lf->u.texture.num); // (or u.flat.lumpnum: same offset)
	return h;
}

// Current offset of the wall being processed (for scrolling walls)
static boolean curHasOff = false; static fixed_t curTo = 0, curRo = 0;
static int stShift = 0;
// Simple wall (2-sided, no slopes/lights/FOFs): heights can be patched in place
static boolean curSimple = false; static UINT32 curSigH = 0; static UINT8 curKind = 0; static float curPlaneY = 0.0f;
static int stPatch = 0, stReshape = 0, stRetry = 0; // stRetry = "never" items that go back to the cache
// [CULPRIT2] named culprits: items that are never cached because they keep changing
static std::unordered_map<UINT64, UINT32> culp2;
// Expensive diagnostics (CULPRIT2/3/4, [SUB], [CHG]) only with the "diag" test option.
// Without it the game (and benchmarks) run clean.
static int diag = -1;
static inline boolean Diag(void)
{
	if (diag < 0)
	{
		diag = 0;
		diag = RRCfg_On("diag");
		RR_DIAGPRINT("[DIAG] diagnostics %s\n", diag ? "ON" : "OFF");
	}
	return diag != 0;
}
} // namespace rre_static (paused)
extern "C" int RRE_DiagOn(void) { return rre_static::Diag() ? 1 : 0; } // used by p_tick.c
namespace rre_static {
static inline boolean IsNeverChg(size_t item) { return item < items.size() && items[item].state == ST_NEVER && items[item].why == 1; }
static inline void Culp2(UINT32 sec, UINT32 texA, UINT32 kind) { culp2[((UINT64)sec << 32) | ((UINT64)(kind & 0xF) << 28) | (texA & 0x0FFFFFFFu)]++; }
// [CULPRIT3] "never" floors: which field changes from one frame to the next (bits, see report)
struct PSnap { INT32 pic, ll, fll; const void *cm; UINT32 fl, slz, sln; fixed_t xo, yo, h; angle_t an; UINT32 sf; const void *sl; UINT32 sig, sigH, view; const void *ecm; UINT8 ok; };
static std::vector<PSnap> psnap;
static std::unordered_map<UINT32, std::pair<UINT32, UINT32>> culp3; // sector -> (bits, count)
// [CULPRIT4] "never" walls: which part of the signature changes. bits 1 front_sector 2 back_sector 4 side 8 line 16 heights
struct WSnap { UINT32 c[14]; UINT8 ok; }; // 14 parts
static std::vector<WSnap> wsnap;
static std::unordered_map<UINT64, std::pair<UINT32, UINT32>> culp4; // (front<<32|back) -> (bits, count)
static void Culp4(size_t seg, UINT32 fsec, UINT32 bsec, const UINT32 *c)
{
	if (wsnap.size() != numsegs) wsnap.assign(numsegs, WSnap{});
	if (seg >= wsnap.size()) return;
	WSnap &o = wsnap[seg];
	if (o.ok)
	{
		UINT32 b = 0;
		for (int i = 0; i < 14; i++) b |= (o.c[i] != c[i]) ? (1u << i) : 0u;
		auto &e = culp4[((UINT64)fsec << 32) | bsec]; e.first |= b; e.second++;
	}
	for (int i = 0; i < 14; i++) o.c[i] = c[i];
	o.ok = 1;
}
static void Culp3(size_t sub, const sector_t *s, INT32 fll, const void *cm, UINT32 fl, fixed_t h, UINT32 sig, UINT32 sigH)
{
	if (psnap.size() != numsubsectors) psnap.assign(numsubsectors, PSnap{});
	if (sub >= psnap.size()) return;
	PSnap n; n.pic = s->floorpic; n.ll = s->lightlevel; n.fll = fll; n.cm = cm; n.fl = fl;
	n.sl = s->f_slope; n.slz = s->f_slope ? (UINT32)s->f_slope->o.z : 0; n.sln = s->f_slope ? (UINT32)(s->f_slope->normal.x ^ s->f_slope->normal.y ^ s->f_slope->normal.z) : 0;
	n.xo = s->floor_xoffs ^ s->floor_yoffs; n.yo = 0; n.an = s->floorpic_angle; n.sf = (UINT32)s->flags; n.h = h; n.ok = 1;
	n.sig = sig; n.sigH = sigH; n.view = (UINT32)::RRE_ViewNow(); n.ecm = s->extra_colormap;
	PSnap &o = psnap[sub];
	if (o.ok)
	{
		UINT32 b = 0;
		b |= (o.pic != n.pic) ? 1u : 0u; b |= (o.ll != n.ll) ? 2u : 0u; b |= (o.fll != n.fll) ? 4u : 0u; b |= (o.cm != n.cm) ? 8u : 0u;
		b |= (o.fl != n.fl) ? 16u : 0u; b |= (o.slz != n.slz) ? 32u : 0u; b |= (o.sln != n.sln) ? 64u : 0u; b |= (o.xo != n.xo) ? 128u : 0u;
		b |= (o.an != n.an) ? 256u : 0u; b |= (o.sf != n.sf) ? 512u : 0u; b |= (o.h != n.h) ? 1024u : 0u; b |= (o.sl != n.sl) ? 2048u : 0u;
		b |= (o.sig != n.sig) ? 4096u : 0u; b |= (o.sigH != n.sigH) ? 8192u : 0u; b |= (o.view != n.view) ? 16384u : 0u; b |= (o.ecm != n.ecm) ? 32768u : 0u;
		auto &e = culp3[(UINT32)(s - sectors)]; e.first |= b; e.second++;
	}
	o = n;
}
} // namespace rre_static (paused)
static boolean RRE_PatchSegHeights(const rre_static::SItem &it);
static boolean RRE_PatchHeights(size_t item, const rre_static::SItem &it);
namespace rre_static {
static UINT32 SideSig(UINT32 h, const side_t *sd, boolean withOffsets = true)
{
	if (!sd) return H(h, 0x5Du);
	if (withOffsets) h = H(h, (UINT32)sd->textureoffset);
	h = H(h, (UINT32)sd->rowoffset); // vertical: can clip the geometry (midtexture), stays in the signature
	// BASE texture (without the animation frame): animated walls
	// stay cached and the current frame is picked at draw time (RRE_StaticFrameDraw).
	h = H(h, (UINT32)sd->toptexture);
	h = H(h, (UINT32)sd->midtexture);
	h = H(h, (UINT32)sd->bottomtexture);
	h = H(h, (UINT32)sd->repeatcnt);
	return h;
}

// mode 0 = base (floors/ceilings), 1 = one-sided wall (lights, no FOFs), 2 = full
static UINT32 SectorSigRaw(const sector_t *s, int mode = 2)
{
	UINT32 h = 2166136261u;
	// +16 = FLOOR fields only, +32 = CEILING fields only (for planes: a ceiling that
	// moves or scrolls no longer marks the floor of the same sector as "never", and vice versa)
	const boolean onlyF = (mode & 16) != 0, onlyC = (mode & 32) != 0;
	if (!(mode & 4)) { if (!onlyC) h = H(h, (UINT32)s->floorheight); if (!onlyF) h = H(h, (UINT32)s->ceilingheight); }
	const boolean noFofH = (mode & 8) != 0; // no FOF heights and no light list
	mode &= 3;
	if (onlyF || onlyC)
	{
		const boolean f = onlyF;
		h = H(h, (UINT32)(f ? s->floorpic : s->ceilingpic)); h = FlatSig(h, f ? s->floorpic : s->ceilingpic);
		h = H(h, (UINT32)s->lightlevel);
		h = H(h, (UINT32)(f ? s->floorlightlevel : s->ceilinglightlevel));
		h = H(h, (UINT32)(f ? s->floorlightabsolute : s->ceilinglightabsolute));
		const INT32 ls = f ? s->floorlightsec : s->ceilinglightsec; h = H(h, (UINT32)ls);
		if (ls >= 0 && (size_t)ls < numsectors) h = H(h, (UINT32)sectors[ls].lightlevel);
		h = HP(h, s->extra_colormap);
		h = H(h, (UINT32)(f ? s->floor_xoffs : s->ceiling_xoffs)); h = H(h, (UINT32)(f ? s->floor_yoffs : s->ceiling_yoffs));
		h = H(h, (UINT32)(f ? s->floorpic_angle : s->ceilingpic_angle));
		h = H(h, (UINT32)s->flags);
		h = SlopeSig(h, f ? s->f_slope : s->c_slope);
		return h;
	}
	h = H(h, (UINT32)s->floorpic);    h = H(h, (UINT32)s->ceilingpic);
	h = FlatSig(h, s->floorpic);      h = FlatSig(h, s->ceilingpic);
	h = H(h, (UINT32)s->lightlevel);
	h = H(h, (UINT32)s->floorlightlevel); h = H(h, (UINT32)s->ceilinglightlevel);
	h = H(h, (UINT32)s->floorlightabsolute | ((UINT32)s->ceilinglightabsolute << 1));
	h = H(h, (UINT32)s->floorlightsec); h = H(h, (UINT32)s->ceilinglightsec);
	if (s->floorlightsec >= 0 && (size_t)s->floorlightsec < numsectors) h = H(h, (UINT32)sectors[s->floorlightsec].lightlevel);
	if (s->ceilinglightsec >= 0 && (size_t)s->ceilinglightsec < numsectors) h = H(h, (UINT32)sectors[s->ceilinglightsec].lightlevel);
	h = HP(h, s->extra_colormap);
	h = H(h, (UINT32)s->floor_xoffs); h = H(h, (UINT32)s->floor_yoffs);
	h = H(h, (UINT32)s->ceiling_xoffs); h = H(h, (UINT32)s->ceiling_yoffs);
	h = H(h, (UINT32)s->floorpic_angle); h = H(h, (UINT32)s->ceilingpic_angle);
	h = H(h, (UINT32)s->flags);
	h = SlopeSig(h, s->f_slope); h = SlopeSig(h, s->c_slope);
	if (mode == 0) return h; // floors/ceilings: light and colormap are already in their own signature
	// Only the light bands that change something visible (another light/colormap)
	// or that clip the wall (CUTSOLIDS/CUTEXTRA). The others split the wall
	// into identical pieces: moving them changes nothing on screen.
	{
		UINT32 pl = 0xFFFFFFFFu; const extracolormap_t *pc = (const extracolormap_t *)(uintptr_t)1;
		for (INT32 i = 0; !noFofH && i < s->numlights && s->lightlist; i++)
		{
			const lightlist_t *l = &s->lightlist[i];
			const boolean cut = (l->flags & (FOF_CUTSOLIDS|FOF_CUTEXTRA)) != 0;
			boolean vis = false;
			if (!(l->flags & FOF_NOSHADE))
			{
				const UINT32 ll = l->lightlevel ? (UINT32)*l->lightlevel : 0xFFFFu;
				const extracolormap_t *lc = l->extra_colormap ? *l->extra_colormap : NULL;
				if (ll != pl || lc != pc) { vis = true; pl = ll; pc = lc; }
			}
			if (!cut && !vis) continue;
			h = H(h, (UINT32)l->height); h = H(h, (UINT32)l->flags);
			h = H(h, pl); h = HP(h, pc);
			h = SlopeSig(h, l->slope);
			if (cut && l->caster) h = H(h, l->caster->bottomheight ? (UINT32)*l->caster->bottomheight : 0);
		}
	}
	if (mode == 1) return h; // one-sided wall: FOFs do not draw walls here
	for (const ffloor_t *r = s->ffloors; r; r = r->next)
	{
		h = H(h, (UINT32)r->fofflags); h = H(h, (UINT32)r->alpha); h = H(h, (UINT32)r->blend);
		if (!noFofH)
		{
			h = H(h, r->topheight ? (UINT32)*r->topheight : 0);
			h = H(h, r->bottomheight ? (UINT32)*r->bottomheight : 0);
		}
		h = H(h, r->toppic ? (UINT32)*r->toppic : 0);
		h = H(h, r->bottompic ? (UINT32)*r->bottompic : 0);
		h = H(h, r->toplightlevel ? (UINT32)*r->toplightlevel : 0);
		h = SlopeSig(h, r->t_slope ? *r->t_slope : NULL);
		h = SlopeSig(h, r->b_slope ? *r->b_slope : NULL);
		if (r->master && r->master->sidenum[0] != 0xffff)
			h = SideSig(h, &sides[r->master->sidenum[0]]);
	}
	return h;
}

// Signature of a sector, once per render pass.
static std::vector<UINT32> secSigB, secStampB;
// Signature without FOFs or light list (for the sector's floors/ceilings).
[[maybe_unused]] static UINT32 SectorSigBase(const sector_t *s)
{
	const size_t i = (size_t)(s - sectors);
	if (i >= secSigB.size())
		return SectorSigRaw(s, 0);
	if (secStampB[i] != stamp) { secStampB[i] = stamp; secSigB[i] = SectorSigRaw(s, 0); }
	return secSigB[i];
}
static std::vector<UINT32> secSigW, secStampW;
// Signature for one-sided walls (no FOFs)
static UINT32 SectorSigWall1(const sector_t *s)
{
	const size_t i = (size_t)(s - sectors);
	if (i >= secSigW.size())
		return SectorSigRaw(s, 1);
	if (secStampW[i] != stamp) { secStampW[i] = stamp; secSigW[i] = SectorSigRaw(s, 1); }
	return secSigW[i];
}
static std::vector<UINT32> secSigN, secStampN;
// Full signature WITHOUT floor/ceiling heights (simple walls: heights get patched)
static UINT32 SectorSigNoH(const sector_t *s)
{
	const size_t i = (size_t)(s - sectors);
	if (i >= secSigN.size())
		return SectorSigRaw(s, 2 | 4);
	if (secStampN[i] != stamp) { secStampN[i] = stamp; secSigN[i] = SectorSigRaw(s, 2 | 4); }
	return secSigN[i];
}
static UINT32 SectorSig(const sector_t *s)
{
	const size_t i = (size_t)(s - sectors);
	if (i >= secSig.size())
		return SectorSigRaw(s);
	if (secStamp[i] != stamp) { secStamp[i] = stamp; secSig[i] = SectorSigRaw(s); }
	return secSig[i];
}

static inline void Emit(const SItem &it)
{
	for (UINT32 p = it.firstPoly; p < it.firstPoly + it.numPolys; p++)
	{
		const SPoly &sp = polys[p];
		std::vector<UINT16> &ix = groups[sp.group].idx;
		for (UINT16 k = 1; k + 1 < sp.n; k++)
		{
			ix.push_back(sp.base);
			ix.push_back((UINT16)(sp.base + k));
			ix.push_back((UINT16)(sp.base + k + 1));
		}
	}
}

// true = already drawn from the cache (skip the normal path).
// false = draw normally (capturing it, if applicable).
static boolean Use(size_t item, UINT32 sig)
{
	if (!enabled || item >= items.size() || capActive)
		return false;
	SItem &it = items[item];
	if (it.state == ST_NEVER)
	{
		// [CULPRIT3] showed "never" floors whose signature NO LONGER changes (they were marked
		// when the race started). If the signature holds for 60<<strikes frames in a row, cache it again.
		if (it.why == 1)
		{
			if (it.sig == sig && it.sigH == curSigH)
			{
				const UINT32 need = 60u << (it.strikes < 4 ? it.strikes : 4);
				if (++it.stable >= need)
				{
					if (it.strikes < 255) it.strikes++;
					it.stable = 0; it.changes = 0; stRetry++;
					goto recapture;
				}
			}
			else { it.sig = sig; it.sigH = curSigH; it.stable = 0; }
		}
		stWhy[it.why < 6 ? it.why : 0]++;
		if (it.why == 1) { if (item < segBase) stWhyPlane++; else stWhyWall++; }
		return false;
	}
	if (it.state == ST_VALID && it.gen == gen)
	{
		boolean hOk = true;
		if (it.sig == sig && it.sigH != curSigH)
		{
			hOk = false;
			// Only floor/ceiling heights changed: patch y/t without recomputing
			if (it.patchOk && curSimple && RRE_PatchHeights(item, it)) { it.sigH = curSigH; stPatch++; hOk = true; }
			else if (it.patchOk && curSimple) { stReshape++; goto recapture; } // a piece appears/disappears: recapture without counting a change
		}
		if (it.sig == sig && hOk)
		{
			if (it.hasOff && curHasOff && (it.to != curTo || it.ro != curRo))
			{
				const float dto = (float)(curTo - it.to), dro = (float)(curRo - it.ro);
				for (UINT32 p = it.firstPoly; p < it.firstPoly + it.numPolys; p++)
				{
					const SPoly &sp = polys[p];
					if (sp.sx != 0.0f || sp.sy != 0.0f)
						RRE_StaticShiftST(sp.base, sp.n, dto * sp.sx, dro * sp.sy);
				}
				it.to = curTo; it.ro = curRo; stShift++;
			}
			Emit(it); stHits++; return true;
		}
		if (++it.changes >= 4) { it.state = ST_NEVER; it.why = 1; stNever++; return false; }
	}
recapture:
	it.state = ST_NONE;
	it.sig = sig;
	it.sigH = curSigH; it.kmask = 0; it.fofMask = 0; it.patchOk = curSimple ? 1 : 0;
	capActive = true; capFail = false; capNever = false; capWhy = 0;
	capItem = item; capPoly0 = polys.size(); capVert0 = RRE_StaticUsed();
	return false;
}

static void EndCapture(void)
{
	if (!capActive)
		return;
	capActive = false;
	SItem &it = items[capItem];
	if (capNever || capFail)
	{
		polys.resize(capPoly0);
		RRE_StaticTruncate(capVert0);
		if (capNever) { it.state = ST_NEVER; it.why = capWhy; stNever++; }
		else needReset = true; // pool full: flush and recapture
		return;
	}
	it.state = ST_VALID; it.gen = gen;
	it.hasOff = curHasOff ? 1 : 0; it.to = curTo; it.ro = curRo;
	it.firstPoly = (UINT32)capPoly0;
	it.numPolys = (UINT16)(polys.size() - capPoly0);
	stCapt++;
}

static void Never(void) { if (capActive) { capNever = true; if (!capWhy) capWhy = 2; } }

static boolean SectorOk(const sector_t *s) { return !s || s->heightsec == -1; }
} // namespace rre_static

// Called by HWR_ProcessPolygon (hw_batching.cpp) for every polygon.
void RRE_StaticCapturePoly(FSurfaceInfo *surf, FOutVector *v, FUINT n, FBITFIELD flags, boolean horizon)
{
	using namespace rre_static;
	if (!capActive)
		return;
	if (horizon || n < 3 || n > 255 || (flags & (PF_Fog | PF_NoDepthTest))
		|| (flags & PF_EnvironmentTrans)) // note: PF_Blending includes PF_Invert == PF_Occlude
	{
		capNever = true;
		if (!capWhy) capWhy = (n > 255) ? 5 : 3;
		return;
	}
	// Zero-area polygon (e.g. a wall of height 0 from slope rounding): skipped.
	// Invisible anyway.
	{
		double ax = 0, ay = 0, az = 0;
		for (FUINT i = 1; i + 1 < n; i++)
		{
			const double ux = v[i].x - v[0].x, uy = v[i].y - v[0].y, uz = v[i].z - v[0].z;
			const double wx = v[i+1].x - v[0].x, wy = v[i+1].y - v[0].y, wz = v[i+1].z - v[0].z;
			ax += uy*wz - uz*wy; ay += uz*wx - ux*wz; az += ux*wy - uy*wx;
		}
		if (ax*ax + ay*ay + az*az < 1.0) return; // area < 0.5 (0.001-wide slivers)
	}
	GLMipmap_t *mip = (flags & PF_NoTexture) ? NULL : rre_lastmip;
	if (!mip && !(flags & PF_NoTexture)) { capNever = true; if (!capWhy) capWhy = 4; return; }
	// rgb = tint; the alpha byte carries the LIGHT LEVEL (the shader does the
	// distance darkening). Colored fade (colored fog): not cached
	// (the normal path handles it correctly).
	if (surf && (flags & PF_Modulated) && (surf->FadeColor.rgba & 0x00FFFFFFu))
	{
		capNever = true;
		if (!capWhy) capWhy = 3;
		return;
	}
	// The cache draws with per-vertex light (alpha) and the default black fade; with
	// shaders the tint goes in a uniform (not in PolyColor). Anything that does not fit: normal path.
	if (surf
		&& (surf->TintColor.s.alpha != 0 || surf->FadeColor.rgba != GL_DEFAULTFOG
		|| surf->LightInfo.fade_start != 0 || surf->LightInfo.fade_end != 31
		|| surf->LightInfo.directional || surf->PolyColor.s.alpha != 0xFF || (flags & PF_Ripple)))
	{
		capNever = true;
		if (!capWhy) capWhy = 3;
		return;
	}
	const UINT32 col = (surf && (flags & (PF_Modulated | PF_NoTexture)))
		? ((surf->PolyColor.rgba & 0x00FFFFFFu) | ((UINT32)(surf->LightInfo.light_level & 0xFF) << 24))
		: 0xFFFFFFFFu;
	// FIXED memory: polys/groups are reserved at map load and never grow
	// (growing asks for a contiguous block twice as big, which can fail on a fragmented
	// heap -> std::bad_alloc mid-race). Full = flush and recapture.
	if (polys.size() >= polys.capacity() || groups.size() >= groups.capacity()) { capFail = true; return; }
	const int base = RRE_StaticAdd(v, (int)n, col);
	if (base < 0) { capFail = true; return; }
	// Wall textures are grouped by BASE texture (animated ones included).
	const boolean byBase = (mip && rre_texreq.kind == 1 && rre_texreq.b >= 0 && rre_texreq.b < numtextures
		&& R_GetTextureNum(rre_texreq.b) == rre_texreq.a);
	const UINT64 key = byBase ? (((UINT64)(UINT32)(rre_texreq.b + 1) << 32) | (UINT32)flags)
		: (((UINT64)(uintptr_t)mip << 32) | (UINT32)flags);
	UINT16 g;
	auto f = groupMap.find(key);
	if (f == groupMap.end())
	{
		if (groups.size() >= 65535) { capFail = true; return; }
		g = (UINT16)groups.size();
		groups.push_back(SGroup{ mip, flags, {}, rre_texreq, mip ? rre_lastbright : NULL });
		groupMap.emplace(key, g);
	}
	else g = f->second;
	float sx = 0.0f, sy = 0.0f;
	if (rre_texreq.kind == 1 && rre_lastgrtex && mip == &rre_lastgrtex->mipmap)
		{ sx = rre_lastgrtex->scaleX; sy = rre_lastgrtex->scaleY; }
	else if (byBase && rre_lastgrtex)
		{ sx = rre_lastgrtex->scaleX; sy = rre_lastgrtex->scaleY; }
	polys.push_back(SPoly{ (UINT16)base, (UINT8)n, curKind, g, sx, sy });
	if (curSimple)
	{
		SItem &ci = items[capItem];
		if (curKind >= 8) // FOF wall (from the back sector), bit = FOF order
		{
			const UINT32 b = 1u << (curKind - 8);
			if (n != 4 || sy == 0.0f || (ci.fofMask & b)) ci.patchOk = 0;
			ci.fofMask |= b;
		}
		else
		{
			if (curKind == 0 || (curKind != 6 && n != 4) || (curKind <= 3 && sy == 0.0f) || (curKind != 6 && (ci.kmask & (1u << curKind)))) ci.patchOk = 0;
			ci.kmask |= (UINT8)(1u << curKind);
		}
	}
}


// Diagnostics: which sector/side fields change over time.
namespace rre_chg {
	struct SecSnap { fixed_t fh, ch, fx, fy, cx, cy; INT16 light; INT32 fpic, cpic; void *cmap; INT32 nl; angle_t fa, ca; };
	struct SideSnap { fixed_t to, ro; INT32 t, m, b; };
	static std::vector<SecSnap> sec; static std::vector<SideSnap> sid;
	static int frames = 0;
	static SecSnap Snap(const sector_t *s) { return SecSnap{ s->floorheight, s->ceilingheight, s->floor_xoffs, s->floor_yoffs, s->ceiling_xoffs, s->ceiling_yoffs, s->lightlevel, s->floorpic, s->ceilingpic, s->extra_colormap, s->numlights, s->floorpic_angle, s->ceilingpic_angle }; }
	static SideSnap SnapS(const side_t *d) { return SideSnap{ d->textureoffset, d->rowoffset, d->toptexture, d->midtexture, d->bottomtexture }; }
	void Tick(void)
	{
		if (sec.size() != numsectors || sid.size() != numsides)
		{
			sec.resize(numsectors); sid.resize(numsides);
			for (size_t i = 0; i < numsectors; i++) sec[i] = Snap(&sectors[i]);
			for (size_t i = 0; i < numsides; i++) sid[i] = SnapS(&sides[i]);
			frames = 0; return;
		}
		if (++frames < 60) return;
		frames = 0;
		int c[12] = {0};
		for (size_t i = 0; i < numsectors; i++)
		{
			SecSnap n = Snap(&sectors[i]), &o = sec[i];
			c[0] += n.fh != o.fh; c[1] += n.ch != o.ch; c[2] += (n.fx != o.fx || n.fy != o.fy); c[3] += (n.cx != o.cx || n.cy != o.cy);
			c[4] += n.light != o.light; c[5] += (n.fpic != o.fpic || n.cpic != o.cpic); c[6] += n.cmap != o.cmap; c[7] += n.nl != o.nl;
			c[8] += (n.fa != o.fa || n.ca != o.ca);
			o = n;
		}
		for (size_t i = 0; i < numsides; i++)
		{
			SideSnap n = SnapS(&sides[i]), &o = sid[i];
			c[9] += (n.to != o.to || n.ro != o.ro); c[10] += (n.t != o.t || n.m != o.m || n.b != o.b);
			o = n;
		}
		RR_DIAGPRINT("[CHG] 60f sectors: floor_h=%d ceil_h=%d floor_ofs=%d ceil_ofs=%d light=%d flat=%d colormap=%d lights=%d angle=%d | sides: ofs=%d texture=%d\n",
			c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7], c[8], c[9], c[10]);
		RR_DIAGPRINT("[STATIC] never(changes) planes=%d walls=%d\n", rre_static::stWhyPlane, rre_static::stWhyWall);
			{
				// [CULPRIT2] once per map (3rd report): top 25 by count/60f, with texture name
				static int rep2 = 0; static INT16 map2 = -1;
				if (gamemap != map2) { map2 = gamemap; rep2 = 0; rre_static::culp2.clear(); }
				if (++rep2 == 3)
				{
					std::vector<std::pair<UINT32, UINT64>> v;
					for (auto &kv : rre_static::culp2) v.push_back({kv.second, kv.first});
					std::sort(v.begin(), v.end(), [](const std::pair<UINT32, UINT64> &a, const std::pair<UINT32, UINT64> &b){ return a.first > b.first; });
					for (size_t i = 0; i < v.size() && i < 25; i++)
					{
						const UINT32 sec = (UINT32)(v[i].second >> 32), kind = (UINT32)(v[i].second >> 28) & 0xF;
						const INT32 tx = (INT32)(v[i].second & 0x0FFFFFFFu);
						char nm[9] = "?";
						if (kind == 1) { if (tx > 0 && tx < numtextures && textures[tx]) { memcpy(nm, textures[tx]->name, 8); nm[8] = 0; } else strcpy(nm, "(none)"); }
						else if (tx >= 0 && (size_t)tx < numlevelflats) { memcpy(nm, levelflats[tx].name, 8); nm[8] = 0; }
						const sector_t *sc = sec < numsectors ? &sectors[sec] : NULL;
						RR_DIAGPRINT("[CULPRIT2] %s sector %u tag %d texture=%s count=%u\n", kind == 1 ? "wall" : (kind == 2 ? "floor" : "ceiling"),
							(unsigned)sec, (sc && sc->tags.count) ? (int)sc->tags.tags[0] : 0, nm, (unsigned)v[i].first);
					}
					RR_DIAGPRINT("[CULPRIT2] total distinct=%u\n", (unsigned)v.size());
					// [CULPRIT3] bits: 1 flat 2 sector_light 4 floor_light 8 colormap 16 blend 32 slope_z 64 slope_normal 128 offset 256 angle 512 flags 1024 height 2048 slope_ptr
					std::vector<std::pair<UINT32, UINT32>> w;
					for (auto &kv : rre_static::culp3) w.push_back({kv.second.second, kv.first});
					std::sort(w.begin(), w.end(), [](const std::pair<UINT32, UINT32> &a, const std::pair<UINT32, UINT32> &b){ return a.first > b.first; });
					for (size_t i = 0; i < w.size() && i < 10; i++)
						RR_DIAGPRINT("[CULPRIT3] floor sector %u bits=%u count=%u\n", (unsigned)w[i].second, (unsigned)rre_static::culp3[w[i].second].first, (unsigned)w[i].first);
					// [CULPRIT4] bits: 1 front_sector 2 back_sector 4 side 8 line 16 heights
					std::vector<std::pair<UINT32, UINT64>> w4;
					for (auto &kv : rre_static::culp4) w4.push_back({kv.second.second, kv.first});
					std::sort(w4.begin(), w4.end(), [](const std::pair<UINT32, UINT64> &a, const std::pair<UINT32, UINT64> &b){ return a.first > b.first; });
					for (size_t i = 0; i < w4.size() && i < 30; i++)
						RR_DIAGPRINT("[CULPRIT4] wall front %u back %u bits=%u count=%u\n", (unsigned)(w4[i].second >> 32), (unsigned)(w4[i].second & 0xFFFFFFFFu),
							(unsigned)rre_static::culp4[w4[i].second].first, (unsigned)w4[i].first);
				}
				rre_static::culp2.clear(); rre_static::culp3.clear(); rre_static::culp4.clear();
			}
		rre_static::stWhyPlane = rre_static::stWhyWall = 0;
	}
}
// HWR_LoadLevel
static void RRE_StaticLevelInit(void)
{
	using namespace rre_static;
	enabled = !RRCfg_On("nostatic");
	// Also in Low-End mode with OpenGL (drawn by r_opengl.cpp DrawStaticPool).
	if (!(rr_lowend && rendermode == render_opengl))
		enabled = false;
	segBase = numsubsectors * 2;
	try { items.assign(segBase + numsegs, SItem{ 0, 0, 0, 0, ST_NONE, 0, 0, 0, 0, 0, 0, 0, 0, 0 }); }
	catch (const std::bad_alloc &) // out of memory -> no cache (slower, but no crash)
	{
		decltype(items)().swap(items); enabled = false;
		I_OutputMsg("[STATIC] OUT OF MEMORY for the cache (%u items): cache OFF\n", (unsigned)(segBase + numsegs));
	}
	secSig.assign(numsectors, 0);
	secStamp.assign(numsectors, 0);
	try { segMemo.assign(numsegs, SegMemo{}); } // ~36 B x segs, linear memory
	catch (const std::bad_alloc &) { decltype(segMemo)().swap(segMemo); I_OutputMsg("[STATIC] out of memory for segMemo: memo OFF\n"); }
	secSigB.assign(numsectors, 0);
	secStampB.assign(numsectors, 0);
	secSigW.assign(numsectors, 0);
	secStampW.assign(numsectors, 0);
	secSigN.assign(numsectors, 0);
	secStampN.assign(numsectors, 0);
	// Fixed reservation (see AddPoly). ~16 B per poly; the maximum seen was ~11.6k.
	try {
	if (polys.capacity() < RRE_MAXPOLYS) { decltype(polys)().swap(polys); polys.reserve(RRE_MAXPOLYS); }
	if (groups.capacity() < RRE_MAXGROUPS) { decltype(groups)().swap(groups); groups.reserve(RRE_MAXGROUPS); }
	} catch (const std::bad_alloc &) { enabled = false; I_OutputMsg("[STATIC] OUT OF MEMORY for polys/groups: cache OFF\n"); }
	groupMap.reserve(RRE_MAXGROUPS);
	needReset = true;
	RR_DIAGPRINT("[STATIC] map: %u subsectors, %u segs, cache %s\n",
		(unsigned)numsubsectors, (unsigned)numsegs, enabled ? "ON" : "OFF");
}

#define RRE_TEXTRANSP(t) ((t)->mipmap.flags & TF_TRANSPARENT)

// Before HWR_RenderBSPNode
static void RRE_StaticFrameBegin(void)
{
	using namespace rre_static;
	stamp++;
	if (needReset && RRE_StaticReset())
	{
		needReset = false;
		gen++;
		polys.clear();
		groups.clear();
		groupMap.clear();
		stResets++;
	}
}

static inline bool RRE_LfValid(const void *p)
{
	const levelflat_t *lf = (const levelflat_t *)p;
	return levelflats && lf >= levelflats && lf < levelflats + numlevelflats;
}

// After the BSP (and HWR_RenderBatches): draws what is in the cache.
static void RRE_StaticFrameDraw(void)
{
	using namespace rre_static;
	EndCapture();
	boolean any = false;
	for (SGroup &g : groups)
	{
		if (g.idx.empty())
			continue;
		any = true;
		// Animated texture: use the current frame.
		if (g.req.kind == 1 && g.req.b >= 0 && g.req.b < numtextures && g.mip && !(g.flags & PF_NoTexture))
		{
			const INT32 cur = R_GetTextureNum(g.req.b);
			if (cur != g.req.a && cur >= 0 && cur < numtextures)
			{
				GLMapTexture_t *t = HWR_GetTexture(cur, g.req.b);
				if (t) { g.mip = &t->mipmap; g.req.a = cur; stAnim++; }
			}
		}
		// Animated flat: use the current frame.
		// The stored pointer can go stale if levelflats is reallocated
		// (this crashed once): only use it if it is still inside the array.
		if (g.req.kind == 2 && g.req.p && !RRE_LfValid(g.req.p))
		{
			g.req.p = NULL;
			needReset = true; // recapture everything with fresh pointers
		}
		if (g.req.kind == 2 && g.req.p && g.mip && !(g.flags & PF_NoTexture))
		{
			levelflat_t *lf = (levelflat_t *)g.req.p;
			if (lf->speed && (lf->type == LEVELFLAT_TEXTURE || lf->type == LEVELFLAT_FLAT))
			{
				const INT32 cur = lf->type == LEVELFLAT_TEXTURE ? lf->u.texture.num : (INT32)lf->u.flat.lumpnum;
				if (cur != g.req.a)
				{
					rre_lastmip = NULL;
					HWR_GetLevelFlat(lf, (boolean)g.req.b);
					if (rre_lastmip) { g.mip = rre_lastmip; g.req.a = cur; stAnim++; }
				}
			}
		}
		// If the RAM copy was purged and the GPU evicted it, regenerate it
		// through the same path that requested it (otherwise: empty/white frame).
		if (g.mip && !(g.flags & PF_NoTexture) && !g.mip->downloaded && !g.mip->data)
		{
			switch (g.req.kind)
			{
				case 1: HWR_GetTexture(g.req.a, g.req.b); break;
				case 2: if (g.req.p) HWR_GetLevelFlat((levelflat_t *)g.req.p, (boolean)g.req.b); break;
				case 3: HWR_GetRawFlat((lumpnum_t)g.req.a, (boolean)g.req.b); break;
				default: break;
			}
		}
		const int r = RRE_StaticDraw(g.mip, g.bright, g.flags, g.idx.data(), (int)g.idx.size());
		if (r == 0) { needReset = true; stDrawFail++; } // texture lost: recapture everything
		else if (r < 0) stDrawFail++;
		g.idx.clear();
	}
	if (any)
		RRE_StaticEnd();

	if (++stFrames >= 30)
	{
		RR_DIAGPRINT("[STATIC] 30f: used=%d captured=%d never=%d flushed=%d failed=%d | pool=%d verts, %u polys, %u groups\n",
			stHits, stCapt, stNever, stResets, stDrawFail, RRE_StaticUsed(),
			(unsigned)polys.size(), (unsigned)groups.size());
		RR_DIAGPRINT("[STATIC] never used/30f: changes=%d translucent=%d flags=%d notex=%d big=%d other=%d\n",
			stWhy[1], stWhy[2], stWhy[3], stWhy[4], stWhy[5], stWhy[0]);
		RR_DIAGPRINT("[STATIC] animated frame changes/30f: %d  scrolled walls/30f: %d  patched heights/30f: %d  shape recaptures/30f: %d  never retries/30f: %d\n", stAnim, stShift, stPatch, stReshape, stRetry); stAnim = 0; stShift = 0; stPatch = 0; stReshape = 0; stRetry = 0;
		for (int w = 0; w < 6; w++) stWhy[w] = 0;
		stHits = stCapt = stNever = stResets = stDrawFail = stFrames = 0;
	}
}

static void HWR_RenderPlane(subsector_t *subsector, extrasubsector_t *xsub, boolean isceiling, fixed_t fixedheight, FBITFIELD PolyFlags, INT32 lightlevel, levelflat_t *levelflat, sector_t *FOFsector, UINT8 alpha, extracolormap_t *planecolormap)
{
	polyvertex_t *  pv;
	float           height; //constant y for all points on the convex flat polygon
	FOutVector      *v3d;
	INT32             nrPlaneVerts;   //verts original define of convex flat polygon
	INT32             i;
	float           flatxref,flatyref;
	float fflatwidth = 64.0f, fflatheight = 64.0f;
	INT32 flatflag = 63;
	boolean texflat = false;
	float scrollx = 0.0f, scrolly = 0.0f, anglef = 0.0f;
	angle_t angle = 0;
	FSurfaceInfo    Surf;
	float tempxsow, tempytow;
	pslope_t *slope = NULL;

	static FOutVector *planeVerts = NULL;
	static UINT16 numAllocedPlaneVerts = 0;

	INT32 shader = SHADER_DEFAULT;

	// no convex poly were generated for this subsector
	if (!xsub->planepoly)
		return;

	// Get the slope pointer to simplify future code
	if (FOFsector)
	{
		if (FOFsector->f_slope && !isceiling)
			slope = FOFsector->f_slope;
		else if (FOFsector->c_slope && isceiling)
			slope = FOFsector->c_slope;
	}
	else
	{
		if (gl_frontsector->f_slope && !isceiling)
			slope = gl_frontsector->f_slope;
		else if (gl_frontsector->c_slope && isceiling)
			slope = gl_frontsector->c_slope;
	}

	// Set fixedheight to the slope's height from our viewpoint, if we have a slope
	if (slope)
		fixedheight = P_GetSlopeZAt(slope, viewx, viewy);

	height = FIXED_TO_FLOAT(fixedheight);

	pv  = xsub->planepoly->pts;
	nrPlaneVerts = xsub->planepoly->numpts;

	if (nrPlaneVerts < 3)   //not even a triangle ?
		return;

	// Allocate plane-vertex buffer if we need to
	if (!planeVerts || nrPlaneVerts > numAllocedPlaneVerts)
	{
		numAllocedPlaneVerts = (UINT16)nrPlaneVerts;
		Z_Free(planeVerts);
		Z_Malloc(numAllocedPlaneVerts * sizeof (FOutVector), PU_LEVEL, &planeVerts);
	}

	// set texture for polygon
	if (levelflat != NULL)
	{
		if (levelflat->type == LEVELFLAT_FLAT)
		{
			size_t len = W_LumpLength(levelflat->u.flat.lumpnum);
			size_t sflatsize = R_FlatDimensionsFromLumpSize(len);
			fflatwidth = fflatheight = (double)sflatsize;
			flatflag = sflatsize-1;
		}
		else
		{
			if (levelflat->type == LEVELFLAT_TEXTURE)
			{
				fflatwidth = textures[levelflat->u.texture.num]->width;
				fflatheight = textures[levelflat->u.texture.num]->height;
			}
			else if (levelflat->type == LEVELFLAT_PATCH || levelflat->type == LEVELFLAT_PNG)
			{
				fflatwidth = levelflat->width;
				fflatheight = levelflat->height;
			}
			texflat = true;
		}
	}
	else // set no texture
		HWR_SetCurrentTexture(NULL);

	// reference point for flat texture coord for each vertex around the polygon
	flatxref = (float)(((fixed_t)pv->x & (~flatflag)) / fflatwidth);
	flatyref = (float)(((fixed_t)pv->y & (~flatflag)) / fflatheight);

	// transform
	if (FOFsector != NULL)
	{
		if (!isceiling) // it's a floor
		{
			scrollx = FIXED_TO_FLOAT(FOFsector->floor_xoffs)/fflatwidth;
			scrolly = FIXED_TO_FLOAT(FOFsector->floor_yoffs)/fflatheight;
			angle = FOFsector->floorpic_angle;
		}
		else // it's a ceiling
		{
			scrollx = FIXED_TO_FLOAT(FOFsector->ceiling_xoffs)/fflatwidth;
			scrolly = FIXED_TO_FLOAT(FOFsector->ceiling_yoffs)/fflatheight;
			angle = FOFsector->ceilingpic_angle;
		}
	}
	else if (gl_frontsector)
	{
		if (!isceiling) // it's a floor
		{
			scrollx = FIXED_TO_FLOAT(gl_frontsector->floor_xoffs)/fflatwidth;
			scrolly = FIXED_TO_FLOAT(gl_frontsector->floor_yoffs)/fflatheight;
			angle = gl_frontsector->floorpic_angle;
		}
		else // it's a ceiling
		{
			scrollx = FIXED_TO_FLOAT(gl_frontsector->ceiling_xoffs)/fflatwidth;
			scrolly = FIXED_TO_FLOAT(gl_frontsector->ceiling_yoffs)/fflatheight;
			angle = gl_frontsector->ceilingpic_angle;
		}
	}


	if (angle) // Only needs to be done if there's an altered angle
	{
		tempxsow = flatxref;
		tempytow = flatyref;

		anglef = ANG2RAD(InvAngle(angle));

		flatxref = (tempxsow * cos(anglef)) - (tempytow * sin(anglef));
		flatyref = (tempxsow * sin(anglef)) + (tempytow * cos(anglef));
	}

#define SETUP3DVERT(vert, vx, vy) {\
		/* Hurdler: add scrolling texture on floor/ceiling */\
		if (texflat)\
		{\
			vert->s = (float)((vx) / fflatwidth) + scrollx;\
			vert->t = -(float)((vy) / fflatheight) + scrolly;\
		}\
		else\
		{\
			vert->s = (float)(((vx) / fflatwidth) - flatxref + scrollx);\
			vert->t = (float)(flatyref - ((vy) / fflatheight) + scrolly);\
		}\
\
		/* Need to rotate before translate */\
		if (angle) /* Only needs to be done if there's an altered angle */\
		{\
			tempxsow = vert->s;\
			tempytow = vert->t;\
			vert->s = (tempxsow * cos(anglef)) - (tempytow * sin(anglef));\
			vert->t = (tempxsow * sin(anglef)) + (tempytow * cos(anglef));\
		}\
\
		vert->x = (vx);\
		vert->y = height;\
		vert->z = (vy);\
\
		if (slope)\
		{\
			fixedheight = P_GetSlopeZAt(slope, FLOAT_TO_FIXED((vx)), FLOAT_TO_FIXED((vy)));\
			vert->y = FIXED_TO_FLOAT(fixedheight);\
		}\
}

	for (i = 0, v3d = planeVerts; i < nrPlaneVerts; i++,v3d++,pv++)
		SETUP3DVERT(v3d, pv->x, pv->y);

	lightlevel = HWR_CalcSlopeLight(lightlevel, slope, gl_frontsector, (FOFsector != NULL));
	HWR_Lighting(&Surf, lightlevel, planecolormap, P_SectorUsesDirectionalLighting(gl_frontsector));

	if (PolyFlags & PF_EnvironmentTrans)
	{
		Surf.PolyColor.s.alpha = (UINT8)alpha;
		PolyFlags |= PF_Modulated;
	}
	else
		PolyFlags |= PF_Masked|PF_Modulated;

	if (HWR_UseShader())
	{
		if (PolyFlags & PF_Fog)
			shader = SHADER_FOG;
		else if (PolyFlags & PF_Ripple)
			shader = SHADER_WATER;
		else
			shader = SHADER_FLOOR;

		PolyFlags |= PF_ColorMapped;
	}

	if (FOFsector) DBG_TINT(&Surf, &PolyFlags, 255, 0, 255);
	else if (isceiling) DBG_TINT(&Surf, &PolyFlags, 0, 255, 255);
	else DBG_TINT(&Surf, &PolyFlags, 255, 0, 0);
	HWR_ProcessPolygon(&Surf, planeVerts, nrPlaneVerts, PolyFlags, shader, false);

	// Low-End: horizon lines are disabled: they raised the floor up to the
	// camera height (a huge plane covering the track).
	if (!RRE_GLLOWEND && subsector)
	{
		// Horizon lines
		FOutVector horizonpts[6];
		float dist, vx, vy;
		float x1, y1, xd, yd;
		UINT8 numplanes, j;
		vertex_t v; // For determining the closest distance from the line to the camera, to split render planes for minimum distortion;

		const float renderdist = 27000.0f; // How far out to properly render the plane
		const float farrenderdist = 32768.0f; // From here, raise plane to horizon level to fill in the line with some texture distortion

		seg_t *line = &segs[subsector->firstline];

		for (i = 0; i < subsector->numlines; i++, line++)
		{
			if (!line->glseg && line->linedef->special == HORIZONSPECIAL && R_PointOnSegSide(dup_viewx, dup_viewy, line) == 0)
			{
				P_ClosestPointOnLine(viewx, viewy, line->linedef, &v);
				dist = FIXED_TO_FLOAT(R_PointToDist(v.x, v.y));

				x1 = ((polyvertex_t *)line->pv1)->x;
				y1 = ((polyvertex_t *)line->pv1)->y;
				xd = ((polyvertex_t *)line->pv2)->x - x1;
				yd = ((polyvertex_t *)line->pv2)->y - y1;

				// Based on the seg length and the distance from the line, split horizon into multiple poly sets to reduce distortion
				dist = sqrtf((xd*xd) + (yd*yd)) / dist / 16.0f;
				if (dist > 100.0f)
					numplanes = 100;
				else
					numplanes = (UINT8)dist + 1;

				for (j = 0; j < numplanes; j++)
				{
					// Left side
					vx = x1 + xd * j / numplanes;
					vy = y1 + yd * j / numplanes;
					SETUP3DVERT((&horizonpts[1]), vx, vy);

					dist = sqrtf(powf(vx - gl_viewx, 2) + powf(vy - gl_viewy, 2));
					vx = (vx - gl_viewx) * renderdist / dist + gl_viewx;
					vy = (vy - gl_viewy) * renderdist / dist + gl_viewy;
					SETUP3DVERT((&horizonpts[0]), vx, vy);

					// Right side
					vx = x1 + xd * (j+1) / numplanes;
					vy = y1 + yd * (j+1) / numplanes;
					SETUP3DVERT((&horizonpts[2]), vx, vy);

					dist = sqrtf(powf(vx - gl_viewx, 2) + powf(vy - gl_viewy, 2));
					vx = (vx - gl_viewx) * renderdist / dist + gl_viewx;
					vy = (vy - gl_viewy) * renderdist / dist + gl_viewy;
					SETUP3DVERT((&horizonpts[3]), vx, vy);

					// Horizon fills
					vx = (horizonpts[0].x - gl_viewx) * farrenderdist / renderdist + gl_viewx;
					vy = (horizonpts[0].z - gl_viewy) * farrenderdist / renderdist + gl_viewy;
					SETUP3DVERT((&horizonpts[5]), vx, vy);
					horizonpts[5].y = gl_viewz;

					vx = (horizonpts[3].x - gl_viewx) * farrenderdist / renderdist + gl_viewx;
					vy = (horizonpts[3].z - gl_viewy) * farrenderdist / renderdist + gl_viewy;
					SETUP3DVERT((&horizonpts[4]), vx, vy);
					horizonpts[4].y = gl_viewz;

					// Draw
					HWR_ProcessPolygon(&Surf, horizonpts, 6, PolyFlags, shader, true);
				}
			}
		}
	}

#ifdef ALAM_LIGHTING
	// add here code for dynamic lighting on planes
	HWR_PlaneLighting(planeVerts, nrPlaneVerts);
#endif
}

#ifdef POLYSKY
// this don't draw anything it only update the z-buffer so there isn't problem with
// wall/things upper that sky (map12)
static void HWR_RenderSkyPlane(extrasubsector_t *xsub, fixed_t fixedheight)
{
	polyvertex_t *pv;
	float height; //constant y for all points on the convex flat polygon
	FOutVector *v3d;
	INT32 nrPlaneVerts;   //verts original define of convex flat polygon
	INT32 i;

	// no convex poly were generated for this subsector
	if (!xsub->planepoly)
		return;

	height = FIXED_TO_FLOAT(fixedheight);

	pv  = xsub->planepoly->pts;
	nrPlaneVerts = xsub->planepoly->numpts;

	if (nrPlaneVerts < 3) // not even a triangle?
		return;

	if (nrPlaneVerts > MAXPLANEVERTICES) // FIXME: exceeds plVerts size
	{
		CONS_Debug(DBG_RENDER, "polygon size of %d exceeds max value of %d vertices\n", nrPlaneVerts, MAXPLANEVERTICES);
		return;
	}

	// transform
	v3d = planeVerts;
	for (i = 0; i < nrPlaneVerts; i++,v3d++,pv++)
	{
		v3d->s = 0.0f;
		v3d->t = 0.0f;
		v3d->x = pv->x;
		v3d->y = height;
		v3d->z = pv->y;
	}

	HWD.pfnDrawPolygon(NULL, planeVerts, nrPlaneVerts, PF_Invisible|PF_NoTexture|PF_Occlude);
}
#endif //polysky

#endif //doplanes

FBITFIELD HWR_GetBlendModeFlag(INT32 ast)
{
	switch (ast)
	{
		//case AST_COPY: -- intentionally defaults to translucent
		case AST_OVERLAY:
			return PF_Masked;
		case AST_ADD:
			return PF_Additive;
		case AST_SUBTRACT:
			return PF_ReverseSubtract;
		case AST_REVERSESUBTRACT:
			return PF_Subtractive;
		case AST_MODULATE:
			return PF_Multiplicative;
		default:
			return PF_Translucent;
	}

	return 0;
}

UINT8 HWR_GetTranstableAlpha(INT32 transtablenum)
{
	transtablenum = std::max<INT32>(std::min<INT32>(transtablenum, (INT32)tr_trans90), 0);

	switch (transtablenum)
	{
		case 0          : return 0xff;
		case tr_trans10 : return 0xe6;
		case tr_trans20 : return 0xcc;
		case tr_trans30 : return 0xb3;
		case tr_trans40 : return 0x99;
		case tr_trans50 : return 0x80;
		case tr_trans60 : return 0x66;
		case tr_trans70 : return 0x4c;
		case tr_trans80 : return 0x33;
		case tr_trans90 : return 0x19;
	}

	return 0xff;
}

FBITFIELD HWR_SurfaceBlend(INT32 style, INT32 transtablenum, FSurfaceInfo *pSurf)
{
	pSurf->PolyColor.s.alpha = 0xff;

	if (style == AST_MODULATE)
		return PF_Multiplicative;

	if (!transtablenum && !style)
		return PF_Masked;

	pSurf->PolyColor.s.alpha = HWR_GetTranstableAlpha(transtablenum);
	return HWR_GetBlendModeFlag(style);
}

FBITFIELD HWR_TranstableToAlpha(INT32 transtablenum, FSurfaceInfo *pSurf)
{
	if (!transtablenum)
	{
		pSurf->PolyColor.s.alpha = 0x00;
		return PF_Masked;
	}

	pSurf->PolyColor.s.alpha = HWR_GetTranstableAlpha(transtablenum);
	return PF_Translucent;
}

static void HWR_AddTransparentWall(FOutVector *wallVerts, FSurfaceInfo *pSurf, INT32 texnum, INT32 basetexnum, FBITFIELD blend, boolean fogwall, INT32 lightlevel, extracolormap_t *wallcolormap);

// ==========================================================================
// Wall generation from subsector segs
// ==========================================================================

/*
   wallVerts order is :
		3--2
		| /|
		|/ |
		0--1
*/

//
// HWR_ProjectWall
//
static void HWR_ProjectWall(FOutVector *wallVerts, FSurfaceInfo *pSurf, FBITFIELD blendmode, INT32 lightlevel, extracolormap_t *wallcolormap)
{
	INT32 shader = SHADER_DEFAULT;

	HWR_Lighting(pSurf, lightlevel, wallcolormap, P_SectorUsesDirectionalLighting(gl_frontsector));

	if (HWR_UseShader())
	{
		shader = SHADER_WALL;
		blendmode |= PF_ColorMapped;
	}

	DBG_TINT(pSurf, &blendmode, 0, 255, 0);
	HWR_ProcessPolygon(pSurf, wallVerts, 4, blendmode|PF_Modulated|PF_Occlude, shader, false);
}

// ==========================================================================
//                                                          BSP, CULL, ETC..
// ==========================================================================

//
// HWR_SplitWall
//
static void HWR_SplitWall(sector_t *sector, FOutVector *wallVerts, INT32 texnum, INT32 basetexnum, FSurfaceInfo* Surf, INT32 cutflag, ffloor_t *pfloor, FBITFIELD polyflags)
{
	/* SoM: split up and light walls according to the
	 lightlist. This may also include leaving out parts
	 of the wall that can't be seen */

	float realtop, realbot, top, bot;
	float pegt, pegb, pegmul;
	float height = 0.0f, bheight = 0.0f;

	float endrealtop, endrealbot, endtop, endbot;
	float endpegt, endpegb, endpegmul;
	float endheight = 0.0f, endbheight = 0.0f;

	// compiler complains when P_GetSlopeZAt is used in FLOAT_TO_FIXED directly
	// use this as a temp var to store P_GetSlopeZAt's return value each time
	fixed_t temp;

	fixed_t v1x = FLOAT_TO_FIXED(wallVerts[0].x);
	fixed_t v1y = FLOAT_TO_FIXED(wallVerts[0].z); // not a typo
	fixed_t v2x = FLOAT_TO_FIXED(wallVerts[1].x);
	fixed_t v2y = FLOAT_TO_FIXED(wallVerts[1].z); // not a typo

	INT32 solid, i;
	lightlist_t *  list = sector->lightlist;
	const UINT8 alpha = Surf->PolyColor.s.alpha;
	FUINT lightnum = sector->lightlevel;
	extracolormap_t *colormap = NULL;

	realtop = top = wallVerts[3].y;
	realbot = bot = wallVerts[0].y;
	pegt = wallVerts[3].t;
	pegb = wallVerts[0].t;
	pegmul = (pegb - pegt) / (top - bot);

	endrealtop = endtop = wallVerts[2].y;
	endrealbot = endbot = wallVerts[1].y;
	endpegt = wallVerts[2].t;
	endpegb = wallVerts[1].t;
	endpegmul = (endpegb - endpegt) / (endtop - endbot);

	for (i = 0; i < sector->numlights; i++)
	{
		if (endtop < endrealbot && top < realbot)
			return;

		if (!(list[i].flags & FOF_NOSHADE))
		{
			if (pfloor && (pfloor->fofflags & FOF_FOG))
			{
				lightnum = pfloor->master->frontsector->lightlevel;
				colormap = pfloor->master->frontsector->extra_colormap;
			}
			else
			{
				lightnum = *list[i].lightlevel;
				colormap = *list[i].extra_colormap;
			}
		}

		solid = false;

		if ((sector->lightlist[i].flags & FOF_CUTSOLIDS) && !(cutflag & FOF_EXTRA))
			solid = true;
		else if ((sector->lightlist[i].flags & FOF_CUTEXTRA) && (cutflag & FOF_EXTRA))
		{
			if (sector->lightlist[i].flags & FOF_EXTRA)
			{
				if ((sector->lightlist[i].flags & (FOF_FOG|FOF_SWIMMABLE)) == (cutflag & (FOF_FOG|FOF_SWIMMABLE))) // Only merge with your own types
					solid = true;
			}
			else
				solid = true;
		}
		else
			solid = false;

		temp = P_GetLightZAt(&list[i], v1x, v1y);
		height = FIXED_TO_FLOAT(temp);
		temp = P_GetLightZAt(&list[i], v2x, v2y);
		endheight = FIXED_TO_FLOAT(temp);
		if (solid)
		{
			temp = P_GetFFloorBottomZAt(list[i].caster, v1x, v1y);
			bheight = FIXED_TO_FLOAT(temp);
			temp = P_GetFFloorBottomZAt(list[i].caster, v2x, v2y);
			endbheight = FIXED_TO_FLOAT(temp);
		}

		if (endheight >= endtop && height >= top)
		{
			if (solid && top > bheight)
				top = bheight;
			if (solid && endtop > endbheight)
				endtop = endbheight;
		}

		if (i + 1 < sector->numlights)
		{
			temp = P_GetLightZAt(&list[i+1], v1x, v1y);
			bheight = FIXED_TO_FLOAT(temp);
			temp = P_GetLightZAt(&list[i+1], v2x, v2y);
			endbheight = FIXED_TO_FLOAT(temp);
		}
		else
		{
			bheight = realbot;
			endbheight = endrealbot;
		}

		if (endbheight >= endtop && bheight >= top)
			continue;

		//Found a break;
		bot = bheight;

		if (bot < realbot)
			bot = realbot;

		endbot = endbheight;

		if (endbot < endrealbot)
			endbot = endrealbot;

		Surf->PolyColor.s.alpha = alpha;

		wallVerts[3].t = pegt + ((realtop - top) * pegmul);
		wallVerts[2].t = endpegt + ((endrealtop - endtop) * endpegmul);
		wallVerts[0].t = pegt + ((realtop - bot) * pegmul);
		wallVerts[1].t = endpegt + ((endrealtop - endbot) * endpegmul);

		// set top/bottom coords
		wallVerts[3].y = top;
		wallVerts[2].y = endtop;
		wallVerts[0].y = bot;
		wallVerts[1].y = endbot;

		if (polyflags & PF_Fog)
			HWR_AddTransparentWall(wallVerts, Surf, texnum, basetexnum, polyflags, true, HWR_CalcWallLight(lightnum, gl_curline), colormap);
		else if (polyflags & PF_EnvironmentTrans)
			HWR_AddTransparentWall(wallVerts, Surf, texnum, basetexnum, polyflags, false, HWR_CalcWallLight(lightnum, gl_curline), colormap);
		else
			HWR_ProjectWall(wallVerts, Surf, polyflags, HWR_CalcWallLight(lightnum, gl_curline), colormap);

		top = bot;
		endtop = endbot;
	}

	bot = realbot;
	endbot = endrealbot;
	if (endtop <= endrealbot && top <= realbot)
		return;

	Surf->PolyColor.s.alpha = alpha;

	wallVerts[3].t = pegt + ((realtop - top) * pegmul);
	wallVerts[2].t = endpegt + ((endrealtop - endtop) * endpegmul);
	wallVerts[0].t = pegt + ((realtop - bot) * pegmul);
	wallVerts[1].t = endpegt + ((endrealtop - endbot) * endpegmul);

	// set top/bottom coords
	wallVerts[3].y = top;
	wallVerts[2].y = endtop;
	wallVerts[0].y = bot;
	wallVerts[1].y = endbot;

	if (polyflags & PF_Fog)
		HWR_AddTransparentWall(wallVerts, Surf, texnum, basetexnum, polyflags, true, HWR_CalcWallLight(lightnum, gl_curline), colormap);
	else if (polyflags & PF_EnvironmentTrans)
		HWR_AddTransparentWall(wallVerts, Surf, texnum, basetexnum, polyflags, false, HWR_CalcWallLight(lightnum, gl_curline), colormap);
	else
		HWR_ProjectWall(wallVerts, Surf, polyflags, HWR_CalcWallLight(lightnum, gl_curline), colormap);
}

// HWR_DrawSkyWall
// Draw walls into the depth buffer so that anything behind is culled properly
static void HWR_DrawSkyWall(FOutVector *wallVerts, FSurfaceInfo *Surf)
{
	HWR_SetCurrentTexture(NULL);
	// no texture
	wallVerts[3].t = wallVerts[2].t = 0;
	wallVerts[0].t = wallVerts[1].t = 0;
	wallVerts[0].s = wallVerts[3].s = 0;
	wallVerts[2].s = wallVerts[1].s = 0;
	// this no longer sets top/bottom coords, this should be done before caling the function
	HWR_ProjectWall(wallVerts, Surf, PF_Invisible|PF_NoTexture, 255, NULL);
	// PF_Invisible so it's not drawn into the colour buffer
	// PF_NoTexture for no texture
	// PF_Occlude is set in HWR_ProjectWall to draw into the depth buffer
}

//
// HWR_ProcessSeg
// A portion or all of a wall segment will be drawn, from startfrac to endfrac,
//  where 0 is the start of the segment, 1 the end of the segment
// Anything between means the wall segment has been clipped with solidsegs,
//  reducing wall overdraw to a minimum
//
#define RRE_KIND(k) (rre_static::curKind = (k))
static void HWR_ProcessSeg(void) // Sort of like GLWall::Process in GZDoom
{
	RRE_KIND(0);
	FOutVector wallVerts[4];
	v2d_t vs, ve; // start, end vertices of 2d line (view from above)

	fixed_t worldtop, worldbottom;
	fixed_t worldhigh = 0, worldlow = 0;
	fixed_t worldtopslope, worldbottomslope;
	fixed_t worldhighslope = 0, worldlowslope = 0;
	fixed_t v1x, v1y, v2x, v2y;

	GLMapTexture_t *grTex = NULL;
	float cliplow = 0.0f, cliphigh = 0.0f;
	INT32 gl_midtexture;
	fixed_t h, l; // 3D sides and 2s middle textures
	fixed_t hS, lS;

	FUINT lightnum = 255;
	extracolormap_t *colormap;
	FSurfaceInfo Surf;

	boolean tripwire;

	gl_sidedef = gl_curline->sidedef;
	gl_linedef = gl_curline->linedef;

	vs.x = ((polyvertex_t *)gl_curline->pv1)->x;
	vs.y = ((polyvertex_t *)gl_curline->pv1)->y;
	ve.x = ((polyvertex_t *)gl_curline->pv2)->x;
	ve.y = ((polyvertex_t *)gl_curline->pv2)->y;

	v1x = FLOAT_TO_FIXED(vs.x);
	v1y = FLOAT_TO_FIXED(vs.y);
	v2x = FLOAT_TO_FIXED(ve.x);
	v2y = FLOAT_TO_FIXED(ve.y);

#define SLOPEPARAMS(slope, end1, end2, normalheight) \
	end1 = P_GetZAt(slope, v1x, v1y, normalheight); \
	end2 = P_GetZAt(slope, v2x, v2y, normalheight);

	SLOPEPARAMS(gl_frontsector->c_slope, worldtop,    worldtopslope,    gl_frontsector->ceilingheight)
	SLOPEPARAMS(gl_frontsector->f_slope, worldbottom, worldbottomslope, gl_frontsector->floorheight)

	// remember vertices ordering
	//  3--2
	//  | /|
	//  |/ |
	//  0--1
	// make a wall polygon (with 2 triangles), using the floor/ceiling heights,
	// and the 2d map coords of start/end vertices
	wallVerts[0].x = wallVerts[3].x = vs.x;
	wallVerts[0].z = wallVerts[3].z = vs.y;
	wallVerts[2].x = wallVerts[1].x = ve.x;
	wallVerts[2].z = wallVerts[1].z = ve.y;

	// x offset the texture
	{
		fixed_t texturehpeg = gl_sidedef->textureoffset + gl_curline->offset;
		cliplow = (float)texturehpeg;
		cliphigh = (float)(texturehpeg + (gl_curline->flength*FRACUNIT));
	}

	tripwire = P_IsLineTripWire(gl_linedef);

	if (tripwire == false)
	{
		lightnum = HWR_CalcWallLight(gl_frontsector->lightlevel, gl_curline);
	}

	colormap = gl_frontsector->extra_colormap;

	if (gl_frontsector)
		Surf.PolyColor.s.alpha = 255;

	if (gl_backsector)
	{
		INT32 gl_toptexture = 0, gl_bottomtexture = 0;
		INT32 gl_basetoptexture = 0, gl_basebottomtexture = 0;
		// two sided line
		boolean bothceilingssky = false; // turned on if both back and front ceilings are sky
		boolean bothfloorssky = false; // likewise, but for floors

		SLOPEPARAMS(gl_backsector->c_slope, worldhigh, worldhighslope, gl_backsector->ceilingheight)
		SLOPEPARAMS(gl_backsector->f_slope, worldlow,  worldlowslope,  gl_backsector->floorheight)

		// hack to allow height changes in outdoor areas
		// This is what gets rid of the upper textures if there should be sky
		if (gl_frontsector->ceilingpic == skyflatnum
			&& gl_backsector->ceilingpic  == skyflatnum)
		{
			bothceilingssky = true;
		}

		// likewise, but for floors and upper textures
		if (gl_frontsector->floorpic == skyflatnum
			&& gl_backsector->floorpic == skyflatnum)
		{
			bothfloorssky = true;
		}

		if (!bothceilingssky)
		{
			gl_basetoptexture = gl_sidedef->toptexture;
			gl_toptexture = R_GetTextureNum(gl_basetoptexture);
		}
		if (!bothfloorssky)
		{
			gl_basebottomtexture = gl_sidedef->bottomtexture;
			gl_bottomtexture = R_GetTextureNum(gl_basebottomtexture);
		}

		// check TOP TEXTURE
		if ((worldhighslope < worldtopslope || worldhigh < worldtop) && gl_toptexture)
		{
			RRE_KIND(1);
			{
				fixed_t texturevpegtop; // top

				grTex = HWR_GetTexture(gl_toptexture, gl_basetoptexture);

				// PEGGING
				if (gl_linedef->flags & ML_DONTPEGTOP)
					texturevpegtop = 0;
				else if (gl_linedef->flags & ML_SKEWTD)
					texturevpegtop = worldhigh + textureheight[gl_sidedef->toptexture] - worldtop;
				else
					texturevpegtop = gl_backsector->ceilingheight + textureheight[gl_sidedef->toptexture] - gl_frontsector->ceilingheight;

				texturevpegtop += gl_sidedef->rowoffset;

				// This is so that it doesn't overflow and screw up the wall, it doesn't need to go higher than the texture's height anyway
				texturevpegtop %= (textures[gl_toptexture]->height)<<FRACBITS;

				wallVerts[3].t = wallVerts[2].t = texturevpegtop * grTex->scaleY;
				wallVerts[0].t = wallVerts[1].t = (texturevpegtop + gl_frontsector->ceilingheight - gl_backsector->ceilingheight) * grTex->scaleY;
				wallVerts[0].s = wallVerts[3].s = cliplow * grTex->scaleX;
				wallVerts[2].s = wallVerts[1].s = cliphigh * grTex->scaleX;

				// Adjust t value for sloped walls
				if (!(gl_linedef->flags & ML_SKEWTD))
				{
					// Unskewed
					wallVerts[3].t -= (worldtop - gl_frontsector->ceilingheight) * grTex->scaleY;
					wallVerts[2].t -= (worldtopslope - gl_frontsector->ceilingheight) * grTex->scaleY;
					wallVerts[0].t -= (worldhigh - gl_backsector->ceilingheight) * grTex->scaleY;
					wallVerts[1].t -= (worldhighslope - gl_backsector->ceilingheight) * grTex->scaleY;
				}
				else if (gl_linedef->flags & ML_DONTPEGTOP)
				{
					// Skewed by top
					wallVerts[0].t = (texturevpegtop + worldtop - worldhigh) * grTex->scaleY;
					wallVerts[1].t = (texturevpegtop + worldtopslope - worldhighslope) * grTex->scaleY;
				}
				else
				{
					// Skewed by bottom
					wallVerts[0].t = wallVerts[1].t = (texturevpegtop + worldtop - worldhigh) * grTex->scaleY;
					wallVerts[3].t = wallVerts[0].t - (worldtop - worldhigh) * grTex->scaleY;
					wallVerts[2].t = wallVerts[1].t - (worldtopslope - worldhighslope) * grTex->scaleY;
				}
			}

			// set top/bottom coords
			wallVerts[3].y = FIXED_TO_FLOAT(worldtop);
			wallVerts[0].y = FIXED_TO_FLOAT(worldhigh);
			wallVerts[2].y = FIXED_TO_FLOAT(worldtopslope);
			wallVerts[1].y = FIXED_TO_FLOAT(worldhighslope);

			{
				FBITFIELD polyflags = PF_Masked;

				if (RRE_TEXTRANSP(grTex))
					polyflags = PF_Environment;

				if (gl_frontsector->numlights)
					HWR_SplitWall(gl_frontsector, wallVerts, gl_toptexture, gl_basetoptexture, &Surf, FOF_CUTLEVEL, NULL, polyflags);
				else if (RRE_TEXTRANSP(grTex))
					HWR_AddTransparentWall(wallVerts, &Surf, gl_toptexture, gl_basetoptexture, polyflags, false, lightnum, colormap);
				else
					HWR_ProjectWall(wallVerts, &Surf, polyflags, lightnum, colormap);
			}
		}

		// check BOTTOM TEXTURE
		if ((
			worldlowslope > worldbottomslope ||
            worldlow > worldbottom) && gl_bottomtexture) //only if VISIBLE!!!
		{
			RRE_KIND(2);
			{
				fixed_t texturevpegbottom = 0; // bottom

				grTex = HWR_GetTexture(gl_bottomtexture, gl_basebottomtexture);

				// PEGGING
				if (!(gl_linedef->flags & ML_DONTPEGBOTTOM))
					texturevpegbottom = 0;
				else if (gl_linedef->flags & ML_SKEWTD)
					texturevpegbottom = worldbottom - worldlow;
				else
					texturevpegbottom = gl_frontsector->floorheight - gl_backsector->floorheight;

				texturevpegbottom += gl_sidedef->rowoffset;

				// This is so that it doesn't overflow and screw up the wall, it doesn't need to go higher than the texture's height anyway
				texturevpegbottom %= (textures[gl_bottomtexture]->height)<<FRACBITS;

				wallVerts[3].t = wallVerts[2].t = texturevpegbottom * grTex->scaleY;
				wallVerts[0].t = wallVerts[1].t = (texturevpegbottom + gl_backsector->floorheight - gl_frontsector->floorheight) * grTex->scaleY;
				wallVerts[0].s = wallVerts[3].s = cliplow * grTex->scaleX;
				wallVerts[2].s = wallVerts[1].s = cliphigh * grTex->scaleX;

				// Adjust t value for sloped walls
				if (!(gl_linedef->flags & ML_SKEWTD))
				{
					// Unskewed
					wallVerts[0].t -= (worldbottom - gl_frontsector->floorheight) * grTex->scaleY;
					wallVerts[1].t -= (worldbottomslope - gl_frontsector->floorheight) * grTex->scaleY;
					wallVerts[3].t -= (worldlow - gl_backsector->floorheight) * grTex->scaleY;
					wallVerts[2].t -= (worldlowslope - gl_backsector->floorheight) * grTex->scaleY;
				}
				else if (gl_linedef->flags & ML_DONTPEGBOTTOM)
				{
					// Skewed by bottom
					wallVerts[0].t = wallVerts[1].t = (texturevpegbottom + worldlow - worldbottom) * grTex->scaleY;
					//wallVerts[3].t = wallVerts[0].t - (worldlow - worldbottom) * grTex->scaleY; // no need, [3] is already this
					wallVerts[2].t = wallVerts[1].t - (worldlowslope - worldbottomslope) * grTex->scaleY;
				}
				else
				{
					// Skewed by top
					wallVerts[0].t = (texturevpegbottom + worldlow - worldbottom) * grTex->scaleY;
					wallVerts[1].t = (texturevpegbottom + worldlowslope - worldbottomslope) * grTex->scaleY;
				}
			}

			// set top/bottom coords
			wallVerts[3].y = FIXED_TO_FLOAT(worldlow);
			wallVerts[0].y = FIXED_TO_FLOAT(worldbottom);
			wallVerts[2].y = FIXED_TO_FLOAT(worldlowslope);
			wallVerts[1].y = FIXED_TO_FLOAT(worldbottomslope);

			{
				FBITFIELD polyflags = PF_Masked;

				if (RRE_TEXTRANSP(grTex))
					polyflags = PF_Environment;

				if (gl_frontsector->numlights)
					HWR_SplitWall(gl_frontsector, wallVerts, gl_bottomtexture, gl_basebottomtexture, &Surf, FOF_CUTLEVEL, NULL, polyflags);
				else if (RRE_TEXTRANSP(grTex))
					HWR_AddTransparentWall(wallVerts, &Surf, gl_bottomtexture, gl_basebottomtexture, polyflags, false, lightnum, colormap);
				else
					HWR_ProjectWall(wallVerts, &Surf, polyflags, lightnum, colormap);
			}
		}
		gl_midtexture = R_GetTextureNum(gl_sidedef->midtexture);
		if (gl_midtexture)
		{
			RRE_KIND(3);
			FBITFIELD blendmode;
			sector_t *front, *back;
			fixed_t  popentop, popenbottom, polytop, polybottom, lowcut, highcut;
			fixed_t     texturevpeg = 0;
			INT32 repeats;

			if (gl_linedef->frontsector->heightsec != -1)
				front = &sectors[gl_linedef->frontsector->heightsec];
			else
				front = gl_linedef->frontsector;

			if (gl_linedef->backsector->heightsec != -1)
				back = &sectors[gl_linedef->backsector->heightsec];
			else
				back = gl_linedef->backsector;

			if (gl_sidedef->repeatcnt)
				repeats = 1 + gl_sidedef->repeatcnt;
			else if (gl_linedef->flags & ML_WRAPMIDTEX)
			{
				fixed_t high, low;

				if (front->ceilingheight > back->ceilingheight)
					high = back->ceilingheight;
				else
					high = front->ceilingheight;

				if (front->floorheight > back->floorheight)
					low = front->floorheight;
				else
					low = back->floorheight;

				repeats = (high - low)/textureheight[gl_sidedef->midtexture];
				if ((high-low)%textureheight[gl_sidedef->midtexture])
					repeats++; // tile an extra time to fill the gap -- Monster Iestyn
			}
			else
				repeats = 1;

			// SoM: a little note: This code re-arranging will
			// fix the bug in Nimrod map02. popentop and popenbottom
			// record the limits the texture can be displayed in.
			// polytop and polybottom, are the ideal (i.e. unclipped)
			// heights of the polygon, and h & l, are the final (clipped)
			// poly coords.

			// NOTE: With polyobjects, whenever you need to check the properties of the polyobject sector it belongs to,
			// you must use the linedef's backsector to be correct
			// From CB
			if (gl_curline->polyseg)
			{
				popentop = back->ceilingheight;
				popenbottom = back->floorheight;
			}
			else
            {
				popentop = std::min(worldtop, worldhigh);
				popenbottom = std::max(worldbottom, worldlow);
			}

			if (gl_linedef->flags & ML_NOSKEW)
			{
				if (gl_linedef->flags & ML_MIDPEG)
				{
					polybottom = std::max(front->floorheight, back->floorheight) + gl_sidedef->rowoffset;
					polytop = polybottom + textureheight[gl_midtexture]*repeats;
				}
				else
				{
					polytop = std::min(front->ceilingheight, back->ceilingheight) + gl_sidedef->rowoffset;
					polybottom = polytop - textureheight[gl_midtexture]*repeats;
				}
			}
			else if (gl_linedef->flags & ML_MIDPEG)
			{
				polybottom = popenbottom + gl_sidedef->rowoffset;
				polytop = polybottom + textureheight[gl_midtexture]*repeats;
			}
			else
			{
				polytop = popentop + gl_sidedef->rowoffset;
				polybottom = polytop - textureheight[gl_midtexture]*repeats;
			}
			// CB
			// NOTE: With polyobjects, whenever you need to check the properties of the polyobject sector it belongs to,
			// you must use the linedef's backsector to be correct
			if (gl_curline->polyseg)
			{
				lowcut = polybottom;
				highcut = polytop;
			}
			else
			{
				// The cut-off values of a linedef can always be constant, since every line has an absoulute front and or back sector
				lowcut = popenbottom;
				highcut = popentop;
			}

			h = std::min(highcut, polytop);
			l = std::max(polybottom, lowcut);

			{
				// PEGGING
				if (gl_linedef->flags & ML_MIDPEG)
					texturevpeg = textureheight[gl_sidedef->midtexture]*repeats - h + polybottom;
				else
					texturevpeg = polytop - h;

				grTex = HWR_GetTexture(gl_midtexture, gl_sidedef->midtexture);

				// Check if we should flip tripwire texture vertically for unpegged tripwires
				if (R_ShouldFlipTripWire(gl_linedef))
				{
					// Flip texture coordinates vertically
					wallVerts[0].t = wallVerts[1].t = texturevpeg * grTex->scaleY;
					wallVerts[3].t = wallVerts[2].t = (h - l + texturevpeg) * grTex->scaleY;
				}
				else
				{
					wallVerts[3].t = wallVerts[2].t = texturevpeg * grTex->scaleY;
					wallVerts[0].t = wallVerts[1].t = (h - l + texturevpeg) * grTex->scaleY;
				}
				wallVerts[0].s = wallVerts[3].s = cliplow * grTex->scaleX;
				wallVerts[2].s = wallVerts[1].s = cliphigh * grTex->scaleX;
			}

			// set top/bottom coords
			// Take the texture peg into account, rather than changing the offsets past
			// where the polygon might not be.
			wallVerts[2].y = wallVerts[3].y = FIXED_TO_FLOAT(h);
			wallVerts[0].y = wallVerts[1].y = FIXED_TO_FLOAT(l);

			// Correct to account for slopes
			{
				fixed_t midtextureslant;

				if (gl_linedef->flags & ML_NOSKEW)
					midtextureslant = 0;
				else if (gl_linedef->flags & ML_MIDPEG)
					midtextureslant = worldlow < worldbottom
							  ? worldbottomslope-worldbottom
							  : worldlowslope-worldlow;
				else
					midtextureslant = worldtop < worldhigh
							  ? worldtopslope-worldtop
							  : worldhighslope-worldhigh;

				polytop += midtextureslant;
				polybottom += midtextureslant;

				highcut += worldtop < worldhigh
						 ? worldtopslope-worldtop
						 : worldhighslope-worldhigh;
				lowcut += worldlow < worldbottom
						? worldbottomslope-worldbottom
						: worldlowslope-worldlow;

				// Texture stuff
				h = std::min(highcut, polytop);
				l = std::max(polybottom, lowcut);

				{
					// PEGGING
					if (gl_linedef->flags & ML_MIDPEG)
						texturevpeg = textureheight[gl_sidedef->midtexture]*repeats - h + polybottom;
					else
						texturevpeg = polytop - h;

					// Apply tripwire flipping for slope correction as well
					if (R_ShouldFlipTripWire(gl_linedef))
					{
						// Flip texture coordinates vertically
						wallVerts[1].t = texturevpeg * grTex->scaleY;
						wallVerts[2].t = (h - l + texturevpeg) * grTex->scaleY;
					}
					else
					{
						wallVerts[2].t = texturevpeg * grTex->scaleY;
						wallVerts[1].t = (h - l + texturevpeg) * grTex->scaleY;
					}
				}

				wallVerts[2].y = FIXED_TO_FLOAT(h);
				wallVerts[1].y = FIXED_TO_FLOAT(l);
			}

			// set alpha for transparent walls
			// ooops ! this do not work at all because render order we should render it in backtofront order
			if (gl_linedef->blendmode && gl_linedef->blendmode != AST_FOG)
			{
				if (gl_linedef->alpha >= 0 && gl_linedef->alpha < FRACUNIT)
					blendmode = HWR_SurfaceBlend(gl_linedef->blendmode, R_GetLinedefTransTable(gl_linedef->alpha), &Surf);
				else
					blendmode = HWR_GetBlendModeFlag(gl_linedef->blendmode);
			}
			else if (gl_linedef->alpha >= 0 && gl_linedef->alpha < FRACUNIT)
				blendmode = HWR_TranstableToAlpha(R_GetLinedefTransTable(gl_linedef->alpha), &Surf);
			else
				blendmode = PF_Masked;

			if (gl_curline->polyseg && gl_curline->polyseg->translucency > 0)
			{
				if (gl_curline->polyseg->translucency >= NUMTRANSMAPS) // wall not drawn
				{
					Surf.PolyColor.s.alpha = 0x00; // This shouldn't draw anything regardless of blendmode
					blendmode = PF_Masked;
				}
				else
					blendmode = HWR_TranstableToAlpha(gl_curline->polyseg->translucency, &Surf);
			}

			// Render midtextures on two-sided lines with a z-buffer offset.
			// This will cause the midtexture appear on top, if a FOF overlaps with it.
			blendmode |= PF_Decal;

			if (tripwire == false && gl_frontsector->numlights)
			{
				if (!(blendmode & PF_Masked))
					HWR_SplitWall(gl_frontsector, wallVerts, gl_midtexture, gl_sidedef->midtexture, &Surf, FOF_TRANSLUCENT, NULL, blendmode); // vanilla just uses PF_Masked here - if we run into any issues, maybe change to that
				else
				{
					HWR_SplitWall(gl_frontsector, wallVerts, gl_midtexture, gl_sidedef->midtexture, &Surf, FOF_CUTLEVEL, NULL, blendmode); // vanilla just uses PF_Masked here - if we run into any issues, maybe change to that
				}
			}
			else if (!(blendmode & PF_Masked))
				HWR_AddTransparentWall(wallVerts, &Surf, gl_midtexture, gl_sidedef->midtexture, blendmode, false, lightnum, colormap);
			else
				HWR_ProjectWall(wallVerts, &Surf, blendmode, lightnum, colormap);
		}

		// Sky culling
		// No longer so much a mess as before!
		if (!gl_curline->polyseg) // Don't do it for polyobjects
		{
			if (gl_frontsector->ceilingpic == skyflatnum)
			{
				if (gl_backsector->ceilingpic != skyflatnum) // don't cull if back sector is also sky
				{
					RRE_KIND(4);
					wallVerts[2].y = wallVerts[3].y = FIXED_TO_FLOAT(INT32_MAX); // draw to top of map space
					wallVerts[0].y = FIXED_TO_FLOAT(worldtop);
					wallVerts[1].y = FIXED_TO_FLOAT(worldtopslope);
					HWR_DrawSkyWall(wallVerts, &Surf);
				}
			}

			if (gl_frontsector->floorpic == skyflatnum)
			{
				if (gl_backsector->floorpic != skyflatnum) // don't cull if back sector is also sky
				{
					RRE_KIND(5);
					wallVerts[3].y = FIXED_TO_FLOAT(worldbottom);
					wallVerts[2].y = FIXED_TO_FLOAT(worldbottomslope);
					wallVerts[0].y = wallVerts[1].y = FIXED_TO_FLOAT(INT32_MIN); // draw to bottom of map space
					HWR_DrawSkyWall(wallVerts, &Surf);
				}
			}
		}
		RRE_KIND(0);
	}
	else
	{
		// Single sided line... Deal only with the middletexture (if one exists)
		gl_midtexture = R_GetTextureNum(gl_sidedef->midtexture);
		if (gl_midtexture && gl_linedef->special != 41) // (Ignore horizon line for OGL)
		{
			{
				fixed_t     texturevpeg;
				// PEGGING
				if ((gl_linedef->flags & (ML_DONTPEGBOTTOM|ML_NOSKEW)) == (ML_DONTPEGBOTTOM|ML_NOSKEW))
					texturevpeg = gl_frontsector->floorheight + textureheight[gl_sidedef->midtexture] - gl_frontsector->ceilingheight + gl_sidedef->rowoffset;
				else if (gl_linedef->flags & ML_DONTPEGBOTTOM)
					texturevpeg = worldbottom + textureheight[gl_sidedef->midtexture] - worldtop + gl_sidedef->rowoffset;
				else
					// top of texture at top
					texturevpeg = gl_sidedef->rowoffset;

				grTex = HWR_GetTexture(gl_midtexture, gl_sidedef->midtexture);

				// Check if we should flip tripwire texture vertically for single-sided lines too
				if (R_ShouldFlipTripWire(gl_linedef))
				{
					// Flip texture coordinates vertically
					wallVerts[0].t = wallVerts[1].t = texturevpeg * grTex->scaleY;
					wallVerts[3].t = wallVerts[2].t = (texturevpeg + gl_frontsector->ceilingheight - gl_frontsector->floorheight) * grTex->scaleY;
				}
				else
				{
					wallVerts[3].t = wallVerts[2].t = texturevpeg * grTex->scaleY;
					wallVerts[0].t = wallVerts[1].t = (texturevpeg + gl_frontsector->ceilingheight - gl_frontsector->floorheight) * grTex->scaleY;
				}
				wallVerts[0].s = wallVerts[3].s = cliplow * grTex->scaleX;
				wallVerts[2].s = wallVerts[1].s = cliphigh * grTex->scaleX;

				// Texture correction for slopes
				if (gl_linedef->flags & ML_NOSKEW) {
					wallVerts[3].t += (gl_frontsector->ceilingheight - worldtop) * grTex->scaleY;
					wallVerts[2].t += (gl_frontsector->ceilingheight - worldtopslope) * grTex->scaleY;
					wallVerts[0].t += (gl_frontsector->floorheight - worldbottom) * grTex->scaleY;
					wallVerts[1].t += (gl_frontsector->floorheight - worldbottomslope) * grTex->scaleY;
				} else if (gl_linedef->flags & ML_DONTPEGBOTTOM) {
					wallVerts[3].t = wallVerts[0].t + (worldbottom-worldtop) * grTex->scaleY;
					wallVerts[2].t = wallVerts[1].t + (worldbottomslope-worldtopslope) * grTex->scaleY;
				} else {
					wallVerts[0].t = wallVerts[3].t - (worldbottom-worldtop) * grTex->scaleY;
					wallVerts[1].t = wallVerts[2].t - (worldbottomslope-worldtopslope) * grTex->scaleY;
				}
			}

			//Set textures properly on single sided walls that are sloped
			wallVerts[3].y = FIXED_TO_FLOAT(worldtop);
			wallVerts[0].y = FIXED_TO_FLOAT(worldbottom);
			wallVerts[2].y = FIXED_TO_FLOAT(worldtopslope);
			wallVerts[1].y = FIXED_TO_FLOAT(worldbottomslope);

			// I don't think that solid walls can use translucent linedef types...
			if (gl_frontsector->numlights)
				HWR_SplitWall(gl_frontsector, wallVerts, gl_midtexture, gl_sidedef->midtexture, &Surf, FOF_CUTLEVEL, NULL, 0);
			else
			{
				FBITFIELD blendmode = PF_Masked;
				if (RRE_TEXTRANSP(grTex))
					blendmode = PF_Environment;

				// I don't think that solid walls can use translucent linedef types...
				if (gl_frontsector->numlights)
					HWR_SplitWall(gl_frontsector, wallVerts, gl_midtexture, gl_sidedef->midtexture, &Surf, FOF_CUTLEVEL, NULL, blendmode);
				else
				{
					if (RRE_TEXTRANSP(grTex))
						HWR_AddTransparentWall(wallVerts, &Surf, gl_midtexture, gl_sidedef->midtexture, blendmode, false, lightnum, colormap);
					else
						HWR_ProjectWall(wallVerts, &Surf, blendmode, lightnum, colormap);
				}
			}
		}

		if (!gl_curline->polyseg)
		{
			if (gl_frontsector->ceilingpic == skyflatnum) // It's a single-sided line with sky for its sector
			{
				wallVerts[2].y = wallVerts[3].y = FIXED_TO_FLOAT(INT32_MAX); // draw to top of map space
				wallVerts[0].y = FIXED_TO_FLOAT(worldtop);
				wallVerts[1].y = FIXED_TO_FLOAT(worldtopslope);
				HWR_DrawSkyWall(wallVerts, &Surf);
			}
			if (gl_frontsector->floorpic == skyflatnum)
			{
				wallVerts[3].y = FIXED_TO_FLOAT(worldbottom);
				wallVerts[2].y = FIXED_TO_FLOAT(worldbottomslope);
				wallVerts[0].y = wallVerts[1].y = FIXED_TO_FLOAT(INT32_MIN); // draw to bottom of map space
				HWR_DrawSkyWall(wallVerts, &Surf);
			}
		}
	}


	//Hurdler: 3d-floors test
	if (gl_frontsector && gl_backsector && !Tag_Compare(&gl_frontsector->tags, &gl_backsector->tags) && (gl_backsector->ffloors || gl_frontsector->ffloors))
	{
		ffloor_t * rover;
		fixed_t    highcut = 0, lowcut = 0;
		fixed_t lowcutslope, highcutslope;

		// Used for height comparisons and etc across FOFs and slopes
		fixed_t high1, highslope1, low1, lowslope1;

		INT32 texnum, basetexnum;
		line_t * newline = NULL; // Multi-Property FOF

		lowcut = std::max(worldbottom, worldlow);
		highcut = std::min(worldtop, worldhigh);
		lowcutslope = std::max(worldbottomslope, worldlowslope);
		highcutslope = std::min(worldtopslope, worldhighslope);

		if (gl_backsector->ffloors)
		{
			INT32 rr_ord = -1; (void)rr_ord;
			for (rover = gl_backsector->ffloors; rover; rover = rover->next)
			{
				rr_ord++;
				RRE_KIND(0);
				boolean bothsides = false;
				// Skip if it exists on both sectors.
				ffloor_t * r2;
				for (r2 = gl_frontsector->ffloors; r2; r2 = r2->next)
					if (rover->master == r2->master)
					{
						bothsides = true;
						break;
					}

				if (bothsides) continue;

				if (!(rover->fofflags & FOF_EXISTS) || !(rover->fofflags & FOF_RENDERSIDES))
					continue;
				if (!(rover->fofflags & FOF_ALLSIDES) && rover->fofflags & FOF_INVERTSIDES)
					continue;

				SLOPEPARAMS(*rover->t_slope, high1, highslope1, *rover->topheight)
				SLOPEPARAMS(*rover->b_slope, low1,  lowslope1,  *rover->bottomheight)

				if ((high1 < lowcut && highslope1 < lowcutslope) || (low1 > highcut && lowslope1 > highcutslope))
					continue;
				RRE_KIND(rr_ord < 24 ? 8 + rr_ord : 0);

				basetexnum = sides[rover->master->sidenum[0]].midtexture;

				if (rover->master->flags & ML_TFERLINE)
				{
					size_t linenum = gl_curline->linedef-gl_backsector->lines[0];
					newline = rover->master->frontsector->lines[0] + linenum;
					basetexnum = sides[newline->sidenum[0]].midtexture;
				}

				texnum = R_GetTextureNum(basetexnum);

				h  = P_GetFFloorTopZAt   (rover, v1x, v1y);
				hS = P_GetFFloorTopZAt   (rover, v2x, v2y);
				l  = P_GetFFloorBottomZAt(rover, v1x, v1y);
				lS = P_GetFFloorBottomZAt(rover, v2x, v2y);
				// Adjust the heights so the FOF does not overlap with top and bottom textures.
				if (h >= highcut && hS >= highcutslope)
				{
					h = highcut;
					hS = highcutslope;
				}
				if (l <= lowcut && lS <= lowcutslope)
				{
					l = lowcut;
					lS = lowcutslope;
				}
				//Hurdler: HW code starts here
				//FIXME: check if peging is correct
				// set top/bottom coords

				wallVerts[3].y = FIXED_TO_FLOAT(h);
				wallVerts[2].y = FIXED_TO_FLOAT(hS);
				wallVerts[0].y = FIXED_TO_FLOAT(l);
				wallVerts[1].y = FIXED_TO_FLOAT(lS);
				if (rover->fofflags & FOF_FOG)
				{
					wallVerts[3].t = wallVerts[2].t = 0;
					wallVerts[0].t = wallVerts[1].t = 0;
					wallVerts[0].s = wallVerts[3].s = 0;
					wallVerts[2].s = wallVerts[1].s = 0;
				}
				else
				{
					fixed_t texturevpeg;
					boolean attachtobottom = false;
					boolean slopeskew = false; // skew FOF walls with slopes?

					// Wow, how was this missing from OpenGL for so long?
					// ...Oh well, anyway, Lower Unpegged now changes pegging of FOFs like in software
					// -- Monster Iestyn 26/06/18
					if (newline)
					{
						texturevpeg = sides[newline->sidenum[0]].rowoffset;
						attachtobottom = !!(newline->flags & ML_DONTPEGBOTTOM);
						slopeskew = !!(newline->flags & ML_SKEWTD);
					}
					else
					{
						texturevpeg = sides[rover->master->sidenum[0]].rowoffset;
						attachtobottom = !!(gl_linedef->flags & ML_DONTPEGBOTTOM);
						slopeskew = !!(rover->master->flags & ML_SKEWTD);
					}

					grTex = HWR_GetTexture(texnum, basetexnum);

					if (!slopeskew) // no skewing
					{
						if (attachtobottom)
							texturevpeg -= *rover->topheight - *rover->bottomheight;
						wallVerts[3].t = (*rover->topheight - h + texturevpeg) * grTex->scaleY;
						wallVerts[2].t = (*rover->topheight - hS + texturevpeg) * grTex->scaleY;
						wallVerts[0].t = (*rover->topheight - l + texturevpeg) * grTex->scaleY;
						wallVerts[1].t = (*rover->topheight - lS + texturevpeg) * grTex->scaleY;
					}
					else
					{
						if (!attachtobottom) // skew by top
						{
							wallVerts[3].t = wallVerts[2].t = texturevpeg * grTex->scaleY;
							wallVerts[0].t = (h - l + texturevpeg) * grTex->scaleY;
							wallVerts[1].t = (hS - lS + texturevpeg) * grTex->scaleY;
						}
						else // skew by bottom
						{
							wallVerts[0].t = wallVerts[1].t = texturevpeg * grTex->scaleY;
							wallVerts[3].t = wallVerts[0].t - (h - l) * grTex->scaleY;
							wallVerts[2].t = wallVerts[1].t - (hS - lS) * grTex->scaleY;
						}
					}

					wallVerts[0].s = wallVerts[3].s = cliplow * grTex->scaleX;
					wallVerts[2].s = wallVerts[1].s = cliphigh * grTex->scaleX;
				}
				if (rover->fofflags & FOF_FOG)
				{
					FBITFIELD blendmode;

					blendmode = PF_Fog|PF_NoTexture;

					lightnum = HWR_CalcWallLight(rover->master->frontsector->lightlevel, gl_curline);
					colormap = rover->master->frontsector->extra_colormap;

					Surf.PolyColor.s.alpha = HWR_FogBlockAlpha(rover->master->frontsector->lightlevel, rover->master->frontsector->extra_colormap);

					if (gl_frontsector->numlights)
						HWR_SplitWall(gl_frontsector, wallVerts, 0, 0, &Surf, rover->fofflags, rover, blendmode);
					else
						HWR_AddTransparentWall(wallVerts, &Surf, 0, 0, blendmode, true, lightnum, colormap);
				}
				else
				{
					FBITFIELD blendmode = PF_Masked;

					if (rover->fofflags & FOF_TRANSLUCENT)
					{
						if (rover->alpha < 256 || rover->blend)
						{
							blendmode = HWR_GetBlendModeFlag(rover->blend);
							Surf.PolyColor.s.alpha = std::max<INT32>(0, std::min<INT32>(rover->alpha, 255));
						}
					}

					if (gl_frontsector->numlights)
						HWR_SplitWall(gl_frontsector, wallVerts, texnum, basetexnum, &Surf, rover->fofflags, rover, blendmode);
					else
					{
						if (blendmode != PF_Masked)
							HWR_AddTransparentWall(wallVerts, &Surf, texnum, basetexnum, blendmode, false, lightnum, colormap);
						else
							HWR_ProjectWall(wallVerts, &Surf, PF_Masked, lightnum, colormap);
					}
				}
			}
		}

		RRE_KIND(0);
		if (gl_frontsector->ffloors) // Putting this seperate should allow 2 FOF sectors to be connected without too many errors? I think?
		{
			for (rover = gl_frontsector->ffloors; rover; rover = rover->next)
			{
				boolean bothsides = false;
				// Skip if it exists on both sectors.
				ffloor_t * r2;
				for (r2 = gl_backsector->ffloors; r2; r2 = r2->next)
					if (rover->master == r2->master)
					{
						bothsides = true;
						break;
					}

				if (bothsides) continue;

				if (!(rover->fofflags & FOF_EXISTS) || !(rover->fofflags & FOF_RENDERSIDES))
					continue;
				if (!(rover->fofflags & FOF_ALLSIDES || rover->fofflags & FOF_INVERTSIDES))
					continue;

				SLOPEPARAMS(*rover->t_slope, high1, highslope1, *rover->topheight)
				SLOPEPARAMS(*rover->b_slope, low1,  lowslope1,  *rover->bottomheight)

				if ((high1 < lowcut && highslope1 < lowcutslope) || (low1 > highcut && lowslope1 > highcutslope))
					continue;

				basetexnum = sides[rover->master->sidenum[0]].midtexture;

				if (rover->master->flags & ML_TFERLINE)
				{
					size_t linenum = gl_curline->linedef-gl_backsector->lines[0];
					newline = rover->master->frontsector->lines[0] + linenum;
					basetexnum = sides[newline->sidenum[0]].midtexture;
				}

				texnum = R_GetTextureNum(basetexnum);

				h  = P_GetFFloorTopZAt   (rover, v1x, v1y);
				hS = P_GetFFloorTopZAt   (rover, v2x, v2y);
				l  = P_GetFFloorBottomZAt(rover, v1x, v1y);
				lS = P_GetFFloorBottomZAt(rover, v2x, v2y);
				// Adjust the heights so the FOF does not overlap with top and bottom textures.
				if (h >= highcut && hS >= highcutslope)
				{
					h = highcut;
					hS = highcutslope;
				}
				if (l <= lowcut && lS <= lowcutslope)
				{
					l = lowcut;
					lS = lowcutslope;
				}
				//Hurdler: HW code starts here
				//FIXME: check if peging is correct
				// set top/bottom coords

				wallVerts[3].y = FIXED_TO_FLOAT(h);
				wallVerts[2].y = FIXED_TO_FLOAT(hS);
				wallVerts[0].y = FIXED_TO_FLOAT(l);
				wallVerts[1].y = FIXED_TO_FLOAT(lS);
				if (rover->fofflags & FOF_FOG)
				{
					wallVerts[3].t = wallVerts[2].t = 0;
					wallVerts[0].t = wallVerts[1].t = 0;
					wallVerts[0].s = wallVerts[3].s = 0;
					wallVerts[2].s = wallVerts[1].s = 0;
				}
				else
				{
					grTex = HWR_GetTexture(texnum, basetexnum);

					if (newline)
					{
						wallVerts[3].t = wallVerts[2].t = (*rover->topheight - h + sides[newline->sidenum[0]].rowoffset) * grTex->scaleY;
						wallVerts[0].t = wallVerts[1].t = (h - l + (*rover->topheight - h + sides[newline->sidenum[0]].rowoffset)) * grTex->scaleY;
					}
					else
					{
						wallVerts[3].t = wallVerts[2].t = (*rover->topheight - h + sides[rover->master->sidenum[0]].rowoffset) * grTex->scaleY;
						wallVerts[0].t = wallVerts[1].t = (h - l + (*rover->topheight - h + sides[rover->master->sidenum[0]].rowoffset)) * grTex->scaleY;
					}

					wallVerts[0].s = wallVerts[3].s = cliplow * grTex->scaleX;
					wallVerts[2].s = wallVerts[1].s = cliphigh * grTex->scaleX;
				}

				if (rover->fofflags & FOF_FOG)
				{
					FBITFIELD blendmode;

					blendmode = PF_Fog|PF_NoTexture;

					lightnum = HWR_CalcWallLight(rover->master->frontsector->lightlevel, gl_curline);
					colormap = rover->master->frontsector->extra_colormap;

					Surf.PolyColor.s.alpha = HWR_FogBlockAlpha(rover->master->frontsector->lightlevel, rover->master->frontsector->extra_colormap);

					if (gl_backsector->numlights)
						HWR_SplitWall(gl_backsector, wallVerts, 0, 0, &Surf, rover->fofflags, rover, blendmode);
					else
						HWR_AddTransparentWall(wallVerts, &Surf, 0, 0, blendmode, true, lightnum, colormap);
				}
				else
				{
					FBITFIELD blendmode = PF_Masked;

					if (rover->fofflags & FOF_TRANSLUCENT)
					{
						if (rover->alpha < 256 || rover->blend)
						{
							blendmode = HWR_GetBlendModeFlag(rover->blend);
							Surf.PolyColor.s.alpha = std::max<INT32>(0, std::min<INT32>(rover->alpha, 255));
						}
					}

					if (gl_backsector->numlights)
						HWR_SplitWall(gl_backsector, wallVerts, texnum, basetexnum, &Surf, rover->fofflags, rover, blendmode);
					else
					{
						if (blendmode != PF_Masked)
							HWR_AddTransparentWall(wallVerts, &Surf, texnum, basetexnum, blendmode, false, lightnum, colormap);
						else
							HWR_ProjectWall(wallVerts, &Surf, PF_Masked, lightnum, colormap);
					}
				}
			}
		}
	}
#undef SLOPEPARAMS
//Hurdler: end of 3d-floors test
}

// From PrBoom:
//
// e6y: Check whether the player can look beyond this line
//
boolean checkforemptylines = true;
// Don't modify anything here, just check
// Kalaron: Modified for sloped linedefs
static boolean CheckClip(seg_t * seg, sector_t * afrontsector, sector_t * abacksector)
{
	fixed_t frontf1,frontf2, frontc1, frontc2; // front floor/ceiling ends
	fixed_t backf1, backf2, backc1, backc2; // back floor ceiling ends
	boolean bothceilingssky = false, bothfloorssky = false;

	if (abacksector->ceilingpic == skyflatnum && afrontsector->ceilingpic == skyflatnum)
		bothceilingssky = true;
	if (abacksector->floorpic == skyflatnum && afrontsector->floorpic == skyflatnum)
		bothfloorssky = true;

	// GZDoom method of sloped line clipping

	if (afrontsector->f_slope || afrontsector->c_slope || abacksector->f_slope || abacksector->c_slope)
	{
		fixed_t v1x, v1y, v2x, v2y; // the seg's vertexes as fixed_t
		v1x = FLOAT_TO_FIXED(((polyvertex_t *)gl_curline->pv1)->x);
		v1y = FLOAT_TO_FIXED(((polyvertex_t *)gl_curline->pv1)->y);
		v2x = FLOAT_TO_FIXED(((polyvertex_t *)gl_curline->pv2)->x);
		v2y = FLOAT_TO_FIXED(((polyvertex_t *)gl_curline->pv2)->y);
#define SLOPEPARAMS(slope, end1, end2, normalheight) \
		end1 = P_GetZAt(slope, v1x, v1y, normalheight); \
		end2 = P_GetZAt(slope, v2x, v2y, normalheight);

		SLOPEPARAMS(afrontsector->f_slope, frontf1, frontf2, afrontsector->  floorheight)
		SLOPEPARAMS(afrontsector->c_slope, frontc1, frontc2, afrontsector->ceilingheight)
		SLOPEPARAMS( abacksector->f_slope,  backf1,  backf2,  abacksector->  floorheight)
		SLOPEPARAMS( abacksector->c_slope,  backc1,  backc2,  abacksector->ceilingheight)
#undef SLOPEPARAMS
	}
	else
	{
		frontf1 = frontf2 = afrontsector->  floorheight;
		frontc1 = frontc2 = afrontsector->ceilingheight;
		backf1  =  backf2 =  abacksector->  floorheight;
		backc1  =  backc2 =  abacksector->ceilingheight;
	}
	// properly render skies (consider door "open" if both ceilings are sky)
	// same for floors
	if (!bothceilingssky && !bothfloorssky)
	{
		// now check for closed sectors!
		if ((backc1 <= frontf1 && backc2 <= frontf2)
			|| (backf1 >= frontc1 && backf2 >= frontc2))
		{
			checkforemptylines = false;
			return true;
		}

		if (backc1 <= backf1 && backc2 <= backf2)
		{
			// preserve a kind of transparent door/lift special effect:
			if (((backc1 >= frontc1 && backc2 >= frontc2) || seg->sidedef->toptexture)
			&& ((backf1 <= frontf1 && backf2 <= frontf2) || seg->sidedef->bottomtexture))
			{
				checkforemptylines = false;
				return true;
			}
		}
	}

	if (!bothceilingssky) {
		if (backc1 != frontc1 || backc2 != frontc2)
		{
			checkforemptylines = false;
			return false;
		}
	}

	if (!bothfloorssky) {
		if (backf1 != frontf1 || backf2 != frontf2)
		{
			checkforemptylines = false;
			return false;
		}
	}

	return false;
}

// -----------------+
// HWR_AddLine      : Clips the given segment and adds any visible pieces to the line list.
// Notes            : gl_cursectorlight is set to the current subsector -> sector -> light value
//                  : (it may be mixed with the wall's own flat colour in the future ...)
// [SUB] breakdown inside the BSP, only 1 frame in 4 (sampling).
namespace rre_sub {
	enum { S_FAKE, S_PLANES, S_POLY, S_SPR, S_LINES, S_N };
	static UINT64 accV[2][S_N];
	static UINT64 nSubV[2], nLinesV[2], bspTotV[2], skyBgV[2];
	static UINT64 segT[2], nFront[2], nProc[2], nMiss[2], nAngReuse[2];
	static UINT64 sigT[2], useT[2], missT[2]; // breakdown of process_time
	static int framesV[2];
	static int on = 0, frameNo = 0, view = 0;
	static UINT64 last;
	static inline void Start(void) { if (on) last = I_GetPreciseTime(); }
	static inline void Mark(int seg) { if (on) { UINT64 n = I_GetPreciseTime(); accV[view][seg] += n - last; last = n; } }
}
#define SUB_START() rre_sub::Start()
#define SUB_MARK(x) rre_sub::Mark(rre_sub::x)
// -----------------+
// Wraps HWR_ProcessSeg with the static geometry cache.
// Sector data that only depends on the view (heights, light, colormap):
// computed once per sector and view, not for every subsector (~3 per sector).
namespace rre_seccache {
	struct E { UINT32 stamp; fixed_t cullF, cullC, locF, locC; INT32 fll, cll; extracolormap_t *fcm, *ccm; };
	static std::vector<E> e;
	static UINT32 stamp = 1;
	void LevelReset(void) { e.assign(numsectors, E{0, 0, 0, 0, 0, 0, 0, NULL, NULL}); stamp = 1; }
}
// Recomputes y/t of a simple wall (no slopes) with the current heights,
// same as HWR_ProcessSeg. False if the set of visible pieces changes (must recapture).
// Actual heights at both ends of the seg (with static slopes)
static void RRE_WorldH(fixed_t &wt, fixed_t &wts, fixed_t &wb, fixed_t &wbs, fixed_t &wh, fixed_t &whs, fixed_t &wl, fixed_t &wls)
{
	const sector_t *fs = gl_frontsector, *bs = gl_backsector;
	const fixed_t v1x = FLOAT_TO_FIXED(((polyvertex_t *)gl_curline->pv1)->x), v1y = FLOAT_TO_FIXED(((polyvertex_t *)gl_curline->pv1)->y);
	const fixed_t v2x = FLOAT_TO_FIXED(((polyvertex_t *)gl_curline->pv2)->x), v2y = FLOAT_TO_FIXED(((polyvertex_t *)gl_curline->pv2)->y);
	wt = P_GetZAt(fs->c_slope, v1x, v1y, fs->ceilingheight); wts = P_GetZAt(fs->c_slope, v2x, v2y, fs->ceilingheight);
	wb = P_GetZAt(fs->f_slope, v1x, v1y, fs->floorheight);   wbs = P_GetZAt(fs->f_slope, v2x, v2y, fs->floorheight);
	wh = P_GetZAt(bs->c_slope, v1x, v1y, bs->ceilingheight); whs = P_GetZAt(bs->c_slope, v2x, v2y, bs->ceilingheight);
	wl = P_GetZAt(bs->f_slope, v1x, v1y, bs->floorheight);   wls = P_GetZAt(bs->f_slope, v2x, v2y, bs->floorheight);
}
static UINT8 RRE_SimpleMask(void)
{
	const sector_t *fs = gl_frontsector, *bs = gl_backsector;
	const side_t *sd = gl_curline->sidedef;
	UINT8 m = 0;
	const boolean csky = fs->ceilingpic == skyflatnum && bs->ceilingpic == skyflatnum;
	const boolean fsky = fs->floorpic == skyflatnum && bs->floorpic == skyflatnum;
	fixed_t wt, wts, wb, wbs, wh, whs, wl, wls; RRE_WorldH(wt, wts, wb, wbs, wh, whs, wl, wls);
	if (!csky && R_GetTextureNum(sd->toptexture) && (whs < wts || wh < wt)) m |= 1u << 1;
	if (!fsky && R_GetTextureNum(sd->bottomtexture) && (wls > wbs || wl > wb)) m |= 1u << 2;
	if (R_GetTextureNum(sd->midtexture)) m |= 1u << 3;
	if (!gl_curline->polyseg)
	{
		if (fs->ceilingpic == skyflatnum && bs->ceilingpic != skyflatnum) m |= 1u << 4;
		if (fs->floorpic == skyflatnum && bs->floorpic != skyflatnum) m |= 1u << 5;
	}
	return m;
}
// Which FOFs of the back sector draw a wall right now (same as HWR_ProcessSeg, no slopes)
static UINT32 RRE_FofMask(void)
{
	const sector_t *fs = gl_frontsector, *bs = gl_backsector;
	if (!bs->ffloors || Tag_Compare(&fs->tags, &bs->tags)) return 0;
	const fixed_t lowcut = std::max(fs->floorheight, bs->floorheight), highcut = std::min(fs->ceilingheight, bs->ceilingheight);
	UINT32 m = 0; int k = 0;
	for (const ffloor_t *r = bs->ffloors; r; r = r->next, k++)
	{
		if (!(r->fofflags & FOF_EXISTS) || !(r->fofflags & FOF_RENDERSIDES)) continue;
		if (!(r->fofflags & FOF_ALLSIDES) && (r->fofflags & FOF_INVERTSIDES)) continue;
		if (*r->topheight < lowcut || *r->bottomheight > highcut) continue;
		if (k < 24) m |= 1u << k;
	}
	return m;
}
static boolean RRE_PatchSegHeights(const rre_static::SItem &it)
{
	using namespace rre_static;
	if (RRE_SimpleMask() != it.kmask || RRE_FofMask() != it.fofMask)
		return false;
	const sector_t *fs = gl_frontsector, *bs = gl_backsector;
	const side_t *sd = gl_curline->sidedef;
	const line_t *ld = gl_curline->linedef;
	const fixed_t wt = fs->ceilingheight, wb = fs->floorheight, wh = bs->ceilingheight, wl = bs->floorheight;
	fixed_t xwt, xwts, xwb, xwbs, xwh, xwhs, xwl, xwls; RRE_WorldH(xwt, xwts, xwb, xwbs, xwh, xwhs, xwl, xwls);
	const boolean anySl = fs->f_slope || fs->c_slope || bs->f_slope || bs->c_slope;
	for (UINT32 p = it.firstPoly; p < it.firstPoly + it.numPolys; p++)
	{
		const SPoly &sp = polys[p];
		float y[4], t[4];
		const float sy = sp.sy;
		switch (sp.pad)
		{
			case 1: // top (with static slopes, same as HWR_ProcessSeg)
			{
				const INT32 tex = R_GetTextureNum(sd->toptexture);
				fixed_t v;
				if (ld->flags & ML_DONTPEGTOP) v = 0;
				else if (ld->flags & ML_SKEWTD) v = xwh + textureheight[sd->toptexture] - xwt;
				else v = bs->ceilingheight + textureheight[sd->toptexture] - fs->ceilingheight;
				v += sd->rowoffset;
				v %= (textures[tex]->height) << FRACBITS;
				t[3] = t[2] = v * sy; t[0] = t[1] = (v + fs->ceilingheight - bs->ceilingheight) * sy;
				if (!(ld->flags & ML_SKEWTD))
				{
					t[3] -= (xwt - fs->ceilingheight) * sy; t[2] -= (xwts - fs->ceilingheight) * sy;
					t[0] -= (xwh - bs->ceilingheight) * sy; t[1] -= (xwhs - bs->ceilingheight) * sy;
				}
				else if (ld->flags & ML_DONTPEGTOP)
				{
					t[0] = (v + xwt - xwh) * sy; t[1] = (v + xwts - xwhs) * sy;
				}
				else
				{
					t[0] = t[1] = (v + xwt - xwh) * sy;
					t[3] = t[0] - (xwt - xwh) * sy; t[2] = t[1] - (xwts - xwhs) * sy;
				}
				y[3] = FIXED_TO_FLOAT(xwt); y[0] = FIXED_TO_FLOAT(xwh); y[2] = FIXED_TO_FLOAT(xwts); y[1] = FIXED_TO_FLOAT(xwhs);
				break;
			}
			case 2: // bottom (with static slopes)
			{
				const INT32 tex = R_GetTextureNum(sd->bottomtexture);
				fixed_t v;
				if (!(ld->flags & ML_DONTPEGBOTTOM)) v = 0;
				else if (ld->flags & ML_SKEWTD) v = xwb - xwl;
				else v = fs->floorheight - bs->floorheight;
				v += sd->rowoffset;
				v %= (textures[tex]->height) << FRACBITS;
				t[3] = t[2] = v * sy; t[0] = t[1] = (v + bs->floorheight - fs->floorheight) * sy;
				if (!(ld->flags & ML_SKEWTD))
				{
					t[0] -= (xwb - fs->floorheight) * sy; t[1] -= (xwbs - fs->floorheight) * sy;
					t[3] -= (xwl - bs->floorheight) * sy; t[2] -= (xwls - bs->floorheight) * sy;
				}
				else if (ld->flags & ML_DONTPEGBOTTOM)
				{
					t[0] = t[1] = (v + xwl - xwb) * sy;
					t[2] = t[1] - (xwls - xwbs) * sy;
				}
				else
				{
					t[0] = (v + xwl - xwb) * sy; t[1] = (v + xwls - xwbs) * sy;
				}
				y[3] = FIXED_TO_FLOAT(xwl); y[0] = FIXED_TO_FLOAT(xwb); y[2] = FIXED_TO_FLOAT(xwls); y[1] = FIXED_TO_FLOAT(xwbs);
				break;
			}
			case 3: // middle
			{
				if (anySl) return false;
				const INT32 mid = R_GetTextureNum(sd->midtexture);
				const sector_t *front = ld->frontsector, *back = ld->backsector;
				const INT32 repeats = sd->repeatcnt ? 1 + sd->repeatcnt : 1;
				const fixed_t popentop = std::min(wt, wh), popenbottom = std::max(wb, wl);
				fixed_t polytop, polybottom;
				if (ld->flags & ML_NOSKEW)
				{
					if (ld->flags & ML_MIDPEG)
					{
						polybottom = std::max(front->floorheight, back->floorheight) + sd->rowoffset;
						polytop = polybottom + textureheight[mid]*repeats;
					}
					else
					{
						polytop = std::min(front->ceilingheight, back->ceilingheight) + sd->rowoffset;
						polybottom = polytop - textureheight[mid]*repeats;
					}
				}
				else if (ld->flags & ML_MIDPEG)
				{
					polybottom = popenbottom + sd->rowoffset;
					polytop = polybottom + textureheight[mid]*repeats;
				}
				else
				{
					polytop = popentop + sd->rowoffset;
					polybottom = polytop - textureheight[mid]*repeats;
				}
				const fixed_t h = std::min(popentop, polytop), l = std::max(polybottom, popenbottom);
				const fixed_t v = (ld->flags & ML_MIDPEG) ? textureheight[sd->midtexture]*repeats - h + polybottom : polytop - h;
				t[3] = t[2] = v * sy; t[0] = t[1] = (h - l + v) * sy;
				y[2] = y[3] = FIXED_TO_FLOAT(h); y[0] = y[1] = FIXED_TO_FLOAT(l);
				break;
			}
			case 4: // sky, top
				if (anySl) return false;
				y[2] = y[3] = FIXED_TO_FLOAT(INT32_MAX); y[0] = y[1] = FIXED_TO_FLOAT(wt);
				t[0] = t[1] = t[2] = t[3] = 0.0f;
				break;
			case 5: // sky, bottom
				if (anySl) return false;
				y[3] = y[2] = FIXED_TO_FLOAT(wb); y[0] = y[1] = FIXED_TO_FLOAT(INT32_MIN);
				t[0] = t[1] = t[2] = t[3] = 0.0f;
				break;
			default:
			{
				if (sp.pad < 8 || anySl) return false;
				// FOF wall of the back sector (order sp.pad-8)
				const ffloor_t *r = bs->ffloors; for (int k = 8; r && k < sp.pad; k++) r = r->next;
				if (!r) return false;
				const fixed_t lowcut = std::max(wb, wl), highcut = std::min(wt, wh);
				const fixed_t top = *r->topheight, bot = *r->bottomheight;
				fixed_t hh = top, ll = bot;
				if (hh >= highcut) hh = highcut;
				if (ll <= lowcut) ll = lowcut;
				fixed_t v = sides[r->master->sidenum[0]].rowoffset;
				const boolean attach = (ld->flags & ML_DONTPEGBOTTOM) != 0;
				if (!(r->master->flags & ML_SKEWTD))
				{
					if (attach) v -= top - bot;
					t[3] = t[2] = (top - hh + v) * sy; t[0] = t[1] = (top - ll + v) * sy;
				}
				else if (!attach) { t[3] = t[2] = v * sy; t[0] = t[1] = (hh - ll + v) * sy; }
				else { t[0] = t[1] = v * sy; t[3] = t[2] = t[0] - (hh - ll) * sy; }
				y[3] = y[2] = FIXED_TO_FLOAT(hh); y[0] = y[1] = FIXED_TO_FLOAT(ll);
				break;
			}
		}
		RRE_StaticSetYT(sp.base, 4, y, t);
	}
	return true;
}
// Flat plane (no slope): only the height changes -> every y becomes the new one
static boolean RRE_PatchHeights(size_t item, const rre_static::SItem &it)
{
	using namespace rre_static;
	if (item >= segBase)
		return RRE_PatchSegHeights(it);
	if (it.kmask != (1u << 6))
		return false;
	for (UINT32 p = it.firstPoly; p < it.firstPoly + it.numPolys; p++)
		RRE_StaticSetY(polys[p].base, polys[p].n, curPlaneY);
	return true;
}
static void RRE_ProcessSegCachedImpl(void);
static void RRE_ProcessSegCached(void)
{
	if (rre_sub::on)
	{
		const UINT64 t0 = I_GetPreciseTime();
		RRE_ProcessSegCachedImpl();
		rre_sub::segT[rre_sub::view] += I_GetPreciseTime() - t0;
		rre_sub::nProc[rre_sub::view]++;
		return;
	}
	RRE_ProcessSegCachedImpl();
}
static int RRE_ViewNow(void) { return rre_sub::view; }
static void RRE_ProcessSegCachedImpl(void)
{
	using namespace rre_static;
	seg_t *line = gl_curline;
	if (enabled && line >= segs && line < segs + numsegs && !line->polyseg
		&& SectorOk(line->frontsector) && SectorOk(line->backsector)
		&& gl_frontsector >= sectors && gl_frontsector < sectors + numsectors
		&& (!gl_backsector || (gl_backsector >= sectors && gl_backsector < sectors + numsectors)))
	{
		const UINT64 rrT0 = rre_sub::on ? I_GetPreciseTime() : 0;
		// Simple wall -> signature without heights; heights go in curSigH and get patched
		const sector_t *fs = gl_frontsector, *bs = gl_backsector;
		// Reuse what was decided if nothing about this wall changed
		const size_t segIdx = (size_t)(line - segs);
		const UINT32 mF = SectorSig(fs), mB = bs ? SectorSig(bs) : 0xBAC0u;
		UINT32 mK = 2166136261u;
		if (const side_t *sd = line->sidedef)
		{
			mK = H(mK, (UINT32)sd->rowoffset); mK = H(mK, (UINT32)sd->toptexture); mK = H(mK, (UINT32)sd->midtexture);
			mK = H(mK, (UINT32)sd->bottomtexture); mK = H(mK, (UINT32)sd->repeatcnt);
		}
		else mK = H(mK, 0x5Du);
		if (const line_t *ld = line->linedef) { mK = H(mK, (UINT32)ld->flags); mK = H(mK, (UINT32)ld->alpha); mK = H(mK, (UINT32)ld->blendmode); }
		const UINT32 mFs = (UINT32)(fs - sectors), mBs = bs ? (UINT32)(bs - sectors) : 0xFFFFFFFFu;
		SegMemo *mm = segIdx < segMemo.size() ? &segMemo[segIdx] : NULL;
		UINT32 sig;
		if (mm && mm->ok && mm->fSig == mF && mm->bSig == mB && mm->sKey == mK && mm->fs == mFs && mm->bs == mBs)
		{
			sig = mm->sig; curSigH = mm->sigH; curSimple = mm->simple ? true : false; stMemoHit++;
		}
		else
		{
			// STATIC slopes allowed if there are only top/bottom walls (whack-a-mole moles)
			const boolean anySl = bs && (fs->f_slope || fs->c_slope || bs->f_slope || bs->c_slope);
			const auto dynSl = [](const pslope_t *p) { return p && (p->flags & SL_DYNAMIC); };
			const boolean slOk = !anySl || (line->sidedef && !line->sidedef->midtexture && !bs->ffloors && !bs->numlights
				&& !dynSl(fs->f_slope) && !dynSl(fs->c_slope) && !dynSl(bs->f_slope) && !dynSl(bs->c_slope)
				&& (fs->ceilingpic == skyflatnum) == (bs->ceilingpic == skyflatnum)
				&& (fs->floorpic == skyflatnum) == (bs->floorpic == skyflatnum));
			curSimple = (bs && line->linedef && line->sidedef && slOk
				&& !fs->numlights && !fs->ffloors
				&& !(line->linedef->flags & ML_WRAPMIDTEX) && !P_IsLineTripWire(line->linedef));
			// FOFs in the back sector (drums): allowed if they are flat and simple
			boolean bFof = false;
			if (curSimple && (bs->ffloors || bs->numlights))
			{
				bFof = true; int k = 0;
				for (const ffloor_t *r = bs->ffloors; r; r = r->next, k++)
					if (k >= 24 || (r->t_slope && *r->t_slope) || (r->b_slope && *r->b_slope) || !r->topheight || !r->bottomheight
						|| !r->master || (r->master->flags & ML_TFERLINE) || r->master->sidenum[0] == 0xffff)
						{ curSimple = false; break; }
			}
			if (curSimple)
			{
				sig = H(SectorSigNoH(fs), bFof ? SectorSigRaw(bs, 2 | 4 | 8) : SectorSigNoH(bs));
				curSigH = H(H(H(H(2166136261u, (UINT32)fs->floorheight), (UINT32)fs->ceilingheight), (UINT32)bs->floorheight), (UINT32)bs->ceilingheight);
				for (const ffloor_t *r = bs->ffloors; r; r = r->next)
					curSigH = H(H(curSigH, (UINT32)*r->topheight), (UINT32)*r->bottomheight);
			}
			else
			{
				curSigH = 0;
				sig = gl_backsector ? SectorSig(gl_frontsector) : SectorSigWall1(gl_frontsector);
				sig = H(sig, gl_backsector ? SectorSig(gl_backsector) : 0xBAC0u);
			}
			sig = SideSig(sig, line->sidedef, false); // without offset (corrected at draw time)
			if (line->linedef)
			{
				sig = H(sig, line->linedef->flags);
				sig = H(sig, (UINT32)line->linedef->alpha);
				sig = H(sig, line->linedef->blendmode);
			}
			if (mm) { mm->fSig = mF; mm->bSig = mB; mm->sKey = mK; mm->fs = mFs; mm->bs = mBs; mm->sig = sig; mm->sigH = curSigH; mm->simple = curSimple ? 1 : 0; mm->ok = 1; }
			stMemoMiss++;
		}
		curHasOff = (line->sidedef != NULL);
		curTo = line->sidedef ? line->sidedef->textureoffset : 0;
		curRo = 0; // horizontal only
		const UINT64 rrT1 = rre_sub::on ? I_GetPreciseTime() : 0;
		const boolean usedC = Use(segBase + (size_t)(line - segs), sig);
		if (rre_sub::on) { const UINT64 rrT2 = I_GetPreciseTime(); rre_sub::sigT[rre_sub::view] += rrT1 - rrT0; rre_sub::useT[rre_sub::view] += rrT2 - rrT1; }
		if (usedC) { curHasOff = false; curSimple = false; return; }
		if (rre_sub::on) rre_sub::nMiss[rre_sub::view]++;
		if (Diag() && IsNeverChg(segBase + (size_t)(line - segs)) && line->sidedef)
		{
			const side_t *sd = line->sidedef;
			const INT32 tx = sd->midtexture ? sd->midtexture : (sd->bottomtexture ? sd->bottomtexture : sd->toptexture);
			Culp2((UINT32)(gl_frontsector - sectors), (UINT32)tx, 1);
			// [CULPRIT4]
			UINT32 cc[14];
			cc[0] = SectorSig(gl_frontsector);
			cc[1] = gl_backsector ? SectorSig(gl_backsector) : 0u;
			cc[2] = SideSig(2166136261u, line->sidedef, false);
			cc[3] = line->linedef ? H(H(H(2166136261u, line->linedef->flags), (UINT32)line->linedef->alpha), line->linedef->blendmode) : 0u;
			cc[4] = curSigH;
			// breakdown: 32 view, 64/128/256/512 front (base, heights, lights, FOFs), 1024..8192 back, same order
			cc[5] = (UINT32)rre_sub::view;
			const sector_t *ps[2] = { gl_frontsector, gl_backsector };
			for (int q = 0; q < 2; q++)
			{
				const sector_t *z = ps[q];
				cc[6 + q*4] = z ? SectorSigRaw(z, 0 | 4) : 0u;
				cc[7 + q*4] = z ? H(H(2166136261u, (UINT32)z->floorheight), (UINT32)z->ceilingheight) : 0u;
				cc[8 + q*4] = z ? SectorSigRaw(z, 1 | 4) : 0u;
				cc[9 + q*4] = z ? SectorSigRaw(z, 2 | 4) : 0u;
			}
			Culp4((size_t)(line - segs), (UINT32)(gl_frontsector - sectors), gl_backsector ? (UINT32)(gl_backsector - sectors) : 0xFFFFu, cc);
		}
		const UINT64 rrT3 = rre_sub::on ? I_GetPreciseTime() : 0;
		HWR_ProcessSeg();
		EndCapture();
		if (rre_sub::on)
		{
			const UINT64 rrD = I_GetPreciseTime() - rrT3;
			rre_sub::missT[rre_sub::view] += rrD;
			// diag: which kind of wall is slow in the SKY view (max 24 lines per race)
			static int slowN = 0;
			const UINT32 rrUs = (UINT32)(rrD * 1000000 / I_GetPrecisePrecision());
			if (Diag() && rre_sub::view == 1 && slowN < 24 && rrUs > 500 && line->sidedef && line->linedef)
			{
				slowN++;
				const side_t *sd = line->sidedef;
				RR_DIAGPRINT("[SLOW] seg=%u us=%u lflags=%X tex t/m/b=%d/%d/%d fs ffl=%d lights=%d bs ffl=%d lights=%d alpha=%d blend=%d\n",
					(unsigned)(line - segs), (unsigned)rrUs, (unsigned)line->linedef->flags,
					(int)sd->toptexture, (int)sd->midtexture, (int)sd->bottomtexture,
					gl_frontsector->ffloors ? 1 : 0, (int)gl_frontsector->numlights,
					(gl_backsector && gl_backsector->ffloors) ? 1 : 0, gl_backsector ? (int)gl_backsector->numlights : 0,
					(int)line->linedef->alpha, (int)line->linedef->blendmode);
			}
		}
		if (curSimple) // if what was captured does not match the prediction, do not patch
		{
			SItem &ci = items[segBase + (size_t)(line - segs)];
			if (ci.state == ST_VALID && (ci.kmask != RRE_SimpleMask() || ci.fofMask != RRE_FofMask())) ci.patchOk = 0;
		}
		curHasOff = false; curSimple = false; curKind = 0;
		return;
	}
	HWR_ProcessSeg();
}

// Float version of R_PointToAngle64 (which uses a 64-bit division:
// slow on weak CPUs). Same result: tantoangle[slope].
static inline UINT32 RRE_SlopeF(float num, float den)
{
	if (den < (512.0f / 65536.0f))
		return SLOPERANGE;
	const float a = num * (float)SLOPERANGE / den;
	return a < (float)SLOPERANGE ? (UINT32)a : SLOPERANGE;
}
static inline angle_t RRE_AngF(float x, float y)
{
	x -= FIXED_TO_FLOAT(viewx); y -= FIXED_TO_FLOAT(viewy); // same as R_PointToAngle64
	if (x == 0.0f && y == 0.0f) return 0;
	if (x >= 0)
	{
		if (y >= 0) return (x > y) ? tantoangle[RRE_SlopeF(y, x)] : ANGLE_90 - tantoangle[RRE_SlopeF(x, y)];
		y = -y;
		return (x > y) ? 0 - tantoangle[RRE_SlopeF(y, x)] : ANGLE_270 + tantoangle[RRE_SlopeF(x, y)];
	}
	x = -x;
	if (y >= 0) return (x > y) ? ANGLE_180 - tantoangle[RRE_SlopeF(y, x)] : ANGLE_90 + tantoangle[RRE_SlopeF(x, y)];
	y = -y;
	return (x > y) ? ANGLE_180 + tantoangle[RRE_SlopeF(y, x)] : ANGLE_270 - tantoangle[RRE_SlopeF(x, y)];
}

static void HWR_AddLine(seg_t * line)
{
	angle_t angle1, angle2;

	// SoM: Backsector needs to be run through R_FakeFlat
	static sector_t tempsec;

	fixed_t v1x, v1y, v2x, v2y; // the seg's vertexes as fixed_t
	if (line->polyseg && !(line->polyseg->flags & POF_RENDERSIDES))
		return;

	gl_curline = line;

	(void)v1x; (void)v1y; (void)v2x; (void)v2y;
	// Quick backface rejection (side of the seg relative to the camera,
	// like R_PointOnSegSide) before computing the 2 angles.
	{
		const polyvertex_t *a = (polyvertex_t *)gl_curline->pv1, *b = (polyvertex_t *)gl_curline->pv2;
		const float vx = FIXED_TO_FLOAT(viewx), vy = FIXED_TO_FLOAT(viewy);
		if ((vy - a->y) * (b->x - a->x) > (b->y - a->y) * (vx - a->x))
			return;
	}
	if (rre_sub::on) rre_sub::nFront[rre_sub::view]++;
	{
		// A seg's v1 is usually the previous seg's v2: reuse its angle.
		static float lx = 0.0f, ly = 0.0f; static angle_t la = 0; static UINT32 lstamp = 0;
		const polyvertex_t *a = (polyvertex_t *)gl_curline->pv1, *b = (polyvertex_t *)gl_curline->pv2;
		if (lstamp == rre_seccache::stamp && a->x == lx && a->y == ly)
		{
			angle1 = la;
			if (rre_sub::on) rre_sub::nAngReuse[rre_sub::view]++;
		}
		else
			angle1 = RRE_AngF(a->x, a->y);
		angle2 = RRE_AngF(b->x, b->y);
		lx = b->x; ly = b->y; la = angle2; lstamp = rre_seccache::stamp;
	}

	 // PrBoom: Back side, i.e. backface culling - read: endAngle >= startAngle!
	if (angle2 - angle1 < ANGLE_180)
		return;

	// PrBoom: use REAL clipping math YAYYYYYYY!!!

	if (!gld_clipper_SafeCheckRange(angle2, angle1))
	{
		return;
	}

	checkforemptylines = true;

	gl_backsector = line->backsector;

	if (!line->backsector)
	{
		gld_clipper_SafeAddClipRange(angle2, angle1);
	}
	else
	{
		boolean bothceilingssky = false, bothfloorssky = false;

		gl_backsector = R_FakeFlat(gl_backsector, &tempsec, NULL, NULL, true);

		if (gl_backsector->ceilingpic == skyflatnum && gl_frontsector->ceilingpic == skyflatnum)
			bothceilingssky = true;
		if (gl_backsector->floorpic == skyflatnum && gl_frontsector->floorpic == skyflatnum)
			bothfloorssky = true;

		if (bothceilingssky && bothfloorssky) // everything's sky? let's save us a bit of time then
		{
			if (!line->polyseg &&
				!line->sidedef->midtexture
				&& ((!gl_frontsector->ffloors && !gl_backsector->ffloors)
					|| Tag_Compare(&gl_frontsector->tags, &gl_backsector->tags)))
				return; // line is empty, don't even bother
			// treat like wide open window instead
			RRE_ProcessSegCached();
			return;
		}

		if (CheckClip(line, gl_frontsector, gl_backsector))
		{
			gld_clipper_SafeAddClipRange(angle2, angle1);
			checkforemptylines = false;
		}
		// Reject empty lines used for triggers and special events.
		// Identical floor and ceiling on both sides,
		//  identical light levels on both sides,
		//  and no middle texture.
		if (checkforemptylines && R_IsEmptyLine(line, gl_frontsector, gl_backsector))
			return;
	}

	RRE_ProcessSegCached();
	return;
}

// HWR_CheckBBox
// Checks BSP node/subtree bounding box.
// Returns true
//  if some part of the bbox might be visible.
//
// modified to use local variables

static boolean HWR_CheckBBox(fixed_t *bspcoord)
{
	INT32 boxpos;
	fixed_t px1, py1, px2, py2;
	angle_t angle1, angle2;

	// Find the corners of the box
	// that define the edges from current viewpoint.
	if (dup_viewx <= bspcoord[BOXLEFT])
		boxpos = 0;
	else if (dup_viewx < bspcoord[BOXRIGHT])
		boxpos = 1;
	else
		boxpos = 2;

	if (dup_viewy >= bspcoord[BOXTOP])
		boxpos |= 0;
	else if (dup_viewy > bspcoord[BOXBOTTOM])
		boxpos |= 1<<2;
	else
		boxpos |= 2<<2;

	if (boxpos == 5)
		return true;

	// Skip BSP branches farther than RRE_WORLDDIST
	// (distance from the closest point of the box to the camera).
	{
		// Draw distance, configurable for testing: "dist" test option (e.g. 8192). Default = 12288.
		static INT32 RRE_WORLDDIST = -1;
		if (RRE_WORLDDIST < 0)
		{
			RRE_WORLDDIST = 12288;
			{ int v = RRCfg_Int("dist", 0); if (v >= 1024 && v <= 32768) RRE_WORLDDIST = v; }
			I_OutputMsg("[DIST] draw distance = %d\n", (int)RRE_WORLDDIST);
		}
		INT32 vx = dup_viewx >> FRACBITS, vy = dup_viewy >> FRACBITS;
		INT32 dx = 0, dy = 0;
		if (vx < (bspcoord[BOXLEFT] >> FRACBITS)) dx = (bspcoord[BOXLEFT] >> FRACBITS) - vx;
		else if (vx > (bspcoord[BOXRIGHT] >> FRACBITS)) dx = vx - (bspcoord[BOXRIGHT] >> FRACBITS);
		if (vy < (bspcoord[BOXBOTTOM] >> FRACBITS)) dy = (bspcoord[BOXBOTTOM] >> FRACBITS) - vy;
		else if (vy > (bspcoord[BOXTOP] >> FRACBITS)) dy = vy - (bspcoord[BOXTOP] >> FRACBITS);
		if (RRE_GLLOWEND && (INT64)dx * dx + (INT64)dy * dy > (INT64)RRE_WORLDDIST * RRE_WORLDDIST)
			return false;
	}

	px1 = bspcoord[checkcoord[boxpos][0]];
	py1 = bspcoord[checkcoord[boxpos][1]];
	px2 = bspcoord[checkcoord[boxpos][2]];
	py2 = bspcoord[checkcoord[boxpos][3]];

	angle1 = RRE_AngF(FIXED_TO_FLOAT(px1), FIXED_TO_FLOAT(py1));
	angle2 = RRE_AngF(FIXED_TO_FLOAT(px2), FIXED_TO_FLOAT(py2));
	return gld_clipper_SafeCheckRange(angle2, angle1);
}

//
// HWR_AddPolyObjectSegs
//
// haleyjd 02/19/06
// Adds all segs in all polyobjects in the given subsector.
// Modified for hardware rendering.
//
static inline void HWR_AddPolyObjectSegs(void)
{
	size_t i, j;
	seg_t *gl_fakeline = (seg_t *)Z_Calloc(sizeof(seg_t), PU_STATIC, NULL);
	polyvertex_t *pv1 = (polyvertex_t *)Z_Calloc(sizeof(polyvertex_t), PU_STATIC, NULL);
	polyvertex_t *pv2 = (polyvertex_t *)Z_Calloc(sizeof(polyvertex_t), PU_STATIC, NULL);

	// Sort through all the polyobjects
	for (i = 0; i < numpolys; ++i)
	{
		// Render the polyobject's lines
		for (j = 0; j < po_ptrs[i]->segCount; ++j)
		{
			// Copy the info of a polyobject's seg, then convert it to OpenGL floating point
			M_Memcpy(gl_fakeline, po_ptrs[i]->segs[j], sizeof(seg_t));

			// Now convert the line to float and add it to be rendered
			pv1->x = FIXED_TO_FLOAT(gl_fakeline->v1->x);
			pv1->y = FIXED_TO_FLOAT(gl_fakeline->v1->y);
			pv2->x = FIXED_TO_FLOAT(gl_fakeline->v2->x);
			pv2->y = FIXED_TO_FLOAT(gl_fakeline->v2->y);

			gl_fakeline->pv1 = pv1;
			gl_fakeline->pv2 = pv2;

			HWR_AddLine(gl_fakeline);
		}
	}

	// Free temporary data no longer needed
	Z_Free(pv2);
	Z_Free(pv1);
	Z_Free(gl_fakeline);
}

static void HWR_RenderPolyObjectPlane(polyobj_t *polysector, boolean isceiling, fixed_t fixedheight,
									FBITFIELD blendmode, UINT8 lightlevel, levelflat_t *levelflat, sector_t *FOFsector,
									UINT8 alpha, extracolormap_t *planecolormap)
{
	FSurfaceInfo Surf;
	FOutVector *v3d;
	INT32 shader = SHADER_DEFAULT;

	size_t nrPlaneVerts = polysector->numVertices;
	INT32 i;

	float height = FIXED_TO_FLOAT(fixedheight); // constant y for all points on the convex flat polygon
	float flatxref, flatyref;
	float fflatwidth = 64.0f, fflatheight = 64.0f;
	INT32 flatflag = 63;

	boolean texflat = false;

	float scrollx = 0.0f, scrolly = 0.0f;
	angle_t angle = 0;
	fixed_t tempxs, tempyt;

	static FOutVector *planeVerts = NULL;
	static UINT16 numAllocedPlaneVerts = 0;

	if (nrPlaneVerts < 3)   // Not even a triangle?
		return;
	else if (nrPlaneVerts > (size_t)UINT16_MAX) // FIXME: exceeds plVerts size
	{
		CONS_Debug(DBG_RENDER, "polygon size of %s exceeds max value of %d vertices\n", sizeu1(nrPlaneVerts), UINT16_MAX);
		return;
	}

	// Allocate plane-vertex buffer if we need to
	if (!planeVerts || nrPlaneVerts > numAllocedPlaneVerts)
	{
		numAllocedPlaneVerts = (UINT16)nrPlaneVerts;
		Z_Free(planeVerts);
		Z_Malloc(numAllocedPlaneVerts * sizeof (FOutVector), PU_LEVEL, &planeVerts);
	}

	// set texture for polygon
	if (levelflat != NULL)
	{
		if (levelflat->type == LEVELFLAT_FLAT)
		{
			size_t len = W_LumpLength(levelflat->u.flat.lumpnum);
			size_t sflatsize = R_FlatDimensionsFromLumpSize(len);
			fflatwidth = fflatheight = (double)sflatsize;
			flatflag = sflatsize-1;
		}
		else
		{
			if (levelflat->type == LEVELFLAT_TEXTURE)
			{
				fflatwidth = textures[levelflat->u.texture.num]->width;
				fflatheight = textures[levelflat->u.texture.num]->height;
			}
			else if (levelflat->type == LEVELFLAT_PATCH || levelflat->type == LEVELFLAT_PNG)
			{
				fflatwidth = levelflat->width;
				fflatheight = levelflat->height;
			}
			texflat = true;
		}
	}
	else // set no texture
		HWR_SetCurrentTexture(NULL);

	// reference point for flat texture coord for each vertex around the polygon
	flatxref = FIXED_TO_FLOAT(polysector->origVerts[0].x);
	flatyref = FIXED_TO_FLOAT(polysector->origVerts[0].y);

	flatxref = (float)(((fixed_t)flatxref & (~flatflag)) / fflatwidth);
	flatyref = (float)(((fixed_t)flatyref & (~flatflag)) / fflatheight);

	// transform
	v3d = planeVerts;

	if (FOFsector != NULL)
	{
		if (!isceiling) // it's a floor
		{
			scrollx = FIXED_TO_FLOAT(FOFsector->floor_xoffs)/fflatwidth;
			scrolly = FIXED_TO_FLOAT(FOFsector->floor_yoffs)/fflatheight;
			angle = FOFsector->floorpic_angle;
		}
		else // it's a ceiling
		{
			scrollx = FIXED_TO_FLOAT(FOFsector->ceiling_xoffs)/fflatwidth;
			scrolly = FIXED_TO_FLOAT(FOFsector->ceiling_yoffs)/fflatheight;
			angle = FOFsector->ceilingpic_angle;
		}
	}
	else if (gl_frontsector)
	{
		if (!isceiling) // it's a floor
		{
			scrollx = FIXED_TO_FLOAT(gl_frontsector->floor_xoffs)/fflatwidth;
			scrolly = FIXED_TO_FLOAT(gl_frontsector->floor_yoffs)/fflatheight;
			angle = gl_frontsector->floorpic_angle;
		}
		else // it's a ceiling
		{
			scrollx = FIXED_TO_FLOAT(gl_frontsector->ceiling_xoffs)/fflatwidth;
			scrolly = FIXED_TO_FLOAT(gl_frontsector->ceiling_yoffs)/fflatheight;
			angle = gl_frontsector->ceilingpic_angle;
		}
	}

	if (angle) // Only needs to be done if there's an altered angle
	{
		angle = (InvAngle(angle))>>ANGLETOFINESHIFT;

		// This needs to be done so that it scrolls in a different direction after rotation like software
		/*tempxs = FLOAT_TO_FIXED(scrollx);
		tempyt = FLOAT_TO_FIXED(scrolly);
		scrollx = (FIXED_TO_FLOAT(FixedMul(tempxs, FINECOSINE(angle)) - FixedMul(tempyt, FINESINE(angle))));
		scrolly = (FIXED_TO_FLOAT(FixedMul(tempxs, FINESINE(angle)) + FixedMul(tempyt, FINECOSINE(angle))));*/

		// This needs to be done so everything aligns after rotation
		// It would be done so that rotation is done, THEN the translation, but I couldn't get it to rotate AND scroll like software does
		tempxs = FLOAT_TO_FIXED(flatxref);
		tempyt = FLOAT_TO_FIXED(flatyref);
		flatxref = (FIXED_TO_FLOAT(FixedMul(tempxs, FINECOSINE(angle)) - FixedMul(tempyt, FINESINE(angle))));
		flatyref = (FIXED_TO_FLOAT(FixedMul(tempxs, FINESINE(angle)) + FixedMul(tempyt, FINECOSINE(angle))));
	}

	for (i = 0; i < (INT32)nrPlaneVerts; i++,v3d++)
	{
		// Go from the polysector's original vertex locations
		// Means the flat is offset based on the original vertex locations
		if (texflat)
		{
			v3d->s = (float)(FIXED_TO_FLOAT(polysector->origVerts[i].x) / fflatwidth) + scrollx;
			v3d->t = -(float)(FIXED_TO_FLOAT(polysector->origVerts[i].y) / fflatheight) + scrolly;
		}
		else
		{
			v3d->s = (float)((FIXED_TO_FLOAT(polysector->origVerts[i].x) / fflatwidth) - flatxref + scrollx);
			v3d->t = (float)(flatyref - (FIXED_TO_FLOAT(polysector->origVerts[i].y) / fflatheight) + scrolly);
		}

		// Need to rotate before translate
		if (angle) // Only needs to be done if there's an altered angle
		{
			tempxs = FLOAT_TO_FIXED(v3d->s);
			tempyt = FLOAT_TO_FIXED(v3d->t);
			v3d->s = (FIXED_TO_FLOAT(FixedMul(tempxs, FINECOSINE(angle)) - FixedMul(tempyt, FINESINE(angle))));
			v3d->t = (FIXED_TO_FLOAT(FixedMul(tempxs, FINESINE(angle)) + FixedMul(tempyt, FINECOSINE(angle))));
		}

		v3d->x = FIXED_TO_FLOAT(polysector->vertices[i]->x);
		v3d->y = height;
		v3d->z = FIXED_TO_FLOAT(polysector->vertices[i]->y);
	}

	HWR_Lighting(&Surf, lightlevel, planecolormap, P_SectorUsesDirectionalLighting((FOFsector != NULL) ? FOFsector : gl_frontsector));

	if (blendmode & PF_Translucent)
	{
		Surf.PolyColor.s.alpha = (UINT8)alpha;
		blendmode |= PF_Modulated|PF_Occlude;
	}
	else
		blendmode |= PF_Masked|PF_Modulated;

	if (HWR_UseShader())
	{
		shader = SHADER_FLOOR;
		blendmode |= PF_ColorMapped;
	}

	DBG_TINT(&Surf, &blendmode, 255, 255, 0);
	HWR_ProcessPolygon(&Surf, planeVerts, nrPlaneVerts, blendmode, shader, false);
}

static void HWR_AddPolyObjectPlanes(void)
{
	size_t i;
	sector_t *polyobjsector;
	INT32 light = 0;

	// Polyobject Planes need their own function for drawing because they don't have extrasubsectors by themselves
	// It should be okay because polyobjects should always be convex anyway

	for (i  = 0; i < numpolys; i++)
	{
		polyobjsector = po_ptrs[i]->lines[0]->backsector; // the in-level polyobject sector

		if (!(po_ptrs[i]->flags & POF_RENDERPLANES)) // Only render planes when you should
			continue;

		if (po_ptrs[i]->translucency >= NUMTRANSMAPS)
			continue;

		if (polyobjsector->floorheight <= gl_frontsector->ceilingheight
			&& polyobjsector->floorheight >= gl_frontsector->floorheight
			&& (viewz < polyobjsector->floorheight))
		{
			light = R_GetPlaneLight(gl_frontsector, polyobjsector->floorheight, true);
			if (po_ptrs[i]->translucency > 0)
			{
				FSurfaceInfo Surf;
				FBITFIELD blendmode;
				memset(&Surf, 0x00, sizeof(Surf));
				blendmode = HWR_TranstableToAlpha(po_ptrs[i]->translucency, &Surf);
				HWR_AddTransparentPolyobjectFloor(&levelflats[polyobjsector->floorpic], po_ptrs[i], false, polyobjsector->floorheight,
													(light == -1 ? gl_frontsector->lightlevel : *gl_frontsector->lightlist[light].lightlevel), Surf.PolyColor.s.alpha, polyobjsector, blendmode, (light == -1 ? gl_frontsector->extra_colormap : *gl_frontsector->lightlist[light].extra_colormap));
			}
			else
			{
				HWR_GetLevelFlat(&levelflats[polyobjsector->floorpic], R_NoEncore(polyobjsector, &levelflats[polyobjsector->floorpic], false));
				HWR_RenderPolyObjectPlane(po_ptrs[i], false, polyobjsector->floorheight, PF_Occlude,
										(light == -1 ? gl_frontsector->lightlevel : *gl_frontsector->lightlist[light].lightlevel), &levelflats[polyobjsector->floorpic],
										polyobjsector, 255, (light == -1 ? gl_frontsector->extra_colormap : *gl_frontsector->lightlist[light].extra_colormap));
			}
		}

		if (polyobjsector->ceilingheight >= gl_frontsector->floorheight
			&& polyobjsector->ceilingheight <= gl_frontsector->ceilingheight
			&& (viewz > polyobjsector->ceilingheight))
		{
			light = R_GetPlaneLight(gl_frontsector, polyobjsector->ceilingheight, true);
			if (po_ptrs[i]->translucency > 0)
			{
				FSurfaceInfo Surf;
				FBITFIELD blendmode;
				memset(&Surf, 0x00, sizeof(Surf));
				blendmode = HWR_TranstableToAlpha(po_ptrs[i]->translucency, &Surf);
				HWR_AddTransparentPolyobjectFloor(&levelflats[polyobjsector->ceilingpic], po_ptrs[i], true, polyobjsector->ceilingheight,
				                                  (light == -1 ? gl_frontsector->lightlevel : *gl_frontsector->lightlist[light].lightlevel), Surf.PolyColor.s.alpha, polyobjsector, blendmode, (light == -1 ? gl_frontsector->extra_colormap : *gl_frontsector->lightlist[light].extra_colormap));
			}
			else
			{
				HWR_GetLevelFlat(&levelflats[polyobjsector->ceilingpic], R_NoEncore(polyobjsector, &levelflats[polyobjsector->ceilingpic], true));
				HWR_RenderPolyObjectPlane(po_ptrs[i], true, polyobjsector->ceilingheight, PF_Occlude,
				                          (light == -1 ? gl_frontsector->lightlevel : *gl_frontsector->lightlist[light].lightlevel), &levelflats[polyobjsector->ceilingpic],
				                          polyobjsector, 255, (light == -1 ? gl_frontsector->extra_colormap : *gl_frontsector->lightlist[light].extra_colormap));
			}
		}
	}
}

static FBITFIELD HWR_RippleBlend(sector_t *sector, ffloor_t *rover, boolean ceiling)
{
	return R_IsRipplePlane(sector, rover, ceiling) ? PF_Ripple : 0;
}

// -----------------+
// HWR_Subsector    : Determine floor/ceiling planes.
//                  : Add sprites of things in sector.
//                  : Draw one or more line segments.
// Notes            : Sets gl_cursectorlight to the light of the parent sector, to modulate wall textures
// -----------------+


static void HWR_Subsector(size_t num)
{
	INT16 count;
	seg_t *line;
	subsector_t *sub;
	static sector_t tempsec; //SoM: 4/7/2000
	INT32 floorlightlevel;
	INT32 ceilinglightlevel;
	INT32 locFloorHeight, locCeilingHeight;
	INT32 cullFloorHeight, cullCeilingHeight;
	INT32 light = 0;
	extracolormap_t *floorcolormap;
	extracolormap_t *ceilingcolormap;
	ffloor_t *rover;

#ifdef PARANOIA //no risk while developing, enough debugging nights!
	if (num >= addsubsector)
		I_Error("HWR_Subsector: ss %s with numss = %s, addss = %s\n",
			sizeu1(num), sizeu2(numsubsectors), sizeu3(addsubsector));

	/*if (num >= numsubsectors)
		I_Error("HWR_Subsector: ss %i with numss = %i",
		        num,
		        numsubsectors);*/
#endif

	if (rre_sub::on) rre_sub::nSubV[rre_sub::view]++;
	SUB_START();
	if (num < numsubsectors)
	{
		// subsector
		sub = &subsectors[num];
		// sector
		gl_frontsector = sub->sector;
		// how many linedefs
		count = sub->numlines;
		// first line seg
		line = &segs[sub->firstline];
	}
	else
	{
		// there are no segs but only planes
		sub = &subsectors[0];
		gl_frontsector = sub->sector;
		count = 0;
		line = NULL;
	}

	rre_seccache::E *rr_sc = NULL;
	if (gl_frontsector->heightsec == -1 && gl_frontsector >= sectors && (size_t)(gl_frontsector - sectors) < rre_seccache::e.size())
	{
		rr_sc = &rre_seccache::e[gl_frontsector - sectors];
		if (rr_sc->stamp == rre_seccache::stamp)
		{
			floorlightlevel = rr_sc->fll; ceilinglightlevel = rr_sc->cll;
			floorcolormap = rr_sc->fcm; ceilingcolormap = rr_sc->ccm;
			cullFloorHeight = rr_sc->cullF; cullCeilingHeight = rr_sc->cullC;
			locFloorHeight = rr_sc->locF; locCeilingHeight = rr_sc->locC;
			goto rr_sc_done;
		}
	}
	//SoM: 4/7/2000: Test to make Boom water work in Hardware mode.
	gl_frontsector = R_FakeFlat(gl_frontsector, &tempsec, &floorlightlevel,
								&ceilinglightlevel, false);
	//FIXME: Use floorlightlevel and ceilinglightlevel insted of lightlevel.

	floorcolormap = ceilingcolormap = gl_frontsector->extra_colormap;

	// ------------------------------------------------------------------------
	// sector lighting, DISABLED because it's done in HWR_StoreWallRange
	// ------------------------------------------------------------------------
	/// \todo store a RGBA instead of just intensity, allow coloured sector lighting
	//light = (FUBYTE)(sub->sector->lightlevel & 0xFF) / 255.0f;
	//gl_cursectorlight.red   = light;
	//gl_cursectorlight.green = light;
	//gl_cursectorlight.blue  = light;
	//gl_cursectorlight.alpha = light;

// ----- end special tricks -----
	cullFloorHeight   = P_GetSectorFloorZAt  (gl_frontsector, viewx, viewy);
	cullCeilingHeight = P_GetSectorCeilingZAt(gl_frontsector, viewx, viewy);
	locFloorHeight    = P_GetSectorFloorZAt  (gl_frontsector, gl_frontsector->soundorg.x, gl_frontsector->soundorg.y);
	locCeilingHeight  = P_GetSectorCeilingZAt(gl_frontsector, gl_frontsector->soundorg.x, gl_frontsector->soundorg.y);

	if (gl_frontsector->ffloors)
	{
		boolean anyMoved = gl_frontsector->moved;

		if (anyMoved == false)
		{
			for (rover = gl_frontsector->ffloors; rover; rover = rover->next)
			{
				sector_t *controlSec = &sectors[rover->secnum];
				if (controlSec->moved == true)
				{
					anyMoved = true;
					break;
				}
			}
		}

		if (anyMoved == true)
		{
			gl_frontsector->numlights = sub->sector->numlights = 0;
			R_Prep3DFloors(gl_frontsector);
			sub->sector->lightlist = gl_frontsector->lightlist;
			sub->sector->numlights = gl_frontsector->numlights;
			sub->sector->moved = gl_frontsector->moved = false;
		}

		light = R_GetPlaneLight(gl_frontsector, locFloorHeight, false);
		if (gl_frontsector->floorlightsec == -1 && !gl_frontsector->floorlightabsolute)
			floorlightlevel = std::max<INT32>(0, std::min<INT32>(255, *gl_frontsector->lightlist[light].lightlevel + gl_frontsector->floorlightlevel));
		floorcolormap = *gl_frontsector->lightlist[light].extra_colormap;

		light = R_GetPlaneLight(gl_frontsector, locCeilingHeight, false);
		if (gl_frontsector->ceilinglightsec == -1 && !gl_frontsector->ceilinglightabsolute)
			ceilinglightlevel = std::max<INT32>(0, std::min<INT32>(255, *gl_frontsector->lightlist[light].lightlevel + gl_frontsector->ceilinglightlevel));
		ceilingcolormap = *gl_frontsector->lightlist[light].extra_colormap;
	}

	if (rr_sc)
	{
		rr_sc->stamp = rre_seccache::stamp;
		rr_sc->fll = floorlightlevel; rr_sc->cll = ceilinglightlevel;
		rr_sc->fcm = floorcolormap; rr_sc->ccm = ceilingcolormap;
		rr_sc->cullF = cullFloorHeight; rr_sc->cullC = cullCeilingHeight;
		rr_sc->locF = locFloorHeight; rr_sc->locC = locCeilingHeight;
	}
rr_sc_done:
	sub->sector->extra_colormap = gl_frontsector->extra_colormap;
	SUB_MARK(S_FAKE);

	// render floor ?
#ifdef DOPLANES
	// yeah, easy backface cull! :)
	if (cullFloorHeight < dup_viewz)
	{
		if (gl_frontsector->floorpic != skyflatnum)
		{
			if (sub->validcount != validcount)
			{
				const fixed_t rr_fh = locFloorHeight == cullFloorHeight ? locFloorHeight : gl_frontsector->floorheight;
				const FBITFIELD rr_fl = PF_Occlude | HWR_RippleBlend(gl_frontsector, NULL, false);
				UINT32 rr_sig = 0;
				boolean rr_cached = false;
				if (num < numsubsectors && rre_static::SectorOk(sub->sector))
				{
					using namespace rre_static;
					if (!gl_frontsector->f_slope && !sub->sector->f_slope) // flat: height kept apart (gets patched)
					{
						rr_sig = H(H(H(SectorSigRaw(sub->sector, 0 | 4 | 16), rr_fl), (UINT32)floorlightlevel), (UINT32)(uintptr_t)floorcolormap);
						curSimple = true; curKind = 6; curSigH = H(2166136261u, (UINT32)rr_fh); curPlaneY = FIXED_TO_FLOAT(rr_fh);
					}
					else {
						rr_sig = H(H(H(H(SectorSigRaw(sub->sector, 0 | 16), gl_frontsector->f_slope ? 0 : (UINT32)rr_fh), rr_fl), (UINT32)floorlightlevel), (UINT32)(uintptr_t)floorcolormap); curSimple = false; curKind = 0; curSigH = 0; } // slope: clear a stale curSigH (left over from another wall/floor, it marked sloped floors as "never")
					rr_cached = Use(num * 2, rr_sig);
					if (!rr_cached && Diag() && IsNeverChg(num * 2)) { Culp2((UINT32)(sub->sector - sectors), (UINT32)gl_frontsector->floorpic, 2);
						Culp3((size_t)num, sub->sector, (INT32)floorlightlevel, floorcolormap, (UINT32)rr_fl, rr_fh, rr_sig, curSigH); }
					if (rr_cached) { curSimple = false; curKind = 0; curSigH = 0; }
				}
				if (!rr_cached)
				{
				HWR_GetLevelFlat(&levelflats[gl_frontsector->floorpic], R_NoEncore(gl_frontsector, &levelflats[gl_frontsector->floorpic], false));
				HWR_RenderPlane(sub, &extrasubsectors[num], false,
					// Hack to make things continue to work around slopes.
					locFloorHeight == cullFloorHeight ? locFloorHeight : gl_frontsector->floorheight,
					// We now return you to your regularly scheduled rendering.
					PF_Occlude | HWR_RippleBlend(gl_frontsector, NULL, false),
					floorlightlevel, &levelflats[gl_frontsector->floorpic], NULL, 255, floorcolormap);
				rre_static::EndCapture();
				rre_static::curSimple = false; rre_static::curKind = 0; rre_static::curSigH = 0;
				}
			}
		}
		else
		{
#ifdef POLYSKY
			HWR_RenderSkyPlane(&extrasubsectors[num], locFloorHeight);
#endif
		}
	}

	if (cullCeilingHeight > dup_viewz)
	{
		if (gl_frontsector->ceilingpic != skyflatnum)
		{
			if (sub->validcount != validcount)
			{
				const fixed_t rr_ch = locCeilingHeight == cullCeilingHeight ? locCeilingHeight : gl_frontsector->ceilingheight;
				const FBITFIELD rr_cl = PF_Occlude | HWR_RippleBlend(gl_frontsector, NULL, true);
				UINT32 rr_sig = 0;
				boolean rr_cached = false;
				if (num < numsubsectors && rre_static::SectorOk(sub->sector))
				{
					using namespace rre_static;
					if (!gl_frontsector->c_slope && !sub->sector->c_slope) // flat: height kept apart (gets patched)
					{
						rr_sig = H(H(H(SectorSigRaw(sub->sector, 0 | 4 | 32), rr_cl), (UINT32)ceilinglightlevel), (UINT32)(uintptr_t)ceilingcolormap);
						curSimple = true; curKind = 6; curSigH = H(2166136261u, (UINT32)rr_ch); curPlaneY = FIXED_TO_FLOAT(rr_ch);
					}
					else {
						rr_sig = H(H(H(H(SectorSigRaw(sub->sector, 0 | 32), gl_frontsector->c_slope ? 0 : (UINT32)rr_ch), rr_cl), (UINT32)ceilinglightlevel), (UINT32)(uintptr_t)ceilingcolormap); curSimple = false; curKind = 0; curSigH = 0; } // slope: clear a stale curSigH (left over from another wall/floor, it marked sloped ceilings as "never")
					rr_cached = Use(num * 2 + 1, rr_sig);
					if (!rr_cached && Diag() && IsNeverChg(num * 2 + 1)) Culp2((UINT32)(sub->sector - sectors), (UINT32)gl_frontsector->ceilingpic, 3);
					if (rr_cached) { curSimple = false; curKind = 0; curSigH = 0; }
				}
				if (!rr_cached)
				{
				HWR_GetLevelFlat(&levelflats[gl_frontsector->ceilingpic], R_NoEncore(gl_frontsector, &levelflats[gl_frontsector->ceilingpic], true));
				HWR_RenderPlane(sub, &extrasubsectors[num], true,
					// Hack to make things continue to work around slopes.
					locCeilingHeight == cullCeilingHeight ? locCeilingHeight : gl_frontsector->ceilingheight,
					// We now return you to your regularly scheduled rendering.
					PF_Occlude | HWR_RippleBlend(gl_frontsector, NULL, true),
					ceilinglightlevel, &levelflats[gl_frontsector->ceilingpic], NULL, 255, ceilingcolormap);
				rre_static::EndCapture();
				rre_static::curSimple = false; rre_static::curKind = 0; rre_static::curSigH = 0;
				}
			}
		}
		else
		{
#ifdef POLYSKY
			HWR_RenderSkyPlane(&extrasubsectors[num], locCeilingHeight);
#endif
		}
	}

#ifndef POLYSKY
	// Moved here because before, when above the ceiling and the floor does not have the sky flat, it doesn't draw the sky
	if (gl_frontsector->ceilingpic == skyflatnum || gl_frontsector->floorpic == skyflatnum)
		drawsky = true;
#endif

#ifdef R_FAKEFLOORS
	if (gl_frontsector->ffloors)
	{
		/// \todo fix light, xoffs, yoffs, extracolormap ?
		for (rover = gl_frontsector->ffloors;
			rover; rover = rover->next)
		{
			fixed_t cullHeight, centerHeight;

            // bottom plane
			cullHeight   = P_GetFFloorBottomZAt(rover, viewx, viewy);
			centerHeight = P_GetFFloorBottomZAt(rover, gl_frontsector->soundorg.x, gl_frontsector->soundorg.y);

			if (!(rover->fofflags & FOF_EXISTS) || !(rover->fofflags & FOF_RENDERPLANES))
				continue;
			if (sub->validcount == validcount)
				continue;

			if (centerHeight <= locCeilingHeight &&
			    centerHeight >= locFloorHeight &&
			    ((dup_viewz < cullHeight && (rover->fofflags & FOF_BOTHPLANES || !(rover->fofflags & FOF_INVERTPLANES))) ||
			     (dup_viewz > cullHeight && (rover->fofflags & FOF_BOTHPLANES || rover->fofflags & FOF_INVERTPLANES))))
			{
				if (rover->fofflags & FOF_FOG)
				{
					UINT8 alpha;

					light = R_GetPlaneLight(gl_frontsector, centerHeight, dup_viewz < cullHeight ? true : false);
					alpha = HWR_FogBlockAlpha(*gl_frontsector->lightlist[light].lightlevel, rover->master->frontsector->extra_colormap);

					HWR_AddTransparentFloor(0,
					                       &extrasubsectors[num],
										   false,
					                       *rover->bottomheight,
					                       *gl_frontsector->lightlist[light].lightlevel,
					                       alpha, rover->master->frontsector, PF_Fog|PF_NoTexture,
										   true, rover->master->frontsector->extra_colormap);
				}
				else if (rover->fofflags & FOF_TRANSLUCENT
					&& (rover->alpha < 256 || rover->blend)) // SoM: Flags are more efficient
				{
					FBITFIELD blendmode = HWR_GetBlendModeFlag(rover->blend) | HWR_RippleBlend(gl_frontsector, rover, false);

					light = R_GetPlaneLight(gl_frontsector, centerHeight, dup_viewz < cullHeight ? true : false);

					HWR_AddTransparentFloor(&levelflats[*rover->bottompic],
					                       &extrasubsectors[num],
										   false,
					                       *rover->bottomheight,
					                       *gl_frontsector->lightlist[light].lightlevel,
										   std::max<INT32>(0, std::min<INT32>(rover->alpha, 255)), rover->master->frontsector, blendmode,
					                       false, *gl_frontsector->lightlist[light].extra_colormap);
				}
				else
				{
					HWR_GetLevelFlat(&levelflats[*rover->bottompic], R_NoEncore(gl_frontsector, &levelflats[*rover->bottompic], false));
					light = R_GetPlaneLight(gl_frontsector, centerHeight, dup_viewz < cullHeight ? true : false);
					HWR_RenderPlane(sub, &extrasubsectors[num], false, *rover->bottomheight, HWR_RippleBlend(gl_frontsector, rover, false) | PF_Occlude, *gl_frontsector->lightlist[light].lightlevel, &levelflats[*rover->bottompic],
					                rover->master->frontsector, 255, *gl_frontsector->lightlist[light].extra_colormap);
				}
			}

			// top plane
			cullHeight   = P_GetFFloorTopZAt(rover, viewx, viewy);
			centerHeight = P_GetFFloorTopZAt(rover, gl_frontsector->soundorg.x, gl_frontsector->soundorg.y);

			if (centerHeight >= locFloorHeight &&
			    centerHeight <= locCeilingHeight &&
			    ((dup_viewz > cullHeight && (rover->fofflags & FOF_BOTHPLANES || !(rover->fofflags & FOF_INVERTPLANES))) ||
			     (dup_viewz < cullHeight && (rover->fofflags & FOF_BOTHPLANES || rover->fofflags & FOF_INVERTPLANES))))
			{
				if (rover->fofflags & FOF_FOG)
				{
					UINT8 alpha;

					light = R_GetPlaneLight(gl_frontsector, centerHeight, dup_viewz < cullHeight ? true : false);
					alpha = HWR_FogBlockAlpha(*gl_frontsector->lightlist[light].lightlevel, rover->master->frontsector->extra_colormap);

					HWR_AddTransparentFloor(0,
					                       &extrasubsectors[num],
										   true,
					                       *rover->topheight,
					                       *gl_frontsector->lightlist[light].lightlevel,
					                       alpha, rover->master->frontsector, PF_Fog|PF_NoTexture,
										   true, rover->master->frontsector->extra_colormap);
				}
				else if (rover->fofflags & FOF_TRANSLUCENT
					&& (rover->alpha < 256 || rover->blend)) // SoM: Flags are more efficient
				{
					FBITFIELD blendmode = HWR_GetBlendModeFlag(rover->blend) | HWR_RippleBlend(gl_frontsector, rover, false);

					light = R_GetPlaneLight(gl_frontsector, centerHeight, dup_viewz < cullHeight ? true : false);

					HWR_AddTransparentFloor(&levelflats[*rover->toppic],
					                        &extrasubsectors[num],
											true,
					                        *rover->topheight,
					                        *gl_frontsector->lightlist[light].lightlevel,
											std::max<INT32>(0, std::min<INT32>(rover->alpha, 255)), rover->master->frontsector, blendmode,
					                        false, *gl_frontsector->lightlist[light].extra_colormap);
				}
				else
				{
					HWR_GetLevelFlat(&levelflats[*rover->toppic], R_NoEncore(gl_frontsector, &levelflats[*rover->toppic], true));
					light = R_GetPlaneLight(gl_frontsector, centerHeight, dup_viewz < cullHeight ? true : false);
					HWR_RenderPlane(sub, &extrasubsectors[num], true, *rover->topheight, HWR_RippleBlend(gl_frontsector, rover, true) | PF_Occlude, *gl_frontsector->lightlist[light].lightlevel, &levelflats[*rover->toppic],
					                  rover->master->frontsector, 255, *gl_frontsector->lightlist[light].extra_colormap);
				}
			}
		}
	}
#endif
#endif //doplanes
	SUB_MARK(S_PLANES);

	// Draw all the polyobjects in this subsector
	if (sub->polyList)
	{
		polyobj_t *po = sub->polyList;

		numpolys = 0;

		// Count all the polyobjects, reset the list, and recount them
		while (po)
		{
			++numpolys;
			po = (polyobj_t *)(po->link.next);
		}

		// for render stats
		ps_numpolyobjects += numpolys;

		// Sort polyobjects
		R_SortPolyObjects(sub);

		// Draw polyobject lines.
		HWR_AddPolyObjectSegs();

		if (sub->validcount != validcount) // This validcount situation seems to let us know that the floors have already been drawn.
		{
			// Draw polyobject planes
			HWR_AddPolyObjectPlanes();
		}
	}

// Hurder ici se passe les choses INT32�essantes!
// on vient de tracer le sol et le plafond
// on trace �pr�ent d'abord les sprites et ensuite les murs
// hurdler: faux: on ajoute seulement les sprites, le murs sont trac� d'abord
	SUB_MARK(S_POLY);
	if (line)
	{
		// draw sprites first, coz they are clipped to the solidsegs of
		// subsectors more 'in front'
		HWR_AddSprites(gl_frontsector);
		SUB_MARK(S_SPR);
		if (rre_sub::on) rre_sub::nLinesV[rre_sub::view] += count;

		//Hurdler: at this point validcount must be the same, but is not because
		//         gl_frontsector doesn't point anymore to sub->sector due to
		//         the call gl_frontsector = R_FakeFlat(...)
		//         if it's not done, the sprite is drawn more than once,
		//         what looks really bad with translucency or dynamic light,
		//         without talking about the overdraw of course.
		sub->sector->validcount = validcount;/// \todo fix that in a better way

		while (count--)
		{

			if (!line->glseg && !line->polyseg) // ignore segs that belong to polyobjects
				HWR_AddLine(line);
			line++;
		}
		SUB_MARK(S_LINES);
	}

	sub->validcount = validcount;
}

//
// Renders all subsectors below a given node,
//  traversing subtree recursively.
// Just call with BSP root.

#ifdef coolhack
//t;b;l;r
static fixed_t hackbbox[4];
//BOXTOP,
//BOXBOTTOM,
//BOXLEFT,
//BOXRIGHT
static boolean HWR_CheckHackBBox(fixed_t *bb)
{
	if (bb[BOXTOP] < hackbbox[BOXBOTTOM]) //y up
		return false;
	if (bb[BOXBOTTOM] > hackbbox[BOXTOP])
		return false;
	if (bb[BOXLEFT] > hackbbox[BOXRIGHT])
		return false;
	if (bb[BOXRIGHT] < hackbbox[BOXLEFT])
		return false;
	return true;
}
#endif

// BP: big hack for a test in lighning ref : 1249753487AB
fixed_t *hwbbox;

// In the skybox view only the skybox "room" is walked
// (sectors connected to the viewpoint's sector), not the whole map.
namespace rre_skycull {
	static boolean active = false;
	static sector_t *cachedSec = NULL;
	static boolean valid = false;
	static fixed_t box[4];
	static std::vector<UINT8> seen;
	static std::vector<sector_t*> stack;
	static void Build(sector_t *start)
	{
		cachedSec = start; valid = false;
		if (!start) return;
		seen.assign(numsectors, 0);
		stack.clear(); stack.push_back(start); seen[start - sectors] = 1;
		box[BOXTOP] = box[BOXRIGHT] = INT32_MIN; box[BOXBOTTOM] = box[BOXLEFT] = INT32_MAX;
		size_t count = 0;
		while (!stack.empty())
		{
			sector_t *sec = stack.back(); stack.pop_back(); count++;
			for (size_t i = 0; i < sec->linecount; i++)
			{
				line_t *ld = sec->lines[i];
				vertex_t *vs[2] = { ld->v1, ld->v2 };
				for (int k = 0; k < 2; k++)
				{
					if (vs[k]->y > box[BOXTOP]) box[BOXTOP] = vs[k]->y;
					if (vs[k]->y < box[BOXBOTTOM]) box[BOXBOTTOM] = vs[k]->y;
					if (vs[k]->x > box[BOXRIGHT]) box[BOXRIGHT] = vs[k]->x;
					if (vs[k]->x < box[BOXLEFT]) box[BOXLEFT] = vs[k]->x;
				}
				sector_t *o = (ld->frontsector == sec) ? ld->backsector : ld->frontsector;
				if (o && !seen[o - sectors]) { seen[o - sectors] = 1; stack.push_back(o); }
			}
		}
		// If the "room" is almost the whole map, do not cull (safe side).
		valid = (count < numsectors / 2) && box[BOXTOP] > box[BOXBOTTOM];
		RR_DIAGPRINT("[SKYCULL] sectors=%u of %u, cull=%s\n", (unsigned)count, (unsigned)numsectors, valid ? "yes" : "no");
	}
	static void Begin(player_t *player)
	{
		mobj_t *vp = player->skybox.viewpoint;
		sector_t *sec = (vp && vp->subsector) ? vp->subsector->sector : NULL;
		if (sec != cachedSec) Build(sec);
		active = valid;
	}
	static inline boolean Outside(const fixed_t *b)
	{
		return b[BOXLEFT] > box[BOXRIGHT] || b[BOXRIGHT] < box[BOXLEFT]
			|| b[BOXBOTTOM] > box[BOXTOP] || b[BOXTOP] < box[BOXBOTTOM];
	}
	void LevelReset(void) { cachedSec = NULL; valid = false; active = false; }
}


static void HWR_RenderBSPNode(INT32 bspnum)
{
	/*//GZDoom code
	if(bspnum == -1)
	{
		HWR_Subsector(subsectors);
		return;
	}
	while(!((size_t)bspnum&(~NF_SUBSECTOR))) // Keep going until found a subsector
	{
		node_t *bsp = &nodes[bspnum];

		// Decide which side the view point is on
		INT32 side = R_PointOnSide(dup_viewx, dup_viewy, bsp);

		// Recursively divide front space (toward the viewer)
		HWR_RenderBSPNode(bsp->children[side]);

		// Possibly divide back space (away from viewer)
		side ^= 1;

		if (!HWR_CheckBBox(bsp->bbox[side]))
			return;

		bspnum = bsp->children[side];
	}

	HWR_Subsector(bspnum-1);
*/
	node_t *bsp = &nodes[bspnum];

	// Decide which side the view point is on
	INT32 side;

	ps_numbspcalls++;

	// Found a subsector?
	if (bspnum & NF_SUBSECTOR)
	{
		if (bspnum == -1)
		{
			//*(gl_drawsubsector_p++) = 0;
			HWR_Subsector(0);
		}
		else
		{
			//*(gl_drawsubsector_p++) = bspnum&(~NF_SUBSECTOR);
			HWR_Subsector(bspnum&(~NF_SUBSECTOR));
		}
		return;
	}

	// Decide which side the view point is on.
	side = R_PointOnSide(dup_viewx, dup_viewy, bsp);

	// BP: big hack for a test in lighning ref : 1249753487AB
	hwbbox = bsp->bbox[side];

	// Recursively divide front space.
	if (!rre_skycull::active || !rre_skycull::Outside(bsp->bbox[side]))
	HWR_RenderBSPNode(bsp->children[side]);

	// Possibly divide back space.
	if (rre_skycull::active && rre_skycull::Outside(bsp->bbox[side^1]))
		return;
	if (HWR_CheckBBox(bsp->bbox[side^1]))
	{
		// BP: big hack for a test in lighning ref : 1249753487AB
		hwbbox = bsp->bbox[side^1];
		HWR_RenderBSPNode(bsp->children[side^1]);
	}
}

/*
//
// Clear 'stack' of subsectors to draw
//
static void HWR_ClearDrawSubsectors(void)
{
	gl_drawsubsector_p = gl_drawsubsectors;
}

//
// Draw subsectors pushed on the drawsubsectors 'stack', back to front
//
static void HWR_RenderSubsectors(void)
{
	while (gl_drawsubsector_p > gl_drawsubsectors)
	{
		HWR_RenderBSPNode(
		lastsubsec->nextsubsec = bspnum & (~NF_SUBSECTOR);
	}
}
*/

// ==========================================================================
//                                                              FROM R_MAIN.C
// ==========================================================================

//BP : exactely the same as R_InitTextureMapping
void HWR_InitTextureMapping(void)
{
	angle_t i;
	INT32 x;
	INT32 t;
	fixed_t focallength;
	fixed_t grcenterx;
	fixed_t grcenterxfrac;
	INT32 grviewwidth;

#define clipanglefov (FIELDOFVIEW>>ANGLETOFINESHIFT)

	grviewwidth = vid.width;
	grcenterx = grviewwidth/2;
	grcenterxfrac = grcenterx<<FRACBITS;

	// Use tangent table to generate viewangletox:
	//  viewangletox will give the next greatest x
	//  after the view angle.
	//
	// Calc focallength
	//  so FIELDOFVIEW angles covers SCREENWIDTH.
	focallength = FixedDiv(grcenterxfrac,
		FINETANGENT(FINEANGLES/4+clipanglefov/2));

	for (i = 0; i < FINEANGLES/2; i++)
	{
		if (FINETANGENT(i) > FRACUNIT*2)
			t = -1;
		else if (FINETANGENT(i) < -FRACUNIT*2)
			t = grviewwidth+1;
		else
		{
			t = FixedMul(FINETANGENT(i), focallength);
			t = (grcenterxfrac - t+FRACUNIT-1)>>FRACBITS;

			if (t < -1)
				t = -1;
			else if (t > grviewwidth+1)
				t = grviewwidth+1;
		}
		gl_viewangletox[i] = t;
	}

	// Scan viewangletox[] to generate xtoviewangle[]:
	//  xtoviewangle will give the smallest view angle
	//  that maps to x.
	for (x = 0; x <= grviewwidth; x++)
	{
		i = 0;
		while (gl_viewangletox[i]>x)
			i++;
		gl_xtoviewangle[x] = (i<<ANGLETOFINESHIFT) - ANGLE_90;
	}

	// Take out the fencepost cases from viewangletox.
	for (i = 0; i < FINEANGLES/2; i++)
	{
		if (gl_viewangletox[i] == -1)
			gl_viewangletox[i] = 0;
		else if (gl_viewangletox[i] == grviewwidth+1)
			gl_viewangletox[i]  = grviewwidth;
	}

	gl_clipangle = gl_xtoviewangle[0];
}

// ==========================================================================
// gl_things.c
// ==========================================================================

// sprites are drawn after all wall and planes are rendered, so that
// sprite translucency effects apply on the rendered view (instead of the background sky!!)

static UINT32 gl_visspritecount;
static gl_vissprite_t *gl_visspritechunks[MAXVISSPRITES >> VISSPRITECHUNKBITS] = {NULL};

// --------------------------------------------------------------------------
// HWR_ClearSprites
// Called at frame start.
// --------------------------------------------------------------------------
static void HWR_ClearSprites(void)
{
	gl_visspritecount = 0;
}

// --------------------------------------------------------------------------
// HWR_NewVisSprite
// --------------------------------------------------------------------------
static gl_vissprite_t gl_overflowsprite;

static gl_vissprite_t *HWR_GetVisSprite(UINT32 num)
{
		UINT32 chunk = num >> VISSPRITECHUNKBITS;

		// Allocate chunk if necessary
		if (!gl_visspritechunks[chunk])
			Z_Malloc(sizeof(gl_vissprite_t) * VISSPRITESPERCHUNK, PU_LEVEL, &gl_visspritechunks[chunk]);

		return gl_visspritechunks[chunk] + (num & VISSPRITEINDEXMASK);
}

static gl_vissprite_t *HWR_NewVisSprite(void)
{
	if (gl_visspritecount == MAXVISSPRITES)
		return &gl_overflowsprite;

	gl_vissprite_t *v = HWR_GetVisSprite(gl_visspritecount++);
	v->rr_roll = 0.0f;
	return v;
}

// A hack solution for transparent surfaces appearing on top of linkdraw sprites.
// Keep a list of linkdraw sprites and draw their shapes to the z-buffer after all other
// sprite drawing is done. (effectively the z-buffer drawing of linkdraw sprites is delayed)
// NOTE: This will no longer be necessary once full translucent sorting is implemented, where
// translucent sprites and surfaces are sorted together.

typedef struct
{
	FOutVector verts[4];
	gl_vissprite_t *spr;
} zbuffersprite_t;

// this list is used to store data about linkdraw sprites
zbuffersprite_t linkdrawlist[MAXVISSPRITES];
UINT32 linkdrawcount = 0;

// add the necessary data to the list for delayed z-buffer drawing
static void HWR_LinkDrawHackAdd(FOutVector *verts, gl_vissprite_t *spr)
{
	if (linkdrawcount < MAXVISSPRITES)
	{
		memcpy(linkdrawlist[linkdrawcount].verts, verts, sizeof(FOutVector) * 4);
		linkdrawlist[linkdrawcount].spr = spr;
		linkdrawcount++;
	}
}

// process and clear the list of sprites for delayed z-buffer drawing
static void HWR_LinkDrawHackFinish(void)
{
	UINT32 i;
	FSurfaceInfo surf;
	surf.PolyColor.rgba = 0xFFFFFFFF;
	surf.TintColor.rgba = 0xFFFFFFFF;
	surf.FadeColor.rgba = 0xFFFFFFFF;
	surf.LightInfo.light_level = 0;
	surf.LightInfo.fade_start = 0;
	surf.LightInfo.fade_end = 31;
	for (i = 0; i < linkdrawcount; i++)
	{
		// draw sprite shape, only to z-buffer
		HWR_GetPatch(linkdrawlist[i].spr->gpatch);
		HWR_ProcessPolygon(&surf, linkdrawlist[i].verts, 4, PF_Translucent|PF_Occlude|PF_Invisible, 0, false);
	}
	// reset list
	linkdrawcount = 0;
}

//
// HWR_DoCulling
// Hardware version of R_DoCulling
// (see r_main.c)
static boolean HWR_DoCulling(line_t *cullheight, line_t *viewcullheight, float vz, float bottomh, float toph)
{
	float cullplane;

	if (!cullheight)
		return false;

	cullplane = FIXED_TO_FLOAT(cullheight->frontsector->floorheight);
	if (cullheight->args[1]) // Group culling
	{
		if (!viewcullheight)
			return false;

		// Make sure this is part of the same group
		if (viewcullheight->frontsector == cullheight->frontsector)
		{
			// OK, we can cull
			if (vz > cullplane && toph < cullplane) // Cull if below plane
				return true;

			if (bottomh > cullplane && vz <= cullplane) // Cull if above plane
				return true;
		}
	}
	else // Quick culling
	{
		if (vz > cullplane && toph < cullplane) // Cull if below plane
			return true;

		if (bottomh > cullplane && vz <= cullplane) // Cull if above plane
			return true;
	}

	return false;
}

static void HWR_DrawDropShadow(mobj_t *thing, fixed_t scale)
{
	patch_t *gpatch;
	FOutVector shadowVerts[4];
	FSurfaceInfo sSurf;
	float fscale; float fx; float fy; float offset;
	float ph;
	extracolormap_t *colormap = NULL;
	FBITFIELD blendmode = PF_ReverseSubtract;
	INT32 shader = SHADER_DEFAULT;
	UINT8 i;
	SINT8 flip = P_MobjFlip(thing);

	INT32 light;
	fixed_t scalemul;
	fixed_t groundz;
	fixed_t slopez;
	pslope_t *groundslope;

	// uncapped/interpolation
	interpmobjstate_t interp = {0};

	if (R_UsingFrameInterpolation() && !paused)
	{
		R_InterpolateMobjState(thing, rendertimefrac, &interp);
	}
	else
	{
		R_InterpolateMobjState(thing, FRACUNIT, &interp);
	}

	// hitlag vibrating
	if (thing->hitlag > 0 && (thing->eflags & MFE_DAMAGEHITLAG))
	{
		fixed_t jitters = HITLAGJITTERS;
		if (R_UsingFrameInterpolation() && !paused)
			jitters += (rendertimefrac / HITLAGDIV);
		fixed_t mul = thing->hitlag * jitters;

		// perhaps there could be a way to interp this too?
		if (leveltime & 1)
		{
			mul = -mul;
		}

		interp.x += FixedMul(thing->momx, mul);
		interp.y += FixedMul(thing->momy, mul);
		interp.z += FixedMul(thing->momz, mul);
	}

	// sprite offset
	interp.x += thing->sprxoff;
	interp.y += thing->spryoff;
	interp.z += thing->sprzoff;

	groundz = R_GetShadowZ(thing, &groundslope);

	gpatch = (patch_t *)W_CachePatchName("DSHADOW", PU_SPRITE);
	if (!(gpatch && ((GLPatch_t *)gpatch->hardware)->mipmap->format)) return;
	HWR_GetPatch(gpatch);

	scalemul = FixedMul(scale, (thing->radius * 2) / gpatch->height);

	ph = (float)gpatch->height;

	fscale = FIXED_TO_FLOAT(scalemul);
	fx = FIXED_TO_FLOAT(interp.x);
	fy = FIXED_TO_FLOAT(interp.y);

	if (fscale > 0.0)
	{
		offset = (ph / 2) * fscale;
	}
	else
	{
		return;
	}

	//  3--2
	//  | /|
	//  |/ |
	//  0--1

	shadowVerts[2].x = shadowVerts[3].x = fx + offset;
	shadowVerts[1].x = shadowVerts[0].x = fx - offset;
	shadowVerts[1].z = shadowVerts[2].z = fy - offset;
	shadowVerts[0].z = shadowVerts[3].z = fy + offset;

	for (i = 0; i < 4; i++)
	{
		float oldx = shadowVerts[i].x;
		float oldy = shadowVerts[i].z;
		shadowVerts[i].x = fx + ((oldx - fx) * gl_viewcos) - ((oldy - fy) * gl_viewsin);
		shadowVerts[i].z = fy + ((oldx - fx) * gl_viewsin) + ((oldy - fy) * gl_viewcos);
	}

	if (groundslope)
	{
		for (i = 0; i < 4; i++)
		{
			slopez = P_GetSlopeZAt(groundslope, FLOAT_TO_FIXED(shadowVerts[i].x), FLOAT_TO_FIXED(shadowVerts[i].z));
			shadowVerts[i].y = FIXED_TO_FLOAT(slopez) + flip * 0.05f;
		}
	}
	else
	{
		for (i = 0; i < 4; i++)
			shadowVerts[i].y = FIXED_TO_FLOAT(groundz) + flip * 0.05f;
	}

	shadowVerts[0].s = shadowVerts[3].s = 0;
	shadowVerts[2].s = shadowVerts[1].s = ((GLPatch_t *)gpatch->hardware)->max_s;

	shadowVerts[3].t = shadowVerts[2].t = 0;
	shadowVerts[0].t = shadowVerts[1].t = ((GLPatch_t *)gpatch->hardware)->max_t;

	if (!(thing->renderflags & RF_NOCOLORMAPS))
	{
		if (thing->subsector->sector->numlights)
		{
			// Always use the light at the top instead of whatever I was doing before
			light = R_GetPlaneLight(thing->subsector->sector, groundz, false);

			if (*thing->subsector->sector->lightlist[light].extra_colormap)
				colormap = *thing->subsector->sector->lightlist[light].extra_colormap;
		}
		else if (thing->subsector->sector->extra_colormap)
			colormap = thing->subsector->sector->extra_colormap;
	}

	HWR_Lighting(&sSurf, 255, colormap, P_SectorUsesDirectionalLighting(thing->subsector->sector));
	sSurf.PolyColor.s.alpha = 255;

	if (thing->whiteshadow == true)
	{
		blendmode = PF_Additive;
	}

	if (HWR_UseShader())
	{
		shader = SHADER_SPRITE;
		blendmode |= PF_ColorMapped;
	}

	DBG_TINT(&sSurf, &blendmode, 255, 128, 0);
	HWR_ProcessPolygon(&sSurf, shadowVerts, 4, blendmode|PF_Modulated, shader, false);
}

// This is expecting a pointer to an array containing 4 wallVerts for a sprite
static void HWR_RotateSpritePolyToAim(gl_vissprite_t *spr, FOutVector *wallVerts, const boolean precip)
{
	if (cv_glspritebillboarding.value
		&& spr && spr->mobj && !R_ThingIsPaperSprite(spr->mobj)
		&& wallVerts)
	{
		// uncapped/interpolation
		interpmobjstate_t interp = {0};

		// do interpolation
		if (R_UsingFrameInterpolation() && !paused)
		{
			if (spr->precip)
			{
				R_InterpolatePrecipMobjState((precipmobj_t *)spr->mobj, rendertimefrac, &interp);
			}
			else
			{
				R_InterpolateMobjState(spr->mobj, rendertimefrac, &interp);
			}
		}
		else
		{
			if (spr->precip)
			{
				R_InterpolatePrecipMobjState((precipmobj_t *)spr->mobj, FRACUNIT, &interp);
			}
			else
			{
				R_InterpolateMobjState(spr->mobj, FRACUNIT, &interp);
			}
		}

		float basey = FIXED_TO_FLOAT(interp.z);
		float lowy = wallVerts[0].y;
		if (!precip && P_MobjFlip(spr->mobj) == -1) // precip doesn't have eflags so they can't flip
		{
			basey = FIXED_TO_FLOAT(interp.z + spr->mobj->height);
		}
		// Rotate sprites to fully billboard with the camera
		// X, Y, AND Z need to be manipulated for the polys to rotate around the
		// origin, because of how the origin setting works I believe that should
		// be mobj->z or mobj->z + mobj->height
		wallVerts[2].y = wallVerts[3].y = (spr->gzt - basey) * gl_viewludsin + basey;
		wallVerts[0].y = wallVerts[1].y = (lowy - basey) * gl_viewludsin + basey;
		// translate back to be around 0 before translating back
		wallVerts[3].x += ((spr->gzt - basey) * gl_viewludcos) * gl_viewcos;
		wallVerts[2].x += ((spr->gzt - basey) * gl_viewludcos) * gl_viewcos;

		wallVerts[0].x += ((lowy - basey) * gl_viewludcos) * gl_viewcos;
		wallVerts[1].x += ((lowy - basey) * gl_viewludcos) * gl_viewcos;

		wallVerts[3].z += ((spr->gzt - basey) * gl_viewludcos) * gl_viewsin;
		wallVerts[2].z += ((spr->gzt - basey) * gl_viewludcos) * gl_viewsin;

		wallVerts[0].z += ((lowy - basey) * gl_viewludcos) * gl_viewsin;
		wallVerts[1].z += ((lowy - basey) * gl_viewludcos) * gl_viewsin;
	}
}

static void HWR_SplitSprite(gl_vissprite_t *spr)
{
	FOutVector wallVerts[4];
	FOutVector baseWallVerts[4]; // This is what the verts should end up as
	patch_t *gpatch;
	FSurfaceInfo Surf;
	extracolormap_t *colormap = NULL;
	INT32 lightlevel;
	boolean lightset = true;
	FBITFIELD blend = 0;
	FBITFIELD occlusion;
	INT32 shader = SHADER_DEFAULT;
	boolean use_linkdraw_hack = false;
	UINT8 alpha;

	INT32 i;
	float realtop, realbot, top, bot;
	float ttop, tbot, tmult;
	float bheight;
	float realheight, heightmult;
	const sector_t *sector = spr->mobj->subsector->sector;
	const lightlist_t *list = sector->lightlist;
	float endrealtop, endrealbot, endtop, endbot;
	float endbheight;
	float endrealheight;
	fixed_t temp;
	fixed_t v1x, v1y, v2x, v2y;

	gpatch = spr->gpatch;

	// cache the patch in the graphics card memory
	//12/12/99: Hurdler: same comment as above (for md2)
	//Hurdler: 25/04/2000: now support colormap in hardware mode
	rre_sprmobj = spr->mobj; HWR_GetMappedPatch(gpatch, spr->colormap); rre_sprmobj = NULL;

	baseWallVerts[0].x = baseWallVerts[3].x = spr->x1;
	baseWallVerts[2].x = baseWallVerts[1].x = spr->x2;
	baseWallVerts[0].z = baseWallVerts[3].z = spr->z1;
	baseWallVerts[1].z = baseWallVerts[2].z = spr->z2;

	baseWallVerts[2].y = baseWallVerts[3].y = spr->gzt;
	baseWallVerts[0].y = baseWallVerts[1].y = spr->gz;

	v1x = FLOAT_TO_FIXED(spr->x1);
	v1y = FLOAT_TO_FIXED(spr->z1);
	v2x = FLOAT_TO_FIXED(spr->x2);
	v2y = FLOAT_TO_FIXED(spr->z2);

	if (spr->flip)
	{
		baseWallVerts[0].s = baseWallVerts[3].s = ((GLPatch_t *)gpatch->hardware)->max_s;
		baseWallVerts[2].s = baseWallVerts[1].s = 0;
	}
	else
	{
		baseWallVerts[0].s = baseWallVerts[3].s = 0;
		baseWallVerts[2].s = baseWallVerts[1].s = ((GLPatch_t *)gpatch->hardware)->max_s;
	}

	// flip the texture coords (look familiar?)
	if (spr->vflip)
	{
		baseWallVerts[3].t = baseWallVerts[2].t = ((GLPatch_t *)gpatch->hardware)->max_t;
		baseWallVerts[0].t = baseWallVerts[1].t = 0;
	}
	else
	{
		baseWallVerts[3].t = baseWallVerts[2].t = 0;
		baseWallVerts[0].t = baseWallVerts[1].t = ((GLPatch_t *)gpatch->hardware)->max_t;
	}

	// if it has a dispoffset, push it a little towards the camera
	if (spr->dispoffset) {
		float co = -gl_viewcos*(0.05f*spr->dispoffset);
		float si = -gl_viewsin*(0.05f*spr->dispoffset);
		baseWallVerts[0].z = baseWallVerts[3].z = baseWallVerts[0].z+si;
		baseWallVerts[1].z = baseWallVerts[2].z = baseWallVerts[1].z+si;
		baseWallVerts[0].x = baseWallVerts[3].x = baseWallVerts[0].x+co;
		baseWallVerts[1].x = baseWallVerts[2].x = baseWallVerts[1].x+co;
	}

	// Let dispoffset work first since this adjust each vertex
	HWR_RotateSpritePolyToAim(spr, baseWallVerts, false);

	realtop = top = baseWallVerts[3].y;
	realbot = bot = baseWallVerts[0].y;
	ttop = baseWallVerts[3].t;
	tbot = baseWallVerts[0].t;
	tmult = (tbot - ttop) / (top - bot);

	endrealtop = endtop = baseWallVerts[2].y;
	endrealbot = endbot = baseWallVerts[1].y;

	// copy the contents of baseWallVerts into the drawn wallVerts array
	// baseWallVerts is used to know the final shape to easily get the vertex
	// co-ordinates
	memcpy(wallVerts, baseWallVerts, sizeof(baseWallVerts));

	// if sprite has linkdraw, then dont write to z-buffer (by not using PF_Occlude)
	// this will result in sprites drawn afterwards to be drawn on top like intended when using linkdraw.
	if ((spr->mobj->flags2 & MF2_LINKDRAW) && spr->mobj->tracer)
		occlusion = 0;
	else
		occlusion = PF_Occlude;

	// Determine the blendmode and translucency value
	{
		UINT32 blendmode, trans;
		if (spr->mobj->renderflags & RF_BLENDMASK)
			blendmode = (spr->mobj->renderflags & RF_BLENDMASK) >> RF_BLENDSHIFT;
		else
			blendmode = (spr->mobj->frame & FF_BLENDMASK) >> FF_BLENDSHIFT;
		if (blendmode)
			blendmode++; // realign to constants

		if (spr->mobj->renderflags & RF_TRANSMASK)
			trans = (spr->mobj->renderflags & RF_TRANSMASK) >> RF_TRANSSHIFT;
		else
			trans = (spr->mobj->frame & FF_TRANSMASK) >> FF_TRANSSHIFT;
		if (trans >= NUMTRANSMAPS)
			return; // cap

		blend = HWR_SurfaceBlend(blendmode, trans, &Surf);

		// if sprite has PF_ALWAYSONTOP, draw on top of everything.
		if (cv_debugrender_spriteclip.value || spr->mobj->renderflags & RF_ALWAYSONTOP)
			blend |= PF_NoDepthTest;

		if (!trans && !blendmode)
		{
			// BP: i agree that is little better in environement but it don't
			//     work properly under glide nor with fogcolor to ffffff :(
			// Hurdler: PF_Environement would be cool, but we need to fix
			//          the issue with the fog before
			blend |= occlusion;
			if (!occlusion) use_linkdraw_hack = true;
		}
	}

	if (HWR_UseShader())
	{
		shader = (R_ThingIsPaperSprite(spr->mobj) || R_ThingIsFloorSprite(spr->mobj)) ? SHADER_SPRITE : SHADER_SPRITECLIPHACK;
		blend |= PF_ColorMapped;
	}

	alpha = Surf.PolyColor.s.alpha;

	// Start with the lightlevel and colormap from the top of the sprite
	lightlevel = *list[sector->numlights - 1].lightlevel;
	if (!R_ThingIsFullBright(spr->mobj) && !(spr->mobj->renderflags & RF_NOCOLORMAPS))
		colormap = *list[sector->numlights - 1].extra_colormap;

	i = 0;
	temp = FLOAT_TO_FIXED(realtop);

	lightset = HWR_OverrideObjectLightLevel(spr->mobj, &lightlevel);

	for (i = 1; i < sector->numlights; i++)
	{
		fixed_t h = P_GetLightZAt(&sector->lightlist[i], spr->mobj->x, spr->mobj->y);
		if (h <= temp)
		{
			if (!lightset)
				lightlevel = *list[i-1].lightlevel > 255 ? 255 : *list[i-1].lightlevel;
			if (!R_ThingIsFullBright(spr->mobj) && !(spr->mobj->renderflags & RF_NOCOLORMAPS))
				colormap = *list[i-1].extra_colormap;
			break;
		}
	}

	if (!lightset)
		HWR_ObjectLightLevelPost(spr, sector, &lightlevel, false);

	for (i = 0; i < sector->numlights; i++)
	{
		if (endtop < endrealbot && top < realbot)
			return;

		// even if we aren't changing colormap or lightlevel, we still need to continue drawing down the sprite
		if (!(list[i].flags & FOF_NOSHADE) && (list[i].flags & FOF_CUTSPRITES))
		{
			if (!lightset)
			{
				lightlevel = *list[i].lightlevel > 255 ? 255 : *list[i].lightlevel;
				HWR_ObjectLightLevelPost(spr, sector, &lightlevel, false);
			}

			if (!R_ThingIsFullBright(spr->mobj) && !(spr->mobj->renderflags & RF_NOCOLORMAPS))
				colormap = *list[i].extra_colormap;
		}

		if (i + 1 < sector->numlights)
		{
			temp = P_GetLightZAt(&list[i+1], v1x, v1y);
			bheight = FIXED_TO_FLOAT(temp);
			temp = P_GetLightZAt(&list[i+1], v2x, v2y);
			endbheight = FIXED_TO_FLOAT(temp);
		}
		else
		{
			bheight = realbot;
			endbheight = endrealbot;
		}

		if (endbheight >= endtop && bheight >= top)
			continue;

		bot = bheight;

		if (bot < realbot)
			bot = realbot;

		endbot = endbheight;

		if (endbot < endrealbot)
			endbot = endrealbot;

		wallVerts[3].t = ttop + ((realtop - top) * tmult);
		wallVerts[2].t = ttop + ((endrealtop - endtop) * tmult);
		wallVerts[0].t = ttop + ((realtop - bot) * tmult);
		wallVerts[1].t = ttop + ((endrealtop - endbot) * tmult);

		wallVerts[3].y = top;
		wallVerts[2].y = endtop;
		wallVerts[0].y = bot;
		wallVerts[1].y = endbot;

		// The x and y only need to be adjusted in the case that it's not a papersprite
		if (cv_glspritebillboarding.value
			&& spr->mobj && !R_ThingIsPaperSprite(spr->mobj))
		{
			// Get the x and z of the vertices so billboarding draws correctly
			realheight = realbot - realtop;
			endrealheight = endrealbot - endrealtop;
			heightmult = (realtop - top) / realheight;
			wallVerts[3].x = baseWallVerts[3].x + (baseWallVerts[3].x - baseWallVerts[0].x) * heightmult;
			wallVerts[3].z = baseWallVerts[3].z + (baseWallVerts[3].z - baseWallVerts[0].z) * heightmult;

			heightmult = (endrealtop - endtop) / endrealheight;
			wallVerts[2].x = baseWallVerts[2].x + (baseWallVerts[2].x - baseWallVerts[1].x) * heightmult;
			wallVerts[2].z = baseWallVerts[2].z + (baseWallVerts[2].z - baseWallVerts[1].z) * heightmult;

			heightmult = (realtop - bot) / realheight;
			wallVerts[0].x = baseWallVerts[3].x + (baseWallVerts[3].x - baseWallVerts[0].x) * heightmult;
			wallVerts[0].z = baseWallVerts[3].z + (baseWallVerts[3].z - baseWallVerts[0].z) * heightmult;

			heightmult = (endrealtop - endbot) / endrealheight;
			wallVerts[1].x = baseWallVerts[2].x + (baseWallVerts[2].x - baseWallVerts[1].x) * heightmult;
			wallVerts[1].z = baseWallVerts[2].z + (baseWallVerts[2].z - baseWallVerts[1].z) * heightmult;
		}

		HWR_Lighting(&Surf, lightlevel, colormap, P_SectorUsesDirectionalLighting(sector) && !R_ThingIsFullBright(spr->mobj));

		Surf.PolyColor.s.alpha = alpha;

		{ DBG_TINT(&Surf, NULL, 0, 0, 255); HWR_ProcessPolygon(&Surf, wallVerts, 4, blend|PF_Modulated, shader, false); }

		if (use_linkdraw_hack)
			HWR_LinkDrawHackAdd(wallVerts, spr);

		top = bot;
		endtop = endbot;
	}

	bot = realbot;
	endbot = endrealbot;
	if (endtop <= endrealbot && top <= realbot)
		return;

	// If we're ever down here, somehow the above loop hasn't draw all the light levels of sprite
	wallVerts[3].t = ttop + ((realtop - top) * tmult);
	wallVerts[2].t = ttop + ((endrealtop - endtop) * tmult);
	wallVerts[0].t = ttop + ((realtop - bot) * tmult);
	wallVerts[1].t = ttop + ((endrealtop - endbot) * tmult);

	wallVerts[3].y = top;
	wallVerts[2].y = endtop;
	wallVerts[0].y = bot;
	wallVerts[1].y = endbot;

	HWR_Lighting(&Surf, lightlevel, colormap, P_SectorUsesDirectionalLighting(sector));

	Surf.PolyColor.s.alpha = alpha;

	{ DBG_TINT(&Surf, NULL, 0, 0, 255); HWR_ProcessPolygon(&Surf, wallVerts, 4, blend|PF_Modulated, shader, false); }

	if (use_linkdraw_hack)
		HWR_LinkDrawHackAdd(wallVerts, spr);
}

static void HWR_DrawBoundingBox(gl_vissprite_t *vis)
{
	FOutVector v[24];
	FSurfaceInfo Surf = {0};

	//
	// create a cube (side view)
	//
	//  5--4  3
	//        |
	//        |
	//  0--1  2
	//
	// repeat this 4 times (overhead)
	//
	//
	// 17    20  21    11
	//    16 15  14 10
	// 27 22  *--*  07 12
	//        |  |
	// 26 23  *--*  06 13
	//    24 00  01 02
	// 25    05  04    03
	//

	v[000].x = v[005].x = v[015].x = v[016].x = v[017].x = v[020].x =
		v[022].x = v[023].x = v[024].x = v[025].x = v[026].x = v[027].x = vis->x1; // west

	v[001].x = v[002].x = v[003].x = v[004].x = v[006].x = v[007].x =
		v[010].x = v[011].x = v[012].x = v[013].x = v[014].x = v[021].x = vis->x2; // east

	v[000].z = v[001].z = v[002].z = v[003].z = v[004].z = v[005].z =
		v[006].z = v[013].z = v[023].z = v[024].z = v[025].z = v[026].z = vis->z1; // south

	v[007].z = v[010].z = v[011].z = v[012].z = v[014].z = v[015].z =
		v[016].z = v[017].z = v[020].z = v[021].z = v[022].z = v[027].z = vis->z2; // north

	v[000].y = v[001].y = v[002].y = v[006].y = v[007].y = v[010].y =
		v[014].y = v[015].y = v[016].y = v[022].y = v[023].y = v[024].y = vis->gz; // bottom

	v[003].y = v[004].y = v[005].y = v[011].y = v[012].y = v[013].y =
		v[017].y = v[020].y = v[021].y = v[025].y = v[026].y = v[027].y = vis->gzt; // top

	Surf.PolyColor = V_GetColor(R_GetBoundingBoxColor(vis->mobj));

	HWR_ProcessPolygon(&Surf, v, 24, PF_Modulated|PF_NoTexture|PF_WireFrame, SHADER_NONE, false);
}

// -----------------+
// HWR_DrawSprite   : Draw flat sprites
//                  : (monsters, bonuses, weapons, lights, ...)
// Returns          :
// -----------------+
// Floor sprites (boost panels, etc.) in Legacy GL.
// Software draws them without a z-buffer and they are ALWAYS visible; in GL the floor hid them when the
// sprite ended up slightly below it. Real case (RR_RobotnikCoaster): panel on floor 394 using
// the slope of the NEIGHBOURING sector (414) -> part of the panel under the floor.
// Moves the point up to the real floor (or down to the ceiling if upside down). Only differences < 128 units.
// Returns true if it moved it.
static boolean HWR_SplatClampY(FOutVector *v, mobj_t *mo, float zoffset)
{
	const float maxfix = 128.0f * FIXED_TO_FLOAT(mapobjectscale);
	const fixed_t vx = FLOAT_TO_FIXED(v->x), vy = FLOAT_TO_FIXED(v->z);
	const sector_t *vsec = R_PointInSubsector(vx, vy)->sector;
	if (!(mo->eflags & MFE_VERTICALFLIP))
	{
		const float fz = FIXED_TO_FLOAT(P_GetSectorFloorZAt(vsec, vx, vy)) + zoffset;
		if (v->y < fz && fz - v->y < maxfix)
		{
			v->y = fz;
			return true;
		}
	}
	else
	{
		const float cz = FIXED_TO_FLOAT(P_GetSectorCeilingZAt(vsec, vx, vy)) + zoffset;
		if (v->y > cz && v->y - cz < maxfix)
		{
			v->y = cz;
			return true;
		}
	}
	return false;
}

// If the floor under the sprite BENDS (two sectors with different slopes), a single
// flat quad cuts through it -> flicker (z-fighting). It is drawn as an NxN grid and each
// point follows the real floor. Returns false if not needed (the normal quad is drawn).
// Cost: (N+1)^2 subsector lookups per floor sprite.
static boolean HWR_SplatFollowFloor(FSurfaceInfo *surf, const FOutVector *q, mobj_t *mo, float zoffset, FBITFIELD blend, INT32 shader)
{
	enum { N = 8 };
	FOutVector g[N+1][N+1];
	boolean moved = false;
	INT32 a, b;

	// q: 3--2 / 0--1 ; u goes from 0 to 1, w goes from 0 to 3
	for (a = 0; a <= N; a++)
	{
		const float u = (float)a / N;
		for (b = 0; b <= N; b++)
		{
			const float w = (float)b / N;
			const float k0 = (1.0f - u) * (1.0f - w), k1 = u * (1.0f - w), k2 = u * w, k3 = (1.0f - u) * w;
			FOutVector *p = &g[a][b];
			memset(p, 0, sizeof(*p));
			p->x = k0 * q[0].x + k1 * q[1].x + k2 * q[2].x + k3 * q[3].x;
			p->y = k0 * q[0].y + k1 * q[1].y + k2 * q[2].y + k3 * q[3].y;
			p->z = k0 * q[0].z + k1 * q[1].z + k2 * q[2].z + k3 * q[3].z;
			p->s = k0 * q[0].s + k1 * q[1].s + k2 * q[2].s + k3 * q[3].s;
			p->t = k0 * q[0].t + k1 * q[1].t + k2 * q[2].t + k3 * q[3].t;
			if (HWR_SplatClampY(p, mo, zoffset))
				moved = true;
		}
	}

	if (!moved)
		return false;

	for (a = 0; a < N; a++)
	{
		for (b = 0; b < N; b++)
		{
			FOutVector cell[4];
			cell[0] = g[a][b];
			cell[1] = g[a+1][b];
			cell[2] = g[a+1][b+1];
			cell[3] = g[a][b+1];
			HWR_ProcessPolygon(surf, cell, 4, blend, shader, false);
		}
	}
	return true;
}

static void HWR_DrawSprite(gl_vissprite_t *spr)
{
	FOutVector wallVerts[4];
	patch_t *gpatch;
	FSurfaceInfo Surf;
	const boolean splat = R_ThingIsFloorSprite(spr->mobj);
	// Floor sprite with its OWN slope (not the floor's), defined on the
	// object (floorspriteslope): tilted on purpose. Do not stick it to the real floor point by point.
	boolean splatfree = false;

	if (!spr->mobj)
		return;

	if (!spr->mobj->subsector)
		return;

	if (spr->mobj->subsector->sector->numlights
		&& (spr->mobj->renderflags & RF_ABSOLUTELIGHTLEVEL) == 0
		&& !splat)
	{
		HWR_SplitSprite(spr);
		return;
	}

	// cache sprite graphics
	//12/12/99: Hurdler:
	//          OK, I don't change anything for MD2 support because I want to be
	//          sure to do it the right way. So actually, we keep normal sprite
	//          in memory and we add the md2 model if it exists for that sprite

	gpatch = spr->gpatch;

#ifdef ALAM_LIGHTING
	if (!(spr->mobj->flags2 & MF2_DEBRIS) && (spr->mobj->sprite != SPR_PLAY))
		HWR_DL_AddLight(spr, gpatch);
#endif

	// create the sprite billboard
	//
	//  3--2
	//  | /|
	//  |/ |
	//  0--1

	if (splat)
	{
		F2DCoord verts[4];
		F2DCoord rotated[4];

		angle_t angle;
		float ca, sa;
		float w, h;
		float xscale, yscale;
		float xoffset, yoffset;
		float leftoffset, topoffset;
		float scale = spr->scale;
		float zoffset = (P_MobjFlip(spr->mobj) * 0.05f);
		pslope_t *splatslope = NULL;
		INT32 i;

		renderflags_t renderflags = spr->renderflags;
		if (renderflags & RF_SHADOWEFFECTS)
			scale *= spr->shadowscale;

		if (spr->rotateflags & SRF_3D || renderflags & RF_NOSPLATBILLBOARD)
			angle = spr->angle;
		else
			angle = viewangle;

		if (!spr->rotated)
			angle += spr->mobj->rollangle;

		angle = -angle;
		angle += ANGLE_90;

		topoffset = spr->spriteyoffset;
		leftoffset = spr->spritexoffset;
		if (spr->flip)
			leftoffset = ((float)gpatch->width - leftoffset);

		xscale = spr->scale * spr->spritexscale;
		yscale = spr->scale * spr->spriteyscale;

		xoffset = leftoffset * xscale;
		yoffset = topoffset * yscale;

		w = (float)gpatch->width * xscale;
		h = (float)gpatch->height * yscale;

		// Set positions

		// 3--2
		// |  |
		// 0--1

		verts[3].x = -xoffset;
		verts[3].y = yoffset;

		verts[2].x = w - xoffset;
		verts[2].y = yoffset;

		verts[1].x = w - xoffset;
		verts[1].y = -h + yoffset;

		verts[0].x = -xoffset;
		verts[0].y = -h + yoffset;

		ca = FIXED_TO_FLOAT(FINECOSINE((-angle)>>ANGLETOFINESHIFT));
		sa = FIXED_TO_FLOAT(FINESINE((-angle)>>ANGLETOFINESHIFT));

		// Rotate
		for (i = 0; i < 4; i++)
		{
			rotated[i].x = (verts[i].x * ca) - (verts[i].y * sa);
			rotated[i].y = (verts[i].x * sa) + (verts[i].y * ca);
		}

		// Translate
		for (i = 0; i < 4; i++)
		{
			wallVerts[i].x = rotated[i].x + spr->x1;
			wallVerts[i].z = rotated[i].y + spr->z1;
		}

		if (renderflags & (RF_SLOPESPLAT | RF_OBJECTSLOPESPLAT))
		{
			pslope_t *standingslope = spr->mobj->standingslope; // The slope that the object is standing on.

			// The slope that was defined for the sprite.
			if (renderflags & RF_SLOPESPLAT)
				splatslope = spr->mobj->floorspriteslope;

			if (standingslope && (renderflags & RF_OBJECTSLOPESPLAT))
				splatslope = standingslope;
		}

		// R_SplatSlope (whip recharge, MT_INSTAWHIP_RECHARGE) is NOT used in GL:
		// sloped, it showed up as "white pillars" in GL (Software clips it differently). Flat = original GL.
		splatfree = (splatslope != NULL && splatslope != spr->mobj->standingslope);

		// Set vertical position
		if (splatslope)
		{
			for (i = 0; i < 4; i++)
			{
				fixed_t slopez = P_GetSlopeZAt(splatslope, FLOAT_TO_FIXED(wallVerts[i].x), FLOAT_TO_FIXED(wallVerts[i].z));
				wallVerts[i].y = FIXED_TO_FLOAT(slopez) + zoffset;
			}
		}
		else
		{
			for (i = 0; i < 4; i++)
				wallVerts[i].y = FIXED_TO_FLOAT(spr->gz) + zoffset;
		}

		// A floor sprite can never end up BELOW the real floor (see HWR_SplatFollowFloor).
		if (!splatfree)
			for (i = 0; i < 4; i++)
				HWR_SplatClampY(&wallVerts[i], spr->mobj, zoffset);
	}
	else
	{
		// these were already scaled in HWR_ProjectSprite
		wallVerts[0].x = wallVerts[3].x = spr->x1;
		wallVerts[2].x = wallVerts[1].x = spr->x2;
		wallVerts[2].y = wallVerts[3].y = spr->gzt;
		wallVerts[0].y = wallVerts[1].y = spr->gz;

		// make a wall polygon (with 2 triangles), using the floor/ceiling heights,
		// and the 2d map coords of start/end vertices
		wallVerts[0].z = wallVerts[3].z = spr->z1;
		wallVerts[1].z = wallVerts[2].z = spr->z2;
		if (spr->rr_roll != 0.0f)
		{
			// L axis = from x2 towards x1 (left on screen), vertical axis = y
			float lx = spr->x1 - spr->x2, lz = spr->z1 - spr->z2;
			const float len = sqrtf(lx * lx + lz * lz);
			if (len > 0.0001f)
			{
				lx /= len; lz /= len;
				const float c = cosf(spr->rr_roll), sn = sinf(spr->rr_roll);
				for (INT32 i = 0; i < 4; i++)
				{
					const float u = (wallVerts[i].x - spr->rr_px) * lx + (wallVerts[i].z - spr->rr_pz) * lz;
					const float w = wallVerts[i].y - spr->rr_py;
					const float u2 = u * c + w * sn;
					const float w2 = -u * sn + w * c;
					wallVerts[i].x = spr->rr_px + u2 * lx;
					wallVerts[i].z = spr->rr_pz + u2 * lz;
					wallVerts[i].y = spr->rr_py + w2;
				}
			}
		}
	}

	// cache the patch in the graphics card memory
	//12/12/99: Hurdler: same comment as above (for md2)
	//Hurdler: 25/04/2000: now support colormap in hardware mode
	rre_sprmobj = spr->mobj; HWR_GetMappedPatch(gpatch, spr->colormap); rre_sprmobj = NULL;

	if (spr->flip)
	{
		wallVerts[0].s = wallVerts[3].s = ((GLPatch_t *)gpatch->hardware)->max_s;
		wallVerts[2].s = wallVerts[1].s = 0;
	}else{
		wallVerts[0].s = wallVerts[3].s = 0;
		wallVerts[2].s = wallVerts[1].s = ((GLPatch_t *)gpatch->hardware)->max_s;
	}

	// flip the texture coords (look familiar?)
	if (spr->vflip)
	{
		wallVerts[3].t = wallVerts[2].t = ((GLPatch_t *)gpatch->hardware)->max_t;
		wallVerts[0].t = wallVerts[1].t = 0;
	}else{
		wallVerts[3].t = wallVerts[2].t = 0;
		wallVerts[0].t = wallVerts[1].t = ((GLPatch_t *)gpatch->hardware)->max_t;
	}

	if (!splat)
	{
		// if it has a dispoffset, push it a little towards the camera
		if (spr->dispoffset) {
			float co = -gl_viewcos*(0.05f*spr->dispoffset);
			float si = -gl_viewsin*(0.05f*spr->dispoffset);
			wallVerts[0].z = wallVerts[3].z = wallVerts[0].z+si;
			wallVerts[1].z = wallVerts[2].z = wallVerts[1].z+si;
			wallVerts[0].x = wallVerts[3].x = wallVerts[0].x+co;
			wallVerts[1].x = wallVerts[2].x = wallVerts[1].x+co;
		}

		// Let dispoffset work first since this adjust each vertex
		HWR_RotateSpritePolyToAim(spr, wallVerts, false);
	}

	// This needs to be AFTER the shadows so that the regular sprites aren't drawn completely black.
	// sprite lighting by modulating the RGB components
	/// \todo coloured

	// colormap test
	{
		sector_t *sector = spr->mobj->subsector->sector;
		INT32 lightlevel = 0;
		boolean lightset = HWR_OverrideObjectLightLevel(spr->mobj, &lightlevel);
		extracolormap_t *colormap = NULL;

		if (!R_ThingIsFullBright(spr->mobj) && !(spr->mobj->renderflags & RF_NOCOLORMAPS))
			colormap = sector->extra_colormap;

		if (splat && sector->numlights)
		{
			INT32 light = R_GetPlaneLight(sector, spr->mobj->z, false);

			if (!lightset)
				lightlevel = *sector->lightlist[light].lightlevel > 255 ? 255 : *sector->lightlist[light].lightlevel;

			if (!R_ThingIsFullBright(spr->mobj) && *sector->lightlist[light].extra_colormap && !(spr->mobj->renderflags & RF_NOCOLORMAPS))
				colormap = *sector->lightlist[light].extra_colormap;
		}
		else if (!lightset)
			lightlevel = sector->lightlevel > 255 ? 255 : sector->lightlevel;

		if (!lightset)
			HWR_ObjectLightLevelPost(spr, sector, &lightlevel, false);

		HWR_Lighting(&Surf, lightlevel, colormap, P_SectorUsesDirectionalLighting(sector) && !R_ThingIsFullBright(spr->mobj));
		// DIAG [DARK2]: why Sonic shows up black in Sealed Stars
		if (spr->mobj->player)
		{
			static tic_t s_lastOsc = 0;
			if (gametic - s_lastOsc >= TICRATE)
			{
				s_lastOsc = gametic;
				RR_DIAGPRINT("[DARK2] sector_light=%d final_light=%d lightset=%d dir=%d/%d cmap=%p tint=%08lx fade=%08lx poly=%08lx fstart=%d fend=%d fb=%d\n",
					sector->lightlevel, lightlevel, (int)lightset, (int)maplighting.directional,
					(int)P_SectorUsesDirectionalLighting(sector), (void *)colormap,
					(unsigned long)Surf.TintColor.rgba, (unsigned long)Surf.FadeColor.rgba, (unsigned long)Surf.PolyColor.rgba,
					(int)Surf.LightInfo.fade_start, (int)Surf.LightInfo.fade_end, (int)R_ThingIsFullBright(spr->mobj));
			}
		}
	}

	{
		INT32 shader = SHADER_DEFAULT;
		FBITFIELD blend = 0;
		FBITFIELD occlusion;
		boolean use_linkdraw_hack = false;

		// if sprite has linkdraw, then dont write to z-buffer (by not using PF_Occlude)
		// this will result in sprites drawn afterwards to be drawn on top like intended when using linkdraw.
		if ((spr->mobj->flags2 & MF2_LINKDRAW) && spr->mobj->tracer)
			occlusion = 0;
		else
			occlusion = PF_Occlude;

		// Determine the blendmode and translucency value
		{
			UINT32 blendmode, trans;
			if (spr->mobj->renderflags & RF_BLENDMASK)
				blendmode = (spr->mobj->renderflags & RF_BLENDMASK) >> RF_BLENDSHIFT;
			else
				blendmode = (spr->mobj->frame & FF_BLENDMASK) >> FF_BLENDSHIFT;
			if (blendmode)
				blendmode++; // realign to constants

			if (spr->mobj->renderflags & RF_TRANSMASK)
				trans = (spr->mobj->renderflags & RF_TRANSMASK) >> RF_TRANSSHIFT;
			else
				trans = (spr->mobj->frame & FF_TRANSMASK) >> FF_TRANSSHIFT;
			if (trans >= NUMTRANSMAPS)
				return; // cap

			blend = HWR_SurfaceBlend(blendmode, trans, &Surf);

			// if sprite has PF_ALWAYSONTOP, draw on top of everything.
			if (cv_debugrender_spriteclip.value || spr->mobj->renderflags & RF_ALWAYSONTOP)
				blend |= PF_NoDepthTest;

			if (!trans && !blendmode)
			{
				// BP: i agree that is little better in environement but it don't
				//     work properly under glide nor with fogcolor to ffffff :(
				// Hurdler: PF_Environement would be cool, but we need to fix
				//          the issue with the fog before
				blend |= occlusion;
				if (!occlusion) use_linkdraw_hack = true;
			}
		}

		if (spr->renderflags & RF_SHADOWEFFECTS)
		{
			INT32 alpha = Surf.PolyColor.s.alpha;
			alpha -= ((INT32)(spr->shadowheight / 4.0f)) + 75;
			if (alpha < 1)
				return;

			Surf.PolyColor.s.alpha = (UINT8)(alpha);
			blend = PF_Translucent|occlusion;
			if (!occlusion) use_linkdraw_hack = true;
		}

		// Floor sprites (boost panels, etc.) with a depth offset.
		// They were only 0.05 units above the floor: from far away the z-buffer precision is not enough and the floor
		// hid them (in Software they are always visible). PF_Decal = glPolygonOffset(-1,-1).
		if (R_ThingIsFloorSprite(spr->mobj))
			blend |= PF_Decal;

		if (HWR_UseShader())
		{
			shader = (R_ThingIsPaperSprite(spr->mobj)
				|| R_ThingIsFloorSprite(spr->mobj)
				|| (spr->mobj->terrain && spr->mobj->terrain->floorClip)
				) ? SHADER_SPRITE : SHADER_SPRITECLIPHACK;
			blend |= PF_ColorMapped;
		}

		{ DBG_TINT(&Surf, NULL, 0, 0, 255);
			if (!(splat && !splatfree && HWR_SplatFollowFloor(&Surf, wallVerts, spr->mobj, P_MobjFlip(spr->mobj) * 0.05f, blend|PF_Modulated, shader)))
				HWR_ProcessPolygon(&Surf, wallVerts, 4, blend|PF_Modulated, shader, false); }

		if (use_linkdraw_hack)
			HWR_LinkDrawHackAdd(wallVerts, spr);
	}
}

#ifdef HWPRECIP
// Sprite drawer for precipitation
static inline void HWR_DrawPrecipitationSprite(gl_vissprite_t *spr)
{
	INT32 shader = SHADER_DEFAULT;
	FBITFIELD blend = 0;
	FOutVector wallVerts[4];
	patch_t *gpatch;
	FSurfaceInfo Surf;

	if (!spr->mobj)
		return;

	if (!spr->mobj->subsector)
		return;

	// cache sprite graphics
	gpatch = spr->gpatch;

	// create the sprite billboard
	//
	//  3--2
	//  | /|
	//  |/ |
	//  0--1
	wallVerts[0].x = wallVerts[3].x = spr->x1;
	wallVerts[2].x = wallVerts[1].x = spr->x2;
	wallVerts[2].y = wallVerts[3].y = spr->gzt;
	wallVerts[0].y = wallVerts[1].y = spr->gz;

	// make a wall polygon (with 2 triangles), using the floor/ceiling heights,
	// and the 2d map coords of start/end vertices
	wallVerts[0].z = wallVerts[3].z = spr->z1;
	wallVerts[1].z = wallVerts[2].z = spr->z2;

	// Let dispoffset work first since this adjust each vertex
	HWR_RotateSpritePolyToAim(spr, wallVerts, true);

	wallVerts[0].s = wallVerts[3].s = 0;
	wallVerts[2].s = wallVerts[1].s = ((GLPatch_t *)gpatch->hardware)->max_s;

	wallVerts[3].t = wallVerts[2].t = 0;
	wallVerts[0].t = wallVerts[1].t = ((GLPatch_t *)gpatch->hardware)->max_t;

	// cache the patch in the graphics card memory
	//12/12/99: Hurdler: same comment as above (for md2)
	//Hurdler: 25/04/2000: now support colormap in hardware mode
	rre_sprmobj = spr->mobj; HWR_GetMappedPatch(gpatch, spr->colormap); rre_sprmobj = NULL;

	// colormap test
	{
		sector_t *sector = spr->mobj->subsector->sector;
		UINT8 lightlevel = 255;
		extracolormap_t *colormap = sector->extra_colormap;

		if (sector->numlights)
		{
			// Always use the light at the top instead of whatever I was doing before
			INT32 light = R_GetPlaneLight(sector, spr->mobj->z + spr->mobj->height, false);

			if (!R_ThingIsFullBright(spr->mobj))
				lightlevel = *sector->lightlist[light].lightlevel > 255 ? 255 : *sector->lightlist[light].lightlevel;

			if (*sector->lightlist[light].extra_colormap)
				colormap = *sector->lightlist[light].extra_colormap;
		}
		else
		{
			if (!R_ThingIsFullBright(spr->mobj))
				lightlevel = sector->lightlevel > 255 ? 255 : sector->lightlevel;

			if (sector->extra_colormap)
				colormap = sector->extra_colormap;
		}

		//lightlevel = 128 + (lightlevel>>1);

		HWR_Lighting(&Surf, lightlevel, colormap, P_SectorUsesDirectionalLighting(sector));
	}

	// Determine the blendmode and translucency value
	{
		UINT32 blendmode, trans;
		if (spr->mobj->renderflags & RF_BLENDMASK)
			blendmode = (spr->mobj->renderflags & RF_BLENDMASK) >> RF_BLENDSHIFT;
		else
			blendmode = (spr->mobj->frame & FF_BLENDMASK) >> FF_BLENDSHIFT;
		if (blendmode)
			blendmode++; // realign to constants

		if (spr->mobj->renderflags & RF_TRANSMASK)
			trans = (spr->mobj->renderflags & RF_TRANSMASK) >> RF_TRANSSHIFT;
		else
			trans = (spr->mobj->frame & FF_TRANSMASK) >> FF_TRANSSHIFT;
		if (trans >= NUMTRANSMAPS)
			return; // cap

		blend = HWR_SurfaceBlend(blendmode, trans, &Surf);
		if (!trans && !blendmode)
		{
			// BP: i agree that is little better in environement but it don't
			//     work properly under glide nor with fogcolor to ffffff :(
			// Hurdler: PF_Environement would be cool, but we need to fix
			//          the issue with the fog before
			blend |= PF_Occlude;
		}
	}

	if (HWR_UseShader())
	{
		shader = SHADER_SPRITE;
		blend |= PF_ColorMapped;
	}

	{ DBG_TINT(&Surf, NULL, 0, 0, 255); HWR_ProcessPolygon(&Surf, wallVerts, 4, blend|PF_Modulated, shader, false); }
}
#endif

// --------------------------------------------------------------------------
// Sort vissprites by distance
// --------------------------------------------------------------------------
gl_vissprite_t* gl_vsprorder[MAXVISSPRITES];

// Note: For more correct transparency the transparent sprites would need to be
// sorted and drawn together with transparent surfaces.
static int CompareVisSprites(const void *p1, const void *p2)
{
	gl_vissprite_t* spr1 = *(gl_vissprite_t*const*)p1;
	gl_vissprite_t* spr2 = *(gl_vissprite_t*const*)p2;
	int idiff;
	float fdiff;
	float tz1, tz2;

	// Make transparent sprites last. Comment from the previous sort implementation:
	// Sryder:	Oh boy, while it's nice having ALL the sprites sorted properly, it fails when we bring MD2's into the
	//			mix and they want to be translucent. So let's place all the translucent sprites and MD2's AFTER
	//			everything else, but still ordered of course, the depth buffer can handle the opaque ones plenty fine.
	//			We just need to move all translucent ones to the end in order
	// TODO:	Fully sort all sprites and MD2s with walls and floors, this part will be unnecessary after that
	int transparency1;
	int transparency2;

	int renderflags1;
	int renderflags2;

	int frame1;
	int frame2;

	int linkdraw1;
	int linkdraw2;

	// bbox doesn't need to be sorted
	if (spr1->bbox || spr2->bbox)
		return 0;

	// check for precip first, because then sprX->mobj is actually a precipmobj_t and does not have flags2 or tracer
	linkdraw1 = !spr1->precip && (spr1->mobj->flags2 & MF2_LINKDRAW) && spr1->mobj->tracer;
	linkdraw2 = !spr2->precip && (spr2->mobj->flags2 & MF2_LINKDRAW) && spr2->mobj->tracer;

	// ^ is the XOR operation
	// if comparing a linkdraw and non-linkdraw sprite or 2 linkdraw sprites with different tracers, then use
	// the tracer's properties instead of the main sprite's.
	if ((linkdraw1 && linkdraw2 && spr1->mobj->tracer != spr2->mobj->tracer) || (linkdraw1 ^ linkdraw2))
	{
		if (linkdraw1)
		{
			tz1 = spr1->tracertz;
			renderflags1 = spr1->mobj->tracer->renderflags;
			frame1 = spr1->mobj->tracer->frame;
		}
		else
		{
			tz1 = spr1->tz;
			renderflags1 = spr1->mobj->renderflags;
			frame1 = spr1->mobj->frame;
		}
		if (linkdraw2)
		{
			tz2 = spr2->tracertz;
			renderflags2 = spr2->mobj->tracer->renderflags;
			frame2 = spr2->mobj->tracer->frame;
		}
		else
		{
			tz2 = spr2->tz;
			renderflags2 = spr2->mobj->renderflags;
			frame2 = spr2->mobj->frame;
		}
	}
	else
	{
		tz1 = spr1->tz;
		renderflags1 = (spr1->precip ? 0 : spr1->mobj->renderflags);
		frame1 = spr1->mobj->frame;
		tz2 = spr2->tz;
		renderflags2 = (spr2->precip ? 0 : spr2->mobj->renderflags);
		frame2 = spr2->mobj->frame;
	}

	// first compare transparency flags, then compare tz, then compare dispoffset

	transparency1 = (renderflags1 & RF_TRANSMASK) ?
		((renderflags1 & RF_TRANSMASK)>>RF_TRANSSHIFT) :
		((frame1 & FF_TRANSMASK)>>FF_TRANSSHIFT);

	transparency2 = (renderflags2 & RF_TRANSMASK) ?
		((renderflags2 & RF_TRANSMASK)>>RF_TRANSSHIFT) :
		((frame2 & FF_TRANSMASK)>>FF_TRANSSHIFT);

	idiff = transparency1 - transparency2;
	if (idiff != 0) return idiff;

	fdiff = tz2 - tz1; // this order seems correct when checking with apitrace. Back to front.
	if (fabsf(fdiff) < 1.0E-36f)
		return spr1->dispoffset - spr2->dispoffset; // smallest dispoffset first if sprites are at (almost) same location.
	else if (fdiff > 0)
		return 1;
	else
		return -1;
}

static void HWR_SortVisSprites(void)
{
	UINT32 i;
	for (i = 0; i < gl_visspritecount; i++)
	{
		gl_vsprorder[i] = HWR_GetVisSprite(i);
	}
	qsort(gl_vsprorder, gl_visspritecount, sizeof(gl_vissprite_t*), CompareVisSprites);
}

// A drawnode is something that points to a 3D floor, 3D side, or masked
// middle texture. This is used for sorting with sprites.
typedef struct
{
	FOutVector    wallVerts[4];
	FSurfaceInfo  Surf;
	INT32         texnum, basetexnum;
	FBITFIELD     blend;
	INT32         drawcount;
	boolean fogwall;
	INT32 lightlevel;
	extracolormap_t *wallcolormap; // Doing the lighting in HWR_RenderWall now for correct fog after sorting
} wallinfo_t;

static wallinfo_t *wallinfo = NULL;
static size_t numwalls = 0; // a list of transparent walls to be drawn

void HWR_RenderWall(FOutVector *wallVerts, FSurfaceInfo *pSurf, FBITFIELD blend, boolean fogwall, INT32 lightlevel, extracolormap_t *wallcolormap);

#define MAX_TRANSPARENTWALL 256

typedef struct
{
	extrasubsector_t *xsub;
	boolean isceiling;
	fixed_t fixedheight;
	INT32 lightlevel;
	levelflat_t *levelflat;
	INT32 alpha;
	sector_t *FOFSector;
	FBITFIELD blend;
	boolean fogplane;
	extracolormap_t *planecolormap;
	INT32 drawcount;
} planeinfo_t;

static size_t numplanes = 0; // a list of transparent floors to be drawn
static planeinfo_t *planeinfo = NULL;

typedef struct
{
	polyobj_t *polysector;
	boolean isceiling;
	fixed_t fixedheight;
	INT32 lightlevel;
	levelflat_t *levelflat;
	INT32 alpha;
	sector_t *FOFSector;
	FBITFIELD blend;
	extracolormap_t *planecolormap;
	INT32 drawcount;
} polyplaneinfo_t;

static size_t numpolyplanes = 0; // a list of transparent poyobject floors to be drawn
static polyplaneinfo_t *polyplaneinfo = NULL;

//Hurdler: 3D water sutffs
typedef struct gl_drawnode_s
{
	planeinfo_t *plane;
	polyplaneinfo_t *polyplane;
	wallinfo_t *wall;
	gl_vissprite_t *sprite;

//	struct gl_drawnode_s *next;
//	struct gl_drawnode_s *prev;
} gl_drawnode_t;

static INT32 drawcount = 0;

#define MAX_TRANSPARENTFLOOR 512

// This will likely turn into a copy of HWR_Add3DWater and replace it.
void HWR_AddTransparentFloor(levelflat_t *levelflat, extrasubsector_t *xsub, boolean isceiling, fixed_t fixedheight, INT32 lightlevel, INT32 alpha, sector_t *FOFSector, FBITFIELD blend, boolean fogplane, extracolormap_t *planecolormap)
{
	static size_t allocedplanes = 0;

	// Force realloc if buffer has been freed
	if (!planeinfo)
		allocedplanes = 0;

	if (allocedplanes < numplanes + 1)
	{
		allocedplanes += MAX_TRANSPARENTFLOOR;
		Z_Realloc(planeinfo, allocedplanes * sizeof (*planeinfo), PU_LEVEL, &planeinfo);
	}

	planeinfo[numplanes].isceiling = isceiling;
	planeinfo[numplanes].fixedheight = fixedheight;
	planeinfo[numplanes].lightlevel = (planecolormap && (planecolormap->flags & CMF_FOG)) ? lightlevel : 255;
	planeinfo[numplanes].levelflat = levelflat;
	planeinfo[numplanes].xsub = xsub;
	planeinfo[numplanes].alpha = alpha;
	planeinfo[numplanes].FOFSector = FOFSector;
	planeinfo[numplanes].blend = blend;
	planeinfo[numplanes].fogplane = fogplane;
	planeinfo[numplanes].planecolormap = planecolormap;
	planeinfo[numplanes].drawcount = drawcount++;

	numplanes++;
}

// Adding this for now until I can create extrasubsector info for polyobjects
// When that happens it'll just be done through HWR_AddTransparentFloor and HWR_RenderPlane
void HWR_AddTransparentPolyobjectFloor(levelflat_t *levelflat, polyobj_t *polysector, boolean isceiling, fixed_t fixedheight, INT32 lightlevel, INT32 alpha, sector_t *FOFSector, FBITFIELD blend, extracolormap_t *planecolormap)
{
	static size_t allocedpolyplanes = 0;

	// Force realloc if buffer has been freed
	if (!polyplaneinfo)
		allocedpolyplanes = 0;

	if (allocedpolyplanes < numpolyplanes + 1)
	{
		allocedpolyplanes += MAX_TRANSPARENTFLOOR;
		Z_Realloc(polyplaneinfo, allocedpolyplanes * sizeof (*polyplaneinfo), PU_LEVEL, &polyplaneinfo);
	}

	polyplaneinfo[numpolyplanes].isceiling = isceiling;
	polyplaneinfo[numpolyplanes].fixedheight = fixedheight;
	polyplaneinfo[numpolyplanes].lightlevel = (planecolormap && (planecolormap->flags & CMF_FOG)) ? lightlevel : 255;
	polyplaneinfo[numpolyplanes].levelflat = levelflat;
	polyplaneinfo[numpolyplanes].polysector = polysector;
	polyplaneinfo[numpolyplanes].alpha = alpha;
	polyplaneinfo[numpolyplanes].FOFSector = FOFSector;
	polyplaneinfo[numpolyplanes].blend = blend;
	polyplaneinfo[numpolyplanes].planecolormap = planecolormap;
	polyplaneinfo[numpolyplanes].drawcount = drawcount++;
	numpolyplanes++;
}

// putting sortindex and sortnode here so the comparator function can see them
gl_drawnode_t *sortnode;
size_t *sortindex;

static int CompareDrawNodes(const void *p1, const void *p2)
{
	size_t n1 = *(const size_t*)p1;
	size_t n2 = *(const size_t*)p2;
	INT32 v1 = 0;
	INT32 v2 = 0;
	INT32 diff;
	if (sortnode[n1].plane)
		v1 = sortnode[n1].plane->drawcount;
	else if (sortnode[n1].polyplane)
		v1 = sortnode[n1].polyplane->drawcount;
	else if (sortnode[n1].wall)
		v1 = sortnode[n1].wall->drawcount;
	else I_Error("CompareDrawNodes: n1 unknown");

	if (sortnode[n2].plane)
		v2 = sortnode[n2].plane->drawcount;
	else if (sortnode[n2].polyplane)
		v2 = sortnode[n2].polyplane->drawcount;
	else if (sortnode[n2].wall)
		v2 = sortnode[n2].wall->drawcount;
	else I_Error("CompareDrawNodes: n2 unknown");

	diff = v2 - v1;
	if (diff == 0) I_Error("CompareDrawNodes: diff is zero");
	return diff;
}

static int CompareDrawNodePlanes(const void *p1, const void *p2)
{
	size_t n1 = *(const size_t*)p1;
	size_t n2 = *(const size_t*)p2;
	if (!sortnode[n1].plane) I_Error("CompareDrawNodePlanes: Uh.. This isn't a plane! (n1)");
	if (!sortnode[n2].plane) I_Error("CompareDrawNodePlanes: Uh.. This isn't a plane! (n2)");
	return ABS(sortnode[n2].plane->fixedheight - viewz) - ABS(sortnode[n1].plane->fixedheight - viewz);
}

//
// HWR_CreateDrawNodes
// Creates and sorts a list of drawnodes for the scene being rendered.
static void HWR_CreateDrawNodes(void)
{
	UINT32 i = 0, p = 0;
	size_t run_start = 0;

	// Dump EVERYTHING into a huge drawnode list. Then we'll sort it!
	// Could this be optimized into _AddTransparentWall/_AddTransparentPlane?
	// Hell yes! But sort algorithm must be modified to use a linked list.
	// MEMORY: this used to be Z_Calloc + Z_Free EVERY frame (variable size, ~90 KB in
	// Playground): a big allocation per frame = fragmentation, and it is where Playground
	// ran out of memory on a 64 MB system. Now a buffer that is reused and only grows (with headroom).
	{
		static gl_drawnode_t *s_nodes = NULL;
		static size_t *s_index = NULL;
		static size_t s_cap = 0;
		const size_t need = (size_t)numplanes + numpolyplanes + numwalls;
		if (need > s_cap || !s_nodes)
		{
			const size_t cap = need + need / 4 + 256;
			if (s_nodes) Z_Free(s_nodes);
			if (s_index) Z_Free(s_index);
			s_nodes = (gl_drawnode_t *)Z_Malloc(sizeof(gl_drawnode_t) * cap, PU_STATIC, NULL);
			s_index = (size_t *)Z_Malloc(sizeof(size_t) * cap, PU_STATIC, NULL);
			s_cap = cap;
		}
		memset(s_nodes, 0, sizeof(gl_drawnode_t) * need);
		sortnode = s_nodes;
		sortindex = s_index;
	}
	// todo:
	// However, in reality we shouldn't be re-copying and shifting all this information
	// that is already lying around. This should all be in some sort of linked list or lists.

	ps_hw_nodesorttime = I_GetPreciseTime();

	for (i = 0; i < numplanes; i++, p++)
	{
		sortnode[p].plane = &planeinfo[i];
		sortindex[p] = p;
	}

	for (i = 0; i < numpolyplanes; i++, p++)
	{
		sortnode[p].polyplane = &polyplaneinfo[i];
		sortindex[p] = p;
	}

	for (i = 0; i < numwalls; i++, p++)
	{
		sortnode[p].wall = &wallinfo[i];
		sortindex[p] = p;
	}

	ps_numdrawnodes = p;

	// p is the number of stuff to sort

	// sort the list based on the value of the 'drawcount' member of the drawnodes.
	qsort(sortindex, p, sizeof(size_t), CompareDrawNodes);

	// an additional pass is needed to correct the order of consecutive planes in the list.
	// for each consecutive run of planes in the list, sort that run based on plane height and view height.
	while (run_start < p-1)// p-1 because a 1 plane run at the end of the list does not count
	{
		// locate run start
		if (sortnode[sortindex[run_start]].plane)
		{
			// found it, now look for run end
			size_t run_end;// (inclusive)
			for (i = run_start+1; i < p; i++)// size_t and UINT32 being used mixed here... shouldnt break anything though..
			{
				if (!sortnode[sortindex[i]].plane) break;
			}
			run_end = i-1;
			if (run_end > run_start)// if there are multiple consecutive planes, not just one
			{
				// consecutive run of planes found, now sort it
				qsort(sortindex + run_start, run_end - run_start + 1, sizeof(size_t), CompareDrawNodePlanes);
			}
			run_start = run_end + 1;// continue looking for runs coming right after this one
		}
		else
		{
			// this wasnt the run start, try next one
			run_start++;
		}
	}

	ps_hw_nodesorttime = I_GetPreciseTime() - ps_hw_nodesorttime;

	ps_hw_nodedrawtime = I_GetPreciseTime();

	// Okay! Let's draw it all! Woo!
	HWD.pfnSetTransform(&atransform);
	HWD.pfnSetShader(SHADER_DEFAULT);

	for (i = 0; i < p; i++)
	{
		if (sortnode[sortindex[i]].plane)
		{
			// We aren't traversing the BSP tree, so make gl_frontsector null to avoid crashes.
			gl_frontsector = NULL;

			if (!(sortnode[sortindex[i]].plane->blend & PF_NoTexture))
				HWR_GetLevelFlat(sortnode[sortindex[i]].plane->levelflat, R_NoEncore(sortnode[sortindex[i]].plane->FOFSector, sortnode[sortindex[i]].plane->levelflat, sortnode[sortindex[i]].plane->isceiling));
			HWR_RenderPlane(NULL, sortnode[sortindex[i]].plane->xsub, sortnode[sortindex[i]].plane->isceiling, sortnode[sortindex[i]].plane->fixedheight, sortnode[sortindex[i]].plane->blend, sortnode[sortindex[i]].plane->lightlevel,
				sortnode[sortindex[i]].plane->levelflat, sortnode[sortindex[i]].plane->FOFSector, sortnode[sortindex[i]].plane->alpha, sortnode[sortindex[i]].plane->planecolormap);
		}
		else if (sortnode[sortindex[i]].polyplane)
		{
			// We aren't traversing the BSP tree, so make gl_frontsector null to avoid crashes.
			gl_frontsector = NULL;

			if (!(sortnode[sortindex[i]].polyplane->blend & PF_NoTexture))
				HWR_GetLevelFlat(sortnode[sortindex[i]].polyplane->levelflat, R_NoEncore(sortnode[sortindex[i]].polyplane->FOFSector, sortnode[sortindex[i]].polyplane->levelflat, sortnode[sortindex[i]].polyplane->isceiling));
			HWR_RenderPolyObjectPlane(sortnode[sortindex[i]].polyplane->polysector, sortnode[sortindex[i]].polyplane->isceiling, sortnode[sortindex[i]].polyplane->fixedheight, sortnode[sortindex[i]].polyplane->blend, sortnode[sortindex[i]].polyplane->lightlevel,
				sortnode[sortindex[i]].polyplane->levelflat, sortnode[sortindex[i]].polyplane->FOFSector, sortnode[sortindex[i]].polyplane->alpha, sortnode[sortindex[i]].polyplane->planecolormap);
		}
		else if (sortnode[sortindex[i]].wall)
		{
			if (!(sortnode[sortindex[i]].wall->blend & PF_NoTexture))
				HWR_GetTexture(sortnode[sortindex[i]].wall->texnum, sortnode[sortindex[i]].wall->basetexnum);
			HWR_RenderWall(sortnode[sortindex[i]].wall->wallVerts, &sortnode[sortindex[i]].wall->Surf, sortnode[sortindex[i]].wall->blend, sortnode[sortindex[i]].wall->fogwall,
				sortnode[sortindex[i]].wall->lightlevel, sortnode[sortindex[i]].wall->wallcolormap);
		}
	}

	ps_hw_nodedrawtime = I_GetPreciseTime() - ps_hw_nodedrawtime;

	numwalls = 0;
	numplanes = 0;
	numpolyplanes = 0;

	// The buffer is reused next frame (no per-frame Z_Free)
	sortnode = NULL;
	sortindex = NULL;
}

// --------------------------------------------------------------------------
//  Draw all vissprites
// --------------------------------------------------------------------------

// added the stransform so they can be switched as drawing happenes so MD2s and sprites are sorted correctly with each other
// sdraw time per mobj type and for shadows
static UINT64 rre_sdTypeT[NUMMOBJTYPES];
static UINT32 rre_sdTypeN[NUMMOBJTYPES];
static UINT64 rre_sdShadowT = 0;
static void HWR_DrawSprites(void)
{
	UINT32 i;
	boolean skipshadow = false; // skip shadow if it was drawn already for a linkdraw sprite encountered earlier in the list

#ifdef BAD_MODEL_OPTIONS
	HWD.pfnSetSpecialState(HWD_SET_MODEL_LIGHTING, cv_glmodellighting.value);
#else
	HWD.pfnSetSpecialState(HWD_SET_MODEL_LIGHTING, 1);
#endif

	for (i = 0; i < gl_visspritecount; i++)
	{
		gl_vissprite_t *spr = gl_vsprorder[i];
		if (spr->bbox)
			HWR_DrawBoundingBox(spr);
		else
#ifdef HWPRECIP
		if (spr->precip)
			HWR_DrawPrecipitationSprite(spr);
		else
#endif
		{
			const UINT64 rrT0 = I_GetPreciseTime();
			if (spr->mobj && spr->mobj->shadowscale && cv_shadow.value && !skipshadow)
			{
				HWR_DrawDropShadow(spr->mobj, spr->mobj->shadowscale);
			}
			const UINT64 rrT1 = I_GetPreciseTime();
			rre_sdShadowT += rrT1 - rrT0;

			if ((spr->mobj->flags2 & MF2_LINKDRAW) && spr->mobj->tracer)
			{
				// If this linkdraw sprite is behind a sprite that has a shadow,
				// then that shadow has to be drawn first, otherwise the shadow ends up on top of
				// the linkdraw sprite because the linkdraw sprite does not modify the z-buffer.
				// The !skipshadow check is there in case there are multiple linkdraw sprites connected
				// to the same tracer, so the tracer's shadow only gets drawn once.
				if (cv_shadow.value && !skipshadow && spr->dispoffset < 0 && spr->mobj->tracer->shadowscale)
				{
					HWR_DrawDropShadow(spr->mobj->tracer, spr->mobj->tracer->shadowscale);
					skipshadow = true;
					// The next sprite in this loop should be either another linkdraw sprite or the tracer.
					// When the tracer is inevitably encountered, skipshadow will cause it's shadow
					// to get skipped and skipshadow will get set to false by the 'else' clause below.
				}
			}
			else
			{
				skipshadow = false;
			}

			if (spr->mobj && spr->mobj->skin && spr->mobj->sprite == SPR_PLAY)
			{
				if (!cv_glmodels.value || md2_playermodels[((skin_t*)(spr->mobj->skin))->skinnum].notfound || md2_playermodels[((skin_t*)(spr->mobj->skin))->skinnum].scale < 0.0f)
					HWR_DrawSprite(spr);
				else
				{
					if (!HWR_DrawModel(spr))
						HWR_DrawSprite(spr);
				}
			}
			else
			{
				if (!cv_glmodels.value || md2_models[spr->mobj->sprite].notfound || md2_models[spr->mobj->sprite].scale < 0.0f)
					HWR_DrawSprite(spr);
				else
				{
					if (!HWR_DrawModel(spr))
						HWR_DrawSprite(spr);
				}
			}
		}
	}
	HWD.pfnSetSpecialState(HWD_SET_MODEL_LIGHTING, 0);

	// At the end of sprite drawing, draw shapes of linkdraw sprites to z-buffer, so they
	// don't get drawn over by transparent surfaces.
	HWR_LinkDrawHackFinish();
	// Work around a r_opengl.c bug with PF_Invisible by making this SetBlend call
	// where PF_Invisible is off and PF_Masked is on.
	// (Other states probably don't matter. Here I left them same as in LinkDrawHackFinish)
	// Without this workaround the rest of the draw calls in this frame (including UI, screen texture)
	// can get drawn using an incorrect glBlendFunc, resulting in a occasional black screen.
	HWD.pfnSetBlend(PF_Translucent|PF_Occlude|PF_Masked);
}

// --------------------------------------------------------------------------
// HWR_AddSprites
// During BSP traversal, this adds sprites by sector.
// --------------------------------------------------------------------------
static UINT8 sectorlight;
static UINT32 rre_sprTypeCnt[NUMMOBJTYPES]; // diag: projected sprites per type
static void HWR_AddSprites(sector_t *sec)
{
	mobj_t *thing;
	fixed_t limit_dist;

	// BSP is traversed by subsector.
	// A sector might have been split into several
	//  subsectors during BSP building.
	// Thus we check whether its already added.
	if (sec->validcount == validcount)
		return;

	// Well, now it will be done.
	sec->validcount = validcount;

	// sprite lighting
	sectorlight = sec->lightlevel & 0xff;

	// Handle all things in sector.
	// If a limit exists, handle things a tiny bit different.
	limit_dist = (fixed_t)(cv_drawdist.value) * mapobjectscale;
	for (thing = sec->thinglist; thing; thing = thing->snext)
	{
		if (R_ThingWithinDist(thing, limit_dist))
		{
			if (R_ThingVisible(thing))
			{
				rre_sprTypeCnt[thing->type]++;
				HWR_ProjectSprite(thing);
			}

			HWR_ProjectBoundingBox(thing);
		}
	}
}

#ifdef HWPRECIP
// --------------------------------------------------------------------------
// HWR_AddPrecipitationSprites
// This renders through the blockmap instead of BSP to avoid
// iterating a huge amount of precipitation sprites in sectors
// that are beyond drawdist.
// --------------------------------------------------------------------------
static void HWR_AddPrecipitationSprites(void)
{
	const fixed_t drawdist = cv_drawdist_precip.value * mapobjectscale;

	INT32 xl, xh, yl, yh, bx, by;
	precipmobj_t *th;

	// no, no infinite draw distance for precipitation. this option at zero is supposed to turn it off
	if (drawdist == 0)
	{
		return;
	}

	R_GetRenderBlockMapDimensions(drawdist, &xl, &xh, &yl, &yh);

	for (bx = xl; bx <= xh; bx++)
	{
		for (by = yl; by <= yh; by++)
		{
			for (th = precipblocklinks[(by * bmapwidth) + bx]; th; th = th->bnext)
			{
				if (R_PrecipThingVisible(th))
				{
					HWR_ProjectPrecipitationSprite(th);
				}
			}
		}
	}
}
#endif

// --------------------------------------------------------------------------
// HWR_ProjectSprite
//  Generates a vissprite for a thing if it might be visible.
// --------------------------------------------------------------------------
// BP why not use xtoviexangle/viewangletox like in bsp ?....
static void HWR_ProjectSprite(mobj_t *thing)
{
	gl_vissprite_t *vis;
	float tr_x, tr_y;
	float tz;
	float tracertz = 0.0f;
	float x1, x2;
	float rightsin, rightcos;
	float this_scale, this_xscale, this_yscale;
	float spritexscale, spriteyscale;
	float shadowheight = 1.0f, shadowscale = 1.0f;
	float gz, gzt;
	spritedef_t *sprdef;
	spriteframe_t *sprframe;
#ifdef ROTSPRITE
	spriteinfo_t *sprinfo;
#endif
	md2_t *md2;
	size_t lumpoff;
	unsigned rot;
	UINT16 flip;
	boolean vflip = (!(thing->eflags & MFE_VERTICALFLIP) != !R_ThingVerticallyFlipped(thing));
	boolean mirrored = thing->mirrored;
	boolean hflip = (!R_ThingHorizontallyFlipped(thing) != !mirrored);
	INT32 dispoffset;

	angle_t ang;
	INT32 heightsec, phs;
	const boolean splat = R_ThingIsFloorSprite(thing);
	const boolean papersprite = (R_ThingIsPaperSprite(thing) && !splat);
	float z1, z2;

	fixed_t spr_width, spr_height;
	fixed_t spr_offset, spr_topoffset;
#ifdef ROTSPRITE
	patch_t *rotsprite = NULL;
	INT32 rollangle = 0;
	angle_t spriterotangle = 0;
	vector2_t visoffs;
#endif
	float rr_roll = 0.0f, rr_px = 0.0f, rr_pz = 0.0f, rr_pyoff = 0.0f;

	// uncapped/interpolation
	interpmobjstate_t interp = {0};

	if (!thing)
		return;

	if (R_UsingFrameInterpolation() && !paused)
	{
		R_InterpolateMobjState(thing, rendertimefrac, &interp);
	}
	else
	{
		R_InterpolateMobjState(thing, FRACUNIT, &interp);
	}

	dispoffset = thing->dispoffset;

	// hitlag vibrating
	if (thing->hitlag > 0 && (thing->eflags & MFE_DAMAGEHITLAG))
	{
		fixed_t jitters = HITLAGJITTERS;
		if (R_UsingFrameInterpolation() && !paused)
			jitters += (rendertimefrac / HITLAGDIV);

		fixed_t mul = thing->hitlag * jitters;

		if (leveltime & 1)
		{
			mul = -mul;
		}

		interp.x += FixedMul(thing->momx, mul);
		interp.y += FixedMul(thing->momy, mul);
		interp.z += FixedMul(thing->momz, mul);
	}

	// sprite offset
	interp.x += thing->sprxoff;
	interp.y += thing->spryoff;
	interp.z += thing->sprzoff;

	if (interp.spritexscale < 1 || interp.spriteyscale < 1)
		return;

	this_scale = FIXED_TO_FLOAT(interp.scale);
	spritexscale = FIXED_TO_FLOAT(interp.spritexscale);
	spriteyscale = FIXED_TO_FLOAT(interp.spriteyscale);

	// transform the origin point
	tr_x = FIXED_TO_FLOAT(interp.x) - gl_viewx;
	tr_y = FIXED_TO_FLOAT(interp.y) - gl_viewy;

	// rotation around vertical axis
	tz = (tr_x * gl_viewcos) + (tr_y * gl_viewsin);

	// thing is behind view plane?
	if (tz < ZCLIP_PLANE && !(papersprite || splat))
	{
		if (cv_glmodels.value) //Yellow: Only MD2's dont disappear
		{
			if (thing->skin && thing->sprite == SPR_PLAY)
				md2 = &md2_playermodels[((skin_t *)thing->skin)->skinnum];
			else
				md2 = &md2_models[thing->sprite];

			if (md2->notfound || md2->scale < 0.0f)
				return;
		}
		else
			return;
	}

	// The above can stay as it works for cutting sprites that are too close
	tr_x = FIXED_TO_FLOAT(interp.x);
	tr_y = FIXED_TO_FLOAT(interp.y);

	// decide which patch to use for sprite relative to player
	if ((unsigned)thing->sprite >= numsprites)
	{
		CONS_Debug(DBG_RENDER, "HWR_ProjectSprite: invalid sprite number %i\n", thing->sprite);
		return;
	}

	rot = thing->frame&FF_FRAMEMASK;

	//Fab : 02-08-98: 'skin' override spritedef currently used for skin
	if (thing->skin && thing->sprite == SPR_PLAY)
	{
		sprdef = &((skin_t *)thing->skin)->sprites[thing->sprite2];
#ifdef ROTSPRITE
		sprinfo = R_SkinSprInfo((skin_t *)thing->skin, thing->sprite2);
#endif
	}
	else
	{
		sprdef = &sprites[thing->sprite];
#ifdef ROTSPRITE
		sprinfo = &spriteinfo[thing->sprite];
#endif
	}

	if (rot >= sprdef->numframes)
	{
		CONS_Alert(CONS_ERROR, M_GetText("HWR_ProjectSprite: invalid sprite frame %s/%s for %s\n"),
			sizeu1(rot), sizeu2(sprdef->numframes), sprnames[thing->sprite]);
		thing->sprite = states[S_UNKNOWN].sprite;
		thing->frame = states[S_UNKNOWN].frame;
		sprdef = &sprites[thing->sprite];
#ifdef ROTSPRITE
		sprinfo = &spriteinfo[thing->sprite];
#endif
		rot = thing->frame&FF_FRAMEMASK;
		thing->state->sprite = thing->sprite;
		thing->state->frame = thing->frame;
	}

	sprframe = &sprdef->spriteframes[rot];

#ifdef PARANOIA
	if (!sprframe)
		I_Error("sprframes NULL for sprite %d\n", thing->sprite);
#endif

	if (splat)
	{
		ang = R_PointToAngle2(0, viewz, 0, interp.z);
	}
	else
	{
		ang = R_PointToAngle (interp.x, interp.y) - interp.angle;
		if (mirrored)
			ang = InvAngle(ang);
	}

	if (sprframe->rotate == SRF_SINGLE)
	{
		// use single rotation for all views
		rot = 0;                        //Fab: for vis->patch below
		lumpoff = sprframe->lumpid[0];     //Fab: see note above
		flip = sprframe->flip; // Will only be 0x00 or 0xFF

		if (papersprite && ang < ANGLE_180)
			flip ^= 0xFFFF;
	}
	else
	{
		// choose a different rotation based on player view
		if ((sprframe->rotate & SRF_RIGHT) && (ang < ANGLE_180)) // See from right
			rot = 6; // F7 slot
		else if ((sprframe->rotate & SRF_LEFT) && (ang >= ANGLE_180)) // See from left
			rot = 2; // F3 slot
		else if (sprframe->rotate & SRF_3DGE) // 16-angle mode
		{
			rot = (ang+ANGLE_180+ANGLE_11hh)>>28;
			rot = ((rot & 1)<<3)|(rot>>1);
		}
		else // Normal behaviour
			rot = (ang+ANGLE_202h)>>29;

		//Fab: lumpid is the index for spritewidth,spriteoffset... tables
		lumpoff = sprframe->lumpid[rot];
		flip = sprframe->flip & (1<<rot);

		if (papersprite && ang < ANGLE_180)
			flip ^= (1<<rot);
	}

	if (thing->skin && ((skin_t *)thing->skin)->highresscale != FRACUNIT)
		this_scale *= FIXED_TO_FLOAT(((skin_t *)thing->skin)->highresscale);

	spr_width = spritecachedinfo[lumpoff].width;
	spr_height = spritecachedinfo[lumpoff].height;
	spr_offset = spritecachedinfo[lumpoff].offset;
	spr_topoffset = spritecachedinfo[lumpoff].topoffset;

#ifdef ROTSPRITE
	spriterotangle = R_SpriteRotationAngle(thing, NULL);

	if (spriterotangle != 0
	&& !(splat && !(thing->renderflags & RF_NOSPLATROLLANGLE))
	// Roll is done by rotating the quad on the GPU (see rr_roll).
	// Rotsprite created a new texture per angle (item boxes: ~1000
	// uploads when the race starts). It is only kept for sprites
	// split by lights (HWR_SplitSprite) and flat ones (splat).
	&& (splat || thing->subsector->sector->numlights)
	)
	{
		rollangle = R_GetRollAngle(papersprite == vflip
				? spriterotangle : InvAngle(spriterotangle));
		rotsprite = Patch_GetRotatedSprite(sprframe, (thing->frame & FF_FRAMEMASK), rot, flip, false, sprinfo, rollangle);

		if (rotsprite != NULL)
		{
			spr_width = rotsprite->width << FRACBITS;
			spr_height = rotsprite->height << FRACBITS;
			spr_offset = rotsprite->leftoffset << FRACBITS;
			spr_topoffset = rotsprite->topoffset << FRACBITS;
			spr_topoffset += FEETADJUST;

			// flip -> rotate, not rotate -> flip
			flip = 0;
		}
	}

	// initialize and rotate pitch/roll vector
	visoffs.x = 0;
	visoffs.y = 0;

	const fixed_t visoffymul = (vflip ? -FRACUNIT : FRACUNIT);

	if (R_ThingIsUsingBakedOffsets(thing))
	{
		R_RotateSpriteOffsetsByPitchRoll(thing,
										 vflip,
										 hflip,
										 &visoffs);
	}
#endif

	if (thing->renderflags & RF_ABSOLUTEOFFSETS)
	{
		spr_offset = interp.spritexoffset;
#ifdef ROTSPRITE
		spr_topoffset = (interp.spriteyoffset + FixedDiv((visoffs.y * visoffymul),
																mapobjectscale));
#else
		spr_topoffset = interp.spriteyoffset;
#endif
	}
	else
	{
		SINT8 flipoffset = 1;

		if ((thing->renderflags & RF_FLIPOFFSETS) && flip)
			flipoffset = -1;

		spr_offset += interp.spritexoffset * flipoffset;
#ifdef ROTSPRITE
		spr_topoffset += (interp.spriteyoffset + FixedDiv((visoffs.y * visoffymul),
															mapobjectscale))
																* flipoffset;
#else
		spr_topoffset += interp.spriteyoffset * flipoffset;
#endif
	}

	if (papersprite)
	{
		rightsin = FIXED_TO_FLOAT(FINESINE(interp.angle >> ANGLETOFINESHIFT));
		rightcos = FIXED_TO_FLOAT(FINECOSINE(interp.angle >> ANGLETOFINESHIFT));
	}
	else
	{
		rightsin = FIXED_TO_FLOAT(FINESINE((viewangle + ANGLE_90)>>ANGLETOFINESHIFT));
		rightcos = FIXED_TO_FLOAT(FINECOSINE((viewangle + ANGLE_90)>>ANGLETOFINESHIFT));
	}

	flip = !flip != !hflip;

	if (thing->renderflags & RF_SHADOWEFFECTS)
	{
		mobj_t *caster = thing->target;

		if (caster && !P_MobjWasRemoved(caster))
		{
			interpmobjstate_t casterinterp = {0};
			fixed_t groundz;
			fixed_t floordiff;

			if (R_UsingFrameInterpolation() && !paused)
			{
				R_InterpolateMobjState(caster, rendertimefrac, &casterinterp);
			}
			else
			{
				R_InterpolateMobjState(caster, FRACUNIT, &casterinterp);
			}

			groundz = R_GetShadowZ(thing, NULL);
			floordiff = abs(((thing->eflags & MFE_VERTICALFLIP) ? caster->height : 0) + casterinterp.z - groundz);

			shadowheight = FIXED_TO_FLOAT(floordiff);
			shadowscale = FIXED_TO_FLOAT(FixedMul(FRACUNIT - floordiff/640, casterinterp.scale));

			if (splat)
				spritexscale *= shadowscale;
			spriteyscale *= shadowscale;
		}
	}

	this_xscale = spritexscale * this_scale;
	this_yscale = spriteyscale * this_scale;

	if (splat)
	{
		z1 = z2 = tr_y;
		x1 = x2 = tr_x;
		gz = gzt = interp.z;
	}
	else
	{
#ifdef ROTSPRITE
		if (visoffs.x)
		{
			visoffs.x = (FixedDiv((visoffs.x * FRACUNIT), mapobjectscale));
		}
#endif
		if (flip)
		{
#ifdef ROTSPRITE
			spr_offset -= visoffs.x;
#endif
			x1 = (FIXED_TO_FLOAT((spr_width - spr_offset)) * this_xscale);
			x2 = (FIXED_TO_FLOAT(spr_offset) * this_xscale);
		}
		else
		{
#ifdef ROTSPRITE
			spr_offset += visoffs.x;
#endif
			x1 = (FIXED_TO_FLOAT(spr_offset) * this_xscale);
			x2 = (FIXED_TO_FLOAT(spr_width - spr_offset) * this_xscale);
		}
		// test if too close
	/*
		if (papersprite)
		{
			z1 = tz - x1 * angle_scalez;
			z2 = tz + x2 * angle_scalez;

			if (max(z1, z2) < ZCLIP_PLANE)
				return;
		}
	*/

		rr_roll = 0.0f;
		if (spriterotangle != 0 && !rotsprite && !thing->subsector->sector->numlights)
		{
			INT32 xpiv, ypiv;
			const size_t fr = (thing->frame & FF_FRAMEMASK);
			patch_t *pp = (patch_t *)W_CachePatchNum(sprframe->lumppat[rot], PU_SPRITE);
			if (in_bit_array(sprinfo->available, fr))
				xpiv = sprinfo->pivot[fr].x, ypiv = sprinfo->pivot[fr].y;
			else if (in_bit_array(sprinfo->available, SPRINFO_DEFAULT_PIVOT))
				xpiv = sprinfo->pivot[SPRINFO_DEFAULT_PIVOT].x, ypiv = sprinfo->pivot[SPRINFO_DEFAULT_PIVOT].y;
			else
				xpiv = pp->leftoffset, ypiv = pp->height / 2;
			// u: distance along (rightcos,rightsin) from the origin
			float pu = flip ? (x1 - (float)(pp->width - xpiv) * this_xscale)
			                : (x1 - (float)xpiv * this_xscale);
			rr_px = tr_x + pu * rightcos;
			rr_pz = tr_y + pu * rightsin;
			rr_pyoff = (float)ypiv * this_yscale;
			// on-screen roll, counter-clockwise (same as rotsprite)
			float deg = FIXED_TO_FLOAT(AngleFixed(spriterotangle));
			rr_roll = deg * (float)(M_PIl / 180.0);
			if (papersprite)
				rr_roll = -rr_roll;
		}
		z1 = tr_y + x1 * rightsin;
		z2 = tr_y - x2 * rightsin;
		x1 = tr_x + x1 * rightcos;
		x2 = tr_x - x2 * rightcos;

		if (thing->terrain && thing->terrain->floorClip)
			spr_topoffset -=  thing->terrain->floorClip;

		if (vflip)
		{
			gz = FIXED_TO_FLOAT(interp.z + thing->height) - (FIXED_TO_FLOAT(spr_topoffset) * this_yscale);
			gzt = gz + (FIXED_TO_FLOAT(spr_height) * this_yscale);
		}
		else
		{
			gzt = FIXED_TO_FLOAT(interp.z) + (FIXED_TO_FLOAT(spr_topoffset) * this_yscale);
			gz = gzt - (FIXED_TO_FLOAT(spr_height) * this_yscale);
		}
	}

	if (thing->subsector->sector->cullheight)
	{
		if (HWR_DoCulling(thing->subsector->sector->cullheight, viewsector->cullheight, gl_viewz, gz, gzt))
			return;
	}

	heightsec = thing->subsector->sector->heightsec;
	if (viewplayer && viewplayer->mo && viewplayer->mo->subsector)
		phs = viewplayer->mo->subsector->sector->heightsec;
	else
		phs = -1;

	if (heightsec != -1 && phs != -1) // only clip things which are in special sectors
	{
		if (gl_viewz < FIXED_TO_FLOAT(sectors[phs].floorheight) ?
		FIXED_TO_FLOAT(interp.z) >= FIXED_TO_FLOAT(sectors[heightsec].floorheight) :
		gzt < FIXED_TO_FLOAT(sectors[heightsec].floorheight))
			return;
		if (gl_viewz > FIXED_TO_FLOAT(sectors[phs].ceilingheight) ?
		gzt < FIXED_TO_FLOAT(sectors[heightsec].ceilingheight) && gl_viewz >= FIXED_TO_FLOAT(sectors[heightsec].ceilingheight) :
		FIXED_TO_FLOAT(interp.z) >= FIXED_TO_FLOAT(sectors[heightsec].ceilingheight))
			return;
	}

	if ((thing->flags2 & MF2_LINKDRAW) && thing->tracer)
	{
		interpmobjstate_t tracer_interp = {0};

		if (! R_ThingVisible(thing->tracer))
			return;

		if (R_UsingFrameInterpolation() && !paused)
		{
			R_InterpolateMobjState(thing->tracer, rendertimefrac, &tracer_interp);
		}
		else
		{
			R_InterpolateMobjState(thing->tracer, FRACUNIT, &tracer_interp);
		}

		// calculate tz for tracer, same way it is calculated for this sprite
		// transform the origin point
		tr_x = FIXED_TO_FLOAT(tracer_interp.x) - gl_viewx;
		tr_y = FIXED_TO_FLOAT(tracer_interp.y) - gl_viewy;

		// rotation around vertical axis
		tracertz = (tr_x * gl_viewcos) + (tr_y * gl_viewsin);

		// Software does not render the linkdraw sprite if the tracer is behind the view plane,
		// so do the same check here.
		// NOTE: This check has the same flaw as the view plane check at the beginning of HWR_ProjectSprite:
		// the view aiming angle is not taken into account, leading to sprites disappearing too early when they
		// can still be seen when looking down/up at steep angles.
		if (tracertz < ZCLIP_PLANE)
			return;

		// if the sprite is behind the tracer, invert dispoffset, putting the sprite behind the tracer
		if (tz > tracertz)
			dispoffset *= -1;
	}

	// store information in a vissprite
	vis = HWR_NewVisSprite();
	vis->x1 = x1;
	vis->x2 = x2;
	vis->z1 = z1;
	vis->z2 = z2;
	vis->rr_roll = splat ? 0.0f : rr_roll;
	vis->rr_px = rr_px; vis->rr_pz = rr_pz;
	vis->rr_py = vflip ? (gz + rr_pyoff) : (gzt - rr_pyoff);

	vis->tz = tz; // Keep tz for the simple sprite sorting that happens
	vis->tracertz = tracertz;

	vis->renderflags = thing->renderflags;
	vis->rotateflags = sprframe->rotate;

	vis->shadowheight = shadowheight;
	vis->shadowscale = shadowscale;
	vis->dispoffset = dispoffset; // Monster Iestyn: 23/11/15: HARDWARE SUPPORT AT LAST
	vis->flip = flip;

	vis->scale = this_scale;
	vis->spritexscale = spritexscale;
	vis->spriteyscale = spriteyscale;
	vis->spritexoffset = FIXED_TO_FLOAT(spr_offset);
	vis->spriteyoffset = FIXED_TO_FLOAT(spr_topoffset);

	vis->rotated = false;

#ifdef ROTSPRITE
	if (rotsprite)
	{
		vis->gpatch = (patch_t *)rotsprite;
		vis->rotated = true;
	}
	else
#endif
		vis->gpatch = (patch_t *)W_CachePatchNum(sprframe->lumppat[rot], PU_SPRITE);

	vis->mobj = thing;

	INT32 skinnum = TC_DEFAULT;

	if (vis->mobj->skin && vis->mobj->sprite == SPR_PLAY) // This thing is a player!
	{
		skinnum = ((skin_t*)vis->mobj->skin)->skinnum;
	}

	// Hide not-yet-unlocked characters in replays from other people
	if (skinnum >= 0 && !R_CanShowSkinInDemo(skinnum))
	{
		vis->colormap = R_GetTranslationColormap(TC_BLINK, (skincolornum_t)thing->color, GTC_CACHE);
	}
	//Hurdler: 25/04/2000: now support colormap in hardware mode
	else if (R_ThingIsFlashing(vis->mobj))
	{
		vis->colormap = R_GetTranslationColormap(TC_HITLAG, (skincolornum_t)0, GTC_CACHE);
	}
	else if (thing->color)
	{
		vis->colormap = R_GetTranslationColormap(thing->colorized ? TC_RAINBOW : skinnum, (skincolornum_t)thing->color, GTC_CACHE);
	}
	else
	{
		vis->colormap = colormaps;

		if (encoremap && !(thing->flags & MF_DONTENCOREMAP))
			vis->colormap += COLORMAP_REMAPOFFSET;
	}

	// set top/bottom coords
	vis->gzt = gzt;
	vis->gz = gz;

	//CONS_Debug(DBG_RENDER, "------------------\nH: sprite  : %d\nH: frame   : %x\nH: type    : %d\nH: sname   : %s\n\n",
	//            thing->sprite, thing->frame, thing->type, sprnames[thing->sprite]);

	vis->vflip = vflip;

	vis->precip = false;
	vis->bbox = false;

	vis->angle = interp.angle;
}

#ifdef HWPRECIP
// Precipitation projector for hardware mode
static void HWR_ProjectPrecipitationSprite(precipmobj_t *thing)
{
	gl_vissprite_t *vis;
	float tr_x, tr_y;
	float tz;
	float x1, x2;
	float z1, z2;
	float rightsin, rightcos;
	float this_scale;
	spritedef_t *sprdef;
	spriteframe_t *sprframe;
	size_t lumpoff;
	unsigned rot = 0;
	UINT8 flip;

	if (!thing)
		return;

	// uncapped/interpolation
	interpmobjstate_t interp = {0};

	// okay... this is a hack, but weather isn't networked, so it should be ok
	if (!P_PrecipThinker(thing))
	{
		return;
	}

	// do interpolation
	if (R_UsingFrameInterpolation() && !paused)
	{
		R_InterpolatePrecipMobjState(thing, rendertimefrac, &interp);
	}
	else
	{
		R_InterpolatePrecipMobjState(thing, FRACUNIT, &interp);
	}

	this_scale = FIXED_TO_FLOAT(interp.scale);

	// transform the origin point
	tr_x = FIXED_TO_FLOAT(interp.x) - gl_viewx;
	tr_y = FIXED_TO_FLOAT(interp.y) - gl_viewy;

	// rotation around vertical axis
	tz = (tr_x * gl_viewcos) + (tr_y * gl_viewsin);

	// thing is behind view plane?
	if (tz < ZCLIP_PLANE)
		return;

	tr_x = FIXED_TO_FLOAT(interp.x);
	tr_y = FIXED_TO_FLOAT(interp.y);

	// decide which patch to use for sprite relative to player
	if ((unsigned)thing->sprite >= numsprites)
	{
		CONS_Debug(DBG_RENDER, "HWR_ProjectPrecipitationSprite: invalid sprite number %i\n",
		        thing->sprite);
		return;
	}

	sprdef = &sprites[thing->sprite];

	if ((size_t)(thing->frame&FF_FRAMEMASK) >= sprdef->numframes)
	{
		CONS_Debug(DBG_RENDER, "HWR_ProjectPrecipitationSprite: invalid sprite frame %i : %i for %s\n",
		        thing->sprite, thing->frame, sprnames[thing->sprite]);
		return;
	}

	sprframe = &sprdef->spriteframes[ thing->frame & FF_FRAMEMASK];

	// use single rotation for all views
	lumpoff = sprframe->lumpid[0];
	flip = sprframe->flip; // Will only be 0x00 or 0xFF

	rightsin = FIXED_TO_FLOAT(FINESINE((viewangle + ANGLE_90)>>ANGLETOFINESHIFT));
	rightcos = FIXED_TO_FLOAT(FINECOSINE((viewangle + ANGLE_90)>>ANGLETOFINESHIFT));
	if (flip)
	{
		x1 = FIXED_TO_FLOAT(spritecachedinfo[lumpoff].width - spritecachedinfo[lumpoff].offset);
		x2 = FIXED_TO_FLOAT(spritecachedinfo[lumpoff].offset);
	}
	else
	{
		x1 = FIXED_TO_FLOAT(spritecachedinfo[lumpoff].offset);
		x2 = FIXED_TO_FLOAT(spritecachedinfo[lumpoff].width - spritecachedinfo[lumpoff].offset);
	}

	x1 *= this_scale;
	x2 *= this_scale;

	z1 = tr_y + x1 * rightsin;
	z2 = tr_y - x2 * rightsin;
	x1 = tr_x + x1 * rightcos;
	x2 = tr_x - x2 * rightcos;

	//
	// store information in a vissprite
	//
	vis = HWR_NewVisSprite();
	vis->x1 = x1;
	vis->x2 = x2;
	vis->z1 = z1;
	vis->z2 = z2;
	vis->tz = tz;
	vis->dispoffset = 0; // Monster Iestyn: 23/11/15: HARDWARE SUPPORT AT LAST
	vis->gpatch = (patch_t *)W_CachePatchNum(sprframe->lumppat[rot], PU_SPRITE);
	vis->flip = flip;
	vis->mobj = (mobj_t *)thing;

	vis->colormap = NULL;

	if (encoremap && !(thing->flags & MF_DONTENCOREMAP))
		vis->colormap += COLORMAP_REMAPOFFSET;

	// set top/bottom coords
	vis->gzt = FIXED_TO_FLOAT(interp.z) + (FIXED_TO_FLOAT(spritecachedinfo[lumpoff].topoffset) * this_scale);
	vis->gz = vis->gzt - (FIXED_TO_FLOAT(spritecachedinfo[lumpoff].height) * this_scale);

	vis->precip = true;
	vis->bbox = false;
}
#endif

static void HWR_ProjectBoundingBox(mobj_t *thing)
{
	gl_vissprite_t *vis;
	float tr_x, tr_y;
	float tz;
	float rad;

	// uncapped/interpolation
	interpmobjstate_t interp = {0};

	if (!thing)
		return;

	if (!R_ThingBoundingBoxVisible(thing))
		return;

	if (R_UsingFrameInterpolation() && !paused)
	{
		R_InterpolateMobjState(thing, rendertimefrac, &interp);
	}
	else
	{
		R_InterpolateMobjState(thing, FRACUNIT, &interp);
	}

	// transform the origin point
	tr_x = FIXED_TO_FLOAT(interp.x) - gl_viewx;
	tr_y = FIXED_TO_FLOAT(interp.y) - gl_viewy;

	// rotation around vertical axis
	tz = (tr_x * gl_viewcos) + (tr_y * gl_viewsin);

	// thing is behind view plane?
	if (tz < ZCLIP_PLANE)
		return;

	tr_x += gl_viewx;
	tr_y += gl_viewy;

	rad = FIXED_TO_FLOAT(thing->radius);

	vis = HWR_NewVisSprite();
	vis->x1 = tr_x - rad;
	vis->x2 = tr_x + rad;
	vis->z1 = tr_y - rad;
	vis->z2 = tr_y + rad;
	vis->gz = FIXED_TO_FLOAT(interp.z);
	vis->gzt = vis->gz + FIXED_TO_FLOAT(thing->height);
	vis->mobj = thing;

	vis->precip = false;
	vis->bbox = true;
}

// ==========================================================================
// Sky dome rendering, ported from PrBoom+
// ==========================================================================

static gl_sky_t gl_sky;

// Live API switch: the sky VBO died with the old context -> rebuild it when drawing
void HWR_SkyContextLost(void)
{
	gl_sky.vbo = 0;
	gl_sky.rebuild = true;
}

static void HWR_SkyDomeVertex(gl_sky_t *sky, gl_skyvertex_t *vbo, int r, int c, signed char yflip, float delta, boolean foglayer)
{
	const float radians = (float)(M_PIl / 180.0f);
	const float scale = 10000.0f;
	const float maxSideAngle = 60.0f;

	float topAngle = (c / (float)sky->columns * 360.0f);
	float sideAngle = (maxSideAngle * (sky->rows - r) / sky->rows);
	float height = (float)(sin(sideAngle * radians));
	float realRadius = (float)(scale * cos(sideAngle * radians));
	float x = (float)(realRadius * cos(topAngle * radians));
	float y = (!yflip) ? scale * height : -scale * height;
	float z = (float)(realRadius * sin(topAngle * radians));
	float timesRepeat = (4 * (256.0f / sky->width));
	if (fpclassify(timesRepeat) == FP_ZERO)
		timesRepeat = 1.0f;

	if (!foglayer)
	{
		vbo->r = 255;
		vbo->g = 255;
		vbo->b = 255;
		vbo->a = (r == 0 ? 0 : 255);

		// And the texture coordinates.
		vbo->u = (-timesRepeat * c / (float)sky->columns);
		if (!yflip)	// Flipped Y is for the lower hemisphere.
			vbo->v = (r / (float)sky->rows) + 0.5f;
		else
			vbo->v = 1.0f + ((sky->rows - r) / (float)sky->rows) + 0.5f;
	}

	if (r != 4)
		y += 300.0f;

	// And finally the vertex.
	vbo->x = x;
	vbo->y = y + delta;
	vbo->z = z;
}

// Clears the sky dome.
void HWR_ClearSkyDome(void)
{
	gl_sky_t *sky = &gl_sky;

	if (sky->loops)
		free(sky->loops);
	if (sky->data)
		free(sky->data);

	sky->loops = NULL;
	sky->data = NULL;

	sky->rows = sky->columns = 0;
	sky->loopcount = 0;

	sky->detail = 0;
	sky->texture = -1;
	sky->width = sky->height = 0;

	sky->rebuild = true;
}

void HWR_BuildSkyDome(void)
{
	int c, r;
	signed char yflip;
	int row_count = 4;
	int col_count = 4;
	float delta;

	gl_sky_t *sky = &gl_sky;
	gl_skyvertex_t *vertex_p;
	texture_t *texture = textures[texturetranslation[skytexture]];

	sky->detail = 16;
	col_count *= sky->detail;

	if ((sky->columns != col_count) || (sky->rows != row_count))
		HWR_ClearSkyDome();

	sky->columns = col_count;
	sky->rows = row_count;
	sky->vertex_count = 2 * sky->rows * (sky->columns * 2 + 2) + sky->columns * 2;

	if (!sky->loops)
		sky->loops = (gl_skyloopdef_t *)malloc((sky->rows * 2 + 2) * sizeof(sky->loops[0]));

	// create vertex array
	if (!sky->data)
		sky->data = (gl_skyvertex_t *)malloc(sky->vertex_count * sizeof(sky->data[0]));

	sky->texture = texturetranslation[skytexture];
	sky->width = texture->width;
	sky->height = texture->height;

	vertex_p = &sky->data[0];
	sky->loopcount = 0;

	for (yflip = 0; yflip < 2; yflip++)
	{
		sky->loops[sky->loopcount].mode = HWD_SKYLOOP_FAN;
		sky->loops[sky->loopcount].vertexindex = vertex_p - &sky->data[0];
		sky->loops[sky->loopcount].vertexcount = col_count;
		sky->loops[sky->loopcount].use_texture = false;
		sky->loopcount++;

		delta = 0.0f;

		for (c = 0; c < col_count; c++)
		{
			HWR_SkyDomeVertex(sky, vertex_p, 1, c, yflip, 0.0f, true);
			vertex_p->r = 255;
			vertex_p->g = 255;
			vertex_p->b = 255;
			vertex_p->a = 255;
			vertex_p++;
		}

		delta = (yflip ? 5.0f : -5.0f) / 128.0f;

		for (r = 0; r < row_count; r++)
		{
			sky->loops[sky->loopcount].mode = HWD_SKYLOOP_STRIP;
			sky->loops[sky->loopcount].vertexindex = vertex_p - &sky->data[0];
			sky->loops[sky->loopcount].vertexcount = 2 * col_count + 2;
			sky->loops[sky->loopcount].use_texture = true;
			sky->loopcount++;

			for (c = 0; c <= col_count; c++)
			{
				HWR_SkyDomeVertex(sky, vertex_p++, r + (yflip ? 1 : 0), (c ? c : 0), yflip, delta, false);
				HWR_SkyDomeVertex(sky, vertex_p++, r + (yflip ? 0 : 1), (c ? c : 0), yflip, delta, false);
			}
		}
	}
}

static void HWR_DrawSkyBackground(player_t *player)
{
	HWD.pfnSetBlend(PF_Translucent|PF_NoDepthTest|PF_Modulated);

	// Low-End: the dome cost 5-34 ms/frame on slow hardware; flat sky (1 quad).
	if (!RRE_GLLOWEND && cv_glskydome.value)
	{
		FTransform dometransform;
		const float fpov = FIXED_TO_FLOAT(R_FOV(viewssnum)+player->fovadd);
		postimg_t *type = &postimgtype[R_GetViewNumber()];

		memset(&dometransform, 0x00, sizeof(FTransform));

		//04/01/2000: Hurdler: added for T&L
		//                     It should replace all other gl_viewxxx when finished
		HWR_SetTransformAiming(&dometransform, player, false);
		dometransform.angley = (float)((viewangle-ANGLE_270)>>ANGLETOFINESHIFT)*(360.0f/(float)FINEANGLES);

		if (*type == postimg_flip)
			dometransform.flip = true;
		else
			dometransform.flip = false;

		if (*type == postimg_mirror)
			dometransform.mirror = true;
		else
			dometransform.mirror = false;

		dometransform.scalex = 1;
		dometransform.scaley = (float)vid.width/vid.height;
		dometransform.scalez = 1;
		dometransform.fovxangle = fpov; // Tails
		dometransform.fovyangle = fpov; // Tails
		HWR_RollTransform(&dometransform, viewroll);
		dometransform.splitscreen = r_splitscreen;

		HWR_GetTexture(texturetranslation[skytexture], skytexture);

		if (gl_sky.texture != texturetranslation[skytexture])
		{
			HWR_ClearSkyDome();
			HWR_BuildSkyDome();
		}

		HWD.pfnSetShader(SHADER_SKY); // sky shader
		HWD.pfnSetTransform(&dometransform);
		HWD.pfnRenderSkyDome(&gl_sky);
	}
	else
	{
		FOutVector v[4];
		angle_t angle;
		float dimensionmultiply;
		float aspectratio;
		float angleturn;

		HWR_GetTexture(texturetranslation[skytexture], skytexture);
		aspectratio = (float)vid.width/(float)vid.height;

		//Hurdler: the sky is the only texture who need 4.0f instead of 1.0
		//         because it's called just after clearing the screen
		//         and thus, the near clipping plane is set to 3.99
		// Sryder: Just use the near clipping plane value then

		//  3--2
		//  | /|
		//  |/ |
		//  0--1
		v[0].x = v[3].x = -ZCLIP_PLANE-1;
		v[1].x = v[2].x =  ZCLIP_PLANE+1;
		v[0].y = v[1].y = -ZCLIP_PLANE-1;
		v[2].y = v[3].y =  ZCLIP_PLANE+1;

		v[0].z = v[1].z = v[2].z = v[3].z = ZCLIP_PLANE+1;

		// X

		// NOTE: This doesn't work right with texture widths greater than 1024
		// software doesn't draw any further than 1024 for skies anyway, but this doesn't overlap properly
		// The only time this will probably be an issue is when a sky wider than 1024 is used as a sky AND a regular wall texture

		angle = (dup_viewangle + gl_xtoviewangle[0]);

		dimensionmultiply = ((float)textures[texturetranslation[skytexture]]->width/256.0f);

		v[0].s = v[3].s = (-1.0f * angle) / ((ANGLE_90-1.0f)*dimensionmultiply); // left
		v[2].s = v[1].s = v[0].s + (1.0f/dimensionmultiply); // right (or left + 1.0f)
		// use +angle and -1.0f above instead if you wanted old backwards behavior

		// Y
		angle = aimingangle;
		dimensionmultiply = ((float)textures[texturetranslation[skytexture]]->height/(128.0f*aspectratio));

		if (r_splitscreen == 1)
		{
			dimensionmultiply *= 2;
			angle *= 2;
		}

		// Middle of the sky should always be at angle 0
		// need to keep correct aspect ratio with X
		if (atransform.flip)
		{
			// During vertical flip the sky should be flipped and it's y movement should also be flipped obviously
			v[3].t = v[2].t = -(0.5f-(0.5f/dimensionmultiply)); // top
			v[0].t = v[1].t = v[3].t - (1.0f/dimensionmultiply); // bottom (or top - 1.0f)
		}
		else
		{
			v[0].t = v[1].t = -(0.5f-(0.5f/dimensionmultiply)); // bottom
			v[3].t = v[2].t = v[0].t - (1.0f/dimensionmultiply); // top (or bottom - 1.0f)
		}

		angleturn = (((float)ANGLE_45-1.0f)*aspectratio)*dimensionmultiply;

		if (angle > ANGLE_180) // Do this because we don't want the sky to suddenly teleport when crossing over 0 to 360 and vice versa
		{
			angle = InvAngle(angle);
			v[3].t = v[2].t += ((float) angle / angleturn);
			v[0].t = v[1].t += ((float) angle / angleturn);
		}
		else
		{
			v[3].t = v[2].t -= ((float) angle / angleturn);
			v[0].t = v[1].t -= ((float) angle / angleturn);
		}

		// Drawing into the sky texture (bigger than the screen):
		// stretch the quad (and its coordinates, which are linear) to cover the margins.
		if (RRE_SkyTexDrawing())
		{
			float rw = 240.0f * vid.width / (float)vid.height; if (rw > 400.0f) rw = 400.0f; // 384 (320x200) or 400 (400x240)
			const float fx = 512.0f / rw, fy = 256.0f / 240.0f;
			const float cs = (v[0].s + v[1].s) * 0.5f, ct = (v[0].t + v[3].t) * 0.5f;
			for (int k = 0; k < 4; k++)
			{
				v[k].x *= fx; v[k].s = cs + (v[k].s - cs) * fx;
				v[k].y *= fy; v[k].t = ct + (v[k].t - ct) * fy;
			}
		}
		HWD.pfnUnSetShader();
		HWD.pfnDrawPolygon(NULL, v, 4, 0);
	}

	HWD.pfnSetShader(SHADER_DEFAULT);
}


// -----------------+
// HWR_ClearView : clear the viewwindow, with maximum z value
// -----------------+
static inline void HWR_ClearView(void)
{
	//  3--2
	//  | /|
	//  |/ |
	//  0--1

	/// \bug faB - enable depth mask, disable color mask

	HWD.pfnGClipRect((INT32)gl_viewwindowx,
	                 (INT32)gl_viewwindowy,
	                 (INT32)(gl_viewwindowx + gl_viewwidth),
	                 (INT32)(gl_viewwindowy + gl_viewheight),
	                 ZCLIP_PLANE);
	HWD.pfnClearBuffer(false, true, 0);

	//disable clip window - set to full size
	// rem by Hurdler
	// HWD.pfnGClipRect(0, 0, vid.width, vid.height);
}


// -----------------+
// HWR_SetViewSize  : set projection and scaling values
// -----------------+
void HWR_SetViewSize(void)
{
	// setup view size
	gl_viewwidth = (float)vid.width;
	gl_viewheight = (float)vid.height;

	if (r_splitscreen > 0)
	{
		gl_viewheight /= 2;

		if (r_splitscreen > 1)
		{
			gl_viewwidth /= 2;
		}
	}

	gl_basecenterx = gl_viewwidth / 2;
	gl_basecentery = gl_viewheight / 2;

	gl_baseviewwindowx = 0;
	gl_basewindowcenterx = gl_viewwidth / 2;

	gl_baseviewwindowy = 0;
	gl_basewindowcentery = gl_viewheight / 2;

	gl_pspritexscale = gl_viewwidth / BASEVIDWIDTH;
	gl_pspriteyscale = ((vid.height*gl_pspritexscale*BASEVIDWIDTH)/BASEVIDHEIGHT)/vid.width;

	HWD.pfnFlushScreenTextures();
}

// -------------------+
// HWR_ShiftViewPort  : offset viewport according to current split
// -------------------+
static void HWR_ShiftViewPort(void)
{
	gl_centerx = gl_basecenterx;
	gl_viewwindowx = gl_baseviewwindowx;
	gl_windowcenterx = gl_basewindowcenterx;

	gl_centery = gl_basecentery;
	gl_viewwindowy = gl_baseviewwindowy;
	gl_windowcentery = gl_basewindowcentery;

	if (viewssnum > ( r_splitscreen > 1 ))
	{
		gl_viewwindowy += gl_viewheight;
		gl_windowcentery += gl_viewheight;
	}

	if (r_splitscreen > 1 && viewssnum & 1)
	{
		gl_viewwindowx += gl_viewwidth;
		gl_windowcenterx += gl_viewwidth;
	}
}

// Set view aiming, for the sky dome, the skybox,
// and the normal view, all with a single function.
static void HWR_SetTransformAiming(FTransform *trans, player_t *player, boolean skybox)
{
	// 1 = always on
	// 2 = chasecam only
	if (cv_glshearing.value == 1 || (cv_glshearing.value == 2 && R_IsViewpointThirdPerson(player, skybox)))
	{
		fixed_t fixedaiming = AIMINGTODY(aimingangle);
		trans->viewaiming = FIXED_TO_FLOAT(fixedaiming);
		trans->shearing = true;
		gl_aimingangle = 0;
	}
	else
	{
		trans->shearing = false;
		gl_aimingangle = aimingangle;
	}

	trans->anglex = (float)(gl_aimingangle>>ANGLETOFINESHIFT)*(360.0f/(float)FINEANGLES);
}

//
// Sets the shader state.
//
static void HWR_SetShaderState(void)
{
	hwdshaderoption_t state = (hwdshaderoption_t)cv_glshaders.value;

	if (!cv_glallowshaders.value)
		state = (hwdshaderoption_t)(cv_glshaders.value == HWD_SHADEROPTION_ON ? HWD_SHADEROPTION_NOCUSTOM : cv_glshaders.value);

	HWD.pfnSetSpecialState(HWD_SET_SHADERS, (INT32)state);
	HWD.pfnSetShader(SHADER_DEFAULT);
}

// [DRAW] drawing breakdown by stage. v=0 main view, 1 skybox.
namespace rre_dib {
	enum { D_PREP, D_BSP, D_BATCH, D_STATIC, D_SSORT, D_SDRAW, D_NODES, D_END, D_N };
	static UINT64 acc[2][D_N];
	static UINT64 accPost = 0, accTotal = 0, accSkyTot = 0;
	static UINT64 nSpr[2], nTr[2], nBsp[2];
	static int frames = 0;
	static UINT64 last;
	static int cur = 0;
	static inline void Start(int v) { cur = v; last = I_GetPreciseTime(); }
	static inline void Mark(int seg) { UINT64 n = I_GetPreciseTime(); acc[cur][seg] += n - last; last = n; }
	static void Report(void)
	{
		const double us = 1000000.0 / (double)I_GetPrecisePrecision() / frames;
		static const char *nm[D_N] = {"prep","bsp","batch","static","ssort","sdraw","nodes","end"};
		RR_DIAGPRINT("[DRAW] us/frame total=%.0f skybox=%.0f post=%.0f\n", accTotal*us, accSkyTot*us, accPost*us);
		{ // top 10 sprite types per frame (both views)
			char tb[256]; int to = 0;
			for (int k = 0; k < 10; k++)
			{
				int bi = -1; UINT32 bv = 0;
				for (int t = 0; t < NUMMOBJTYPES; t++) if (rre_sprTypeCnt[t] > bv) { bv = rre_sprTypeCnt[t]; bi = t; }
				if (bi < 0) break;
				to += snprintf(tb+to, sizeof(tb)-to, " %d=%.1f", bi, bv/(double)frames);
				rre_sprTypeCnt[bi] = 0;
			}
			RR_DIAGPRINT("[SPRTYPE] mobjtype=sprites/frame:%s\n", tb);
			memset(rre_sprTypeCnt, 0, sizeof(rre_sprTypeCnt));
		}
		{ // sdraw per type (us/frame, draws/frame) + shadows + sprite build/upload
			char tb[320]; int to = 0;
			for (int k = 0; k < 8; k++)
			{
				int bi = -1; UINT64 bv = 0;
				for (int t = 0; t < NUMMOBJTYPES; t++) if (rre_sdTypeT[t] > bv) { bv = rre_sdTypeT[t]; bi = t; }
				if (bi < 0) break;
				to += snprintf(tb+to, sizeof(tb)-to, " %d=%.0fus/%.1f", bi, bv*us, rre_sdTypeN[bi]/(double)frames);
				rre_sdTypeT[bi] = 0;
			}
			memset(rre_sdTypeT, 0, sizeof(rre_sdTypeT)); memset(rre_sdTypeN, 0, sizeof(rre_sdTypeN));
			rre_sdShadowT = 0;
		}
		for (int v = 0; v < 2; v++)
		{
			char buf[256]; int o = 0;
			for (int i = 0; i < D_N; i++)
				o += snprintf(buf+o, sizeof(buf)-o, " %s=%.0f", nm[i], acc[v][i]*us);
			RR_DIAGPRINT("[DRAW] %s:%s | spr=%.0f transl=%.0f bspnodes=%.0f\n", v ? "SKY " : "MAIN", buf,
				nSpr[v]/(double)frames, nTr[v]/(double)frames, nBsp[v]/(double)frames);
		}
		for (int v = 0; v < 2; v++)
		{
			using namespace rre_sub;
			if (!framesV[v]) continue;
			const double u2 = 1000000.0 / (double)I_GetPrecisePrecision() / framesV[v];
			UINT64 sum = 0; for (int i = 0; i < S_N; i++) sum += accV[v][i];
			RR_DIAGPRINT("[SUB] %s bsp=%.0f fakeflat=%.0f planes=%.0f polyobj=%.0f addsprites=%.0f lines=%.0f walk=%.0f | subsect=%.0f segs=%.0f\n",
				v ? "SKY " : "MAIN", bspTotV[v]*u2, accV[v][S_FAKE]*u2, accV[v][S_PLANES]*u2, accV[v][S_POLY]*u2, accV[v][S_SPR]*u2, accV[v][S_LINES]*u2,
				((double)bspTotV[v] - (double)sum)*u2, nSubV[v]/(double)framesV[v], nLinesV[v]/(double)framesV[v]);
			RR_DIAGPRINT("[SUB] %s walls: front=%.0f processed=%.0f uncached=%.0f angle_reused=%.0f process_time=%.0f us\n",
				v ? "SKY " : "MAIN", nFront[v]/(double)framesV[v], nProc[v]/(double)framesV[v], nMiss[v]/(double)framesV[v],
				nAngReuse[v]/(double)framesV[v], segT[v]*u2);
			RR_DIAGPRINT("[SUB] %s wall breakdown: signature=%.0f use_cache=%.0f compute_misses=%.0f rest=%.0f us | memo hit=%d miss=%d (both views)\n",
				v ? "SKY " : "MAIN", sigT[v]*u2, useT[v]*u2, missT[v]*u2, ((double)segT[v] - (double)sigT[v] - (double)useT[v] - (double)missT[v])*u2,
				rre_static::stMemoHit, rre_static::stMemoMiss);
		}
		memset(rre_sub::sigT, 0, sizeof(rre_sub::sigT)); memset(rre_sub::useT, 0, sizeof(rre_sub::useT)); memset(rre_sub::missT, 0, sizeof(rre_sub::missT)); rre_static::stMemoHit = rre_static::stMemoMiss = 0;
		{
			const double u3 = 1000000.0 / (double)I_GetPrecisePrecision() / frames;
			RR_DIAGPRINT("[DRAW] sky(background) MAIN=%.0f SKY=%.0f us/frame\n", rre_sub::skyBgV[0]*u3, rre_sub::skyBgV[1]*u3);
		}
		memset(rre_sub::accV, 0, sizeof(rre_sub::accV)); memset(rre_sub::nSubV, 0, sizeof(rre_sub::nSubV));
		memset(rre_sub::nLinesV, 0, sizeof(rre_sub::nLinesV)); memset(rre_sub::bspTotV, 0, sizeof(rre_sub::bspTotV));
		memset(rre_sub::skyBgV, 0, sizeof(rre_sub::skyBgV)); memset(rre_sub::framesV, 0, sizeof(rre_sub::framesV)); memset(rre_sub::segT, 0, sizeof(rre_sub::segT)); memset(rre_sub::nFront, 0, sizeof(rre_sub::nFront));
		memset(rre_sub::nProc, 0, sizeof(rre_sub::nProc)); memset(rre_sub::nMiss, 0, sizeof(rre_sub::nMiss)); memset(rre_sub::nAngReuse, 0, sizeof(rre_sub::nAngReuse));
		RR_DIAGPRINT("[DRAW] skybox: cv=%d viewpoint=%d drawsky=%d\n", (int)cv_skybox.value,
			(int)(players[displayplayers[0]].skybox.viewpoint != NULL), (int)drawsky);
		memset(acc, 0, sizeof(acc)); memset(nSpr,0,sizeof(nSpr)); memset(nTr,0,sizeof(nTr)); memset(nBsp,0,sizeof(nBsp));
		accPost = accTotal = accSkyTot = 0; frames = 0;
	}
}
#define DIB_START(v) rre_dib::Start(v)
#define DIB_MARK(s) rre_dib::Mark(rre_dib::s)

static void HWR_RenderViewpoint(player_t *player, boolean drawSkyTexture, boolean timing)
{
	const float fpov = FIXED_TO_FLOAT(R_FOV(viewssnum)+player->fovadd);
	postimg_t *type = &postimgtype[viewssnum];
	DIB_START(timing ? 0 : 1);

	{
		// do we really need to save player (is it not the same)?
		player_t *saved_player = stplyr;
		stplyr = player;
		ST_doPaletteStuff();
		stplyr = saved_player;
#ifdef ALAM_LIGHTING
		HWR_SetLights(viewssnum);
#endif
	}

	// copy view cam position for local use
	dup_viewx = viewx;
	dup_viewy = viewy;
	dup_viewz = viewz;
	dup_viewangle = viewangle;

	// set window position
	HWR_ShiftViewPort();

	gl_viewx = FIXED_TO_FLOAT(dup_viewx);
	gl_viewy = FIXED_TO_FLOAT(dup_viewy);
	gl_viewz = FIXED_TO_FLOAT(dup_viewz);
	gl_viewsin = FIXED_TO_FLOAT(viewsin);
	gl_viewcos = FIXED_TO_FLOAT(viewcos);

	//04/01/2000: Hurdler: added for T&L
	//                     It should replace all other gl_viewxxx when finished
	memset(&atransform, 0x00, sizeof(FTransform));

	HWR_SetTransformAiming(&atransform, player, false);
	atransform.angley = (float)(viewangle>>ANGLETOFINESHIFT)*(360.0f/(float)FINEANGLES);

	gl_viewludsin = FIXED_TO_FLOAT(FINECOSINE(gl_aimingangle>>ANGLETOFINESHIFT));
	gl_viewludcos = FIXED_TO_FLOAT(-FINESINE(gl_aimingangle>>ANGLETOFINESHIFT));

	if (*type == postimg_flip)
		atransform.flip = true;
	else
		atransform.flip = false;

	if (*type == postimg_mirror)
		atransform.mirror = true;
	else
		atransform.mirror = false;

	atransform.x      = gl_viewx;  // FIXED_TO_FLOAT(viewx)
	atransform.y      = gl_viewy;  // FIXED_TO_FLOAT(viewy)
	atransform.z      = gl_viewz;  // FIXED_TO_FLOAT(viewz)
	atransform.scalex = 1;
	atransform.scaley = (float)vid.width/vid.height;
	atransform.scalez = 1;

	atransform.fovxangle = fpov; // Tails
	atransform.fovyangle = fpov; // Tails
	HWR_RollTransform(&atransform, viewroll);
	atransform.splitscreen = r_splitscreen;

	gl_fovlud = (float)(1.0l/tan((double)(fpov*M_PIl/360l)));

	//------------------------------------------------------------------------
	HWR_ClearView(); // Clears the depth buffer and resets the view I believe

	if (drawSkyTexture)
	{
		const UINT64 sk0 = I_GetPreciseTime();
		HWR_DrawSkyBackground(player);
		rre_sub::skyBgV[timing ? 0 : 1] += I_GetPreciseTime() - sk0;
	}

	//Hurdler: it doesn't work in splitscreen mode
	drawsky = r_splitscreen;

	HWR_ClearSprites();

	drawcount = 0;

	{
		angle_t a1 = gld_FrustumAngle(gl_aimingangle);
		gld_clipper_Clear();
		// In the sky texture, the cull is a bit wider (margins ~+8 degrees per side).
		// Without culling it cost ~3x (Sealed Star: skybox 16-19 ms).
		if (RRE_SkyTexDrawing()) { const angle_t ex = a1 + ANG1 * 12; if (ex > a1 && ex < ANGLE_180) a1 = ex; }
		gld_clipper_SafeAddClipRange(viewangle + a1, viewangle - a1);
#ifdef HAVE_SPHEREFRUSTRUM
		gld_FrustrumSetup();
#endif
	}

	//04/01/2000: Hurdler: added for T&L
	//                     Actually it only works on Walls and Planes
	HWD.pfnSetTransform(&atransform);
	if (timing && rre_skyQuad) RRE_SkyTexDraw(); // the stored sky, behind everything

	// Reset the shader state.
	HWR_SetShaderState();

	if (timing)
	{
		ps_numbspcalls = 0;
		ps_numpolyobjects = 0;
		ps_bsptime = I_GetPreciseTime();
	}

	validcount++;

	if (cv_glbatching.value)
		HWR_StartBatching();
	DIB_MARK(D_PREP);

	RRE_StaticFrameBegin();
	rre_sub::view = timing ? 0 : 1;
	if (timing) rre_sub::frameNo++;
	rre_sub::on = rre_static::Diag() && ((rre_sub::frameNo & 3) == 0);
	const UINT64 subT0 = I_GetPreciseTime();
	rre_seccache::stamp++;
	if (timing && rre_static::Diag()) rre_chg::Tick();
	HWR_RenderBSPNode((INT32)numnodes-1);
	if (rre_sub::on)
	{
		rre_sub::bspTotV[rre_sub::view] += I_GetPreciseTime() - subT0;
		rre_sub::framesV[rre_sub::view]++;
		rre_sub::on = 0;
	}

#ifdef HWPRECIP
	HWR_AddPrecipitationSprites();
#endif

	if (timing)
	{
		ps_bsptime = I_GetPreciseTime() - ps_bsptime;
	}

	DIB_MARK(D_BSP);
	if (timing) rre_dib::nBsp[0] += ps_numbspcalls;
	if (cv_glbatching.value)
		HWR_RenderBatches();
	DIB_MARK(D_BATCH);
	RRE_StaticFrameDraw();
	DIB_MARK(D_STATIC);

#ifdef ALAM_LIGHTING
	//14/11/99: Hurdler: moved here because it doesn't work with
	// subsector, see other comments;
	HWR_ResetLights();
#endif

	// Draw MD2 and sprites

	if (timing)
	{
		ps_numsprites = gl_visspritecount;
		ps_hw_spritesorttime = I_GetPreciseTime();
	}

	HWR_SortVisSprites();
	DIB_MARK(D_SSORT);

	if (timing)
	{
		ps_hw_spritesorttime = I_GetPreciseTime() - ps_hw_spritesorttime;
		ps_hw_spritedrawtime = I_GetPreciseTime();
	}

	rre_dib::nSpr[rre_dib::cur] += gl_visspritecount;
	rre_dib::nTr[rre_dib::cur] += numplanes + numpolyplanes + numwalls;
	HWR_DrawSprites();
	DIB_MARK(D_SDRAW);

	if (timing)
	{
		ps_hw_spritedrawtime = I_GetPreciseTime() - ps_hw_spritedrawtime;
	}

#ifdef NEWCORONAS
	//Hurdler: they must be drawn before translucent planes, what about gl fog?
	HWR_DrawCoronas();
#endif

	if (timing)
	{
		ps_numdrawnodes = 0;
		ps_hw_nodesorttime = 0;
		ps_hw_nodedrawtime = 0;
	}

	if (numplanes || numpolyplanes || numwalls) //Hurdler: render 3D water and transparent walls after everything
	{
		HWR_CreateDrawNodes();
	}
	DIB_MARK(D_NODES);

	HWD.pfnSetTransform(NULL);
	HWD.pfnUnSetShader();

	// added by Hurdler for correct splitscreen
	// moved here by hurdler so it works with the new near clipping plane
	HWD.pfnGClipRect(0, 0, vid.width, vid.height, NZCLIP_PLANE);
	DIB_MARK(D_END);
}

// ==========================================================================
// Same as rendering the player view, but from the skybox object
// ==========================================================================
void HWR_RenderSkyboxView(player_t *player)
{
	// note: sets viewangle, viewx, viewy, viewz
	R_SkyboxFrame(viewssnum);

	rre_skycull::Begin(player);
	HWR_RenderViewpoint(player, drawsky, false);
	rre_skycull::active = false;
}

// ==========================================================================
//
// ==========================================================================

static void HWR_RollTransform(FTransform *tr, angle_t roll)
{
	if (roll != 0)
	{
		tr->rollangle = roll / (float)ANG1;
		tr->roll = true;
		tr->rollx = 1.0f;
		tr->rollz = 0.0f;
	}
}

void HWR_RenderPlayerView(void)
{
	const UINT64 dibT0 = I_GetPreciseTime();
	HWD.pfnResetRenderState();

	player_t * player = &players[displayplayers[viewssnum]];

	const boolean skybox = (player->skybox.viewpoint && cv_skybox.value); // True if there's a skybox object and skyboxes are on

	FRGBAFloat ClearColor;

	ClearColor.red = 0.0f;
	ClearColor.green = 0.0f;
	ClearColor.blue = 0.0f;
	ClearColor.alpha = 1.0f;

	if (cv_glshaders.value)
	{
		HWD.pfnSetShaderInfo(HWD_SHADERINFO_LEVELTIME, (INT32)leveltime); // The water surface shader needs the leveltime.

		const angle_t light_angle = maplighting.angle - viewangle + ANGLE_90; // I fucking hate OGL's coordinate system
		HWD.pfnSetShaderInfo(HWD_SHADERINFO_LIGHT_X, FINECOSINE(light_angle >> ANGLETOFINESHIFT));
		HWD.pfnSetShaderInfo(HWD_SHADERINFO_LIGHT_Y, 0);
		HWD.pfnSetShaderInfo(HWD_SHADERINFO_LIGHT_Z,  -FINESINE(light_angle >> ANGLETOFINESHIFT));

		HWD.pfnSetShaderInfo(HWD_SHADERINFO_LIGHT_CONTRAST, maplighting.contrast);
		HWD.pfnSetShaderInfo(HWD_SHADERINFO_LIGHT_BACKLIGHT, maplighting.backlight);
	}

	if (viewssnum == 0) // Only do it if it's the first screen being rendered
		HWD.pfnClearBuffer(true, false, &ClearColor); // Clear the Color Buffer, stops HOMs. Also seems to fix the skybox issue on Intel GPUs.

	ps_hw_skyboxtime = I_GetPreciseTime();
	// The skybox is drawn into a texture at most 1 in 3 frames
	// (or sooner when turning a lot) and the other frames reuse it, shifted.
	rre_skyQuad = false;
	if (skybox && drawsky)
	{
		static int skyAge = 99;
		static INT32 skyMap = -1;
		if (gamemap != skyMap) { skyMap = gamemap; RRE_SkyTexInvalidate(); skyAge = 99; }
		if (RRE_SkyTexOn() && r_splitscreen == 0)
		{
			if (skyAge >= 2 || RRE_SkyTexNeedRedraw())
			{
				if (RRE_SkyTexBegin())
				{
					HWR_RenderSkyboxView(player);
					RRE_SkyTexEnd();
					skyAge = 0; rre_skyDrawn++;
					rre_skyQuad = true;
				}
				else
					HWR_RenderSkyboxView(player);
			}
			else { skyAge++; rre_skyReused++; rre_skyQuad = true; }
		}
		else
			HWR_RenderSkyboxView(player);
		if (rre_skyDrawn + rre_skyReused >= 90)
		{
			int w0, w1, w2; float ox, oy; RRE_SkyTexWhy(&w0, &w1, &w2, &ox, &oy);
			RR_DIAGPRINT("[SKYTEX] 90 frames: drawn=%d reused=%d | why: no_data=%d behind=%d turn=%d | last shift=%.3f,%.3f\n",
				rre_skyDrawn, rre_skyReused, w0, w1, w2, ox, oy);
			rre_skyDrawn = rre_skyReused = 0;
		}
	}
	ps_hw_skyboxtime = I_GetPreciseTime() - ps_hw_skyboxtime;
	rre_dib::accSkyTot += ps_hw_skyboxtime;

	// note: sets viewangle, viewx, viewy, viewz
	R_SetupFrame(viewssnum);
	framecount++; // timedemo

	HWR_RenderViewpoint(player,
			!skybox && drawsky, // Don't draw the regular sky if there's a skybox
			true); // Main view is profiled

	const UINT64 dibP0 = I_GetPreciseTime();
	HWR_DoPostProcessor(player);
	{
		const UINT64 n = I_GetPreciseTime();
		rre_dib::accPost += n - dibP0;
		rre_dib::accTotal += n - dibT0;
		if (viewssnum == 0 && ++rre_dib::frames >= 30)
			rre_dib::Report();
	}
}

void HWR_LoadLevel(void)
{
#ifdef ALAM_LIGHTING
	// BP: reset light between levels (we draw preview frame lights on current frame)
	HWR_ResetLights();
#endif

	HWR_CreatePlanePolygons((INT32)numnodes - 1);
	RRE_StaticLevelInit();
	rre_skycull::LevelReset();
	rre_seccache::LevelReset();

	// Build the sky dome
	HWR_ClearSkyDome();
	HWR_BuildSkyDome();

	// Preload the track's textures in GL (Software already does it with R_PrecacheLevel,
	// GL did not). Without this every wall/floor was converted and uploaded the first time it was seen: fps drop
	// during the first seconds (RR_SealedStarBalconies: 20-25 ms/frame at the start).
	{
		const precise_t t0 = I_GetPreciseTime();
		size_t i;
		if (skytexture > 0 && skytexture < numtextures)
			HWR_GetTexture(texturetranslation[skytexture], skytexture);
		for (i = 0; i < numsides; i++)
		{
			const INT32 base[3] = { sides[i].toptexture, sides[i].midtexture, sides[i].bottomtexture };
			for (INT32 k = 0; k < 3; k++)
				if (base[k] > 0 && base[k] < numtextures)
					HWR_GetTexture(R_GetTextureNum(base[k]), base[k]);
		}
		for (i = 0; i < numsectors; i++)
		{
			sector_t *sec = &sectors[i];
			if (sec->floorpic >= 0 && (size_t)sec->floorpic < numlevelflats)
				HWR_GetLevelFlat(&levelflats[sec->floorpic], R_NoEncore(sec, &levelflats[sec->floorpic], false));
			if (sec->ceilingpic >= 0 && (size_t)sec->ceilingpic < numlevelflats)
				HWR_GetLevelFlat(&levelflats[sec->ceilingpic], R_NoEncore(sec, &levelflats[sec->ceilingpic], true));
		}
		I_OutputMsg("[PRELOAD] GL track textures: %.0f ms\n",
			(double)(I_GetPreciseTime() - t0) * 1000.0 / (double)I_GetPrecisePrecision());
	}

	gl_maploaded = true;
}

// ==========================================================================
//                                                         3D ENGINE COMMANDS
// ==========================================================================

extern "C" CV_PossibleValue_t glshaders_cons_t[];
CV_PossibleValue_t glshaders_cons_t[] = {{HWD_SHADEROPTION_OFF, "Off"}, {HWD_SHADEROPTION_ON, "On"}, {HWD_SHADEROPTION_NOCUSTOM, "Ignore custom shaders"}, {0, NULL}};

extern "C" CV_PossibleValue_t glfiltermode_cons_t[];
CV_PossibleValue_t glfiltermode_cons_t[]= {{HWD_SET_TEXTUREFILTER_POINTSAMPLED, "Nearest"},
	{HWD_SET_TEXTUREFILTER_BILINEAR, "Bilinear"}, {HWD_SET_TEXTUREFILTER_TRILINEAR, "Trilinear"},
	{HWD_SET_TEXTUREFILTER_MIXED1, "Linear_Nearest"},
	{HWD_SET_TEXTUREFILTER_MIXED2, "Nearest_Linear"},
	{HWD_SET_TEXTUREFILTER_MIXED3, "Nearest_Mipmap"},
	{0, NULL}};

extern "C" CV_PossibleValue_t glanisotropicmode_cons_t[];
CV_PossibleValue_t glanisotropicmode_cons_t[] = {{1, "MIN"}, {16, "MAX"}, {0, NULL}};

extern "C" void CV_glfiltermode_OnChange(void);
void CV_glfiltermode_OnChange(void)
{
	if (rendermode == render_opengl)
		HWD.pfnSetSpecialState(HWD_SET_TEXTUREFILTERMODE, cv_glfiltermode.value);
}

extern "C" void CV_glanisotropic_OnChange(void);
void CV_glanisotropic_OnChange(void)
{
	if (rendermode == render_opengl)
		HWD.pfnSetSpecialState(HWD_SET_TEXTUREANISOTROPICMODE, cv_glanisotropicmode.value);
}

extern "C" struct CVarList *cvlist_opengl;

//added by Hurdler: console varibale that are saved
void HWR_AddCommands(void)
{
	{
		CV_RegisterList(cvlist_opengl);
	}
}

void HWR_AddSessionCommands(void)
{
	if (gl_sessioncommandsadded)
		return;
	// Kept in case we ever need this again.
	gl_sessioncommandsadded = true;
}

// --------------------------------------------------------------------------
// Setup the hardware renderer
// --------------------------------------------------------------------------
void HWR_Startup(void)
{
	if (!gl_init)
	{
		CONS_Printf("HWR_Startup()...\n");

		HWR_InitPolyPool();
		HWR_AddSessionCommands();
		HWR_InitMapTextures();
		HWR_InitModels();
#ifdef ALAM_LIGHTING
		HWR_InitLight();
#endif

		HWR_LoadAllCustomShaders();
		if (!HWR_CompileShaders())
			gl_shadersavailable = false;
	}

	if (rendermode == render_opengl)
		textureformat = patchformat = GL_TEXFMT_RGBA;

	gl_init = true;
}

// --------------------------------------------------------------------------
// Called after switching to the hardware renderer
// --------------------------------------------------------------------------
void HWR_Switch(void)
{
	// Add session commands
	if (!gl_sessioncommandsadded)
		HWR_AddSessionCommands();

	// Set special states from CVARs
	HWD.pfnSetSpecialState(HWD_SET_TEXTUREFILTERMODE, cv_glfiltermode.value);
	HWD.pfnSetSpecialState(HWD_SET_TEXTUREANISOTROPICMODE, cv_glanisotropicmode.value);

	// Load textures
	if (!gl_maptexturesloaded)
		HWR_LoadMapTextures(numtextures);

	// Create plane polygons
	if (!gl_maploaded && (gamestate == GS_LEVEL || (gamestate == GS_TITLESCREEN && titlemapinaction)))
	{
		HWR_ClearAllTextures();
		HWR_LoadLevel();
	}
}

// --------------------------------------------------------------------------
// Free resources allocated by the hardware renderer
// --------------------------------------------------------------------------
void HWR_Shutdown(void)
{
	CONS_Printf("HWR_Shutdown()\n");
	HWR_FreeExtraSubsectors();
	HWR_FreePolyPool();
	HWR_FreeMapTextures();
	HWD.pfnFlushScreenTextures();
}

void transform(float *cx, float *cy, float *cz)
{
	float tr_x,tr_y;
	// translation
	tr_x = *cx - gl_viewx;
	tr_y = *cz - gl_viewy;
//	*cy = *cy;

	// rotation around vertical y axis
	*cx = (tr_x * gl_viewsin) - (tr_y * gl_viewcos);
	tr_x = (tr_x * gl_viewcos) + (tr_y * gl_viewsin);

	//look up/down ----TOTAL SUCKS!!!--- do the 2 in one!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
	tr_y = *cy - gl_viewz;

	*cy = (tr_x * gl_viewludcos) + (tr_y * gl_viewludsin);
	*cz = (tr_x * gl_viewludsin) - (tr_y * gl_viewludcos);

	//scale y before frustum so that frustum can be scaled to screen height
	*cy *= ORIGINAL_ASPECT * gl_fovlud;
	*cx *= gl_fovlud;
}

void HWR_AddTransparentWall(FOutVector *wallVerts, FSurfaceInfo *pSurf, INT32 texnum, INT32 basetexnum, FBITFIELD blend, boolean fogwall, INT32 lightlevel, extracolormap_t *wallcolormap)
{
	rre_static::Never(); // side effect: cannot be cached
	static size_t allocedwalls = 0;

	// Force realloc if buffer has been freed
	if (!wallinfo)
		allocedwalls = 0;

	if (allocedwalls < numwalls + 1)
	{
		allocedwalls += MAX_TRANSPARENTWALL;
		Z_Realloc(wallinfo, allocedwalls * sizeof (*wallinfo), PU_LEVEL, &wallinfo);
	}

	M_Memcpy(wallinfo[numwalls].wallVerts, wallVerts, sizeof (wallinfo[numwalls].wallVerts));
	M_Memcpy(&wallinfo[numwalls].Surf, pSurf, sizeof (FSurfaceInfo));
	wallinfo[numwalls].texnum = texnum;
	wallinfo[numwalls].basetexnum = basetexnum;
	wallinfo[numwalls].blend = blend;
	wallinfo[numwalls].drawcount = drawcount++;
	wallinfo[numwalls].fogwall = fogwall;
	wallinfo[numwalls].lightlevel = lightlevel;
	wallinfo[numwalls].wallcolormap = wallcolormap;
	numwalls++;
}

void HWR_RenderWall(FOutVector *wallVerts, FSurfaceInfo *pSurf, FBITFIELD blend, boolean fogwall, INT32 lightlevel, extracolormap_t *wallcolormap)
{
	FBITFIELD blendmode = blend;
	UINT8 alpha = pSurf->PolyColor.s.alpha; // retain the alpha

	INT32 shader = SHADER_DEFAULT;

	// Lighting is done here instead so that fog isn't drawn incorrectly on transparent walls after sorting
	HWR_Lighting(pSurf, lightlevel, wallcolormap, P_SectorUsesDirectionalLighting(gl_frontsector));

	pSurf->PolyColor.s.alpha = alpha; // put the alpha back after lighting

	if (blend & PF_Environment)
		blendmode |= PF_Occlude;	// PF_Occlude must be used for solid objects

	if (HWR_UseShader())
	{
		if (fogwall)
			shader = SHADER_FOG;
		else
			shader = SHADER_WALL;

		blendmode |= PF_ColorMapped;
	}

	if (fogwall)
		blendmode |= PF_Fog;

	blendmode |= PF_Modulated;	// No PF_Occlude means overlapping (incorrect) transparency
	DBG_TINT(pSurf, &blendmode, 0, 160, 0);
	HWR_ProcessPolygon(pSurf, wallVerts, 4, blendmode, shader, false);
}

INT32 HWR_GetTextureUsed(void)
{
	return HWD.pfnGetTextureUsed();
}

void HWR_DoPostProcessor(player_t *player)
{
	postimg_t *type = &postimgtype[R_GetViewNumber()];

	HWD.pfnUnSetShader();

	// Armageddon Blast Flash!
	// Could this even be considered postprocessor?
	if (player->flashcount)
	{
		FOutVector      v[4];
		FSurfaceInfo Surf;

		v[0].x = v[2].y = v[3].x = v[3].y = -4.0f;
		v[0].y = v[1].x = v[1].y = v[2].x = 4.0f;
		v[0].z = v[1].z = v[2].z = v[3].z = 4.0f; // 4.0 because of the same reason as with the sky, just after the screen is cleared so near clipping plane is 3.99

		// This won't change if the flash palettes are changed unfortunately, but it works for its purpose
		if (player->flashpal == PAL_NUKE)
		{
			Surf.PolyColor.s.red = 0xff;
			Surf.PolyColor.s.green = Surf.PolyColor.s.blue = 0x7F; // The nuke palette is kind of pink-ish
		}
		else
			Surf.PolyColor.s.red = Surf.PolyColor.s.green = Surf.PolyColor.s.blue = 0xff;

		Surf.PolyColor.s.alpha = 0xc0; // match software mode

		HWD.pfnDrawPolygon(&Surf, v, 4, PF_Modulated|PF_Additive|PF_NoTexture|PF_NoDepthTest);
	}

	// Ring Racers E: only with a water/heat effect in this view: that is the only user of the texture
	// (PostImgRedraw, same frame); HWR_DrawIntermissionBG is never called in Ring Racers. Copying the screen
	// every frame cost 1-17 ms with ANGLE (the CPU waits for the GPU) and some time on every API.
	if (gamestate != GS_INTERMISSION && !r_splitscreen && (*type == postimg_water || *type == postimg_heat))
		HWD.pfnMakeScreenTexture();

	if (r_splitscreen) // Not supported in splitscreen - someone want to add support?
		return;

	// Drunken vision! WooOOooo~
	if (*type == postimg_water || *type == postimg_heat)
	{
		// 10 by 10 grid. 2 coordinates (xy)
		float v[SCREENVERTS][SCREENVERTS][2];
		static double disStart = 0;

		UINT8 x, y;
		INT32 WAVELENGTH;
		INT32 AMPLITUDE;
		INT32 FREQUENCY;

		// Modifies the wave.
		if (*type == postimg_water)
		{
			WAVELENGTH = 5;
			AMPLITUDE = 20;
			FREQUENCY = 8;
		}
		else
		{
			WAVELENGTH = 10;
			AMPLITUDE = 60;
			FREQUENCY = 4;
		}

		for (x = 0; x < SCREENVERTS; x++)
		{
			for (y = 0; y < SCREENVERTS; y++)
			{
				// Change X position based on its Y position.
				v[x][y][0] = (x/((float)(SCREENVERTS-1.0f)/9.0f))-4.5f + (float)sin((disStart+(y*WAVELENGTH))/FREQUENCY)/AMPLITUDE;
				v[x][y][1] = (y/((float)(SCREENVERTS-1.0f)/9.0f))-4.5f;
			}
		}
		HWD.pfnPostImgRedraw(v);
		if (!(paused || P_AutoPause()))
			disStart += FIXED_TO_FLOAT(renderdeltatics);

		// Capture the screen again for screen waving on the intermission
		if(gamestate != GS_INTERMISSION)
			HWD.pfnMakeScreenTexture();
	}
	// Flipping of the screen isn't done here anymore
}

void HWR_StartScreenWipe(void)
{
	//CONS_Debug(DBG_RENDER, "In HWR_StartScreenWipe()\n");
	HWD.pfnStartScreenWipe();
}

void HWR_EndScreenWipe(void)
{
	//CONS_Debug(DBG_RENDER, "In HWR_EndScreenWipe()\n");
	HWD.pfnEndScreenWipe();
}

void HWR_DrawIntermissionBG(void)
{
	HWD.pfnDrawIntermissionBG();
}

//
// hwr mode wipes
//
static lumpnum_t wipelumpnum;

// puts wipe lumpname in wipename[9]
static boolean HWR_WipeCheck(UINT8 wipenum, UINT8 scrnnum)
{
	static char lumpname[9] = "FADEmmss";
	size_t lsize;

	// not a valid wipe number
	if (wipenum > 99 || scrnnum > 99)
		return false; // shouldn't end up here really, the loop should've stopped running beforehand

	// puts the numbers into the wipename
	lumpname[4] = '0'+(wipenum/10);
	lumpname[5] = '0'+(wipenum%10);
	lumpname[6] = '0'+(scrnnum/10);
	lumpname[7] = '0'+(scrnnum%10);
	wipelumpnum = W_CheckNumForName(lumpname);

	// again, shouldn't be here really
	if (wipelumpnum == LUMPERROR)
		return false;

	lsize = W_LumpLength(wipelumpnum);
	if (!(lsize == 256000 || lsize == 64000 || lsize == 16000 || lsize == 4000))
	{
		CONS_Alert(CONS_WARNING, "Fade mask lump %s of incorrect size, ignored\n", lumpname);
		return false; // again, shouldn't get here if it is a bad size
	}

	return true;
}

void HWR_DoWipe(UINT8 wipenum, UINT8 scrnnum)
{
	if (!HWR_WipeCheck(wipenum, scrnnum))
		return;

	HWR_GetFadeMask(wipelumpnum);
	HWD.pfnDoScreenWipe();
}

void HWR_DoTintedWipe(UINT8 wipenum, UINT8 scrnnum)
{
	// It does the same thing
	HWR_DoWipe(wipenum, scrnnum);
}

void HWR_MakeScreenFinalTexture(void)
{
    HWD.pfnMakeScreenFinalTexture();
}

void HWR_DrawScreenFinalTexture(int width, int height)
{
    HWD.pfnDrawScreenFinalTexture(width, height);
}

static inline UINT16 HWR_FindShaderDefs(UINT16 wadnum)
{
	UINT16 i;
	lumpinfo_t *lump_p;

	lump_p = wadfiles[wadnum]->lumpinfo;
	for (i = 0; i < wadfiles[wadnum]->numlumps; i++, lump_p++)
		if (memcmp(lump_p->name, "SHADERS", 7) == 0)
			return i;

	return INT16_MAX;
}

boolean HWR_CompileShaders(void)
{
	return HWD.pfnCompileShaders();
}

customshaderxlat_t shaderxlat[] =
{
	{"Flat", SHADER_FLOOR},
	{"WallTexture", SHADER_WALL},
	{"Sprite", SHADER_SPRITE},
	{"Model", SHADER_MODEL},
	{"ModelLighting", SHADER_MODEL_LIGHTING},
	{"WaterRipple", SHADER_WATER},
	{"Fog", SHADER_FOG},
	{"Sky", SHADER_SKY},
	{NULL, 0},
};

void HWR_LoadAllCustomShaders(void)
{
	INT32 i;

	// read every custom shader
	for (i = 0; i < numwadfiles; i++)
		HWR_LoadCustomShadersFromFile(i, (wadfiles[i]->type == RET_PK3));
}

void HWR_LoadCustomShadersFromFile(UINT16 wadnum, boolean PK3)
{
	UINT16 lump;
	char *shaderdef, *line;
	char *stoken;
	char *value;
	size_t size;
	int linenum = 1;
	int shadertype = 0;
	int i;

	lump = HWR_FindShaderDefs(wadnum);
	if (lump == INT16_MAX)
		return;

	shaderdef = (char *)W_CacheLumpNumPwad(wadnum, lump, PU_CACHE);
	size = W_LumpLengthPwad(wadnum, lump);

	line = (char *)Z_Malloc(size+1, PU_STATIC, NULL);
	M_Memcpy(line, shaderdef, size);
	line[size] = '\0';

	stoken = strtok(line, "\r\n ");
	while (stoken)
	{
		if ((stoken[0] == '/' && stoken[1] == '/')
			|| (stoken[0] == '#'))// skip comments
		{
			stoken = strtok(NULL, "\r\n");
			goto skip_field;
		}

		if (!stricmp(stoken, "GLSL"))
		{
			value = strtok(NULL, "\r\n ");
			if (!value)
			{
				CONS_Alert(CONS_WARNING, "HWR_LoadCustomShadersFromFile: Missing shader type (file %s, line %d)\n", wadfiles[wadnum]->filename, linenum);
				stoken = strtok(NULL, "\r\n"); // skip end of line
				goto skip_lump;
			}

			if (!stricmp(value, "VERTEX"))
				shadertype = 1;
			else if (!stricmp(value, "FRAGMENT"))
				shadertype = 2;

skip_lump:
			stoken = strtok(NULL, "\r\n ");
			linenum++;
		}
		else
		{
			value = strtok(NULL, "\r\n= ");
			if (!value)
			{
				CONS_Alert(CONS_WARNING, "HWR_LoadCustomShadersFromFile: Missing shader target (file %s, line %d)\n", wadfiles[wadnum]->filename, linenum);
				stoken = strtok(NULL, "\r\n"); // skip end of line
				goto skip_field;
			}

			if (!shadertype)
			{
				CONS_Alert(CONS_ERROR, "HWR_LoadCustomShadersFromFile: Missing shader type (file %s, line %d)\n", wadfiles[wadnum]->filename, linenum);
				Z_Free(line);
				return;
			}

			for (i = 0; shaderxlat[i].type; i++)
			{
				if (!stricmp(shaderxlat[i].type, stoken))
				{
					size_t shader_size;
					char *shader_source;
					char *shader_lumpname;
					UINT16 shader_lumpnum;

					if (PK3)
					{
						shader_lumpname = (char *)Z_Malloc(strlen(value) + 12, PU_STATIC, NULL);
						strcpy(shader_lumpname, "Shaders/sh_");
						strcat(shader_lumpname, value);
						shader_lumpnum = W_CheckNumForFullNamePK3(shader_lumpname, wadnum, 0);
					}
					else
					{
						shader_lumpname = (char *)Z_Malloc(strlen(value) + 4, PU_STATIC, NULL);
						strcpy(shader_lumpname, "SH_");
						strcat(shader_lumpname, value);
						shader_lumpnum = W_CheckNumForNamePwad(shader_lumpname, wadnum, 0);
					}

					if (shader_lumpnum == INT16_MAX)
					{
						CONS_Alert(CONS_ERROR, "HWR_LoadCustomShadersFromFile: Missing shader source %s (file %s, line %d)\n", shader_lumpname, wadfiles[wadnum]->filename, linenum);
						Z_Free(shader_lumpname);
						continue;
					}

					shader_size = W_LumpLengthPwad(wadnum, shader_lumpnum);
					shader_source = (char *)Z_Malloc(shader_size, PU_STATIC, NULL);
					W_ReadLumpPwad(wadnum, shader_lumpnum, shader_source);

					HWD.pfnLoadCustomShader(shaderxlat[i].id, shader_source, shader_size, (shadertype == 2));

					Z_Free(shader_source);
					Z_Free(shader_lumpname);
				}
			}

skip_field:
			stoken = strtok(NULL, "\r\n= ");
			linenum++;
		}
	}

	Z_Free(line);
	return;
}

const char *HWR_GetShaderName(INT32 shader)
{
	INT32 i;

	if (shader)
	{
		for (i = 0; shaderxlat[i].type; i++)
		{
			if (shaderxlat[i].id == shader)
				return shaderxlat[i].type;
		}

		return "Unknown";
	}

	return "Default";
}

#endif // HWRENDER
