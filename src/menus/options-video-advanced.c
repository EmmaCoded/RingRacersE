// DR. ROBOTNIK'S RING RACERS
//-----------------------------------------------------------------------------
// Copyright (C) 2025 by Kart Krew.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  menus/options-video-gl.c
/// \brief OpenGL Options

#include "../k_menu.h"
#include "../r_main.h"	// cv_skybox
#include "../hardware/hw_main.h"	// gl consvars
#include "../i_video.h" // rendermode
#include "../v_video.h" // info box text
#include "../z_zone.h"
#include "../console.h" // orangemap
#include "../w_wad.h" // RHFAV star

extern consvar_t cv_menuframeskip;

// see vaopt_e
// "Rendering Backend" = LIST of renderers (one per line) with the explanation in a box
// on the right. The ones this PC cannot use are NOT shown (the list is built in M_RefreshAdvancedVideoOptions
// from this template). mvar1 = cv_rrrenderer value; mvar2 = tag (VA_TAG_*).
#include "../hardware/rr_gfxapi.h"

enum { VA_TAG_NONE, VA_TAG_NOTE, VA_TAG_HINT, VA_TAG_LEGACY };
static void M_RendererPick(INT32 choice);

#define VA_RTIP "If you don't know why you're changing this, leave it on Software!"
#define VA_RITEM(name, v) {IT_STRING | IT_CALL, name, VA_RTIP, NULL, {.routine = M_RendererPick}, v, 0}

static menuitem_t VA_Template[] =
{
	{IT_HEADER, "Performance...", NULL,
		NULL, {NULL}, 0, 0},

	{IT_STRING | IT_CVAR, "Draw Distance", "How far objects can be drawn. A tradeoff between performance & visibility.",
		NULL, {.cvar = &cv_drawdist}, 0, 0},

	{IT_STRING | IT_CVAR, "Weather Draw Distance", "Affects how far weather visuals can be drawn. Lower values improve performance.",
		NULL, {.cvar = &cv_drawdist_precip}, 0, 0},

	{IT_STRING | IT_CVAR, "Enable Skyboxes", "Turning this off may improve performance, but reduces courses' background details.",
		NULL, {.cvar = &cv_skybox}, 0, 0},

	{IT_STRING | IT_CVAR, "Parallel Software", "Uses multiple CPU cores for the software renderer if available, for a FPS boost.",
		NULL, {.cvar = &cv_parallelsoftware}, 0, 0},

	{IT_STRING | IT_CVAR, "Extra Frame Skipping", "Skip 3D rendering frames while the menu is open.",
		NULL, {.cvar = &cv_menuframeskip}, 0, 0},

	{IT_STRING | IT_CVAR, "Low-End Mode", "Lighter game logic for weak PCs (from the 3DS port). Disables online. Applies on the next course.",
		NULL, {.cvar = &cv_rrlowend}, 0, 0},


	{IT_HEADER, "Rendering Backend...", VA_RTIP, // mvar2: the gray note goes at the top right
		NULL, {NULL}, 0, VA_TAG_NOTE},

	// DirectX 11/12 and Vulkan = Legacy GL ONLY; "Legacy GL" is called OpenGL here.
	// Values 4/6/8 (Software through Vulkan/DX12/DX11) are still valid in the config, but are not listed.
	VA_RITEM("Software", 0),
	VA_RITEM("OpenGL", 1),
	VA_RITEM("Direct3D 9", 2),
	VA_RITEM("Direct3DEx 9", 3),
	VA_RITEM("DirectX 11", 9),
	VA_RITEM("DirectX 12", 7),
	VA_RITEM("Vulkan", 5),

	{IT_SPACE | IT_NOTHING, NULL, NULL, // the centered "scroll down for Legacy GL options" hint is drawn here
		NULL, {NULL}, 0, VA_TAG_HINT},

	{IT_HEADER, "Legacy GL Options...", "Watch people get confused anyway!!",
		NULL, {NULL}, 0, VA_TAG_LEGACY},

	{IT_STRING | IT_CVAR, "3D Models", "Use 3D models instead of sprites when applicable.",
		NULL, {.cvar = &cv_glmodels}, 0, 0},

	{IT_STRING | IT_CVAR, "Shaders", "Use GLSL Shaders. Turning them off increases performance at the expanse of visual quality.",
		NULL, {.cvar = &cv_glshaders}, 0, 0},

	{IT_SPACE | IT_NOTHING, NULL,  NULL,
		NULL, {NULL}, 0, 0},

	{IT_STRING | IT_CVAR, "Texture Quality", "Texture depth. Higher values are recommended.",
		NULL, {.cvar = &cv_scr_depth}, 0, 0},

	/*
	{IT_STRING | IT_CVAR, "Texture Filter", "Texture Filter. Nearest is recommended.",
		NULL, {.cvar = &cv_glfiltermode}, 0, 0},
	*/

	{IT_STRING | IT_CVAR, "Anisotropic", "Lower values will improve performance at a minor quality loss.",
		NULL, {.cvar = &cv_glanisotropicmode}, 0, 0},

	{IT_SPACE | IT_NOTHING, NULL,  NULL,
		NULL, {NULL}, 0, 0},

	{IT_STRING | IT_CVAR, "Sprite Billboarding", "Adjusts sprites when viewed from above or below to not make them appear flat.",
		NULL, {.cvar = &cv_glspritebillboarding}, 0, 0},

	{IT_STRING | IT_CVAR, "Software Perspective", "Emulates Software shearing when looking up or down. Not recommended.",
		NULL, {.cvar = &cv_glshearing}, 0, 0},
};

#define VA_MAXITEMS (sizeof (VA_Template) / sizeof (menuitem_t))
menuitem_t OPTIONS_VideoAdvanced[VA_MAXITEMS];

static void M_DrawVideoAdvancedOptions(void);
#define VA_DRAW M_DrawVideoAdvancedOptions
#define VA_COUNT 0 // built in M_RefreshAdvancedVideoOptions

menu_t OPTIONS_VideoAdvancedDef = {
	VA_COUNT,
	&OPTIONS_VideoDef,
	0,
	OPTIONS_VideoAdvanced,
	48, 80,
	SKINCOLOR_PLAGUE, 0,
	MBF_DRAWBGWHILEPLAYING,
	NULL,
	2, 5,
	VA_DRAW,
	M_DrawOptionsCogs,
	M_OptionsTick,
	NULL,
	NULL,
	NULL,
};

static int VA_Api(int v)
{
	return v == 2 ? RRGFX_DX9 : v == 3 ? RRGFX_DX9EX : (v == 4 || v == 5) ? RRGFX_VULKAN
		: (v == 6 || v == 7) ? RRGFX_DX12 : (v == 8 || v == 9) ? RRGFX_DX11 : RRGFX_OPENGL;
}

// 1 = this PC can use that renderer (otherwise it is not listed)
static boolean VA_RendererOK(int v)
{
	const int api = VA_Api(v);
	if (api == RRGFX_OPENGL)
		return true;
	return RRGfx_Detected(api) && RRGfx_Implemented(api);
}

static const char *VA_Desc(int v)
{
	switch (v)
	{
		case 0: return "Draws the game with your CPU. The most compatible option. No 3D models.";
		case 1: return "Legacy GL on OpenGL, using your GPU. 3D models and shaders.";
		case 2: return "Software mode shown through Direct3D 9. Good for old GPUs. No 3D models.";
		case 3: return "Like Direct3D 9, with Windows Vista/7+ improvements. No 3D models.";
		case 4: return "Software mode through Vulkan (OpenGL translated by ANGLE).";
		case 5: return "Legacy GL translated to Vulkan by ANGLE. 3D models and shaders.";
		case 6: return "Software mode through DirectX 12 (OpenGL translated by Mesa).";
		case 7: return "Legacy GL translated to DirectX 12 by Mesa. 3D models and shaders.";
		case 8: return "Software mode through DirectX 11 (OpenGL translated by ANGLE).";
		case 9: return "Legacy GL translated to DirectX 11 by ANGLE. 3D models and shaders.";
	}
	return "";
}

static boolean VA_IsRenderer(const menuitem_t *it)
{
	return (it->status & IT_TYPE) == IT_CALL && it->itemaction.routine == M_RendererPick;
}

static void M_RendererPick(INT32 choice)
{
	const int v = currentMenu->menuitems[choice].mvar1;
	if (v != cv_rrrenderer.value)
		CV_SetValue(&cv_rrrenderer, v); // RRGfx_ChangeRenderer: switches live or asks to restart
}

void M_RefreshAdvancedVideoOptions(void)
{
	RRGfx_SyncMenu();

	// the cursor stays on the renderer it was on (the list length can change)
	const boolean open = (currentMenu == &OPTIONS_VideoAdvancedDef);
	int keep = -1;
	if (open && itemOn < OPTIONS_VideoAdvancedDef.numitems && VA_IsRenderer(&OPTIONS_VideoAdvanced[itemOn]))
		keep = OPTIONS_VideoAdvanced[itemOn].mvar1;

	INT16 n = 0;
	for (size_t i = 0; i < VA_MAXITEMS; i++)
	{
		const menuitem_t *it = &VA_Template[i];
		if (it->mvar2 == VA_TAG_LEGACY && rendermode != render_opengl)
			break; // Legacy GL options only with Legacy GL
		if (VA_IsRenderer(it) && !VA_RendererOK(it->mvar1))
			continue;
		OPTIONS_VideoAdvanced[n++] = *it;
	}
	OPTIONS_VideoAdvancedDef.numitems = n;

	if (open)
	{
		for (INT16 i = 0; keep >= 0 && i < n; i++)
			if (VA_IsRenderer(&OPTIONS_VideoAdvanced[i]) && OPTIONS_VideoAdvanced[i].mvar1 == keep)
				itemOn = i;
		if (itemOn >= n)
			itemOn = n - 1;
	}
}

// The list is drawn by the generic menu code; here: explanation box on the right, mark on the renderer
// in use, gray note and centered hint about the Legacy GL options.
static void M_DrawVideoAdvancedOptions(void)
{
	if (OPTIONS_VideoAdvancedDef.numitems == 0)
		M_RefreshAdvancedVideoOptions();

	// With many renderers the list does not fit in 200 px: the page is scrolled up just enough (measured on the previous frame)
	static INT32 shift = 0;
	const INT16 basey = OPTIONS_VideoAdvancedDef.y;
	OPTIONS_VideoAdvancedDef.y = basey - shift;
	M_DrawGenericOptions();
	OPTIONS_VideoAdvancedDef.y = basey;
	{
		INT32 bottom = 0;
		for (INT32 i = 0; i < currentMenu->numitems && i < M_OPTIONITEMY_MAX; i++)
			if (M_OptionItemY[i] != M_OPTIONITEMY_NONE && M_OptionItemY[i] + 10 > bottom)
				bottom = M_OptionItemY[i] + 10;
		const INT32 want = bottom + shift - (BASEVIDHEIGHT - 4); // how much it would need to go up
		shift = want > 0 ? (want > 24 ? 24 : want) : 0;
	}

	if (menutransition.tics != menutransition.dest)
		return;

	const INT32 x = M_OptionItemX;
	INT32 first = -1, last = -1, namew = 0, hdr = -1;
	for (INT32 i = 0; i < currentMenu->numitems && i < M_OPTIONITEMY_MAX; i++)
	{
		const menuitem_t *it = &currentMenu->menuitems[i];
		if (M_OptionItemY[i] == M_OPTIONITEMY_NONE)
			continue;
		if (VA_IsRenderer(it))
		{
			if (first < 0)
				first = i;
			last = i;
			const INT32 w = V_MenuStringWidth(it->text, 0);
			if (w > namew)
				namew = w;
			if (it->mvar1 == cv_rrrenderer.value) // the one in use: star on the left + green text
			{
				patch_t *star = W_CachePatchName("RHFAV", PU_CACHE);
				V_DrawScaledPatch(x - SHORT(star->width) - 2, M_OptionItemY[i] - 2, 0, star);
				if (i != itemOn) // under the cursor it stays light blue, like any selection
					V_DrawMenuString(x, M_OptionItemY[i], V_GREENMAP, it->text);
			}
		}
		else if (it->mvar2 == VA_TAG_HINT)
			V_DrawCenteredThinString(BASEVIDWIDTH/2, M_OptionItemY[i] + 1, V_YELLOWMAP,
				rendermode == render_opengl
					? "Scroll down for Legacy GL options (3D models, shaders)"
					: "Legacy GL options show up with OpenGL, DirectX 11/12 or Vulkan");
		else if (it->mvar2 == VA_TAG_NOTE)
			hdr = i;
	}
	if (first < 0)
		return; // the section is closed

	if (hdr >= 0) // gray note above the orange box, on the right
	{
		const char *note = "Not listed? Then your PC can't use it.";
		V_DrawThinString(BASEVIDWIDTH - (x - 20) - 3 - V_ThinStringWidth(note, 0), M_OptionItemY[hdr] - 4, V_GRAYMAP, note);
	}

	// box to the right of the list, INSIDE the menu's orange box (its right edge is at
	// BASEVIDWIDTH - (x - 20) - 2): dark, slightly transparent navy blue with an orange border and a shadow
	const INT32 x0 = x + namew + 14, x1 = BASEVIDWIDTH - (x - 20) - 7;
	const INT32 y0 = M_OptionItemY[first] - 2, y1 = M_OptionItemY[last] + 7; // the bottom does not cover the yellow hint
	if (x1 - x0 < 60)
		return;
	V_DrawFadeFill(x0, y0, x1 - x0, y1 - y0, 0, 159, 7);
	V_DrawFill(x0, y0, x1 - x0, 1, orangemap[0]);          // top
	V_DrawFill(x0, y1 - 1, x1 - x0, 1, orangemap[0]);      // bottom
	V_DrawFill(x0, y0, 1, y1 - y0, orangemap[0]);          // left
	V_DrawFill(x1 - 1, y0, 1, y1 - y0, orangemap[0]);      // right
	V_DrawFill(x0 + 1, y1, x1 - x0, 1, 31);                // shadow
	V_DrawFill(x1, y0 + 1, 1, y1 - y0, 31);

	int v = cv_rrrenderer.value; // explains the one under the cursor (or the one in use)
	if (itemOn < currentMenu->numitems && VA_IsRenderer(&currentMenu->menuitems[itemOn]))
		v = currentMenu->menuitems[itemOn].mvar1;
	const char *name = "";
	for (INT32 i = 0; i < currentMenu->numitems; i++)
		if (VA_IsRenderer(&currentMenu->menuitems[i]) && currentMenu->menuitems[i].mvar1 == v)
			name = currentMenu->menuitems[i].text;

	V_DrawThinString(x0 + 5, y0 + 4, V_YELLOWMAP, name);
	char *wrapped = V_ScaledWordWrap((x1 - x0 - 10) << FRACBITS, FRACUNIT, FRACUNIT, FRACUNIT, 0, TINY_FONT, VA_Desc(v));
	V_DrawThinString(x0 + 5, y0 + 16, 0, wrapped);
	Z_Free(wrapped);
	if (v == cv_rrrenderer.value)
		V_DrawThinString(x0 + 5, y1 - 11, V_GREENMAP, "IN USE");
	else if (cv_rrapirestart.value && VA_Api(v) != RRGfx_Active()) // everything switches live except with the "safe switch"
		V_DrawThinString(x0 + 5, y1 - 11, V_GRAYMAP, "Needs a restart");
}
