#include "webos/GLES2Compat.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <unistd.h>

#include "webos/WebOSLog.h"

// NOTE: glad.h renames every gl* function to a glad_gl* pointer through macros.
// Inside this file the real ES 2.0 functions are therefore only reached through
// the `es` table below, and the emulated functions are named sh_* (shim).

namespace
{

// ---------------------------------------------------------------------------
// Real OpenGL ES 2.0 entry points
// ---------------------------------------------------------------------------

#define GLES2_FUNCTIONS(X) \
	X(void, AttachShader, (GLuint, GLuint)) \
	X(void, BindAttribLocation, (GLuint, GLuint, const GLchar *)) \
	X(void, BindBuffer, (GLenum, GLuint)) \
	X(void, BindTexture, (GLenum, GLuint)) \
	X(void, BlendFunc, (GLenum, GLenum)) \
	X(void, BlendFuncSeparate, (GLenum, GLenum, GLenum, GLenum)) \
	X(void, BufferData, (GLenum, GLsizeiptr, const void *, GLenum)) \
	X(void, BufferSubData, (GLenum, GLintptr, GLsizeiptr, const void *)) \
	X(void, Clear, (GLbitfield)) \
	X(void, ClearColor, (GLfloat, GLfloat, GLfloat, GLfloat)) \
	X(void, ClearDepthf, (GLfloat)) \
	X(void, ColorMask, (GLboolean, GLboolean, GLboolean, GLboolean)) \
	X(void, CompileShader, (GLuint)) \
	X(GLuint, CreateProgram, (void)) \
	X(GLuint, CreateShader, (GLenum)) \
	X(void, CullFace, (GLenum)) \
	X(void, DeleteBuffers, (GLsizei, const GLuint *)) \
	X(void, DeleteProgram, (GLuint)) \
	X(void, DeleteShader, (GLuint)) \
	X(void, DeleteTextures, (GLsizei, const GLuint *)) \
	X(void, DepthFunc, (GLenum)) \
	X(void, DepthMask, (GLboolean)) \
	X(void, Disable, (GLenum)) \
	X(void, DisableVertexAttribArray, (GLuint)) \
	X(void, DrawArrays, (GLenum, GLint, GLsizei)) \
	X(void, DrawElements, (GLenum, GLsizei, GLenum, const void *)) \
	X(void, Enable, (GLenum)) \
	X(void, EnableVertexAttribArray, (GLuint)) \
	X(void, Finish, (void)) \
	X(void, FrontFace, (GLenum)) \
	X(void, GenBuffers, (GLsizei, GLuint *)) \
	X(void, GenTextures, (GLsizei, GLuint *)) \
	X(GLenum, GetError, (void)) \
	X(void, GetBooleanv, (GLenum, GLboolean *)) \
	X(void, GetIntegerv, (GLenum, GLint *)) \
	X(void, GetProgramInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *)) \
	X(void, GetProgramiv, (GLuint, GLenum, GLint *)) \
	X(void, GetShaderInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *)) \
	X(void, GetShaderiv, (GLuint, GLenum, GLint *)) \
	X(const GLubyte *, GetString, (GLenum)) \
	X(GLint, GetUniformLocation, (GLuint, const GLchar *)) \
	X(GLboolean, IsEnabled, (GLenum)) \
	X(void, LineWidth, (GLfloat)) \
	X(void, LinkProgram, (GLuint)) \
	X(void, PixelStorei, (GLenum, GLint)) \
	X(void, PolygonOffset, (GLfloat, GLfloat)) \
	X(void, ReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *)) \
	X(void, ShaderSource, (GLuint, GLsizei, const GLchar *const *, const GLint *)) \
	X(void, TexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *)) \
	X(void, TexParameteri, (GLenum, GLenum, GLint)) \
	X(void, TexSubImage2D, (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *)) \
	X(void, Uniform1f, (GLint, GLfloat)) \
	X(void, Uniform1i, (GLint, GLint)) \
	X(void, Uniform2f, (GLint, GLfloat, GLfloat)) \
	X(void, Uniform3fv, (GLint, GLsizei, const GLfloat *)) \
	X(void, Uniform4fv, (GLint, GLsizei, const GLfloat *)) \
	X(void, UniformMatrix3fv, (GLint, GLsizei, GLboolean, const GLfloat *)) \
	X(void, UniformMatrix4fv, (GLint, GLsizei, GLboolean, const GLfloat *)) \
	X(void, UseProgram, (GLuint)) \
	X(void, VertexAttrib4f, (GLuint, GLfloat, GLfloat, GLfloat, GLfloat)) \
	X(void, VertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *)) \
	X(void, Viewport, (GLint, GLint, GLsizei, GLsizei))

struct ES
{
#define X(ret, name, args) ret(APIENTRY *name) args = nullptr;
	GLES2_FUNCTIONS(X)
#undef X
};

ES es;

bool loadES(gles2compat::LoaderFn loader)
{
	bool ok = true;
#define X(ret, name, args) \
	es.name = reinterpret_cast<ret(APIENTRY *) args>(loader("gl" #name)); \
	if (es.name == nullptr) \
	{ \
		webos::log("[GLES2] missing entry point gl" #name); \
		ok = false; \
	}
	GLES2_FUNCTIONS(X)
#undef X
	return ok;
}

// ---------------------------------------------------------------------------
// Constants that are not part of core ES 2.0 but are used by the game
// ---------------------------------------------------------------------------

const GLenum CAP_TEXTURE_2D = 0x0DE1;
const GLenum CAP_ALPHA_TEST = 0x0BC0;
const GLenum CAP_FOG = 0x0B60;
const GLenum CAP_LIGHTING = 0x0B50;
const GLenum CAP_LIGHT0 = 0x4000;
const GLenum CAP_LIGHT1 = 0x4001;
const GLenum CAP_COLOR_MATERIAL = 0x0B57;
const GLenum CAP_NORMALIZE = 0x0BA1;
const GLenum CAP_RESCALE_NORMAL = 0x803A;

const GLenum ENUM_MODELVIEW = 0x1700;
const GLenum ENUM_PROJECTION = 0x1701;
const GLenum ENUM_TEXTURE = 0x1702;
const GLenum ENUM_MODELVIEW_MATRIX = 0x0BA6;
const GLenum ENUM_PROJECTION_MATRIX = 0x0BA7;
const GLenum ENUM_TEXTURE_MATRIX = 0x0BA8;

const GLenum ENUM_QUADS = 0x0007;
const GLenum ENUM_CLAMP = 0x2900;
const GLenum ENUM_CLAMP_TO_EDGE = 0x812F;
const GLenum ENUM_BGR_EXT = 0x80E0;
const GLenum ENUM_BGR = 0x80E0;

const GLenum ENUM_LINEAR = 0x2601;
const GLenum ENUM_NEAREST = 0x2600;
const GLenum ENUM_NEAREST_MIPMAP_NEAREST = 0x2700;
const GLenum ENUM_LINEAR_MIPMAP_NEAREST = 0x2701;
const GLenum ENUM_NEAREST_MIPMAP_LINEAR = 0x2702;
const GLenum ENUM_LINEAR_MIPMAP_LINEAR = 0x2703;

const GLenum ENUM_POSITION = 0x1203;
const GLenum ENUM_DIFFUSE = 0x1201;
const GLenum ENUM_AMBIENT = 0x1200;
const GLenum ENUM_LIGHT_MODEL_AMBIENT = 0x0B53;

const GLenum ENUM_FOG_MODE = 0x0B65;
const GLenum ENUM_FOG_DENSITY = 0x0B62;
const GLenum ENUM_FOG_START = 0x0B63;
const GLenum ENUM_FOG_END = 0x0B64;
const GLenum ENUM_FOG_COLOR = 0x0B66;
const GLenum ENUM_EXP = 0x0800;
const GLenum ENUM_EXP2 = 0x0801;
const GLenum ENUM_LINEAR_FOG = 0x2601;

const GLenum ENUM_ALWAYS = 0x0207;

const GLenum ENUM_PACK_ALIGNMENT = 0x0D05;

const GLenum ENUM_TRIANGLES = 0x0004;
const GLenum ENUM_UNSIGNED_SHORT = 0x1403;

const GLenum ENUM_ARRAY_BUFFER = 0x8892;
const GLenum ENUM_ELEMENT_ARRAY_BUFFER = 0x8893;

const GLenum ENUM_VERTEX_ARRAY = 0x8074;
const GLenum ENUM_NORMAL_ARRAY = 0x8075;
const GLenum ENUM_COLOR_ARRAY = 0x8076;
const GLenum ENUM_TEXTURE_COORD_ARRAY = 0x8078;

const GLenum ENUM_FLOAT = 0x1406;
const GLenum ENUM_BYTE = 0x1400;
const GLenum ENUM_UNSIGNED_BYTE = 0x1401;
const GLenum ENUM_SHORT = 0x1402;
const GLenum ENUM_INT = 0x1404;
const GLenum ENUM_UNSIGNED_INT = 0x1405;

const GLenum ENUM_RGB = 0x1907;
const GLenum ENUM_RGBA = 0x1908;
const GLenum ENUM_LUMINANCE = 0x1909;
const GLenum ENUM_LUMINANCE_ALPHA = 0x190A;
const GLenum ENUM_ALPHA = 0x1906;

const GLenum ENUM_TEXTURE_WRAP_S = 0x2802;
const GLenum ENUM_TEXTURE_WRAP_T = 0x2803;
const GLenum ENUM_TEXTURE_MIN_FILTER = 0x2801;

const GLenum ENUM_FRAGMENT_SHADER = 0x8B30;
const GLenum ENUM_VERTEX_SHADER = 0x8B31;
const GLenum ENUM_COMPILE_STATUS = 0x8B81;
const GLenum ENUM_LINK_STATUS = 0x8B82;

const GLenum ENUM_COMPILE = 0x1300;

// Capabilities forwarded unchanged to the ES driver.
bool isPassThroughCap(GLenum cap)
{
	switch (cap)
	{
		case 0x0BE2: // GL_BLEND
		case 0x0B44: // GL_CULL_FACE
		case 0x0B71: // GL_DEPTH_TEST
		case 0x8037: // GL_POLYGON_OFFSET_FILL
		case 0x0C11: // GL_SCISSOR_TEST
		case 0x0B90: // GL_STENCIL_TEST
		case 0x0BD0: // GL_DITHER
			return true;
		default:
			return false;
	}
}

// ---------------------------------------------------------------------------
// Small matrix helpers (column-major, like OpenGL)
// ---------------------------------------------------------------------------

void matIdentity(float *m)
{
	for (int i = 0; i < 16; i++)
		m[i] = 0.0f;
	m[0] = m[5] = m[10] = m[15] = 1.0f;
}

// out = a * b
void matMul(const float *a, const float *b, float *out)
{
	float r[16];
	for (int c = 0; c < 4; c++)
	{
		for (int row = 0; row < 4; row++)
		{
			float sum = 0.0f;
			for (int k = 0; k < 4; k++)
				sum += a[k * 4 + row] * b[c * 4 + k];
			r[c * 4 + row] = sum;
		}
	}
	std::memcpy(out, r, sizeof(r));
}

bool matIsIdentity(const float *m)
{
	for (int c = 0; c < 4; c++)
	{
		for (int r = 0; r < 4; r++)
		{
			float expected = (c == r) ? 1.0f : 0.0f;
			if (std::fabs(m[c * 4 + r] - expected) > 1.0e-6f)
				return false;
		}
	}
	return true;
}

// Inverse transpose of the upper-left 3x3 of a 4x4 matrix (the normal matrix).
void normalMatrix(const float *mv, float *out)
{
	// A(r, c) = mv[c * 4 + r]
	const float a00 = mv[0], a10 = mv[1], a20 = mv[2];
	const float a01 = mv[4], a11 = mv[5], a21 = mv[6];
	const float a02 = mv[8], a12 = mv[9], a22 = mv[10];

	// Cofactors C(r, c)
	const float c00 = a11 * a22 - a12 * a21;
	const float c01 = -(a10 * a22 - a12 * a20);
	const float c02 = a10 * a21 - a11 * a20;
	const float c10 = -(a01 * a22 - a02 * a21);
	const float c11 = a00 * a22 - a02 * a20;
	const float c12 = -(a00 * a21 - a01 * a20);
	const float c20 = a01 * a12 - a02 * a11;
	const float c21 = -(a00 * a12 - a02 * a10);
	const float c22 = a00 * a11 - a01 * a10;

	const float det = a00 * c00 + a01 * c01 + a02 * c02;
	if (std::fabs(det) < 1.0e-12f)
	{
		out[0] = 1; out[1] = 0; out[2] = 0;
		out[3] = 0; out[4] = 1; out[5] = 0;
		out[6] = 0; out[7] = 0; out[8] = 1;
		return;
	}

	const float inv = 1.0f / det;
	// (A^-1)^T == cofactor(A) / det. The uniform is column-major: out[c * 3 + r].
	out[0] = c00 * inv; out[1] = c10 * inv; out[2] = c20 * inv;
	out[3] = c01 * inv; out[4] = c11 * inv; out[5] = c21 * inv;
	out[6] = c02 * inv; out[7] = c12 * inv; out[8] = c22 * inv;
}

// ---------------------------------------------------------------------------
// Emulated state
// ---------------------------------------------------------------------------

enum
{
	ATTR_POS = 0,
	ATTR_COLOR = 1,
	ATTR_NORMAL = 2,
	ATTR_TEX = 3,
	ATTR_COUNT = 4
};

const int MV_DEPTH = 32;
const int PJ_DEPTH = 8;
const int TX_DEPTH = 8;

struct ArrayPtr
{
	bool enabled = false;
	GLint size = 4;
	GLenum type = ENUM_FLOAT;
	GLsizei stride = 0;
	const void *ptr = nullptr;
	GLuint vbo = 0;
};

struct DrawSnapshot
{
	GLenum mode = 0;
	GLsizei count = 0;
	ArrayPtr arrays[ATTR_COUNT];
	std::vector<std::uint8_t> data[ATTR_COUNT];

	// Display-list geometry is static, so after the first replay it is moved
	// into one GL buffer (attribute arrays back to back) instead of being
	// copied from client memory on every draw. See uploadSnapshot().
	GLuint vbo = 0;
	bool uploadTried = false;

	~DrawSnapshot();
};

struct ListCmd
{
	enum Op : std::uint8_t
	{
		Translate,
		Rotate,
		Scale,
		Color,
		Normal,
		Push,
		Pop,
		LoadIdentity,
		MatrixMode,
		Draw,
		Call,
		Enable,
		Disable
	};

	Op op = Translate;
	float f[4] = {0, 0, 0, 0};
	GLuint u = 0;
	std::shared_ptr<DrawSnapshot> draw;
};

struct Light
{
	bool enabled = false;
	float dir[3] = {0, 0, 1}; // eye space, normalized
	float diffuse[3] = {0, 0, 0};
	float ambient[3] = {0, 0, 0};
};

const std::uint32_t K_TEX = 1u << 0;
const std::uint32_t K_FOG = 1u << 1;
const std::uint32_t K_ALPHA = 1u << 2;
const std::uint32_t K_LIGHT = 1u << 3;
const std::uint32_t K_COLMAT = 1u << 4;
const std::uint32_t K_TEXMAT = 1u << 5;

struct Prog
{
	GLuint id = 0;
	GLint uMVP = -1, uMVZ = -1, uNormalMat = -1, uTexMat = -1, uTex = -1;
	GLint uLightDir0 = -1, uLightDir1 = -1, uLightDiff0 = -1, uLightDiff1 = -1;
	GLint uLightAmb0 = -1, uLightAmb1 = -1, uSceneAmb = -1;
	GLint uFogColor = -1, uFogParams = -1, uAlpha = -1;
	std::uint32_t matStamp = 0, lightStamp = 0, fogStamp = 0, alphaStamp = 0;
	bool failed = false;
};

struct State
{
	bool ready = false;

	// Last clear colour / colour mask set by the game (restored after the
	// alpha fix-up in makeOpaque()).
	float clearColor[4] = {0, 0, 0, 0};
	GLboolean colorMask[4] = {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};

	// Matrix stacks
	float mv[MV_DEPTH][16];
	float pj[PJ_DEPTH][16];
	float tx[TX_DEPTH][16];
	int mvTop = 0, pjTop = 0, txTop = 0;
	GLenum matrixMode = ENUM_MODELVIEW;

	// Current vertex attributes
	float color[4] = {1, 1, 1, 1};
	float normal[3] = {0, 0, 1};

	// Emulated capabilities
	bool texture2D = false;
	bool alphaTest = false;
	bool fog = false;
	bool lighting = false;
	bool colorMaterial = false;

	Light lights[2];
	float sceneAmbient[3] = {0.2f, 0.2f, 0.2f};

	GLenum alphaFunc = ENUM_ALWAYS;
	float alphaRef = 0.0f;

	GLenum fogMode = ENUM_EXP;
	float fogStart = 0.0f, fogEnd = 1.0f, fogDensity = 1.0f;
	float fogColor[4] = {0, 0, 0, 0};

	// Client-state arrays
	ArrayPtr arrays[ATTR_COUNT];
	bool attribEnabled[ATTR_COUNT] = {false, false, false, false};

	GLuint arrayBuffer = 0;
	GLuint elementBuffer = 0;
	GLuint quadIndexVbo = 0;
	GLuint boundTexture = 0;
	GLint packAlignment = 4;

	// Shader programs
	std::unordered_map<std::uint32_t, Prog> progs;
	GLuint currentProgram = 0;
	std::uint32_t matStamp = 1, lightStamp = 1, fogStamp = 1, alphaStamp = 1;
	float mvp[16];
	std::uint32_t mvpStamp = 0;

	// Display lists
	std::unordered_map<GLuint, std::vector<ListCmd>> lists;
	GLuint nextList = 1;
	bool compiling = false;
	GLuint compileId = 0;
	std::vector<ListCmd> compileCmds;
	int callDepth = 0;

	// Queries (occlusion queries are not available on ES 2.0)
	GLuint nextQuery = 1;

	// Scratch
	std::vector<std::uint16_t> quadIndices;
	bool warnedGetBufferSubData = false;
	bool warnedUnsignedInt = false;
};

// IMPORTANT: Tesselator::instance is a global whose constructor creates the GL
// context, and therefore calls gles2compat::init(), during static
// initialisation - before this file's own globals are constructed. A plain
// namespace-scope `State S;` was then constructed AFTER init() had set
// S.ready = true, silently resetting it to false (and wiping the shader
// program cache), so every draw call was dropped and the screen stayed black.
// A function-local static is constructed on first use, whatever the order.
State &stateInstance()
{
	static State state;
	return state;
}
#define S (stateInstance())

GLsizei typeSize(GLenum type)
{
	switch (type)
	{
		case ENUM_BYTE:
		case ENUM_UNSIGNED_BYTE:
			return 1;
		case ENUM_SHORT:
		case ENUM_UNSIGNED_SHORT:
			return 2;
		case ENUM_INT:
		case ENUM_UNSIGNED_INT:
		case ENUM_FLOAT:
		default:
			return 4;
	}
}

float *currentMatrix()
{
	switch (S.matrixMode)
	{
		case ENUM_PROJECTION:
			return S.pj[S.pjTop];
		case ENUM_TEXTURE:
			return S.tx[S.txTop];
		case ENUM_MODELVIEW:
		default:
			return S.mv[S.mvTop];
	}
}

void touchMatrices()
{
	S.matStamp++;
}

// ---------------------------------------------------------------------------
// Shader generation
// ---------------------------------------------------------------------------

const char *VERTEX_SHADER_BODY = R"GLSL(
attribute vec4 aPos;
attribute vec4 aColor;
attribute vec3 aNormal;
attribute vec2 aTex;

uniform mat4 uMVP;

varying vec4 vColor;

#ifdef FOG
uniform vec4 uMVZ;
varying float vDist;
#endif

#ifdef TEX
varying vec2 vTex;
#ifdef TEXMAT
uniform mat4 uTexMat;
#endif
#endif

#ifdef LIGHT
uniform mat3 uNormalMat;
uniform vec3 uLightDir0;
uniform vec3 uLightDir1;
uniform vec3 uLightDiff0;
uniform vec3 uLightDiff1;
uniform vec3 uLightAmb0;
uniform vec3 uLightAmb1;
uniform vec3 uSceneAmb;
#endif

void main()
{
	gl_Position = uMVP * aPos;

#ifdef FOG
	vDist = abs(dot(uMVZ, aPos));
#endif

#ifdef TEX
#ifdef TEXMAT
	vTex = (uTexMat * vec4(aTex, 0.0, 1.0)).xy;
#else
	vTex = aTex;
#endif
#endif

#ifdef LIGHT
	vec3 n = normalize(uNormalMat * aNormal);
#ifdef COLMAT
	vec3 mAmb = aColor.rgb;
	vec3 mDif = aColor.rgb;
	float alpha = aColor.a;
#else
	vec3 mAmb = vec3(0.2);
	vec3 mDif = vec3(0.8);
	float alpha = 1.0;
#endif
	vec3 c = uSceneAmb * mAmb;
	c += uLightAmb0 * mAmb + max(dot(n, uLightDir0), 0.0) * uLightDiff0 * mDif;
	c += uLightAmb1 * mAmb + max(dot(n, uLightDir1), 0.0) * uLightDiff1 * mDif;
	vColor = vec4(clamp(c, 0.0, 1.0), alpha);
#else
	vColor = aColor;
#endif
}
)GLSL";

const char *FRAGMENT_SHADER_BODY = R"GLSL(
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif

varying vec4 vColor;

#ifdef TEX
varying vec2 vTex;
uniform sampler2D uTex;
#endif

#ifdef FOG
varying float vDist;
uniform vec4 uFogColor;
uniform vec4 uFogParams; // x = start, y = end, z = density, w = mode (0 linear, 1 exp, 2 exp2)
#endif

#ifdef ALPHA
uniform vec2 uAlpha; // x = comparison function (0..7), y = reference
#endif

void main()
{
	vec4 c = vColor;

#ifdef TEX
	c *= texture2D(uTex, vTex);
#endif

#ifdef ALPHA
	float fn = uAlpha.x;
	float ref = uAlpha.y;
	bool ok = true;
	if (fn < 0.5) ok = false;
	else if (fn < 1.5) ok = c.a < ref;
	else if (fn < 2.5) ok = abs(c.a - ref) < 0.002;
	else if (fn < 3.5) ok = c.a <= ref;
	else if (fn < 4.5) ok = c.a > ref;
	else if (fn < 5.5) ok = abs(c.a - ref) >= 0.002;
	else if (fn < 6.5) ok = c.a >= ref;
	if (!ok) discard;
#endif

#ifdef FOG
	float f;
	if (uFogParams.w < 0.5)
		f = (uFogParams.y - vDist) / max(uFogParams.y - uFogParams.x, 0.00001);
	else if (uFogParams.w < 1.5)
		f = exp(-uFogParams.z * vDist);
	else
	{
		float d = uFogParams.z * vDist;
		f = exp(-d * d);
	}
	f = clamp(f, 0.0, 1.0);
	c.rgb = mix(uFogColor.rgb, c.rgb, f);
#endif

	gl_FragColor = c;
}
)GLSL";

std::string defineBlock(std::uint32_t key)
{
	std::string s;
	if (key & K_TEX) s += "#define TEX 1\n";
	if (key & K_FOG) s += "#define FOG 1\n";
	if (key & K_ALPHA) s += "#define ALPHA 1\n";
	if (key & K_LIGHT) s += "#define LIGHT 1\n";
	if (key & K_COLMAT) s += "#define COLMAT 1\n";
	if (key & K_TEXMAT) s += "#define TEXMAT 1\n";
	return s;
}

GLuint compileShader(GLenum type, const std::string &source)
{
	GLuint shader = es.CreateShader(type);
	const GLchar *text = source.c_str();
	es.ShaderSource(shader, 1, &text, nullptr);
	es.CompileShader(shader);

	GLint ok = 0;
	es.GetShaderiv(shader, ENUM_COMPILE_STATUS, &ok);
	if (!ok)
	{
		char log[1024];
		GLsizei length = 0;
		es.GetShaderInfoLog(shader, sizeof(log) - 1, &length, log);
		log[length > 0 ? length : 0] = '\0';
		webos::log("[GLES2] %s shader failed to compile: %s", type == ENUM_VERTEX_SHADER ? "vertex" : "fragment", log);
		es.DeleteShader(shader);
		return 0;
	}
	return shader;
}

Prog &getProgram(std::uint32_t key)
{
	auto found = S.progs.find(key);
	if (found != S.progs.end())
		return found->second;

	Prog &p = S.progs[key];
	const std::string defines = defineBlock(key);

	GLuint vs = compileShader(ENUM_VERTEX_SHADER, defines + VERTEX_SHADER_BODY);
	GLuint fs = compileShader(ENUM_FRAGMENT_SHADER, defines + FRAGMENT_SHADER_BODY);
	if (vs == 0 || fs == 0)
	{
		if (vs != 0) es.DeleteShader(vs);
		if (fs != 0) es.DeleteShader(fs);
		p.failed = true;
		return p;
	}

	p.id = es.CreateProgram();
	es.AttachShader(p.id, vs);
	es.AttachShader(p.id, fs);
	es.BindAttribLocation(p.id, ATTR_POS, "aPos");
	es.BindAttribLocation(p.id, ATTR_COLOR, "aColor");
	es.BindAttribLocation(p.id, ATTR_NORMAL, "aNormal");
	es.BindAttribLocation(p.id, ATTR_TEX, "aTex");
	es.LinkProgram(p.id);
	es.DeleteShader(vs);
	es.DeleteShader(fs);

	GLint ok = 0;
	es.GetProgramiv(p.id, ENUM_LINK_STATUS, &ok);
	if (!ok)
	{
		char log[1024];
		GLsizei length = 0;
		es.GetProgramInfoLog(p.id, sizeof(log) - 1, &length, log);
		log[length > 0 ? length : 0] = '\0';
		webos::log("[GLES2] program (key 0x%x) failed to link: %s", key, log);
		es.DeleteProgram(p.id);
		p.id = 0;
		p.failed = true;
		return p;
	}

	p.uMVP = es.GetUniformLocation(p.id, "uMVP");
	p.uMVZ = es.GetUniformLocation(p.id, "uMVZ");
	p.uNormalMat = es.GetUniformLocation(p.id, "uNormalMat");
	p.uTexMat = es.GetUniformLocation(p.id, "uTexMat");
	p.uTex = es.GetUniformLocation(p.id, "uTex");
	p.uLightDir0 = es.GetUniformLocation(p.id, "uLightDir0");
	p.uLightDir1 = es.GetUniformLocation(p.id, "uLightDir1");
	p.uLightDiff0 = es.GetUniformLocation(p.id, "uLightDiff0");
	p.uLightDiff1 = es.GetUniformLocation(p.id, "uLightDiff1");
	p.uLightAmb0 = es.GetUniformLocation(p.id, "uLightAmb0");
	p.uLightAmb1 = es.GetUniformLocation(p.id, "uLightAmb1");
	p.uSceneAmb = es.GetUniformLocation(p.id, "uSceneAmb");
	p.uFogColor = es.GetUniformLocation(p.id, "uFogColor");
	p.uFogParams = es.GetUniformLocation(p.id, "uFogParams");
	p.uAlpha = es.GetUniformLocation(p.id, "uAlpha");

	es.UseProgram(p.id);
	S.currentProgram = p.id;
	if (p.uTex >= 0)
		es.Uniform1i(p.uTex, 0);

	webos::log("[GLES2] built shader variant 0x%x", key);
	return p;
}

// Selects the shader variant for the current fixed-function state and uploads
// the uniforms that changed since the program was last used.
bool prepareDraw()
{
	if (!S.ready)
		return false;

	std::uint32_t key = 0;
	const bool textured = S.texture2D && S.boundTexture != 0;
	if (textured) key |= K_TEX;
	if (S.fog) key |= K_FOG;
	if (S.alphaTest && S.alphaFunc != ENUM_ALWAYS) key |= K_ALPHA;
	if (S.lighting)
	{
		key |= K_LIGHT;
		if (S.colorMaterial) key |= K_COLMAT;
	}
	if (textured && !matIsIdentity(S.tx[S.txTop])) key |= K_TEXMAT;

	Prog &p = getProgram(key);
	if (p.failed || p.id == 0)
		return false;

	if (S.currentProgram != p.id)
	{
		es.UseProgram(p.id);
		S.currentProgram = p.id;
	}

	if (S.mvpStamp != S.matStamp)
	{
		matMul(S.pj[S.pjTop], S.mv[S.mvTop], S.mvp);
		S.mvpStamp = S.matStamp;
	}

	if (p.matStamp != S.matStamp)
	{
		p.matStamp = S.matStamp;
		es.UniformMatrix4fv(p.uMVP, 1, GL_FALSE, S.mvp);

		const float *mv = S.mv[S.mvTop];
		if (key & K_FOG)
		{
			const float z[4] = {mv[2], mv[6], mv[10], mv[14]};
			es.Uniform4fv(p.uMVZ, 1, z);
		}
		if (key & K_LIGHT)
		{
			float nm[9];
			normalMatrix(mv, nm);
			es.UniformMatrix3fv(p.uNormalMat, 1, GL_FALSE, nm);
		}
		if (key & K_TEXMAT)
			es.UniformMatrix4fv(p.uTexMat, 1, GL_FALSE, S.tx[S.txTop]);
	}

	if ((key & K_LIGHT) && p.lightStamp != S.lightStamp)
	{
		p.lightStamp = S.lightStamp;
		float zero[3] = {0, 0, 0};
		const Light &l0 = S.lights[0];
		const Light &l1 = S.lights[1];
		es.Uniform3fv(p.uLightDir0, 1, l0.dir);
		es.Uniform3fv(p.uLightDir1, 1, l1.dir);
		es.Uniform3fv(p.uLightDiff0, 1, l0.enabled ? l0.diffuse : zero);
		es.Uniform3fv(p.uLightDiff1, 1, l1.enabled ? l1.diffuse : zero);
		es.Uniform3fv(p.uLightAmb0, 1, l0.enabled ? l0.ambient : zero);
		es.Uniform3fv(p.uLightAmb1, 1, l1.enabled ? l1.ambient : zero);
		es.Uniform3fv(p.uSceneAmb, 1, S.sceneAmbient);
	}

	if ((key & K_FOG) && p.fogStamp != S.fogStamp)
	{
		p.fogStamp = S.fogStamp;
		es.Uniform4fv(p.uFogColor, 1, S.fogColor);
		float mode = 1.0f;
		if (S.fogMode == ENUM_LINEAR_FOG)
			mode = 0.0f;
		else if (S.fogMode == ENUM_EXP2)
			mode = 2.0f;
		const float params[4] = {S.fogStart, S.fogEnd, S.fogDensity, mode};
		es.Uniform4fv(p.uFogParams, 1, params);
	}

	if ((key & K_ALPHA) && p.alphaStamp != S.alphaStamp)
	{
		p.alphaStamp = S.alphaStamp;
		es.Uniform2f(p.uAlpha, static_cast<float>(S.alphaFunc - 0x0200), S.alphaRef);
	}

	return true;
}

// ---------------------------------------------------------------------------
// Vertex array binding and drawing
// ---------------------------------------------------------------------------

void setAttribEnabled(int index, bool enabled)
{
	if (S.attribEnabled[index] == enabled)
		return;
	if (enabled)
		es.EnableVertexAttribArray(static_cast<GLuint>(index));
	else
		es.DisableVertexAttribArray(static_cast<GLuint>(index));
	S.attribEnabled[index] = enabled;
}

// Points the four generic attributes at `arr`, advanced by `first` vertices.
void bindAttribs(const ArrayPtr *arr, GLint first)
{
	for (int i = 0; i < ATTR_COUNT; i++)
	{
		const ArrayPtr &a = arr[i];
		if (a.enabled)
		{
			const GLsizei elemBytes = a.size * typeSize(a.type);
			const GLsizei stride = a.stride != 0 ? a.stride : elemBytes;
			const std::uint8_t *p = static_cast<const std::uint8_t *>(a.ptr) + static_cast<std::ptrdiff_t>(first) * stride;
			const GLboolean normalized = (i == ATTR_COLOR || i == ATTR_NORMAL) && a.type != ENUM_FLOAT ? GL_TRUE : GL_FALSE;

			es.BindBuffer(ENUM_ARRAY_BUFFER, a.vbo);
			es.VertexAttribPointer(static_cast<GLuint>(i), a.size, a.type, normalized, a.stride, p);
			setAttribEnabled(i, true);
		}
		else
		{
			setAttribEnabled(i, false);
			switch (i)
			{
				case ATTR_COLOR:
					es.VertexAttrib4f(ATTR_COLOR, S.color[0], S.color[1], S.color[2], S.color[3]);
					break;
				case ATTR_NORMAL:
					es.VertexAttrib4f(ATTR_NORMAL, S.normal[0], S.normal[1], S.normal[2], 0.0f);
					break;
				case ATTR_TEX:
					es.VertexAttrib4f(ATTR_TEX, 0.0f, 0.0f, 0.0f, 1.0f);
					break;
				default:
					break;
			}
		}
	}
	es.BindBuffer(ENUM_ARRAY_BUFFER, S.arrayBuffer);
}

void buildQuadIndices()
{
	// 16384 quads keeps every index below 65536.
	const int quads = 16384;
	S.quadIndices.resize(static_cast<std::size_t>(quads) * 6);
	std::size_t at = 0;
	for (int q = 0; q < quads; q++)
	{
		const std::uint16_t base = static_cast<std::uint16_t>(q * 4);
		S.quadIndices[at++] = base + 0;
		S.quadIndices[at++] = base + 1;
		S.quadIndices[at++] = base + 2;
		S.quadIndices[at++] = base + 0;
		S.quadIndices[at++] = base + 2;
		S.quadIndices[at++] = base + 3;
	}
}

unsigned long g_statCalls = 0, g_statNoPos = 0, g_statNoProg = 0, g_statDrawn = 0;
unsigned long g_statVerts = 0, g_statClientBytes = 0;
bool g_glAlive = false;

void drawArraysWith(const ArrayPtr *arr, GLenum mode, GLint first, GLsizei count)
{
	g_statCalls++;
	if (count <= 0 || !arr[ATTR_POS].enabled)
	{
		g_statNoPos++;
		return;
	}
	if (!prepareDraw())
	{
		g_statNoProg++;
		return;
	}
	g_statDrawn++;
	g_statVerts += static_cast<unsigned long>(count);
	for (int i = 0; i < ATTR_COUNT; i++)
	{
		const ArrayPtr &a = arr[i];
		if (a.enabled && a.vbo == 0)
		{
			const GLsizei elemBytes = a.size * typeSize(a.type);
			g_statClientBytes += static_cast<unsigned long>(elemBytes) * static_cast<unsigned long>(count);
		}
	}

	if (mode == ENUM_QUADS)
	{
		if (S.quadIndices.empty())
			buildQuadIndices();
		if (S.quadIndexVbo == 0)
		{
			// One static index buffer instead of handing the driver a client
			// array on every draw.
			es.GenBuffers(1, &S.quadIndexVbo);
			if (S.quadIndexVbo != 0)
			{
				es.BindBuffer(ENUM_ELEMENT_ARRAY_BUFFER, S.quadIndexVbo);
				es.BufferData(ENUM_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(S.quadIndices.size() * sizeof(std::uint16_t)),
				              S.quadIndices.data(), 0x88E4 /* GL_STATIC_DRAW */);
				es.BindBuffer(ENUM_ELEMENT_ARRAY_BUFFER, S.elementBuffer);
			}
		}

		const bool useIndexBuffer = S.quadIndexVbo != 0;
		if (useIndexBuffer)
			es.BindBuffer(ENUM_ELEMENT_ARRAY_BUFFER, S.quadIndexVbo);
		else if (S.elementBuffer != 0)
			es.BindBuffer(ENUM_ELEMENT_ARRAY_BUFFER, 0); // client-side indices need it empty

		GLsizei remaining = count - (count % 4);
		GLint offset = first;
		const GLsizei batch = 16384 * 4;
		while (remaining > 0)
		{
			const GLsizei n = std::min(remaining, batch);
			bindAttribs(arr, offset);
			es.DrawElements(ENUM_TRIANGLES, n / 4 * 6, ENUM_UNSIGNED_SHORT,
			                useIndexBuffer ? static_cast<const void *>(nullptr) : static_cast<const void *>(S.quadIndices.data()));
			offset += n;
			remaining -= n;
		}

		if (useIndexBuffer || S.elementBuffer != 0)
			es.BindBuffer(ENUM_ELEMENT_ARRAY_BUFFER, S.elementBuffer);
		return;
	}

	bindAttribs(arr, first);
	es.DrawArrays(mode, 0, count);
}

// ---------------------------------------------------------------------------
// Display lists
// ---------------------------------------------------------------------------

void recordDraw(GLenum mode, GLint first, GLsizei count)
{
	if (!S.arrays[ATTR_POS].enabled)
		return;

	std::shared_ptr<DrawSnapshot> snap = std::make_shared<DrawSnapshot>();
	snap->mode = mode;
	snap->count = count;

	for (int i = 0; i < ATTR_COUNT; i++)
	{
		const ArrayPtr &a = S.arrays[i];
		if (!a.enabled)
			continue;

		if (a.vbo != 0)
		{
			// Geometry that lives in a buffer object cannot be copied out.
			webos::log("[GLES2] display list draw from a buffer object is not supported");
			return;
		}

		const GLsizei elemBytes = a.size * typeSize(a.type);
		const GLsizei stride = a.stride != 0 ? a.stride : elemBytes;
		const std::uint8_t *src = static_cast<const std::uint8_t *>(a.ptr) + static_cast<std::ptrdiff_t>(first) * stride;

		snap->arrays[i] = a;
		snap->arrays[i].stride = 0; // tightly packed copy
		snap->arrays[i].vbo = 0;
		snap->data[i].resize(static_cast<std::size_t>(count) * elemBytes);
		for (GLsizei v = 0; v < count; v++)
			std::memcpy(snap->data[i].data() + static_cast<std::size_t>(v) * elemBytes, src + static_cast<std::ptrdiff_t>(v) * stride, elemBytes);
		snap->arrays[i].ptr = snap->data[i].data();
	}

	ListCmd cmd;
	cmd.op = ListCmd::Draw;
	cmd.draw = snap;
	S.compileCmds.push_back(cmd);
}

// ---------------------------------------------------------------------------
// Executing implementations (used both directly and when replaying lists)
// ---------------------------------------------------------------------------

void doTranslate(float x, float y, float z)
{
	float *m = currentMatrix();
	for (int r = 0; r < 4; r++)
		m[12 + r] += m[r] * x + m[4 + r] * y + m[8 + r] * z;
	touchMatrices();
}

void doScale(float x, float y, float z)
{
	float *m = currentMatrix();
	for (int r = 0; r < 4; r++)
	{
		m[r] *= x;
		m[4 + r] *= y;
		m[8 + r] *= z;
	}
	touchMatrices();
}

void doRotate(float angle, float x, float y, float z)
{
	const float length = std::sqrt(x * x + y * y + z * z);
	if (length < 1.0e-8f)
		return;
	x /= length;
	y /= length;
	z /= length;

	const float rad = angle * 3.14159265358979323846f / 180.0f;
	const float c = std::cos(rad);
	const float s = std::sin(rad);
	const float t = 1.0f - c;

	float rot[16];
	rot[0] = t * x * x + c;
	rot[1] = t * x * y + z * s;
	rot[2] = t * x * z - y * s;
	rot[3] = 0.0f;
	rot[4] = t * x * y - z * s;
	rot[5] = t * y * y + c;
	rot[6] = t * y * z + x * s;
	rot[7] = 0.0f;
	rot[8] = t * x * z + y * s;
	rot[9] = t * y * z - x * s;
	rot[10] = t * z * z + c;
	rot[11] = 0.0f;
	rot[12] = rot[13] = rot[14] = 0.0f;
	rot[15] = 1.0f;

	float *m = currentMatrix();
	matMul(m, rot, m);
	touchMatrices();
}

void doPush()
{
	switch (S.matrixMode)
	{
		case ENUM_PROJECTION:
			if (S.pjTop + 1 < PJ_DEPTH)
			{
				std::memcpy(S.pj[S.pjTop + 1], S.pj[S.pjTop], sizeof(float) * 16);
				S.pjTop++;
			}
			break;
		case ENUM_TEXTURE:
			if (S.txTop + 1 < TX_DEPTH)
			{
				std::memcpy(S.tx[S.txTop + 1], S.tx[S.txTop], sizeof(float) * 16);
				S.txTop++;
			}
			break;
		default:
			if (S.mvTop + 1 < MV_DEPTH)
			{
				std::memcpy(S.mv[S.mvTop + 1], S.mv[S.mvTop], sizeof(float) * 16);
				S.mvTop++;
			}
			break;
	}
}

void doPop()
{
	switch (S.matrixMode)
	{
		case ENUM_PROJECTION:
			if (S.pjTop > 0) S.pjTop--;
			break;
		case ENUM_TEXTURE:
			if (S.txTop > 0) S.txTop--;
			break;
		default:
			if (S.mvTop > 0) S.mvTop--;
			break;
	}
	touchMatrices();
}

void doLoadIdentity()
{
	matIdentity(currentMatrix());
	touchMatrices();
}

void doColor(float r, float g, float b, float a)
{
	S.color[0] = r;
	S.color[1] = g;
	S.color[2] = b;
	S.color[3] = a;
}

void doNormal(float x, float y, float z)
{
	S.normal[0] = x;
	S.normal[1] = y;
	S.normal[2] = z;
}

void doEnable(GLenum cap, bool enable)
{
	if (isPassThroughCap(cap))
	{
		if (enable)
			es.Enable(cap);
		else
			es.Disable(cap);
		return;
	}

	switch (cap)
	{
		case CAP_TEXTURE_2D:
			S.texture2D = enable;
			break;
		case CAP_ALPHA_TEST:
			S.alphaTest = enable;
			break;
		case CAP_FOG:
			S.fog = enable;
			break;
		case CAP_LIGHTING:
			S.lighting = enable;
			break;
		case CAP_LIGHT0:
			S.lights[0].enabled = enable;
			S.lightStamp++;
			break;
		case CAP_LIGHT1:
			S.lights[1].enabled = enable;
			S.lightStamp++;
			break;
		case CAP_COLOR_MATERIAL:
			S.colorMaterial = enable;
			break;
		default:
			// GL_NORMALIZE / GL_RESCALE_NORMAL: normals are always normalized in the shader.
			break;
	}
}

DrawSnapshot::~DrawSnapshot()
{
	if (vbo != 0 && g_glAlive && es.DeleteBuffers != nullptr)
		es.DeleteBuffers(1, &vbo);
}

// Moves the snapshot's vertex data into a GL buffer (once). Falls back to the
// old client-memory path if anything goes wrong.
void uploadSnapshot(DrawSnapshot &snap)
{
	if (snap.uploadTried)
		return;
	snap.uploadTried = true;

	std::size_t offsets[ATTR_COUNT] = {0, 0, 0, 0};
	std::size_t total = 0;
	for (int i = 0; i < ATTR_COUNT; i++)
	{
		if (!snap.arrays[i].enabled)
			continue;
		total = (total + 3) & ~static_cast<std::size_t>(3);
		offsets[i] = total;
		total += snap.data[i].size();
	}
	if (total == 0)
		return;

	std::vector<std::uint8_t> blob(total, 0);
	for (int i = 0; i < ATTR_COUNT; i++)
		if (snap.arrays[i].enabled && !snap.data[i].empty())
			std::memcpy(blob.data() + offsets[i], snap.data[i].data(), snap.data[i].size());

	GLuint buffer = 0;
	es.GenBuffers(1, &buffer);
	if (buffer == 0)
		return;
	es.BindBuffer(ENUM_ARRAY_BUFFER, buffer);
	es.BufferData(ENUM_ARRAY_BUFFER, static_cast<GLsizeiptr>(total), blob.data(), 0x88E4 /* GL_STATIC_DRAW */);
	es.BindBuffer(ENUM_ARRAY_BUFFER, S.arrayBuffer);

	snap.vbo = buffer;
	for (int i = 0; i < ATTR_COUNT; i++)
	{
		if (!snap.arrays[i].enabled)
			continue;
		snap.arrays[i].vbo = buffer;
		snap.arrays[i].ptr = reinterpret_cast<const void *>(offsets[i]);
		std::vector<std::uint8_t>().swap(snap.data[i]); // free the CPU copy
	}
}

void execDraw(const DrawSnapshot &constSnap)
{
	DrawSnapshot &snap = const_cast<DrawSnapshot &>(constSnap);
	if (!snap.uploadTried)
		uploadSnapshot(snap);
	drawArraysWith(snap.arrays, snap.mode, 0, snap.count);
}

void execList(GLuint id);

void execCommand(const ListCmd &c)
{
	switch (c.op)
	{
		case ListCmd::Translate:
			doTranslate(c.f[0], c.f[1], c.f[2]);
			break;
		case ListCmd::Rotate:
			doRotate(c.f[0], c.f[1], c.f[2], c.f[3]);
			break;
		case ListCmd::Scale:
			doScale(c.f[0], c.f[1], c.f[2]);
			break;
		case ListCmd::Color:
			doColor(c.f[0], c.f[1], c.f[2], c.f[3]);
			break;
		case ListCmd::Normal:
			doNormal(c.f[0], c.f[1], c.f[2]);
			break;
		case ListCmd::Push:
			doPush();
			break;
		case ListCmd::Pop:
			doPop();
			break;
		case ListCmd::LoadIdentity:
			doLoadIdentity();
			break;
		case ListCmd::MatrixMode:
			S.matrixMode = c.u;
			break;
		case ListCmd::Draw:
			if (c.draw)
				execDraw(*c.draw);
			break;
		case ListCmd::Call:
			execList(c.u);
			break;
		case ListCmd::Enable:
			doEnable(c.u, true);
			break;
		case ListCmd::Disable:
			doEnable(c.u, false);
			break;
	}
}

void execList(GLuint id)
{
	auto found = S.lists.find(id);
	if (found == S.lists.end())
		return;
	if (S.callDepth >= 8)
		return;

	S.callDepth++;
	const std::vector<ListCmd> &cmds = found->second;
	for (const ListCmd &c : cmds)
		execCommand(c);
	S.callDepth--;
}

void record(const ListCmd &c)
{
	S.compileCmds.push_back(c);
}

// ---------------------------------------------------------------------------
// The emulated OpenGL 1.x entry points (assigned to the glad pointers)
// ---------------------------------------------------------------------------

void APIENTRY sh_Enable(GLenum cap)
{
	if (S.compiling)
	{
		ListCmd c;
		c.op = ListCmd::Enable;
		c.u = cap;
		record(c);
		return;
	}
	doEnable(cap, true);
}

void APIENTRY sh_Disable(GLenum cap)
{
	if (S.compiling)
	{
		ListCmd c;
		c.op = ListCmd::Disable;
		c.u = cap;
		record(c);
		return;
	}
	doEnable(cap, false);
}

void APIENTRY sh_MatrixMode(GLenum mode)
{
	if (S.compiling)
	{
		ListCmd c;
		c.op = ListCmd::MatrixMode;
		c.u = mode;
		record(c);
		return;
	}
	S.matrixMode = mode;
}

void APIENTRY sh_LoadIdentity()
{
	if (S.compiling)
	{
		ListCmd c;
		c.op = ListCmd::LoadIdentity;
		record(c);
		return;
	}
	doLoadIdentity();
}

void APIENTRY sh_PushMatrix()
{
	if (S.compiling)
	{
		ListCmd c;
		c.op = ListCmd::Push;
		record(c);
		return;
	}
	doPush();
}

void APIENTRY sh_PopMatrix()
{
	if (S.compiling)
	{
		ListCmd c;
		c.op = ListCmd::Pop;
		record(c);
		return;
	}
	doPop();
}

void APIENTRY sh_Translatef(GLfloat x, GLfloat y, GLfloat z)
{
	if (S.compiling)
	{
		ListCmd c;
		c.op = ListCmd::Translate;
		c.f[0] = x; c.f[1] = y; c.f[2] = z;
		record(c);
		return;
	}
	doTranslate(x, y, z);
}

void APIENTRY sh_Translated(GLdouble x, GLdouble y, GLdouble z)
{
	sh_Translatef(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
}

void APIENTRY sh_Scalef(GLfloat x, GLfloat y, GLfloat z)
{
	if (S.compiling)
	{
		ListCmd c;
		c.op = ListCmd::Scale;
		c.f[0] = x; c.f[1] = y; c.f[2] = z;
		record(c);
		return;
	}
	doScale(x, y, z);
}

void APIENTRY sh_Scaled(GLdouble x, GLdouble y, GLdouble z)
{
	sh_Scalef(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
}

void APIENTRY sh_Rotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
	if (S.compiling)
	{
		ListCmd c;
		c.op = ListCmd::Rotate;
		c.f[0] = angle; c.f[1] = x; c.f[2] = y; c.f[3] = z;
		record(c);
		return;
	}
	doRotate(angle, x, y, z);
}

void APIENTRY sh_Ortho(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f)
{
	float m[16];
	matIdentity(m);
	m[0] = static_cast<float>(2.0 / (r - l));
	m[5] = static_cast<float>(2.0 / (t - b));
	m[10] = static_cast<float>(-2.0 / (f - n));
	m[12] = static_cast<float>(-(r + l) / (r - l));
	m[13] = static_cast<float>(-(t + b) / (t - b));
	m[14] = static_cast<float>(-(f + n) / (f - n));

	float *cur = currentMatrix();
	matMul(cur, m, cur);
	touchMatrices();
}

void APIENTRY sh_Frustum(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f)
{
	float m[16];
	for (int i = 0; i < 16; i++)
		m[i] = 0.0f;
	m[0] = static_cast<float>(2.0 * n / (r - l));
	m[5] = static_cast<float>(2.0 * n / (t - b));
	m[8] = static_cast<float>((r + l) / (r - l));
	m[9] = static_cast<float>((t + b) / (t - b));
	m[10] = static_cast<float>(-(f + n) / (f - n));
	m[11] = -1.0f;
	m[14] = static_cast<float>(-2.0 * f * n / (f - n));

	float *cur = currentMatrix();
	matMul(cur, m, cur);
	touchMatrices();
}

void APIENTRY sh_LoadMatrixf(const GLfloat *m)
{
	std::memcpy(currentMatrix(), m, sizeof(float) * 16);
	touchMatrices();
}

void APIENTRY sh_MultMatrixf(const GLfloat *m)
{
	float *cur = currentMatrix();
	matMul(cur, m, cur);
	touchMatrices();
}

void APIENTRY sh_Color4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
	if (S.compiling)
	{
		ListCmd c;
		c.op = ListCmd::Color;
		c.f[0] = r; c.f[1] = g; c.f[2] = b; c.f[3] = a;
		record(c);
		return;
	}
	doColor(r, g, b, a);
}

void APIENTRY sh_Color3f(GLfloat r, GLfloat g, GLfloat b)
{
	sh_Color4f(r, g, b, 1.0f);
}

void APIENTRY sh_Normal3f(GLfloat x, GLfloat y, GLfloat z)
{
	if (S.compiling)
	{
		ListCmd c;
		c.op = ListCmd::Normal;
		c.f[0] = x; c.f[1] = y; c.f[2] = z;
		record(c);
		return;
	}
	doNormal(x, y, z);
}

void APIENTRY sh_ShadeModel(GLenum)
{
	// Flat shading only matters when the vertex colors of one primitive differ,
	// which the game never does. Smooth shading is always used.
}

void APIENTRY sh_ColorMaterial(GLenum, GLenum)
{
	// The game always uses GL_AMBIENT_AND_DIFFUSE.
}

void APIENTRY sh_AlphaFunc(GLenum func, GLfloat ref)
{
	S.alphaFunc = func;
	S.alphaRef = ref;
	S.alphaStamp++;
}

void APIENTRY sh_Lightfv(GLenum light, GLenum pname, const GLfloat *params)
{
	const int index = static_cast<int>(light) - static_cast<int>(CAP_LIGHT0);
	if (index < 0 || index > 1)
		return;

	Light &l = S.lights[index];
	switch (pname)
	{
		case ENUM_POSITION:
		{
			// The direction is transformed by the modelview matrix that is current now.
			const float *mv = S.mv[S.mvTop];
			const float x = params[0], y = params[1], z = params[2];
			float ex = mv[0] * x + mv[4] * y + mv[8] * z;
			float ey = mv[1] * x + mv[5] * y + mv[9] * z;
			float ez = mv[2] * x + mv[6] * y + mv[10] * z;
			const float len = std::sqrt(ex * ex + ey * ey + ez * ez);
			if (len > 1.0e-8f)
			{
				ex /= len;
				ey /= len;
				ez /= len;
			}
			l.dir[0] = ex;
			l.dir[1] = ey;
			l.dir[2] = ez;
			break;
		}
		case ENUM_DIFFUSE:
			l.diffuse[0] = params[0];
			l.diffuse[1] = params[1];
			l.diffuse[2] = params[2];
			break;
		case ENUM_AMBIENT:
			l.ambient[0] = params[0];
			l.ambient[1] = params[1];
			l.ambient[2] = params[2];
			break;
		default:
			break; // specular and spot parameters are not used
	}
	S.lightStamp++;
}

void APIENTRY sh_LightModelfv(GLenum pname, const GLfloat *params)
{
	if (pname == ENUM_LIGHT_MODEL_AMBIENT)
	{
		S.sceneAmbient[0] = params[0];
		S.sceneAmbient[1] = params[1];
		S.sceneAmbient[2] = params[2];
		S.lightStamp++;
	}
}

void APIENTRY sh_Fogf(GLenum pname, GLfloat param)
{
	switch (pname)
	{
		case ENUM_FOG_MODE:
			S.fogMode = static_cast<GLenum>(param);
			break;
		case ENUM_FOG_DENSITY:
			S.fogDensity = param;
			break;
		case ENUM_FOG_START:
			S.fogStart = param;
			break;
		case ENUM_FOG_END:
			S.fogEnd = param;
			break;
		default:
			return; // GL_FOG_DISTANCE_MODE_NV and friends do not exist on ES
	}
	S.fogStamp++;
}

void APIENTRY sh_Fogi(GLenum pname, GLint param)
{
	sh_Fogf(pname, static_cast<GLfloat>(param));
}

void APIENTRY sh_Fogfv(GLenum pname, const GLfloat *params)
{
	if (pname == ENUM_FOG_COLOR)
	{
		S.fogColor[0] = params[0];
		S.fogColor[1] = params[1];
		S.fogColor[2] = params[2];
		S.fogColor[3] = params[3];
		S.fogStamp++;
	}
	else
	{
		sh_Fogf(pname, params[0]);
	}
}

// --- Client state arrays ----------------------------------------------------

int arrayIndex(GLenum array)
{
	switch (array)
	{
		case ENUM_VERTEX_ARRAY: return ATTR_POS;
		case ENUM_COLOR_ARRAY: return ATTR_COLOR;
		case ENUM_NORMAL_ARRAY: return ATTR_NORMAL;
		case ENUM_TEXTURE_COORD_ARRAY: return ATTR_TEX;
		default: return -1;
	}
}

void APIENTRY sh_EnableClientState(GLenum array)
{
	const int i = arrayIndex(array);
	if (i >= 0)
		S.arrays[i].enabled = true;
}

void APIENTRY sh_DisableClientState(GLenum array)
{
	const int i = arrayIndex(array);
	if (i >= 0)
		S.arrays[i].enabled = false;
}

void setPointer(int index, GLint size, GLenum type, GLsizei stride, const void *ptr)
{
	ArrayPtr &a = S.arrays[index];
	a.size = size;
	a.type = type;
	a.stride = stride;
	a.ptr = ptr;
	a.vbo = S.arrayBuffer;
}

void APIENTRY sh_VertexPointer(GLint size, GLenum type, GLsizei stride, const void *ptr)
{
	setPointer(ATTR_POS, size, type, stride, ptr);
}

void APIENTRY sh_ColorPointer(GLint size, GLenum type, GLsizei stride, const void *ptr)
{
	setPointer(ATTR_COLOR, size, type, stride, ptr);
}

void APIENTRY sh_NormalPointer(GLenum type, GLsizei stride, const void *ptr)
{
	setPointer(ATTR_NORMAL, 3, type, stride, ptr);
}

void APIENTRY sh_TexCoordPointer(GLint size, GLenum type, GLsizei stride, const void *ptr)
{
	setPointer(ATTR_TEX, size, type, stride, ptr);
}

void APIENTRY sh_DrawArrays(GLenum mode, GLint first, GLsizei count)
{
	if (S.compiling)
	{
		recordDraw(mode, first, count);
		return;
	}
	drawArraysWith(S.arrays, mode, first, count);
}

void APIENTRY sh_DrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices)
{
	if (S.compiling)
	{
		webos::log("[GLES2] glDrawElements inside a display list is not supported");
		return;
	}
	if (count <= 0 || !S.arrays[ATTR_POS].enabled)
		return;
	if (type == ENUM_UNSIGNED_INT)
	{
		if (!S.warnedUnsignedInt)
		{
			webos::log("[GLES2] 32-bit indices are not available on ES 2.0, draw skipped");
			S.warnedUnsignedInt = true;
		}
		return;
	}
	if (!prepareDraw())
		return;

	bindAttribs(S.arrays, 0);
	es.DrawElements(mode, count, type, indices);
}

// --- Buffers and textures ---------------------------------------------------

void APIENTRY sh_BindBuffer(GLenum target, GLuint buffer)
{
	if (target == ENUM_ARRAY_BUFFER)
		S.arrayBuffer = buffer;
	else if (target == ENUM_ELEMENT_ARRAY_BUFFER)
		S.elementBuffer = buffer;
	else
		return; // GL_PIXEL_UNPACK_BUFFER and other targets do not exist on ES 2.0
	es.BindBuffer(target, buffer);
}

void APIENTRY sh_GenBuffers(GLsizei n, GLuint *buffers)
{
	es.GenBuffers(n, buffers);
}

void APIENTRY sh_DeleteBuffers(GLsizei n, const GLuint *buffers)
{
	for (GLsizei i = 0; i < n; i++)
	{
		if (buffers[i] == S.arrayBuffer)
			S.arrayBuffer = 0;
		if (buffers[i] == S.elementBuffer)
			S.elementBuffer = 0;
		for (int a = 0; a < ATTR_COUNT; a++)
			if (S.arrays[a].vbo == buffers[i])
				S.arrays[a].vbo = 0;
	}
	es.DeleteBuffers(n, buffers);
}

void APIENTRY sh_BufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage)
{
	es.BufferData(target, size, data, usage);
}

void APIENTRY sh_BufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void *data)
{
	es.BufferSubData(target, offset, size, data);
}

void APIENTRY sh_GetBufferSubData(GLenum, GLintptr, GLsizeiptr size, void *data)
{
	// ES 2.0 cannot read buffers back. Only the optional region renderer uses
	// this call, and it is disabled for the webOS build.
	if (!S.warnedGetBufferSubData)
	{
		webos::log("[GLES2] glGetBufferSubData is not available on ES 2.0");
		S.warnedGetBufferSubData = true;
	}
	if (data != nullptr && size > 0)
		std::memset(data, 0, static_cast<std::size_t>(size));
}

void APIENTRY sh_BindTexture(GLenum target, GLuint texture)
{
	if (target == CAP_TEXTURE_2D)
		S.boundTexture = texture;
	es.BindTexture(target, texture);
}

void APIENTRY sh_GenTextures(GLsizei n, GLuint *textures)
{
	es.GenTextures(n, textures);
}

void APIENTRY sh_DeleteTextures(GLsizei n, const GLuint *textures)
{
	for (GLsizei i = 0; i < n; i++)
		if (textures[i] == S.boundTexture)
			S.boundTexture = 0;
	es.DeleteTextures(n, textures);
}

void APIENTRY sh_TexParameteri(GLenum target, GLenum pname, GLint param)
{
	if (pname == ENUM_TEXTURE_WRAP_S || pname == ENUM_TEXTURE_WRAP_T)
	{
		if (static_cast<GLenum>(param) == ENUM_CLAMP)
			param = static_cast<GLint>(ENUM_CLAMP_TO_EDGE);
	}
	else if (pname == ENUM_TEXTURE_MIN_FILTER)
	{
		// The game only uploads a partial mip chain (and never enables it), and an
		// incomplete chain samples black on ES 2.0, so mip filters fall back.
		switch (static_cast<GLenum>(param))
		{
			case ENUM_NEAREST_MIPMAP_NEAREST:
			case ENUM_NEAREST_MIPMAP_LINEAR:
				param = static_cast<GLint>(ENUM_NEAREST);
				break;
			case ENUM_LINEAR_MIPMAP_NEAREST:
			case ENUM_LINEAR_MIPMAP_LINEAR:
				param = static_cast<GLint>(ENUM_LINEAR);
				break;
			default:
				break;
		}
	}
	es.TexParameteri(target, pname, param);
}

void APIENTRY sh_TexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void *pixels)
{
	// ES 2.0 requires the internal format to equal the pixel format.
	switch (format)
	{
		case ENUM_RGB:
		case ENUM_RGBA:
		case ENUM_LUMINANCE:
		case ENUM_LUMINANCE_ALPHA:
		case ENUM_ALPHA:
			internalformat = static_cast<GLint>(format);
			break;
		default:
			break;
	}
	es.TexImage2D(target, level, internalformat, width, height, border, format, type, pixels);
}

void APIENTRY sh_TexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const void *pixels)
{
	es.TexSubImage2D(target, level, xoffset, yoffset, width, height, format, type, pixels);
}

void APIENTRY sh_PixelStorei(GLenum pname, GLint param)
{
	if (pname == ENUM_PACK_ALIGNMENT)
		S.packAlignment = param;
	// GL_UNPACK_ROW_LENGTH / SKIP_* are desktop-only and are not forwarded.
	if (pname == 0x0CF2 || pname == 0x0CF3 || pname == 0x0CF4)
		return;
	es.PixelStorei(pname, param);
}

void APIENTRY sh_ReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, void *pixels)
{
	if (format == ENUM_RGBA || type != ENUM_UNSIGNED_BYTE || (format != ENUM_RGB && format != ENUM_BGR_EXT))
	{
		es.ReadPixels(x, y, width, height, format, type, pixels);
		return;
	}

	// ES 2.0 can only read RGBA; convert to RGB / BGR here.
	std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width) * height * 4);
	es.PixelStorei(ENUM_PACK_ALIGNMENT, 4);
	es.ReadPixels(x, y, width, height, ENUM_RGBA, ENUM_UNSIGNED_BYTE, rgba.data());
	es.PixelStorei(ENUM_PACK_ALIGNMENT, S.packAlignment);

	const bool bgr = format == ENUM_BGR_EXT;
	const std::size_t align = static_cast<std::size_t>(S.packAlignment > 0 ? S.packAlignment : 1);
	std::size_t rowBytes = static_cast<std::size_t>(width) * 3;
	rowBytes = (rowBytes + align - 1) / align * align;

	std::uint8_t *dst = static_cast<std::uint8_t *>(pixels);
	for (GLsizei row = 0; row < height; row++)
	{
		const std::uint8_t *src = rgba.data() + static_cast<std::size_t>(row) * width * 4;
		std::uint8_t *out = dst + static_cast<std::size_t>(row) * rowBytes;
		for (GLsizei col = 0; col < width; col++)
		{
			out[col * 3 + 0] = src[col * 4 + (bgr ? 2 : 0)];
			out[col * 3 + 1] = src[col * 4 + 1];
			out[col * 3 + 2] = src[col * 4 + (bgr ? 0 : 2)];
		}
	}
}

void APIENTRY sh_ReadBuffer(GLenum)
{
}

// ---------------------------------------------------------------------------
// Render scale: optionally draw the whole frame into a smaller offscreen target
// and stretch it to the window when presenting. The game keeps seeing the
// window's native size (input, GUI layout and glViewport calls are unchanged);
// only sh_Viewport() shrinks the viewport, so the TV GPU shades far fewer
// pixels. See gles2compat::beginPresent / endPresent below.
// ---------------------------------------------------------------------------

struct FboES
{
	void(APIENTRY *GenFramebuffers)(GLsizei, GLuint *) = nullptr;
	void(APIENTRY *DeleteFramebuffers)(GLsizei, const GLuint *) = nullptr;
	void(APIENTRY *BindFramebuffer)(GLenum, GLuint) = nullptr;
	void(APIENTRY *FramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint) = nullptr;
	void(APIENTRY *FramebufferRenderbuffer)(GLenum, GLenum, GLenum, GLuint) = nullptr;
	GLenum(APIENTRY *CheckFramebufferStatus)(GLenum) = nullptr;
	void(APIENTRY *GenRenderbuffers)(GLsizei, GLuint *) = nullptr;
	void(APIENTRY *DeleteRenderbuffers)(GLsizei, const GLuint *) = nullptr;
	void(APIENTRY *BindRenderbuffer)(GLenum, GLuint) = nullptr;
	void(APIENTRY *RenderbufferStorage)(GLenum, GLenum, GLsizei, GLsizei) = nullptr;
};

FboES &fboInstance()
{
	static FboES f;
	return f;
}
#define fboEs (fboInstance())

const GLenum ENUM_FRAMEBUFFER = 0x8D40;
const GLenum ENUM_RENDERBUFFER = 0x8D41;
const GLenum ENUM_COLOR_ATTACHMENT0 = 0x8CE0;
const GLenum ENUM_DEPTH_ATTACHMENT = 0x8D00;
const GLenum ENUM_FRAMEBUFFER_COMPLETE = 0x8CD5;
const GLenum ENUM_DEPTH_COMPONENT16 = 0x81A5;
const GLenum ENUM_DEPTH_COMPONENT24_OES = 0x81A6;
const GLenum ENUM_TRIANGLE_STRIP = 0x0005;
const GLenum ENUM_TEXTURE_MAG_FILTER = 0x2800;

// Scale steps for the automatic mode, from full resolution downwards.
const float RS_LEVELS[] = {1.0f, 0.8f, 2.0f / 3.0f, 0.5f};
const int RS_LEVEL_COUNT = 4;

struct RenderScale
{
	bool initDone = false;
	bool disabled = false;   // switched off (env var, missing FBO support, failure)
	bool automatic = false;
	bool active = false;     // the game is drawing into the offscreen target
	bool presenting = false; // between beginPresent() and endPresent()
	bool depth24 = false;
	bool supported = false;  // offscreen targets work on this GPU/driver
	bool envForced = false;  // MCBETA_RENDER_SCALE is set: it wins over the in-game setting
	int userMode = 0;        // in-game setting: 0 = auto, 1..4 = RS_LEVELS[mode - 1]
	int pendingMode = -1;    // in-game setting changed, apply at the next present
	bool guardArmed = false; // scale_pending.flag exists and has not been cleared yet
	int goodFrames = 0;      // frames presented through the offscreen target since arming

	float scale = 1.0f;
	int levelIdx = 0;

	int nativeW = 0, nativeH = 0; // window size reported by the game
	int builtW = 0, builtH = 0;   // window size the current target was made for
	int w = 0, h = 0;             // size of the offscreen target

	GLuint fbo = 0, tex = 0, depthRb = 0;
	GLuint program = 0;
	GLint uTex = -1;
	bool programFailed = false;

	// Game GL state saved while the frame is blitted.
	GLboolean savedDepth = GL_FALSE, savedCull = GL_FALSE, savedBlend = GL_FALSE, savedScissor = GL_FALSE;
	GLboolean savedMask[4] = {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
	GLint savedViewport[4] = {0, 0, 0, 0};
	bool savedAttrib[4] = {false, false, false, false};

	// Automatic mode controller.
	double targetMs = 1000.0 / 60.0;
	double lastPresent = 0.0;
	double windowStart = 0.0;
	double lastChange = 0.0;
	double sumMs = 0.0;
	int frames = 0;
	int warmup = 0;
	bool pendingCheck = false;
	double prevAvg = 0.0;
	int prevIdx = 0;
	double noDownscaleUntil = 0.0;
};

RenderScale &renderScaleInstance()
{
	static RenderScale r;
	return r;
}
#define RS (renderScaleInstance())

// --- Pass-through state -----------------------------------------------------

void APIENTRY sh_BlendFunc(GLenum s, GLenum d) { es.BlendFunc(s, d); }
void APIENTRY sh_Clear(GLbitfield mask) { es.Clear(mask); }
void APIENTRY sh_ClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
	S.clearColor[0] = r;
	S.clearColor[1] = g;
	S.clearColor[2] = b;
	S.clearColor[3] = a;
	es.ClearColor(r, g, b, a);
}
void APIENTRY sh_ClearDepth(GLdouble d) { es.ClearDepthf(static_cast<GLfloat>(d)); }
void APIENTRY sh_ColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a)
{
	S.colorMask[0] = r;
	S.colorMask[1] = g;
	S.colorMask[2] = b;
	S.colorMask[3] = a;
	es.ColorMask(r, g, b, a);
}
void APIENTRY sh_CullFace(GLenum mode) { es.CullFace(mode); }
void APIENTRY sh_DepthFunc(GLenum func) { es.DepthFunc(func); }
void APIENTRY sh_DepthMask(GLboolean flag) { es.DepthMask(flag); }
void APIENTRY sh_FrontFace(GLenum mode) { es.FrontFace(mode); }
void APIENTRY sh_LineWidth(GLfloat w) { es.LineWidth(w); }
void APIENTRY sh_PolygonOffset(GLfloat f, GLfloat u) { es.PolygonOffset(f, u); }
void APIENTRY sh_Viewport(GLint x, GLint y, GLsizei w, GLsizei h)
{
	if (RS.active && RS.nativeW > 0 && RS.nativeH > 0)
	{
		// Draw into the smaller offscreen target: same picture, fewer pixels.
		const double fx = static_cast<double>(RS.w) / static_cast<double>(RS.nativeW);
		const double fy = static_cast<double>(RS.h) / static_cast<double>(RS.nativeH);
		x = static_cast<GLint>(std::lround(x * fx));
		y = static_cast<GLint>(std::lround(y * fy));
		w = std::max<GLsizei>(1, static_cast<GLsizei>(std::lround(w * fx)));
		h = std::max<GLsizei>(1, static_cast<GLsizei>(std::lround(h * fy)));
	}
	es.Viewport(x, y, w, h);
}
void APIENTRY sh_Finish() { es.Finish(); }
void APIENTRY sh_Flush() {}
void APIENTRY sh_Hint(GLenum, GLenum) {}
void APIENTRY sh_TexEnvi(GLenum, GLenum, GLint) {}
void APIENTRY sh_TexEnvf(GLenum, GLenum, GLfloat) {}

GLenum APIENTRY sh_GetError() { return es.GetError(); }
const GLubyte *APIENTRY sh_GetString(GLenum name) { return es.GetString(name); }

void APIENTRY sh_GetIntegerv(GLenum pname, GLint *data)
{
	if (pname == ENUM_PACK_ALIGNMENT)
	{
		*data = S.packAlignment;
		return;
	}
	if (pname == 0x0C02) // GL_READ_BUFFER (desktop only)
	{
		*data = 0x0405; // GL_BACK
		return;
	}
	es.GetIntegerv(pname, data);
}

void APIENTRY sh_GetFloatv(GLenum pname, GLfloat *data)
{
	switch (pname)
	{
		case ENUM_MODELVIEW_MATRIX:
			std::memcpy(data, S.mv[S.mvTop], sizeof(float) * 16);
			break;
		case ENUM_PROJECTION_MATRIX:
			std::memcpy(data, S.pj[S.pjTop], sizeof(float) * 16);
			break;
		case ENUM_TEXTURE_MATRIX:
			std::memcpy(data, S.tx[S.txTop], sizeof(float) * 16);
			break;
		default:
			data[0] = 0.0f;
			break;
	}
}

// --- Display list API -------------------------------------------------------

GLuint APIENTRY sh_GenLists(GLsizei range)
{
	if (range <= 0)
		return 0;
	const GLuint first = S.nextList;
	S.nextList += static_cast<GLuint>(range);
	return first;
}

void APIENTRY sh_DeleteLists(GLuint list, GLsizei range)
{
	if (range <= 0)
		return;
	if (static_cast<std::size_t>(range) > S.lists.size())
	{
		for (auto it = S.lists.begin(); it != S.lists.end();)
		{
			if (it->first >= list && it->first < list + static_cast<GLuint>(range))
				it = S.lists.erase(it);
			else
				++it;
		}
	}
	else
	{
		for (GLsizei i = 0; i < range; i++)
			S.lists.erase(list + static_cast<GLuint>(i));
	}
}

void APIENTRY sh_NewList(GLuint list, GLenum)
{
	S.compiling = true;
	S.compileId = list;
	S.compileCmds.clear();
}

void APIENTRY sh_EndList()
{
	if (!S.compiling)
		return;
	S.lists[S.compileId] = std::move(S.compileCmds);
	S.compileCmds.clear();
	S.compiling = false;
}

void APIENTRY sh_CallList(GLuint list)
{
	if (S.compiling)
	{
		ListCmd c;
		c.op = ListCmd::Call;
		c.u = list;
		record(c);
		return;
	}
	execList(list);
}

void APIENTRY sh_CallLists(GLsizei n, GLenum type, const void *lists)
{
	for (GLsizei i = 0; i < n; i++)
	{
		GLuint id = 0;
		switch (type)
		{
			case ENUM_UNSIGNED_BYTE:
				id = static_cast<const std::uint8_t *>(lists)[i];
				break;
			case ENUM_UNSIGNED_SHORT:
				id = static_cast<const std::uint16_t *>(lists)[i];
				break;
			case ENUM_UNSIGNED_INT:
			case ENUM_INT:
				id = static_cast<const std::uint32_t *>(lists)[i];
				break;
			default:
				return;
		}
		sh_CallList(id);
	}
}

// --- Occlusion queries (not available on ES 2.0; the game only uses them when
// GL_ARB_occlusion_query is advertised, which ES 2.0 never does) ------------

void APIENTRY sh_GenQueries(GLsizei n, GLuint *ids)
{
	for (GLsizei i = 0; i < n; i++)
		ids[i] = S.nextQuery++;
}

void APIENTRY sh_DeleteQueries(GLsizei, const GLuint *) {}
void APIENTRY sh_BeginQuery(GLenum, GLuint) {}
void APIENTRY sh_EndQuery(GLenum) {}

void APIENTRY sh_GetQueryObjectuiv(GLuint, GLenum, GLuint *params)
{
	*params = 1; // available, and "visible"
}

// ---------------------------------------------------------------------------
// Software mouse cursor
//
// The TV compositor does not draw a system cursor for the game window, so the
// menus would have no visible pointer. This draws a small arrow on top of the
// finished frame with its own tiny program and puts back every piece of GL
// state it touches.
// ---------------------------------------------------------------------------

struct CursorOverlay
{
	GLuint program = 0;
	GLint uScreen = -1;
	GLint uColor = -1;
	bool failed = false;
};

CursorOverlay &overlayInstance()
{
	static CursorOverlay o;
	return o;
}
#define overlay (overlayInstance())

const char *const CURSOR_VERTEX_SHADER =
    "attribute vec2 aPos;\n"
    "uniform vec2 uScreen;\n"
    "void main()\n"
    "{\n"
    "\tvec2 p = aPos / uScreen * 2.0 - 1.0;\n"
    "\tgl_Position = vec4(p.x, -p.y, 0.0, 1.0);\n"
    "}\n";

const char *const CURSOR_FRAGMENT_SHADER =
    "precision mediump float;\n"
    "uniform vec4 uColor;\n"
    "void main()\n"
    "{\n"
    "\tgl_FragColor = uColor;\n"
    "}\n";

// Arrow with its tip at (0, 0), y pointing down, in units of one "cursor pixel".
// Head: (0,0) (0,16) (4,12) (6,11) (11,11). Tail: (4,12) (7,19) (9,18) (6,11).
const float CURSOR_TRIANGLES[] = {
	0, 0, 0, 16, 4, 12,
	0, 0, 4, 12, 6, 11,
	0, 0, 6, 11, 11, 11,
	4, 12, 7, 19, 9, 18,
	4, 12, 9, 18, 6, 11};
const int CURSOR_VERTEX_COUNT = static_cast<int>(sizeof(CURSOR_TRIANGLES) / sizeof(float) / 2);

bool ensureCursorProgram()
{
	if (overlay.program != 0)
		return true;
	if (overlay.failed)
		return false;

	GLuint vs = compileShader(ENUM_VERTEX_SHADER, CURSOR_VERTEX_SHADER);
	GLuint fs = compileShader(ENUM_FRAGMENT_SHADER, CURSOR_FRAGMENT_SHADER);
	if (vs == 0 || fs == 0)
	{
		if (vs != 0) es.DeleteShader(vs);
		if (fs != 0) es.DeleteShader(fs);
		overlay.failed = true;
		return false;
	}

	GLuint id = es.CreateProgram();
	es.AttachShader(id, vs);
	es.AttachShader(id, fs);
	es.BindAttribLocation(id, ATTR_POS, "aPos");
	es.LinkProgram(id);
	es.DeleteShader(vs);
	es.DeleteShader(fs);

	GLint ok = 0;
	es.GetProgramiv(id, ENUM_LINK_STATUS, &ok);
	if (!ok)
	{
		webos::log("[GLES2] cursor program failed to link");
		es.DeleteProgram(id);
		overlay.failed = true;
		return false;
	}

	overlay.program = id;
	overlay.uScreen = es.GetUniformLocation(id, "uScreen");
	overlay.uColor = es.GetUniformLocation(id, "uColor");
	return true;
}

void setCap(GLenum cap, GLboolean on)
{
	if (on)
		es.Enable(cap);
	else
		es.Disable(cap);
}

void drawCursorOverlay(float x, float y, int width, int height, float scale)
{
	if (!S.ready || width <= 0 || height <= 0 || !ensureCursorProgram())
		return;

	const GLenum CAP_DEPTH_TEST = 0x0B71, CAP_CULL_FACE = 0x0B44, CAP_BLEND = 0x0BE2, CAP_SCISSOR_TEST = 0x0C11;

	// Remember what the game had set up.
	const GLboolean depth = es.IsEnabled(CAP_DEPTH_TEST);
	const GLboolean cull = es.IsEnabled(CAP_CULL_FACE);
	const GLboolean blend = es.IsEnabled(CAP_BLEND);
	const GLboolean scissor = es.IsEnabled(CAP_SCISSOR_TEST);
	GLboolean colorMask[4] = {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
	es.GetBooleanv(0x0C23, colorMask); // GL_COLOR_WRITEMASK
	GLint viewport[4] = {0, 0, width, height};
	es.GetIntegerv(0x0BA2, viewport); // GL_VIEWPORT
	GLint blendFactors[4] = {static_cast<GLint>(GL_SRC_ALPHA), static_cast<GLint>(GL_ONE_MINUS_SRC_ALPHA), 1, 0};
	es.GetIntegerv(0x80C9, &blendFactors[0]); // GL_BLEND_SRC_RGB
	es.GetIntegerv(0x80C8, &blendFactors[1]); // GL_BLEND_DST_RGB
	es.GetIntegerv(0x80CB, &blendFactors[2]); // GL_BLEND_SRC_ALPHA
	es.GetIntegerv(0x80CA, &blendFactors[3]); // GL_BLEND_DST_ALPHA

	// Only the position attribute is used; park the others while drawing.
	bool parked[ATTR_COUNT] = {false, false, false, false};
	for (int i = 1; i < ATTR_COUNT; i++)
	{
		parked[i] = S.attribEnabled[i];
		if (parked[i])
			setAttribEnabled(i, false);
	}

	es.Disable(CAP_DEPTH_TEST);
	es.Disable(CAP_CULL_FACE);
	es.Disable(CAP_SCISSOR_TEST);
	es.Enable(CAP_BLEND);
	es.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	es.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	es.Viewport(0, 0, width, height);

	es.UseProgram(overlay.program);
	S.currentProgram = overlay.program;
	es.Uniform2f(overlay.uScreen, static_cast<float>(width), static_cast<float>(height));
	es.BindBuffer(ENUM_ARRAY_BUFFER, 0);
	setAttribEnabled(ATTR_POS, true);

	std::vector<float> vertices(static_cast<std::size_t>(CURSOR_VERTEX_COUNT) * 2);
	es.VertexAttribPointer(ATTR_POS, 2, ENUM_FLOAT, GL_FALSE, 0, vertices.data());

	auto drawShape = [&](float ox, float oy, float r, float g, float b)
	{
		for (int i = 0; i < CURSOR_VERTEX_COUNT; i++)
		{
			vertices[static_cast<std::size_t>(i) * 2 + 0] = x + ox + CURSOR_TRIANGLES[i * 2 + 0] * scale;
			vertices[static_cast<std::size_t>(i) * 2 + 1] = y + oy + CURSOR_TRIANGLES[i * 2 + 1] * scale;
		}
		const float color[4] = {r, g, b, 1.0f};
		es.Uniform4fv(overlay.uColor, 1, color);
		es.DrawArrays(0x0004 /* GL_TRIANGLES */, 0, CURSOR_VERTEX_COUNT);
	};

	// Dark outline first: the same arrow nudged in eight directions.
	const float o = scale < 1.0f ? 1.0f : scale * 0.75f;
	const float offsets[8][2] = {{-o, 0}, {o, 0}, {0, -o}, {0, o}, {-o, -o}, {o, -o}, {-o, o}, {o, o}};
	for (const auto &offset : offsets)
		drawShape(offset[0], offset[1], 0.0f, 0.0f, 0.0f);
	drawShape(0.0f, 0.0f, 1.0f, 1.0f, 1.0f);

	// Put everything back.
	setCap(CAP_DEPTH_TEST, depth);
	setCap(CAP_CULL_FACE, cull);
	setCap(CAP_BLEND, blend);
	setCap(CAP_SCISSOR_TEST, scissor);
	es.BlendFuncSeparate(static_cast<GLenum>(blendFactors[0]), static_cast<GLenum>(blendFactors[1]),
	                     static_cast<GLenum>(blendFactors[2]), static_cast<GLenum>(blendFactors[3]));
	es.ColorMask(colorMask[0], colorMask[1], colorMask[2], colorMask[3]);
	es.Viewport(viewport[0], viewport[1], viewport[2], viewport[3]);
	es.BindBuffer(ENUM_ARRAY_BUFFER, S.arrayBuffer);
	for (int i = 1; i < ATTR_COUNT; i++)
		if (parked[i])
			setAttribEnabled(i, true);
}

// ---------------------------------------------------------------------------
// Render scale implementation
// ---------------------------------------------------------------------------

gles2compat::LoaderFn g_loader = nullptr;

double monotonicSeconds()
{
	return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool loadFboFunctions()
{
	if (g_loader == nullptr)
		return false;
	FboES &f = fboEs;
#define LOAD_FBO(name) f.name = reinterpret_cast<decltype(f.name)>(g_loader("gl" #name)); if (f.name == nullptr) return false;
	LOAD_FBO(GenFramebuffers)
	LOAD_FBO(DeleteFramebuffers)
	LOAD_FBO(BindFramebuffer)
	LOAD_FBO(FramebufferTexture2D)
	LOAD_FBO(FramebufferRenderbuffer)
	LOAD_FBO(CheckFramebufferStatus)
	LOAD_FBO(GenRenderbuffers)
	LOAD_FBO(DeleteRenderbuffers)
	LOAD_FBO(BindRenderbuffer)
	LOAD_FBO(RenderbufferStorage)
#undef LOAD_FBO
	return true;
}

void destroyTarget()
{
	FboES &f = fboEs;
	if (RS.fbo != 0)
	{
		f.BindFramebuffer(ENUM_FRAMEBUFFER, 0);
		f.DeleteFramebuffers(1, &RS.fbo);
	}
	if (RS.depthRb != 0)
		f.DeleteRenderbuffers(1, &RS.depthRb);
	if (RS.tex != 0)
		es.DeleteTextures(1, &RS.tex);
	RS.fbo = RS.depthRb = RS.tex = 0;
	RS.w = RS.h = 0;
}

// Creates the offscreen target and leaves it bound. On failure everything is
// released, the default framebuffer is bound again and false is returned.
bool createTarget(int w, int h)
{
	FboES &f = fboEs;
	destroyTarget();
	for (int i = 0; i < 8 && es.GetError() != 0; i++)
	{
		// drop errors raised by earlier game calls so they are not blamed on the target
	}

	es.GenTextures(1, &RS.tex);
	es.BindTexture(CAP_TEXTURE_2D, RS.tex);
	es.TexParameteri(CAP_TEXTURE_2D, ENUM_TEXTURE_MIN_FILTER, static_cast<GLint>(ENUM_LINEAR));
	es.TexParameteri(CAP_TEXTURE_2D, ENUM_TEXTURE_MAG_FILTER, static_cast<GLint>(ENUM_LINEAR));
	es.TexParameteri(CAP_TEXTURE_2D, ENUM_TEXTURE_WRAP_S, static_cast<GLint>(ENUM_CLAMP_TO_EDGE));
	es.TexParameteri(CAP_TEXTURE_2D, ENUM_TEXTURE_WRAP_T, static_cast<GLint>(ENUM_CLAMP_TO_EDGE));
	es.TexImage2D(CAP_TEXTURE_2D, 0, static_cast<GLint>(ENUM_RGBA), w, h, 0, ENUM_RGBA, ENUM_UNSIGNED_BYTE, nullptr);
	es.BindTexture(CAP_TEXTURE_2D, S.boundTexture);

	f.GenRenderbuffers(1, &RS.depthRb);
	f.BindRenderbuffer(ENUM_RENDERBUFFER, RS.depthRb);
	f.RenderbufferStorage(ENUM_RENDERBUFFER, RS.depth24 ? ENUM_DEPTH_COMPONENT24_OES : ENUM_DEPTH_COMPONENT16, w, h);
	f.BindRenderbuffer(ENUM_RENDERBUFFER, 0);

	f.GenFramebuffers(1, &RS.fbo);
	f.BindFramebuffer(ENUM_FRAMEBUFFER, RS.fbo);
	f.FramebufferTexture2D(ENUM_FRAMEBUFFER, ENUM_COLOR_ATTACHMENT0, CAP_TEXTURE_2D, RS.tex, 0);
	f.FramebufferRenderbuffer(ENUM_FRAMEBUFFER, ENUM_DEPTH_ATTACHMENT, ENUM_RENDERBUFFER, RS.depthRb);

	const GLenum status = f.CheckFramebufferStatus(ENUM_FRAMEBUFFER);
	if (status != ENUM_FRAMEBUFFER_COMPLETE || es.GetError() != 0)
	{
		webos::log("[SCALE] %dx%d offscreen target is not usable (status 0x%x)", w, h, static_cast<unsigned>(status));
		destroyTarget();
		f.BindFramebuffer(ENUM_FRAMEBUFFER, 0);
		return false;
	}

	RS.w = w;
	RS.h = h;
	es.Viewport(0, 0, w, h);
	return true;
}

const char *const BLIT_VERTEX_SHADER =
    "attribute vec2 aPos;\n"
    "attribute vec2 aUV;\n"
    "varying vec2 vUV;\n"
    "void main()\n"
    "{\n"
    "\tgl_Position = vec4(aPos, 0.0, 1.0);\n"
    "\tvUV = aUV;\n"
    "}\n";

const char *const BLIT_FRAGMENT_SHADER =
    "precision mediump float;\n"
    "varying vec2 vUV;\n"
    "uniform sampler2D uTex;\n"
    "void main()\n"
    "{\n"
    "\tgl_FragColor = vec4(texture2D(uTex, vUV).rgb, 1.0);\n"
    "}\n";

bool ensureBlitProgram()
{
	if (RS.program != 0)
		return true;
	if (RS.programFailed)
		return false;

	GLuint vs = compileShader(ENUM_VERTEX_SHADER, BLIT_VERTEX_SHADER);
	GLuint fs = compileShader(ENUM_FRAGMENT_SHADER, BLIT_FRAGMENT_SHADER);
	if (vs == 0 || fs == 0)
	{
		if (vs != 0) es.DeleteShader(vs);
		if (fs != 0) es.DeleteShader(fs);
		RS.programFailed = true;
		return false;
	}

	GLuint id = es.CreateProgram();
	es.AttachShader(id, vs);
	es.AttachShader(id, fs);
	es.BindAttribLocation(id, ATTR_POS, "aPos");
	es.BindAttribLocation(id, ATTR_TEX, "aUV");
	es.LinkProgram(id);
	es.DeleteShader(vs);
	es.DeleteShader(fs);

	GLint ok = 0;
	es.GetProgramiv(id, ENUM_LINK_STATUS, &ok);
	if (!ok)
	{
		webos::log("[SCALE] blit program failed to link");
		es.DeleteProgram(id);
		RS.programFailed = true;
		return false;
	}
	RS.program = id;
	RS.uTex = es.GetUniformLocation(id, "uTex");
	return true;
}

// Crash guard. The first time a run draws into the offscreen target it drops
// scale_pending.flag next to the executable (or in /tmp); after 300 good frames
// the flag is removed. If the next start finds the flag, the previous run died
// while scaling, so scaling is switched off (scale_crashed.flag) until that file
// is deleted. That way a driver that crashes on offscreen rendering cannot lock
// the game into a crash loop.
std::string guardDirectory()
{
	char exe[512];
	const ssize_t n = ::readlink("/proc/self/exe", exe, sizeof(exe) - 1);
	if (n > 0)
	{
		exe[n] = '\0';
		std::string path(exe);
		const std::size_t slash = path.rfind('/');
		if (slash != std::string::npos)
			return path.substr(0, slash + 1);
	}
	return "/tmp/";
}

bool fileExists(const std::string &path)
{
	if (std::FILE *f = std::fopen(path.c_str(), "r"))
	{
		std::fclose(f);
		return true;
	}
	return false;
}

bool markerExists(const char *name)
{
	return fileExists(guardDirectory() + name) || fileExists(std::string("/tmp/") + name);
}

void writeMarker(const char *name)
{
	std::FILE *f = std::fopen((guardDirectory() + name).c_str(), "w");
	if (f == nullptr)
		f = std::fopen((std::string("/tmp/") + name).c_str(), "w");
	if (f != nullptr)
	{
		std::fputs("render scaling guard\n", f);
		std::fclose(f);
	}
}

void removeMarker(const char *name)
{
	std::remove((guardDirectory() + name).c_str());
	std::remove((std::string("/tmp/") + name).c_str());
}

// Switches the game to `scale` (1.0 = draw straight into the window).
bool applyScale(float scale)
{
	FboES &f = fboEs;
	RS.scale = scale;
	RS.builtW = RS.nativeW;
	RS.builtH = RS.nativeH;

	if (scale >= 0.999f || RS.nativeW <= 0 || RS.nativeH <= 0)
	{
		if (RS.fbo != 0)
		{
			destroyTarget();
			f.BindFramebuffer(ENUM_FRAMEBUFFER, 0);
		}
		RS.active = false;
		return true;
	}

	const int w = std::max(320, static_cast<int>(std::lround(RS.nativeW * scale)));
	const int h = std::max(180, static_cast<int>(std::lround(RS.nativeH * scale)));
	if (!RS.program && !ensureBlitProgram())
	{
		RS.disabled = true;
		RS.active = false;
		return false;
	}
	webos::stage("scale: creating offscreen target");
	if (createTarget(w, h))
	{
		if (!RS.guardArmed)
		{
			writeMarker("scale_pending.flag");
			RS.guardArmed = true;
			RS.goodFrames = 0;
		}
		RS.active = true;
		webos::log("[SCALE] rendering at %dx%d (%.0f%%) and stretching to %dx%d", w, h, scale * 100.0f, RS.nativeW, RS.nativeH);
		return true;
	}

	RS.disabled = true;
	RS.active = false;
	webos::log("[SCALE] render scaling disabled (offscreen target unavailable)");
	return false;
}

// Applies an in-game mode: 0 = automatic, 1..RS_LEVEL_COUNT = fixed RS_LEVELS[mode - 1].
void applyMode(int mode)
{
	RS.pendingCheck = false;
	RS.noDownscaleUntil = 0.0;
	RS.frames = 0;
	RS.sumMs = 0.0;
	const double now = monotonicSeconds();
	RS.windowStart = RS.lastChange = RS.lastPresent = now;

	if (!RS.supported || RS.disabled)
	{
		RS.automatic = false;
		return;
	}

	if (mode <= 0)
	{
		RS.automatic = true;
		RS.levelIdx = 2; // start at 2/3 and adapt
		RS.warmup = 6;
		applyScale(RS_LEVELS[RS.levelIdx]);
		webos::log("[SCALE] automatic: aiming for %.0f fps", 1000.0 / RS.targetMs);
	}
	else
	{
		RS.automatic = false;
		RS.levelIdx = std::min(mode, RS_LEVEL_COUNT) - 1;
		applyScale(RS_LEVELS[RS.levelIdx]);
	}
}

void initRenderScale()
{
	RS.initDone = true;

	if (const char *fps = std::getenv("MCBETA_TARGET_FPS"))
	{
		const double v = std::atof(fps);
		if (v >= 20.0 && v <= 240.0)
			RS.targetMs = 1000.0 / v;
	}

	if (!loadFboFunctions())
	{
		RS.disabled = true;
		webos::log("[SCALE] framebuffer objects unavailable, render scaling off");
		return;
	}
	const char *ext = reinterpret_cast<const char *>(es.GetString(0x1F03)); // GL_EXTENSIONS
	RS.depth24 = ext != nullptr && std::strstr(ext, "GL_OES_depth24") != nullptr;
	if (!RS.depth24)
	{
		// A 16-bit depth buffer z-fights badly at the game's near/far planes.
		RS.disabled = true;
		webos::log("[SCALE] no 24-bit depth renderbuffer (GL_OES_depth24 missing), render scaling off");
		return;
	}
	if (markerExists("scale_crashed.flag") || markerExists("scale_pending.flag"))
	{
		// The previous run died while drawing into the offscreen target.
		removeMarker("scale_pending.flag");
		writeMarker("scale_crashed.flag");
		RS.disabled = true;
		webos::log("[SCALE] render scaling is OFF: an earlier run crashed while using it. Delete scale_crashed.flag (next to McBetaCpp, or in /tmp) to try again.");
		return;
	}
	RS.supported = true;

	// MCBETA_RENDER_SCALE (shell or mcbeta.env) overrides the in-game setting.
	const char *env = std::getenv("MCBETA_RENDER_SCALE");
	if (env != nullptr && *env != '\0')
	{
		RS.envForced = true;
		if (std::strcmp(env, "auto") == 0)
		{
			applyMode(0);
		}
		else
		{
			float v = static_cast<float>(std::atof(env));
			if (std::strcmp(env, "off") == 0 || v <= 0.0f || v >= 0.999f)
			{
				webos::log("[SCALE] render scaling off (MCBETA_RENDER_SCALE=%s)", env);
				applyMode(1);
			}
			else
			{
				applyMode(1);
				RS.automatic = false;
				applyScale(std::max(v, 0.35f));
			}
		}
		webos::log("[SCALE] MCBETA_RENDER_SCALE=%s overrides the in-game Render Scale setting", env);
		return;
	}

	applyMode(RS.userMode);
}

// Automatic mode: looks at the frame time once a second or so and moves one
// step along RS_LEVELS. Stepping down that does not speed things up means the
// CPU is the bottleneck, so it steps back and stops shrinking for a while.
void adaptRenderScale(double now)
{
	const double frameMs = (now - RS.lastPresent) * 1000.0;
	RS.lastPresent = now;

	if (RS.warmup > 0)
	{
		RS.warmup--;
		RS.windowStart = now;
		RS.sumMs = 0.0;
		RS.frames = 0;
		return;
	}

	RS.sumMs += std::min(frameMs, 100.0);
	RS.frames++;
	if (now - RS.windowStart < 1.2 || RS.frames < 8)
		return;

	const double avg = RS.sumMs / RS.frames;
	RS.sumMs = 0.0;
	RS.frames = 0;
	RS.windowStart = now;

	auto pixels = [](int idx) { return static_cast<double>(RS_LEVELS[idx]) * RS_LEVELS[idx]; };
	auto change = [&](int idx)
	{
		RS.levelIdx = idx;
		RS.lastChange = now;
		RS.warmup = 6; // the new target and the first frames on it are not representative
		applyScale(RS_LEVELS[idx]);
	};

	if (RS.pendingCheck)
	{
		RS.pendingCheck = false;
		if (avg > RS.prevAvg * 0.92)
		{
			webos::log("[SCALE] fewer pixels did not help (%.1f -> %.1f ms): CPU-bound, going back to %.0f%%",
			           RS.prevAvg, avg, RS_LEVELS[RS.prevIdx] * 100.0f);
			RS.noDownscaleUntil = now + 60.0;
			change(RS.prevIdx);
		}
		return;
	}

	if (now - RS.lastChange < 2.0)
		return;

	if (avg > RS.targetMs * 1.2 && RS.levelIdx < RS_LEVEL_COUNT - 1 && now >= RS.noDownscaleUntil)
	{
		webos::log("[SCALE] %.1f ms/frame is over the %.1f ms target: %.0f%% -> %.0f%%",
		           avg, RS.targetMs, RS_LEVELS[RS.levelIdx] * 100.0f, RS_LEVELS[RS.levelIdx + 1] * 100.0f);
		RS.prevAvg = avg;
		RS.prevIdx = RS.levelIdx;
		RS.pendingCheck = true;
		change(RS.levelIdx + 1);
	}
	else if (avg < RS.targetMs * 0.75 && RS.levelIdx > 0)
	{
		const double predicted = avg * pixels(RS.levelIdx - 1) / pixels(RS.levelIdx);
		if (predicted < RS.targetMs * 0.95)
		{
			webos::log("[SCALE] %.1f ms/frame leaves room: %.0f%% -> %.0f%%", avg, RS_LEVELS[RS.levelIdx] * 100.0f, RS_LEVELS[RS.levelIdx - 1] * 100.0f);
			change(RS.levelIdx - 1);
		}
	}
}

void resetState()
{
	for (int i = 0; i < MV_DEPTH; i++)
		matIdentity(S.mv[i]);
	for (int i = 0; i < PJ_DEPTH; i++)
		matIdentity(S.pj[i]);
	for (int i = 0; i < TX_DEPTH; i++)
		matIdentity(S.tx[i]);
	S.mvTop = S.pjTop = S.txTop = 0;
	S.matrixMode = ENUM_MODELVIEW;

	// OpenGL defaults: light 0 is white, the others are black.
	S.lights[0] = Light();
	S.lights[0].diffuse[0] = S.lights[0].diffuse[1] = S.lights[0].diffuse[2] = 1.0f;
	S.lights[1] = Light();

	S.progs.clear();
	S.lists.clear();
	S.nextList = 1;
	S.compiling = false;
	S.compileCmds.clear();
	S.callDepth = 0;
	S.currentProgram = 0;
	S.arrayBuffer = S.elementBuffer = S.boundTexture = 0;
	S.matStamp = S.lightStamp = S.fogStamp = S.alphaStamp = 1;
	S.mvpStamp = 0;
	for (int i = 0; i < ATTR_COUNT; i++)
	{
		S.arrays[i] = ArrayPtr();
		S.attribEnabled[i] = false;
	}
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

namespace gles2compat
{

bool init(LoaderFn loader)
{
	g_loader = loader;
	if (!loadES(loader))
		return false;

	resetState();

	// The ES driver keeps attribute arrays 0..3 disabled until we enable them.
	for (int i = 0; i < ATTR_COUNT; i++)
		es.DisableVertexAttribArray(static_cast<GLuint>(i));

#define HOOK(name) glad_gl##name = sh_##name
	HOOK(Enable);
	HOOK(Disable);
	HOOK(MatrixMode);
	HOOK(LoadIdentity);
	HOOK(PushMatrix);
	HOOK(PopMatrix);
	HOOK(Translatef);
	HOOK(Translated);
	HOOK(Scalef);
	HOOK(Scaled);
	HOOK(Rotatef);
	HOOK(Ortho);
	HOOK(Frustum);
	HOOK(LoadMatrixf);
	HOOK(MultMatrixf);
	HOOK(Color4f);
	HOOK(Color3f);
	HOOK(Normal3f);
	HOOK(ShadeModel);
	HOOK(ColorMaterial);
	HOOK(AlphaFunc);
	HOOK(Lightfv);
	HOOK(LightModelfv);
	HOOK(Fogf);
	HOOK(Fogi);
	HOOK(Fogfv);
	HOOK(EnableClientState);
	HOOK(DisableClientState);
	HOOK(VertexPointer);
	HOOK(ColorPointer);
	HOOK(NormalPointer);
	HOOK(TexCoordPointer);
	HOOK(DrawArrays);
	HOOK(DrawElements);
	HOOK(BindBuffer);
	HOOK(GenBuffers);
	HOOK(DeleteBuffers);
	HOOK(BufferData);
	HOOK(BufferSubData);
	HOOK(GetBufferSubData);
	HOOK(BindTexture);
	HOOK(GenTextures);
	HOOK(DeleteTextures);
	HOOK(TexParameteri);
	HOOK(TexImage2D);
	HOOK(TexSubImage2D);
	HOOK(PixelStorei);
	HOOK(ReadPixels);
	HOOK(ReadBuffer);
	HOOK(BlendFunc);
	HOOK(Clear);
	HOOK(ClearColor);
	HOOK(ClearDepth);
	HOOK(ColorMask);
	HOOK(CullFace);
	HOOK(DepthFunc);
	HOOK(DepthMask);
	HOOK(FrontFace);
	HOOK(LineWidth);
	HOOK(PolygonOffset);
	HOOK(Viewport);
	HOOK(Finish);
	HOOK(Flush);
	HOOK(Hint);
	HOOK(TexEnvi);
	HOOK(TexEnvf);
	HOOK(GetError);
	HOOK(GetString);
	HOOK(GetIntegerv);
	HOOK(GetFloatv);
	HOOK(GenLists);
	HOOK(DeleteLists);
	HOOK(NewList);
	HOOK(EndList);
	HOOK(CallList);
	HOOK(CallLists);
	HOOK(GenQueries);
	HOOK(DeleteQueries);
	HOOK(BeginQuery);
	HOOK(EndQuery);
	HOOK(GetQueryObjectuiv);
#undef HOOK

	S.ready = true;
	g_glAlive = true;
	webos::log("[GLES2] fixed-function emulation ready (%s / %s)",
	           reinterpret_cast<const char *>(es.GetString(0x1F01)), // GL_RENDERER
	           reinterpret_cast<const char *>(es.GetString(0x1F02))); // GL_VERSION
	return true;
}

bool beginPresent(int nativeWidth, int nativeHeight)
{
	RS.nativeW = nativeWidth;
	RS.nativeH = nativeHeight;
	RS.presenting = false;
	if (!S.ready || !RS.active || RS.fbo == 0 || RS.disabled || nativeWidth <= 0 || nativeHeight <= 0)
		return false;
	if (!ensureBlitProgram())
		return false;

	FboES &f = fboEs;
	const GLenum CAP_DEPTH_TEST = 0x0B71, CAP_CULL_FACE = 0x0B44, CAP_BLEND = 0x0BE2, CAP_SCISSOR_TEST = 0x0C11;
	webos::stage("scale: blit frame to window");

	// Remember what the game had set up; endPresent() puts it back.
	RS.savedDepth = es.IsEnabled(CAP_DEPTH_TEST);
	RS.savedCull = es.IsEnabled(CAP_CULL_FACE);
	RS.savedBlend = es.IsEnabled(CAP_BLEND);
	RS.savedScissor = es.IsEnabled(CAP_SCISSOR_TEST);
	es.GetBooleanv(0x0C23, RS.savedMask); // GL_COLOR_WRITEMASK
	es.GetIntegerv(0x0BA2, RS.savedViewport); // GL_VIEWPORT
	for (int i = 0; i < ATTR_COUNT; i++)
		RS.savedAttrib[i] = S.attribEnabled[i];

	f.BindFramebuffer(ENUM_FRAMEBUFFER, 0);
	es.Disable(CAP_DEPTH_TEST);
	es.Disable(CAP_CULL_FACE);
	es.Disable(CAP_BLEND);
	es.Disable(CAP_SCISSOR_TEST);
	es.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	es.Viewport(0, 0, nativeWidth, nativeHeight);

	es.UseProgram(RS.program);
	S.currentProgram = RS.program;
	es.BindTexture(CAP_TEXTURE_2D, RS.tex);
	es.Uniform1i(RS.uTex, 0);

	static const float quad[16] = {
		-1.0f, -1.0f, 0.0f, 0.0f,
		1.0f, -1.0f, 1.0f, 0.0f,
		-1.0f, 1.0f, 0.0f, 1.0f,
		1.0f, 1.0f, 1.0f, 1.0f};
	es.BindBuffer(ENUM_ARRAY_BUFFER, 0);
	setAttribEnabled(ATTR_COLOR, false);
	setAttribEnabled(ATTR_NORMAL, false);
	setAttribEnabled(ATTR_POS, true);
	setAttribEnabled(ATTR_TEX, true);
	es.VertexAttribPointer(ATTR_POS, 2, ENUM_FLOAT, GL_FALSE, 16, quad);
	es.VertexAttribPointer(ATTR_TEX, 2, ENUM_FLOAT, GL_FALSE, 16, quad + 2);
	es.DrawArrays(ENUM_TRIANGLE_STRIP, 0, 4);

	RS.presenting = true;
	return true;
}

void endPresent()
{
	if (!S.ready)
		return;

	FboES &f = fboEs;
	const GLenum CAP_DEPTH_TEST = 0x0B71, CAP_CULL_FACE = 0x0B44, CAP_BLEND = 0x0BE2, CAP_SCISSOR_TEST = 0x0C11;

	if (RS.presenting)
	{
		RS.presenting = false;
		if (RS.guardArmed && ++RS.goodFrames >= 300)
		{
			removeMarker("scale_pending.flag");
			RS.guardArmed = false;
			webos::log("[SCALE] offscreen rendering verified (300 frames)");
		}
		es.BindBuffer(ENUM_ARRAY_BUFFER, S.arrayBuffer);
		for (int i = 0; i < ATTR_COUNT; i++)
			setAttribEnabled(i, RS.savedAttrib[i]);
		es.BindTexture(CAP_TEXTURE_2D, S.boundTexture);
		setCap(CAP_DEPTH_TEST, RS.savedDepth);
		setCap(CAP_CULL_FACE, RS.savedCull);
		setCap(CAP_BLEND, RS.savedBlend);
		setCap(CAP_SCISSOR_TEST, RS.savedScissor);
		es.ColorMask(RS.savedMask[0], RS.savedMask[1], RS.savedMask[2], RS.savedMask[3]);
		if (RS.fbo != 0)
		{
			// The swap left the window framebuffer bound; go back to the offscreen one.
			f.BindFramebuffer(ENUM_FRAMEBUFFER, RS.fbo);
			es.Viewport(RS.savedViewport[0], RS.savedViewport[1], RS.savedViewport[2], RS.savedViewport[3]);
		}
	}

	const double now = monotonicSeconds();
	if (!RS.initDone)
	{
		if (RS.nativeW > 0 && RS.nativeH > 0)
			initRenderScale();
		RS.pendingMode = -1;
		return;
	}
	if (RS.disabled)
		return;

	if (RS.pendingMode >= 0)
	{
		const int mode = RS.pendingMode;
		RS.pendingMode = -1;
		if (!RS.envForced)
			applyMode(mode);
		return;
	}

	// The window changed size: rebuild the target for it.
	if (RS.active && (RS.builtW != RS.nativeW || RS.builtH != RS.nativeH))
		applyScale(RS.scale);

	if (RS.automatic)
		adaptRenderScale(now);
}

float renderScale()
{
	return RS.active ? RS.scale : 1.0f;
}

void setRenderScaleMode(int mode)
{
	if (mode < 0)
		mode = 0;
	RS.userMode = mode;
	// Before the first frame the value is simply picked up by the initial setup.
	if (RS.initDone)
		RS.pendingMode = mode;
}

bool renderScaleAvailable()
{
	return !RS.initDone || (RS.supported && !RS.disabled);
}

void drawCursor(float x, float y, int screenWidth, int screenHeight, float scale)
{
	drawCursorOverlay(x, y, screenWidth, screenHeight, scale);
}

void bytesStats(unsigned long &verts, unsigned long &clientKb)
{
	verts = g_statVerts;
	clientKb = g_statClientBytes / 1024;
}

void makeOpaque()
{
	// The scaled blit already writes alpha = 1 over the whole window.
	if (!S.ready || RS.presenting)
		return;

	// The game clears with alpha 0 and draws GUI/sky with blending, so the
	// window's alpha channel ends up all over the place. VNC screenshots ignore
	// alpha, but the TV compositor honours it, which made the world (and only
	// the world) transparent/black. Write alpha = 1 everywhere, touching no
	// colour channel, right before the buffer swap.
	const GLenum CAP_SCISSOR_TEST = 0x0C11;
	const GLboolean scissor = es.IsEnabled(CAP_SCISSOR_TEST);
	if (scissor)
		es.Disable(CAP_SCISSOR_TEST);
	es.ColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
	es.ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	es.Clear(0x00004000); // GL_COLOR_BUFFER_BIT
	es.ColorMask(S.colorMask[0], S.colorMask[1], S.colorMask[2], S.colorMask[3]);
	es.ClearColor(S.clearColor[0], S.clearColor[1], S.clearColor[2], S.clearColor[3]);
	if (scissor)
		es.Enable(CAP_SCISSOR_TEST);
}

void stats(unsigned long &calls, unsigned long &noPos, unsigned long &noProg, unsigned long &drawn)
{
	calls = g_statCalls;
	noPos = g_statNoPos;
	noProg = g_statNoProg;
	drawn = g_statDrawn;
}

void shutdown()
{
	g_glAlive = false;
	if (!S.ready)
		return;

	if (overlay.program != 0)
		es.DeleteProgram(overlay.program);
	overlay = CursorOverlay();

	for (auto &entry : S.progs)
		if (entry.second.id != 0)
			es.DeleteProgram(entry.second.id);
	S.progs.clear();
	S.lists.clear();
	S.currentProgram = 0;
	S.ready = false;
}

}