// rr_staticgeo.cpp - Ring Racers E: static geometry pool (Low-End Mode with OpenGL).
// Level geometry that doesn't change is captured once into this vertex pool and drawn from it
// (r_opengl.cpp DrawStaticPool; the VBO is only re-uploaded when the pool changes: s_ver).
#include <stdlib.h>
#include <string.h>
#include "../doomdef.h"
#include "hw_defs.h"
#include "hw_data.h"
#include "hw_drv.h"
#include "../doomstat.h"
#include "../i_video.h"

namespace {
struct SVert { float x, y, z, s, t; unsigned int c; }; // 24 bytes
static_assert(sizeof(SVert) == 24, "SVert must be 24 bytes");
enum { kStaticVerts = 65535 };
SVert *s_spool = NULL;
size_t s_spoolUsed = 0;
UINT32 s_ver = 1; // changes on every write to the pool
}

extern "C" {
int g_rre_dbgflat = 0;


int RRE_StaticAdd(const FOutVector *v, int n, unsigned int rgba)
{
	if (!s_spool) s_spool = (SVert *)malloc(sizeof(SVert) * kStaticVerts);
	if (!s_spool || n < 3 || s_spoolUsed + (size_t)n > kStaticVerts) return -1;
	const size_t first = s_spoolUsed;
	SVert *d = s_spool + first;
	for (int i = 0; i < n; i++)
	{
		d[i].x = v[i].x; d[i].y = v[i].y; d[i].z = v[i].z;
		d[i].s = v[i].s; d[i].t = v[i].t; d[i].c = rgba;
	}
	s_spoolUsed += (size_t)n;
	s_ver++;
	return (int)first;
}
void RRE_StaticShiftST(int first, int n, float ds, float dt)
{
	if (!s_spool || first < 0 || n <= 0 || (size_t)(first + n) > s_spoolUsed) return;
	for (int i = 0; i < n; i++) { s_spool[first + i].s += ds; s_spool[first + i].t += dt; }
	s_ver++;
}
void RRE_StaticSetYT(int first, int n, const float *y, const float *t)
{
	if (!s_spool || first < 0 || n <= 0 || (size_t)(first + n) > s_spoolUsed) return;
	for (int i = 0; i < n; i++) { s_spool[first + i].y = y[i]; s_spool[first + i].t = t[i]; }
	s_ver++;
}
void RRE_StaticSetY(int first, int n, float y)
{
	if (!s_spool || first < 0 || n <= 0 || (size_t)(first + n) > s_spoolUsed) return;
	for (int i = 0; i < n; i++) s_spool[first + i].y = y;
	s_ver++;
}
int RRE_StaticUsed(void) { return (int)s_spoolUsed; }
void RRE_StaticTruncate(int used) { if (used >= 0 && (size_t)used <= s_spoolUsed) { s_spoolUsed = (size_t)used; s_ver++; } }
int RRE_StaticReset(void) { s_spoolUsed = 0; s_ver++; return 1; }
boolean HWR_UseShaderPC(void); // hw_main.cpp
// 1 = drawn (or nothing to draw), 0 = texture lost (recapture), -1 = couldn't draw.
int RRE_StaticDraw(GLMipmap_t *mip, GLMipmap_t *bright, FBITFIELD flags, const UINT16 *idx, int n)
{
	if (rendermode != render_opengl || !HWD.pfnDrawStaticPool || !HWD.pfnSetTexture)
		return 1; // not OpenGL: nothing to draw
	if (!s_spool || !s_spoolUsed || n < 3)
		return -1;
	if (mip && !(flags & PF_NoTexture))
	{
		if (!mip->downloaded && !mip->data)
			return 0;
		HWD.pfnSetTexture(mip);
		if (bright && (bright->flags & TF_BRIGHTMAP) && (bright->downloaded || bright->data))
			HWD.pfnSetTexture(bright);
	}
	HWD.pfnDrawStaticPool(s_spool, (INT32)s_spoolUsed, s_ver, idx, n, flags, HWR_UseShaderPC());
	return 1;
}
void RRE_StaticEnd(void) {}
}
