// r_gles.inl - Ring Racers E: Legacy GL on top of OpenGL ES 3 (ANGLE: DirectX 11 / Vulkan).
// ES lacks the old OpenGL features (fixed matrices, glVertexPointer, lights, alpha test, texenv,
// LUMINANCE/ALPHA formats with RGBA data, RGB glReadPixels...). They are emulated here: when the context
// is ES (rr_gl_es = 1, set by i_video.cpp for ANGLE) RRGLES_Install() swaps r_opengl.cpp's pgl* pointers
// for these functions. The game's shaders (and mods') are translated from GLSL 1.20 to GLSL ES 1.00.
// Included INSIDE r_opengl.cpp (it uses its static pointers). Regular OpenGL never goes through here.
#include <string>
#include <vector>
#include <unordered_map>
#include <cctype>

extern "C" int rr_gl_es;
int rr_gl_es = 0;

#ifndef GL_CLAMP
#define GL_CLAMP 0x2900
#endif
#define RRG_ALPHA 0x1906
#define RRG_LUMINANCE 0x1909
#define RRG_LUMINANCE_ALPHA 0x190A
#define RRG_ALPHA8 0x803C
#define RRG_LUMINANCE8 0x8040
#define RRG_LUMINANCE8_ALPHA8 0x8045
#define RRG_RGB8 0x8051
#define RRG_VERTEX_ARRAY 0x8074
#define RRG_NORMAL_ARRAY 0x8075
#define RRG_COLOR_ARRAY 0x8076
#define RRG_TEXTURE_COORD_ARRAY 0x8078
#define RRG_MODELVIEW 0x1700
#define RRG_PROJECTION 0x1701
#define RRG_MODELVIEW_MATRIX 0x0BA6
#define RRG_PROJECTION_MATRIX 0x0BA7
#define RRG_ALPHA_TEST 0x0BC0
#define RRG_LIGHTING 0x0B50
#define RRG_LIGHT0 0x4000
#define RRG_NORMALIZE 0x0BA1
#define RRG_FOG 0x0B60
#define RRG_COLOR_MATERIAL 0x0B57
#define RRG_RESCALE_NORMAL 0x803A
#define RRG_TEXTURE_2D 0x0DE1
#define RRG_TEXTURE_ENV_MODE 0x2200
#define RRG_MODULATE 0x2100
#define RRG_REPLACE 0x1E01
#define RRG_LIGHT_MODEL_AMBIENT 0x0B53
#define RRG_AMBIENT 0x1200
#define RRG_DIFFUSE 0x1201
#define RRG_POSITION 0x1203
#define RRG_AMBIENT_AND_DIFFUSE 0x1602
#define RRG_DEPTH_COMPONENT 0x1902
#define RRG_PACK_ALIGNMENT 0x0D05

// Fixed attribute slots of the emulation (also used by the translated shaders)
enum { RRA_VERTEX = 0, RRA_TEX0 = 1, RRA_COLOR = 2, RRA_NORMAL = 3, RRA_TEX1 = 4, RRA_COUNT = 5 };

// ---- real ES functions ----
static struct
{
	void (APIENTRY *TexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
	void (APIENTRY *TexSubImage2D)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void*);
	void (APIENTRY *ReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
	void (APIENTRY *PixelStorei)(GLenum, GLint);
	void (APIENTRY *TexParameteri)(GLenum, GLenum, GLint);
	void (APIENTRY *Enable)(GLenum);
	void (APIENTRY *Disable)(GLenum);
	void (APIENTRY *BindTexture)(GLenum, GLuint);
	void (APIENTRY *DeleteTextures)(GLsizei, const GLuint*);
	// copies of the screen through glBlitFramebuffer (see rrg_blitcopy)
	void (APIENTRY *BlitFramebuffer)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);
	void (APIENTRY *GenFramebuffers)(GLsizei, GLuint*);
	void (APIENTRY *DeleteFramebuffers)(GLsizei, const GLuint*);
	void (APIENTRY *BindFramebuffer)(GLenum, GLuint);
	void (APIENTRY *FramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
	GLboolean (APIENTRY *IsEnabled)(GLenum);
	void (APIENTRY *CopyTexSubImage2D)(GLenum, GLint, GLint, GLint, GLint, GLint, GLsizei, GLsizei);
	void (APIENTRY *ActiveTexture)(GLenum);
	void (APIENTRY *GetFloatv)(GLenum, GLfloat*);
	void (APIENTRY *ClearDepthf)(GLfloat);
	void (APIENTRY *DepthRangef)(GLfloat, GLfloat);
	void (APIENTRY *VertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
	void (APIENTRY *EnableVertexAttribArray)(GLuint);
	void (APIENTRY *DisableVertexAttribArray)(GLuint);
	void (APIENTRY *VertexAttrib4fv)(GLuint, const GLfloat*);
	void (APIENTRY *DrawArrays)(GLenum, GLint, GLsizei);
	void (APIENTRY *DrawElements)(GLenum, GLsizei, GLenum, const void*);
	void (APIENTRY *CopyTexImage2D)(GLenum, GLint, GLenum, GLint, GLint, GLsizei, GLsizei, GLint);
	void (APIENTRY *UseProgram)(GLuint);
	GLuint (APIENTRY *CreateShader)(GLenum);
	void (APIENTRY *ShaderSource)(GLuint, GLsizei, const GLchar* const*, const GLint*);
	void (APIENTRY *CompileShader)(GLuint);
	void (APIENTRY *GetShaderiv)(GLuint, GLenum, GLint*);
	void (APIENTRY *GetShaderInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
	void (APIENTRY *DeleteShader)(GLuint);
	GLuint (APIENTRY *CreateProgram)(void);
	void (APIENTRY *AttachShader)(GLuint, GLuint);
	void (APIENTRY *LinkProgram)(GLuint);
	void (APIENTRY *GetProgramiv)(GLuint, GLenum, GLint*);
	void (APIENTRY *DeleteProgram)(GLuint);
	void (APIENTRY *BindAttribLocation)(GLuint, GLuint, const GLchar*);
	GLint (APIENTRY *GetUniformLocation)(GLuint, const GLchar*);
	void (APIENTRY *Uniform1i)(GLint, GLint);
	void (APIENTRY *Uniform1f)(GLint, GLfloat);
	void (APIENTRY *Uniform4fv)(GLint, GLsizei, const GLfloat*);
	void (APIENTRY *UniformMatrix3fv)(GLint, GLsizei, GLboolean, const GLfloat*);
	void (APIENTRY *UniformMatrix4fv)(GLint, GLsizei, GLboolean, const GLfloat*);
	void (APIENTRY *GenerateMipmap)(GLenum);
	void (APIENTRY *GetIntegerv)(GLenum, GLint*);
} rg;

// ---- emulated state ----
struct RRMat { GLfloat m[16]; };
static RRMat rrg_stack[3][32];
static int rrg_sp[3];
static int rrg_mode; // 0 modelview, 1 projection, 2 texture (ignored)
static unsigned rrg_ser[2] = {1, 1}; // changes every time that matrix changes

static bool rrg_arr[RRA_COUNT];
static GLfloat rrg_const[RRA_COUNT][4] = {{0,0,0,1},{0,0,0,1},{1,1,1,1},{0,0,1,0},{0,0,0,1}};
static int rrg_client_unit, rrg_unit;
static bool rrg_tex_on[2];
static int rrg_env[2] = {RRG_MODULATE, RRG_MODULATE};
static GLuint rrg_bound[2];
static bool rrg_alpha_on;
static GLenum rrg_alpha_func = GL_ALWAYS;
static GLfloat rrg_alpha_ref;
static unsigned rrg_alpha_ser = 1, rrg_ff_ser = 1;
static bool rrg_lighting;
static GLfloat rrg_light_pos[4] = {0,0,1,0}, rrg_light_dif[4] = {1,1,1,1}, rrg_light_amb[4] = {0,0,0,1};
static GLfloat rrg_model_amb[4] = {0.2f,0.2f,0.2f,1}, rrg_mat_amb[4] = {0.2f,0.2f,0.2f,1}, rrg_mat_dif[4] = {0.8f,0.8f,0.8f,1};
static GLint rrg_pack_align = 4;
static GLuint rrg_userprog, rrg_ffprog, rrg_boundprog;
static std::unordered_map<GLuint, GLenum> rrg_shadertype;
static std::unordered_map<GLuint, unsigned char> rrg_texkind; // how the texture was requested (see rrg_kind)
static std::vector<unsigned char> rrg_tmp;

struct RRProg
{
	GLint mv, proj, nm, afunc, aref;
	GLint ff_light, ff_lpos, ff_ldif, ff_lamb, ff_mamb, ff_matamb, ff_matdif, ff_env0, ff_env1;
	unsigned ser_mv, ser_proj, ser_alpha, ser_ff;
};
static std::unordered_map<GLuint, RRProg> rrg_progs;

static GLfloat *rrg_top(void) { return rrg_stack[rrg_mode][rrg_sp[rrg_mode]].m; }
static void rrg_touch(void) { if (rrg_mode < 2) rrg_ser[rrg_mode]++; }

static void rrg_mul(GLfloat *r, const GLfloat *a, const GLfloat *b) // r = a * b (column-major)
{
	GLfloat t[16];
	for (int c = 0; c < 4; c++)
		for (int rr = 0; rr < 4; rr++)
			t[c*4+rr] = a[0*4+rr]*b[c*4+0] + a[1*4+rr]*b[c*4+1] + a[2*4+rr]*b[c*4+2] + a[3*4+rr]*b[c*4+3];
	memcpy(r, t, sizeof t);
}
static void rrg_identity(GLfloat *m)
{
	memset(m, 0, 16 * sizeof(GLfloat));
	m[0] = m[5] = m[10] = m[15] = 1.0f;
}

// ---- matrices ----
static void APIENTRY rrg_MatrixMode(GLenum mode) { rrg_mode = mode == RRG_MODELVIEW ? 0 : mode == RRG_PROJECTION ? 1 : 2; }
static void APIENTRY rrg_LoadIdentity(void) { rrg_identity(rrg_top()); rrg_touch(); }
static void APIENTRY rrg_PushMatrix(void)
{
	if (rrg_sp[rrg_mode] < 31)
	{
		rrg_stack[rrg_mode][rrg_sp[rrg_mode] + 1] = rrg_stack[rrg_mode][rrg_sp[rrg_mode]];
		rrg_sp[rrg_mode]++;
	}
}
static void APIENTRY rrg_PopMatrix(void)
{
	if (rrg_sp[rrg_mode] > 0)
	{
		rrg_sp[rrg_mode]--;
		rrg_touch();
	}
}
static void APIENTRY rrg_MultMatrixf(const GLfloat *m) { rrg_mul(rrg_top(), rrg_top(), m); rrg_touch(); }
static void APIENTRY rrg_Translatef(GLfloat x, GLfloat y, GLfloat z)
{
	GLfloat m[16]; rrg_identity(m);
	m[12] = x; m[13] = y; m[14] = z;
	rrg_MultMatrixf(m);
}
static void APIENTRY rrg_Scalef(GLfloat x, GLfloat y, GLfloat z)
{
	GLfloat m[16]; rrg_identity(m);
	m[0] = x; m[5] = y; m[10] = z;
	rrg_MultMatrixf(m);
}
static void APIENTRY rrg_Rotatef(GLfloat a, GLfloat x, GLfloat y, GLfloat z)
{
	const GLfloat len = sqrtf(x*x + y*y + z*z);
	if (len <= 0.0f)
		return;
	x /= len; y /= len; z /= len;
	const GLfloat r = a * (GLfloat)M_PIl / 180.0f, c = cosf(r), s = sinf(r), ic = 1.0f - c;
	GLfloat m[16]; rrg_identity(m);
	m[0] = x*x*ic + c;   m[4] = x*y*ic - z*s; m[8]  = x*z*ic + y*s;
	m[1] = y*x*ic + z*s; m[5] = y*y*ic + c;   m[9]  = y*z*ic - x*s;
	m[2] = x*z*ic - y*s; m[6] = y*z*ic + x*s; m[10] = z*z*ic + c;
	rrg_MultMatrixf(m);
}
static void APIENTRY rrg_GetFloatv(GLenum pname, GLfloat *p)
{
	if (pname == RRG_MODELVIEW_MATRIX)
		memcpy(p, rrg_stack[0][rrg_sp[0]].m, 16 * sizeof(GLfloat));
	else if (pname == RRG_PROJECTION_MATRIX)
		memcpy(p, rrg_stack[1][rrg_sp[1]].m, 16 * sizeof(GLfloat));
	else
		rg.GetFloatv(pname, p);
}

// ---- state ----
static void rrg_cap(GLenum cap, bool on)
{
	switch (cap)
	{
		case RRG_TEXTURE_2D: if (rrg_tex_on[rrg_unit & 1] != on) { rrg_tex_on[rrg_unit & 1] = on; rrg_ff_ser++; } return;
		case RRG_ALPHA_TEST: if (rrg_alpha_on != on) { rrg_alpha_on = on; rrg_alpha_ser++; } return;
		case RRG_LIGHTING: if (rrg_lighting != on) { rrg_lighting = on; rrg_ff_ser++; } return;
		case RRG_LIGHT0: case RRG_NORMALIZE: case RRG_FOG: case RRG_COLOR_MATERIAL: case RRG_RESCALE_NORMAL: return;
	}
	if (on) rg.Enable(cap); else rg.Disable(cap);
}
static void APIENTRY rrg_Enable(GLenum cap) { rrg_cap(cap, true); }
static void APIENTRY rrg_Disable(GLenum cap) { rrg_cap(cap, false); }
static void APIENTRY rrg_AlphaFunc(GLenum func, GLclampf ref)
{
	if (rrg_alpha_func != func || rrg_alpha_ref != ref) { rrg_alpha_func = func; rrg_alpha_ref = ref; rrg_alpha_ser++; }
}
static void APIENTRY rrg_ClearDepth(GLclampd d) { rg.ClearDepthf((GLfloat)d); }
static void APIENTRY rrg_DepthRange(GLclampd n, GLclampd f) { rg.DepthRangef((GLfloat)n, (GLfloat)f); }
static void APIENTRY rrg_ShadeModel(GLenum) {}
static void APIENTRY rrg_Materiali(GLint, GLenum, GLint) {}
static void APIENTRY rrg_TexEnvi(GLenum, GLenum pname, GLint param)
{
	if (pname == RRG_TEXTURE_ENV_MODE && rrg_env[rrg_unit & 1] != param)
	{
		rrg_env[rrg_unit & 1] = param;
		rrg_ff_ser++;
	}
}
static void APIENTRY rrg_LightModelfv(GLenum pname, GLfloat *p)
{
	if (pname == RRG_LIGHT_MODEL_AMBIENT) { memcpy(rrg_model_amb, p, sizeof rrg_model_amb); rrg_ff_ser++; }
}
static void APIENTRY rrg_Lightfv(GLenum light, GLenum pname, GLfloat *p)
{
	if (light != RRG_LIGHT0)
		return;
	if (pname == RRG_POSITION) // stored in eye coordinates, like OpenGL
	{
		const GLfloat *m = rrg_stack[0][rrg_sp[0]].m;
		for (int i = 0; i < 4; i++)
			rrg_light_pos[i] = m[0*4+i]*p[0] + m[1*4+i]*p[1] + m[2*4+i]*p[2] + m[3*4+i]*p[3];
	}
	else if (pname == RRG_DIFFUSE) memcpy(rrg_light_dif, p, sizeof rrg_light_dif);
	else if (pname == RRG_AMBIENT) memcpy(rrg_light_amb, p, sizeof rrg_light_amb);
	rrg_ff_ser++;
}
static void APIENTRY rrg_Materialfv(GLint, GLenum pname, GLfloat *p)
{
	if (pname == RRG_AMBIENT || pname == RRG_AMBIENT_AND_DIFFUSE) memcpy(rrg_mat_amb, p, sizeof rrg_mat_amb);
	if (pname == RRG_DIFFUSE || pname == RRG_AMBIENT_AND_DIFFUSE) memcpy(rrg_mat_dif, p, sizeof rrg_mat_dif);
	rrg_ff_ser++;
}

// ---- vertex arrays ----
static void APIENTRY rrg_Color4ubv(const GLubyte *v)
{
	for (int i = 0; i < 4; i++)
		rrg_const[RRA_COLOR][i] = v[i] / 255.0f;
}
static void APIENTRY rrg_MultiTexCoord2f(GLenum target, GLfloat s, GLfloat t)
{
	const int a = (target == GL_TEXTURE1) ? RRA_TEX1 : RRA_TEX0;
	rrg_const[a][0] = s; rrg_const[a][1] = t; rrg_const[a][2] = 0; rrg_const[a][3] = 1;
}
static void APIENTRY rrg_MultiTexCoord2fv(GLenum target, const GLfloat *v) { rrg_MultiTexCoord2f(target, v[0], v[1]); }
static void APIENTRY rrg_ClientActiveTexture(GLenum unit) { rrg_client_unit = (unit == GL_TEXTURE1) ? 1 : 0; }
static int rrg_client_attr(GLenum cap)
{
	switch (cap)
	{
		case RRG_VERTEX_ARRAY: return RRA_VERTEX;
		case RRG_NORMAL_ARRAY: return RRA_NORMAL;
		case RRG_COLOR_ARRAY: return RRA_COLOR;
		case RRG_TEXTURE_COORD_ARRAY: return rrg_client_unit ? RRA_TEX1 : RRA_TEX0;
	}
	return -1;
}
static void APIENTRY rrg_EnableClientState(GLenum cap) { const int a = rrg_client_attr(cap); if (a >= 0) rrg_arr[a] = true; }
static void APIENTRY rrg_DisableClientState(GLenum cap) { const int a = rrg_client_attr(cap); if (a >= 0) rrg_arr[a] = false; }
// The pointer is latched NOW (like glVertexPointer: it uses the GL_ARRAY_BUFFER bound at this moment)
static void APIENTRY rrg_VertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *p)
{
	rg.VertexAttribPointer(RRA_VERTEX, size, type, GL_FALSE, stride, p);
}
static void APIENTRY rrg_TexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *p)
{
	rg.VertexAttribPointer(rrg_client_unit ? RRA_TEX1 : RRA_TEX0, size, type, GL_FALSE, stride, p);
}
static void APIENTRY rrg_ColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *p)
{
	rg.VertexAttribPointer(RRA_COLOR, size, type, type != GL_FLOAT, stride, p);
}
static void APIENTRY rrg_NormalPointer(GLenum type, GLsizei stride, const GLvoid *p)
{
	rg.VertexAttribPointer(RRA_NORMAL, 3, type, type != GL_FLOAT, stride, p);
}

// ---- shaders: desktop GLSL 1.20 -> GLSL ES 1.00 ----
static void rrg_replace_token(std::string &s, const char *from, const char *to)
{
	const size_t n = strlen(from);
	size_t pos = 0;
	while ((pos = s.find(from, pos)) != std::string::npos)
	{
		const bool startok = pos == 0 || !(isalnum((unsigned char)s[pos-1]) || s[pos-1] == '_');
		const size_t e = pos + n;
		const bool endok = e >= s.size() || !(isalnum((unsigned char)s[e]) || s[e] == '_');
		if (startok && endok)
		{
			s.replace(pos, n, to);
			pos += strlen(to);
		}
		else
			pos = e;
	}
}

static std::string rrg_translate(const std::string &src, bool vertex)
{
	std::string s = src;
	// drop the shader's own #version (ours is used)
	for (size_t p = s.find("#version"); p != std::string::npos; p = s.find("#version", p))
		s.replace(p, 8, "//ersion");
	rrg_replace_token(s, "gl_ModelViewProjectionMatrix", "(rr_ProjectionMatrix * rr_ModelViewMatrix)");
	rrg_replace_token(s, "gl_ModelViewMatrix", "rr_ModelViewMatrix");
	rrg_replace_token(s, "gl_ProjectionMatrix", "rr_ProjectionMatrix");
	rrg_replace_token(s, "gl_NormalMatrix", "rr_NormalMatrix");
	rrg_replace_token(s, "gl_Vertex", "rr_Vertex");
	rrg_replace_token(s, "gl_Normal", "rr_Normal");
	rrg_replace_token(s, "gl_MultiTexCoord0", "rr_MultiTexCoord0");
	rrg_replace_token(s, "gl_MultiTexCoord1", "rr_MultiTexCoord1");
	rrg_replace_token(s, "gl_Color", vertex ? "rr_Color" : "rr_FrontColor");
	rrg_replace_token(s, "gl_FrontColor", "rr_FrontColor");
	rrg_replace_token(s, "gl_TexCoord", "rr_TexCoord");
	rrg_replace_token(s, "gl_ClipVertex", "rr_ClipVertex");
	rrg_replace_token(s, "main", "rr_main");
	std::string out = "#version 100\nprecision highp float;\nprecision highp int;\n"
		"varying vec4 rr_FrontColor;\nvarying vec4 rr_TexCoord[2];\n";
	if (vertex)
	{
		out += "attribute vec4 rr_Vertex;\nattribute vec4 rr_MultiTexCoord0;\nattribute vec4 rr_Color;\n"
			"attribute vec3 rr_Normal;\nattribute vec4 rr_MultiTexCoord1;\n"
			"uniform mat4 rr_ModelViewMatrix;\nuniform mat4 rr_ProjectionMatrix;\nuniform mat3 rr_NormalMatrix;\n"
			"vec4 rr_ClipVertex;\n";
		out += s;
		out += "\nvoid main() { rr_main(); }\n";
	}
	else
	{
		out += "uniform int rr_AlphaFunc;\nuniform float rr_AlphaRef;\n";
		out += s;
		// old OpenGL alpha test (ES doesn't have it): after the game's shader
		out += "\nvoid main() {\n rr_main();\n float rr_a = gl_FragColor.a;\n"
			" if (rr_AlphaFunc == 1) { if (rr_a == rr_AlphaRef) discard; }\n"
			" else if (rr_AlphaFunc == 2) { if (!(rr_a > rr_AlphaRef)) discard; }\n"
			" else if (rr_AlphaFunc == 3) { if (!(rr_a >= rr_AlphaRef)) discard; }\n"
			" else if (rr_AlphaFunc == 4) { if (!(rr_a < rr_AlphaRef)) discard; }\n"
			" else if (rr_AlphaFunc == 5) { if (!(rr_a <= rr_AlphaRef)) discard; }\n"
			" else if (rr_AlphaFunc == 6) { if (rr_a != rr_AlphaRef) discard; }\n"
			" else if (rr_AlphaFunc == 7) { discard; }\n}\n";
	}
	return out;
}

static GLuint APIENTRY rrg_CreateShader(GLenum type)
{
	const GLuint s = rg.CreateShader(type);
	if (s)
		rrg_shadertype[s] = type;
	return s;
}
static void APIENTRY rrg_ShaderSource(GLuint sh, GLsizei count, const GLchar **str, GLint *len)
{
	std::string src;
	for (GLsizei i = 0; i < count; i++)
		src += (len && len[i] >= 0) ? std::string(str[i], len[i]) : std::string(str[i]);
	auto it = rrg_shadertype.find(sh);
	const std::string out = rrg_translate(src, it == rrg_shadertype.end() || it->second == GL_VERTEX_SHADER);
	const GLchar *p = out.c_str();
	rg.ShaderSource(sh, 1, &p, NULL);
}
static void rrg_proginfo(GLuint p)
{
	RRProg i;
	memset(&i, 0, sizeof i);
#define U(n) rg.GetUniformLocation(p, n)
	i.mv = U("rr_ModelViewMatrix"); i.proj = U("rr_ProjectionMatrix"); i.nm = U("rr_NormalMatrix");
	i.afunc = U("rr_AlphaFunc"); i.aref = U("rr_AlphaRef");
	i.ff_light = U("ff_lighting"); i.ff_lpos = U("ff_light_pos"); i.ff_ldif = U("ff_light_diffuse");
	i.ff_lamb = U("ff_light_ambient"); i.ff_mamb = U("ff_model_ambient"); i.ff_matamb = U("ff_mat_ambient");
	i.ff_matdif = U("ff_mat_diffuse"); i.ff_env0 = U("ff_env0"); i.ff_env1 = U("ff_env1");
#undef U
	rrg_progs[p] = i;
}
static void APIENTRY rrg_LinkProgram(GLuint p)
{
	rg.BindAttribLocation(p, RRA_VERTEX, "rr_Vertex");
	rg.BindAttribLocation(p, RRA_TEX0, "rr_MultiTexCoord0");
	rg.BindAttribLocation(p, RRA_COLOR, "rr_Color");
	rg.BindAttribLocation(p, RRA_NORMAL, "rr_Normal");
	rg.BindAttribLocation(p, RRA_TEX1, "rr_MultiTexCoord1");
	rg.LinkProgram(p);
	rrg_proginfo(p);
}
static void APIENTRY rrg_UseProgram(GLuint p)
{
	rrg_userprog = p;
	if (p && p != rrg_boundprog)
	{
		rg.UseProgram(p);
		rrg_boundprog = p;
	}
}
static void APIENTRY rrg_DeleteProgram(GLuint p)
{
	rrg_progs.erase(p);
	if (rrg_boundprog == p) rrg_boundprog = 0;
	if (rrg_userprog == p) rrg_userprog = 0;
	rg.DeleteProgram(p);
}

// Program emulating the fixed pipeline (color, 2 textures with MODULATE/REPLACE, light 0 for models without shaders)
static const char rrg_ff_vs[] =
	"uniform int ff_lighting;\nuniform vec4 ff_light_pos;\nuniform vec4 ff_light_diffuse;\nuniform vec4 ff_light_ambient;\n"
	"uniform vec4 ff_model_ambient;\nuniform vec4 ff_mat_ambient;\nuniform vec4 ff_mat_diffuse;\n"
	"void main() {\n"
	" vec4 eye = gl_ModelViewMatrix * gl_Vertex;\n"
	" gl_Position = gl_ProjectionMatrix * eye;\n"
	" vec4 c = gl_Color;\n"
	" if (ff_lighting != 0) {\n"
	"  vec3 n = normalize(gl_NormalMatrix * gl_Normal);\n"
	"  vec3 l = ff_light_pos.w == 0.0 ? normalize(ff_light_pos.xyz) : normalize(ff_light_pos.xyz - eye.xyz);\n"
	"  c = ff_model_ambient * ff_mat_ambient + ff_light_ambient * ff_mat_ambient + max(dot(n, l), 0.0) * ff_light_diffuse * ff_mat_diffuse;\n"
	"  c = clamp(c, 0.0, 1.0);\n  c.a = ff_mat_diffuse.a;\n"
	" }\n"
	" gl_FrontColor = c;\n gl_TexCoord[0] = gl_MultiTexCoord0;\n gl_TexCoord[1] = gl_MultiTexCoord1;\n"
	"}\n";
static const char rrg_ff_fs[] =
	"uniform sampler2D ff_tex0;\nuniform sampler2D ff_tex1;\nuniform int ff_env0;\nuniform int ff_env1;\n"
	"void main() {\n"
	" vec4 c = gl_Color;\n"
	" if (ff_env0 == 1) c *= texture2D(ff_tex0, gl_TexCoord[0].st); else if (ff_env0 == 2) c = texture2D(ff_tex0, gl_TexCoord[0].st);\n"
	" if (ff_env1 == 1) c *= texture2D(ff_tex1, gl_TexCoord[1].st); else if (ff_env1 == 2) c = texture2D(ff_tex1, gl_TexCoord[1].st);\n"
	" gl_FragColor = c;\n"
	"}\n";

static GLuint rrg_build(const char *vs, const char *fs)
{
	GLuint v = rrg_CreateShader(GL_VERTEX_SHADER), f = rrg_CreateShader(GL_FRAGMENT_SHADER);
	GLint ok = 0;
	char log[1024];
	rrg_ShaderSource(v, 1, &vs, NULL); rg.CompileShader(v);
	rg.GetShaderiv(v, GL_COMPILE_STATUS, &ok);
	if (!ok) { rg.GetShaderInfoLog(v, sizeof log, NULL, log); GL_MSG_Error("[GLES] fixed-function vertex shader: %s\n", log); }
	rrg_ShaderSource(f, 1, &fs, NULL); rg.CompileShader(f);
	rg.GetShaderiv(f, GL_COMPILE_STATUS, &ok);
	if (!ok) { rg.GetShaderInfoLog(f, sizeof log, NULL, log); GL_MSG_Error("[GLES] fixed-function pixel shader: %s\n", log); }
	const GLuint p = rg.CreateProgram();
	rg.AttachShader(p, v); rg.AttachShader(p, f);
	rrg_LinkProgram(p);
	rg.GetProgramiv(p, GL_LINK_STATUS, &ok);
	if (!ok) GL_MSG_Error("[GLES] fixed-function program failed to link\n");
	rg.DeleteShader(v); rg.DeleteShader(f);
	rg.UseProgram(p);
	rg.Uniform1i(rg.GetUniformLocation(p, "ff_tex0"), 0);
	rg.Uniform1i(rg.GetUniformLocation(p, "ff_tex1"), 1);
	rrg_boundprog = p;
	return p;
}

static void rrg_normalmatrix(GLfloat *out) // inverse transpose of the modelview's 3x3
{
	const GLfloat *m = rrg_stack[0][rrg_sp[0]].m;
	const GLfloat a = m[0], b = m[4], c = m[8], d = m[1], e = m[5], f = m[9], g = m[2], h = m[6], k = m[10];
	const GLfloat A = e*k - f*h, B = -(d*k - f*g), C = d*h - e*g;
	GLfloat det = a*A + b*B + c*C;
	if (fabsf(det) < 1e-20f) det = 1e-20f;
	const GLfloat id = 1.0f / det;
	// cofactors / det = (inverse)^T ; column-major
	out[0] = A*id;                 out[1] = B*id;                 out[2] = C*id;
	out[3] = -(b*k - c*h)*id;      out[4] = (a*k - c*g)*id;       out[5] = -(a*h - b*g)*id;
	out[6] = (b*f - c*e)*id;       out[7] = -(a*f - c*d)*id;      out[8] = (a*e - b*d)*id;
}

static int rrg_alpha_code(void)
{
	if (!rrg_alpha_on) return 0;
	switch (rrg_alpha_func)
	{
		case GL_NOTEQUAL: return 1; case GL_GREATER: return 2; case GL_GEQUAL: return 3;
		case GL_LESS: return 4; case GL_LEQUAL: return 5; case GL_EQUAL: return 6; case GL_NEVER: return 7;
	}
	return 0;
}
static int rrg_env_code(int u)
{
	if (!rrg_tex_on[u]) return 0;
	return rrg_env[u] == RRG_REPLACE ? 2 : 1;
}

// What ES was already told about the attributes (-1 = unknown: the RHI may have changed it)
static int rrg_applied_on[RRA_COUNT] = {-1, -1, -1, -1, -1};
static GLfloat rrg_applied_const[RRA_COUNT][4];

// Before each draw: program, matrices, alpha test, fixed state and arrays
static void rrg_flush(void)
{
	if (!rrg_ffprog)
		rrg_ffprog = rrg_build(rrg_ff_vs, rrg_ff_fs);
	const GLuint p = rrg_userprog ? rrg_userprog : rrg_ffprog;
	// The RHI (Software mode / 2D) always leaves ITS program bound: if ours is still current,
	// nobody touched the state since the last draw and the redundant work can be skipped.
	GLint cur = 0;
	rg.GetIntegerv(GL_CURRENT_PROGRAM, &cur);
	if ((GLuint)cur != rrg_boundprog)
		for (int a = 0; a < RRA_COUNT; a++)
			rrg_applied_on[a] = -1;
	if ((GLuint)cur != p)
		rg.UseProgram(p);
	rrg_boundprog = p;
	auto it = rrg_progs.find(p);
	if (it != rrg_progs.end())
	{
		RRProg &i = it->second;
		if (i.ser_mv != rrg_ser[0])
		{
			i.ser_mv = rrg_ser[0];
			if (i.mv != -1) rg.UniformMatrix4fv(i.mv, 1, GL_FALSE, rrg_stack[0][rrg_sp[0]].m);
			if (i.nm != -1) { GLfloat n[9]; rrg_normalmatrix(n); rg.UniformMatrix3fv(i.nm, 1, GL_FALSE, n); }
		}
		if (i.ser_proj != rrg_ser[1])
		{
			i.ser_proj = rrg_ser[1];
			if (i.proj != -1) rg.UniformMatrix4fv(i.proj, 1, GL_FALSE, rrg_stack[1][rrg_sp[1]].m);
		}
		if (i.ser_alpha != rrg_alpha_ser)
		{
			i.ser_alpha = rrg_alpha_ser;
			if (i.afunc != -1) rg.Uniform1i(i.afunc, rrg_alpha_code());
			if (i.aref != -1) rg.Uniform1f(i.aref, rrg_alpha_ref);
		}
		if (p == rrg_ffprog && i.ser_ff != rrg_ff_ser)
		{
			i.ser_ff = rrg_ff_ser;
			rg.Uniform1i(i.ff_light, rrg_lighting ? 1 : 0);
			rg.Uniform4fv(i.ff_lpos, 1, rrg_light_pos); rg.Uniform4fv(i.ff_ldif, 1, rrg_light_dif);
			rg.Uniform4fv(i.ff_lamb, 1, rrg_light_amb); rg.Uniform4fv(i.ff_mamb, 1, rrg_model_amb);
			rg.Uniform4fv(i.ff_matamb, 1, rrg_mat_amb); rg.Uniform4fv(i.ff_matdif, 1, rrg_mat_dif);
			rg.Uniform1i(i.ff_env0, rrg_env_code(0)); rg.Uniform1i(i.ff_env1, rrg_env_code(1));
		}
	}
	for (int a = 0; a < RRA_COUNT; a++)
	{
		const int on = rrg_arr[a] ? 1 : 0;
		if (rrg_applied_on[a] != on)
		{
			if (on) rg.EnableVertexAttribArray(a);
			else rg.DisableVertexAttribArray(a);
		}
		if (!on && (rrg_applied_on[a] != 0 || memcmp(rrg_applied_const[a], rrg_const[a], sizeof rrg_const[a])))
		{
			rg.VertexAttrib4fv(a, rrg_const[a]);
			memcpy(rrg_applied_const[a], rrg_const[a], sizeof rrg_const[a]);
		}
		rrg_applied_on[a] = on;
	}
}
// ANGLE Vulkan (AMD driver) leaks ~1.5 MB on EVERY glCopyTexSubImage2D into a GL_RGB texture
// (the final screen is copied every frame: ~90 MB/s, crashing after ~70 s). GL_RGBA doesn't leak.
// Ring Racers E: on ES (ANGLE) glCopyTexImage2D/glCopyTexSubImage2D from the game framebuffer fail with
// "Invalid copy texture format combination" (then "Offset overflows texture dimensions"), so Legacy GL screen
// copies stayed empty. The RGBA texture storage is allocated and filled with glBlitFramebuffer, which converts
// between formats (temporary FBO on the bound texture; the framebuffer and scissor state are restored).
static GLuint rrg_copyfb = 0;
static bool rrg_blitcopy(GLint xo, GLint yo, GLint x, GLint y, GLsizei w, GLsizei h)
{
	const GLuint tex = rrg_bound[rrg_unit & 1];
	if (!tex || !rg.BlitFramebuffer || !rg.GenFramebuffers || !rg.BindFramebuffer || !rg.FramebufferTexture2D)
		return false;
	// Only the part that has data. Legacy GL asks for a power-of-two square (2048x2048 at 1366x768): copying
	// outside the screen cost ANGLE ~14 ms per frame (MakeScreenTexture runs EVERY frame).
	// The temporary FBO is reused (forgotten with the context in RRGLES_ContextLost).
	if (screen_width > 0 && x + w > screen_width)
		w = screen_width - x;
	if (screen_height > 0 && y + h > screen_height)
		h = screen_height - y;
	if (w <= 0 || h <= 0)
		return true;
	GLint prev = 0;
	rg.GetIntegerv(0x8CA6, &prev); // GL_FRAMEBUFFER_BINDING
	const GLboolean scissor = rg.IsEnabled ? rg.IsEnabled(GL_SCISSOR_TEST) : GL_FALSE;
	if (!rrg_copyfb)
		rg.GenFramebuffers(1, &rrg_copyfb);
	const GLuint fb = rrg_copyfb;
	rg.BindFramebuffer(0x8CA8, (GLuint)prev); // GL_READ_FRAMEBUFFER
	rg.BindFramebuffer(0x8CA9, fb); // GL_DRAW_FRAMEBUFFER
	rg.FramebufferTexture2D(0x8CA9, 0x8CE0, GL_TEXTURE_2D, tex, 0); // GL_COLOR_ATTACHMENT0
	if (scissor)
		rg.Disable(GL_SCISSOR_TEST);
	rg.BlitFramebuffer(x, y, x + w, y + h, xo, yo, xo + w, yo + h, 0x4000, GL_NEAREST); // GL_COLOR_BUFFER_BIT
	if (scissor)
		rg.Enable(GL_SCISSOR_TEST);
	rg.BindFramebuffer(0x8D40, (GLuint)prev); // GL_FRAMEBUFFER
	return true;
}
static void APIENTRY rrg_CopyTexImage2D(GLenum t, GLint l, GLenum f, GLint x, GLint y, GLsizei w, GLsizei h, GLint b)
{
	if (f == GL_RGB)
		f = GL_RGBA;
	if (t == GL_TEXTURE_2D && l == 0 && b == 0 && f == GL_RGBA)
	{
		rg.TexImage2D(t, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		if (rrg_blitcopy(0, 0, x, y, w, h))
			return;
	}
	rg.CopyTexImage2D(t, l, f, x, y, w, h, b);
}
static void APIENTRY rrg_CopyTexSubImage2D(GLenum t, GLint l, GLint xo, GLint yo, GLint x, GLint y, GLsizei w, GLsizei h)
{
	if (t == GL_TEXTURE_2D && l == 0 && rrg_blitcopy(xo, yo, x, y, w, h))
		return;
	if (rg.CopyTexSubImage2D)
		rg.CopyTexSubImage2D(t, l, xo, yo, x, y, w, h);
}
static void APIENTRY rrg_DrawArrays(GLenum mode, GLint first, GLsizei count)
{
	rrg_flush();
	rg.DrawArrays(mode, first, count);
}
static void APIENTRY rrg_DrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *idx)
{
	rrg_flush();
	rg.DrawElements(mode, count, type, idx);
}

// ---- textures ----
// ES: internal format = data format. The game sends RGBA and asks for LUMINANCE_ALPHA/ALPHA/RGB:
// converted here to RGBA with the same sampling result.
enum { RRK_RGBA, RRK_LA, RRK_A, RRK_L, RRK_RGB };
static unsigned char rrg_kind(GLint ifmt)
{
	switch (ifmt)
	{
		case RRG_LUMINANCE_ALPHA: case RRG_LUMINANCE8_ALPHA8: case 2: return RRK_LA;
		case RRG_ALPHA: case RRG_ALPHA8: return RRK_A;
		case RRG_LUMINANCE: case RRG_LUMINANCE8: case 1: return RRK_L;
		case GL_RGB: case RRG_RGB8: case 3: return RRK_RGB;
	}
	return RRK_RGBA;
}
static const void *rrg_convert(unsigned char kind, GLsizei w, GLsizei h, const void *data)
{
	if (!data || kind == RRK_RGBA)
		return data;
	const size_t n = (size_t)w * h;
	rrg_tmp.resize(n * 4);
	const unsigned char *s = (const unsigned char *)data;
	unsigned char *d = rrg_tmp.data();
	for (size_t i = 0; i < n; i++, s += 4, d += 4)
	{
		switch (kind)
		{
			case RRK_LA: d[0] = d[1] = d[2] = s[0]; d[3] = s[3]; break;
			case RRK_A: d[0] = d[1] = d[2] = 255; d[3] = s[3]; break; // texenv MODULATE leaves the color unchanged
			case RRK_L: d[0] = d[1] = d[2] = s[0]; d[3] = 255; break;
			default: d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = 255; break;
		}
	}
	return rrg_tmp.data();
}
static void APIENTRY rrg_TexImage2D(GLenum target, GLint level, GLint ifmt, GLsizei w, GLsizei h, GLint border, GLenum format, GLenum type, const GLvoid *data)
{
	(void)border;
	if (format != GL_RGBA || type != GL_UNSIGNED_BYTE)
	{
		rg.TexImage2D(target, level, (GLint)format, w, h, 0, format, type, data); // already in an ES format
		return;
	}
	const unsigned char kind = rrg_kind(ifmt);
	rrg_texkind[rrg_bound[rrg_unit & 1]] = kind;
	rg.TexImage2D(target, level, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rrg_convert(kind, w, h, data));
}
static void APIENTRY rrg_TexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, const GLvoid *data)
{
	unsigned char kind = RRK_RGBA;
	auto it = rrg_texkind.find(rrg_bound[rrg_unit & 1]);
	if (it != rrg_texkind.end() && format == GL_RGBA && type == GL_UNSIGNED_BYTE)
		kind = it->second;
	rg.TexSubImage2D(target, level, x, y, w, h, format, type, rrg_convert(kind, w, h, data));
}
static GLint APIENTRY rrg_Build2DMipmaps(GLenum target, GLint ifmt, GLsizei w, GLsizei h, GLenum format, GLenum type, const void *data)
{
	rrg_TexImage2D(target, 0, ifmt, w, h, 0, format, type, data);
	rg.GenerateMipmap(target);
	return 0;
}
static void APIENTRY rrg_TexParameteri(GLenum target, GLenum pname, GLint param)
{
	if (param == GL_CLAMP)
		return; // doesn't exist in ES; the game sets GL_CLAMP_TO_EDGE right after
	rg.TexParameteri(target, pname, param);
}
static void APIENTRY rrg_BindTexture(GLenum target, GLuint tex)
{
	rrg_bound[rrg_unit & 1] = tex;
	rg.BindTexture(target, tex);
}
static void APIENTRY rrg_DeleteTextures(GLsizei n, const GLuint *t)
{
	for (GLsizei i = 0; i < n; i++)
		rrg_texkind.erase(t[i]);
	rg.DeleteTextures(n, t);
}
static void APIENTRY rrg_ActiveTexture(GLenum unit)
{
	rrg_unit = (unit == GL_TEXTURE1) ? 1 : 0;
	rg.ActiveTexture(unit);
}
static void APIENTRY rrg_PixelStorei(GLenum pname, GLint param)
{
	if (pname == RRG_PACK_ALIGNMENT)
		rrg_pack_align = param;
	rg.PixelStorei(pname, param);
}
static void APIENTRY rrg_ReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, GLvoid *out)
{
	if (format == RRG_DEPTH_COMPONENT) // ES can't read depth: "nothing in front" (coronas stay visible)
	{
		if (type == GL_FLOAT)
			for (GLsizei i = 0; i < w * h; i++)
				((GLfloat *)out)[i] = 1.0f;
		return;
	}
	if (format == GL_RGB && type == GL_UNSIGNED_BYTE) // ES only guarantees RGBA
	{
		std::vector<unsigned char> rgba((size_t)w * h * 4);
		rg.PixelStorei(RRG_PACK_ALIGNMENT, 4);
		rg.ReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
		rg.PixelStorei(RRG_PACK_ALIGNMENT, rrg_pack_align);
		const size_t al = rrg_pack_align > 0 ? rrg_pack_align : 1;
		const size_t stride = ((size_t)w * 3 + al - 1) / al * al;
		for (GLsizei yy = 0; yy < h; yy++)
			for (GLsizei xx = 0; xx < w; xx++)
				memcpy((unsigned char *)out + yy * stride + xx * 3, &rgba[((size_t)yy * w + xx) * 4], 3);
		return;
	}
	rg.ReadPixels(x, y, w, h, format, type, out);
}

// Swaps the pgl* pointers for the emulation (ES context only). Called at the end of SetupGLfunc and SetupGLFunc4.
static bool rrg_loaded = false;

// The ES context is about to be destroyed (live API switch): forget programs, applied state and
// the real functions (the DLL may change).
static void RRGLES_ContextLost(void)
{
	rrg_ffprog = rrg_userprog = rrg_boundprog = 0;
	rrg_progs.clear();
	rrg_shadertype.clear();
	rrg_texkind.clear();
	rrg_copyfb = 0; // FBO of the destroyed context
	for (int a = 0; a < RRA_COUNT; a++)
		rrg_applied_on[a] = -1;
	rrg_bound[0] = rrg_bound[1] = 0;
	rrg_unit = rrg_client_unit = 0;
	rrg_loaded = false;
}

static void RRGLES_Install(void)
{
	if (!rr_gl_es)
		return;
	bool &loaded = rrg_loaded;
	static bool logged = false;
	if (!loaded)
	{
		loaded = true;
#define R(f) *(void**)&rg.f = GetGLFunc("gl" #f)
		R(TexImage2D); R(TexSubImage2D); R(ReadPixels); R(PixelStorei); R(TexParameteri); R(Enable); R(Disable);
		R(BindTexture); R(DeleteTextures); R(ActiveTexture); R(GetFloatv); R(ClearDepthf); R(DepthRangef);
		R(VertexAttribPointer); R(EnableVertexAttribArray); R(DisableVertexAttribArray); R(VertexAttrib4fv);
		R(DrawArrays); R(DrawElements); R(UseProgram); R(CreateShader); R(ShaderSource); R(CompileShader);
		R(GetShaderiv); R(GetShaderInfoLog); R(DeleteShader); R(CreateProgram); R(AttachShader); R(LinkProgram);
		R(GetProgramiv); R(DeleteProgram); R(BindAttribLocation); R(GetUniformLocation); R(Uniform1i); R(Uniform1f);
		R(Uniform4fv); R(UniformMatrix3fv); R(UniformMatrix4fv); R(GenerateMipmap); R(GetIntegerv); R(CopyTexImage2D);
		R(BlitFramebuffer); R(GenFramebuffers); R(DeleteFramebuffers); R(BindFramebuffer); R(FramebufferTexture2D);
		R(IsEnabled); R(CopyTexSubImage2D);
#undef R
		for (int m = 0; m < 3; m++)
			rrg_identity(rrg_stack[m][0].m);
	}
#define S(p, f) *(void**)&p = (void*)&f
	S(pglClearDepth, rrg_ClearDepth); S(pglDepthRange, rrg_DepthRange); S(pglAlphaFunc, rrg_AlphaFunc);
	S(pglEnable, rrg_Enable); S(pglDisable, rrg_Disable); S(pglGetFloatv, rrg_GetFloatv);
	S(pglMatrixMode, rrg_MatrixMode); S(pglPushMatrix, rrg_PushMatrix); S(pglPopMatrix, rrg_PopMatrix);
	S(pglLoadIdentity, rrg_LoadIdentity); S(pglMultMatrixf, rrg_MultMatrixf); S(pglRotatef, rrg_Rotatef);
	S(pglScalef, rrg_Scalef); S(pglTranslatef, rrg_Translatef); S(pglColor4ubv, rrg_Color4ubv);
	S(pglVertexPointer, rrg_VertexPointer); S(pglNormalPointer, rrg_NormalPointer);
	S(pglTexCoordPointer, rrg_TexCoordPointer); S(pglColorPointer, rrg_ColorPointer);
	S(pglDrawArrays, rrg_DrawArrays); S(pglDrawElements, rrg_DrawElements);
	S(pglEnableClientState, rrg_EnableClientState); S(pglDisableClientState, rrg_DisableClientState);
	S(pglShadeModel, rrg_ShadeModel); S(pglLightfv, rrg_Lightfv); S(pglLightModelfv, rrg_LightModelfv);
	S(pglMaterialfv, rrg_Materialfv); S(pglMateriali, rrg_Materiali); S(pglPixelStorei, rrg_PixelStorei);
	S(pglReadPixels, rrg_ReadPixels); S(pglTexEnvi, rrg_TexEnvi); S(pglTexParameteri, rrg_TexParameteri);
	S(pglTexImage2D, rrg_TexImage2D); S(pglTexSubImage2D, rrg_TexSubImage2D); S(pglBindTexture, rrg_BindTexture);
	S(pglDeleteTextures, rrg_DeleteTextures); S(pglCopyTexImage2D, rrg_CopyTexImage2D); S(pglCopyTexSubImage2D, rrg_CopyTexSubImage2D); S(pglActiveTexture, rrg_ActiveTexture);
	S(pglMultiTexCoord2f, rrg_MultiTexCoord2f); S(pglMultiTexCoord2fv, rrg_MultiTexCoord2fv);
	S(pglClientActiveTexture, rrg_ClientActiveTexture); S(pgluBuild2DMipmaps, rrg_Build2DMipmaps);
#ifdef GL_SHADERS
	S(pglCreateShader, rrg_CreateShader); S(pglShaderSource, rrg_ShaderSource); S(pglLinkProgram, rrg_LinkProgram);
	S(pglUseProgram, rrg_UseProgram); S(pglDeleteProgram, rrg_DeleteProgram);
#endif
#undef S
	if (!logged)
	{
		logged = true;
		CONS_Printf("[GLES] Legacy GL on OpenGL ES (old OpenGL emulation active)\n");
	}
}
