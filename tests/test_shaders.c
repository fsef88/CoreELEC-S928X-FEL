/*
 * test_shaders.c - Headless GLES 3.2 shader validation suite for felgpu
 */
#include <windows.h>
#include <GL/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER   0x8B31
#define GL_COMPILE_STATUS  0x8B81
#define GL_LINK_STATUS     0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84

typedef GLuint (APIENTRY *PFNGLCREATESHADERPROC)(GLenum type);
typedef void (APIENTRY *PFNGLSHADERSOURCEPROC)(GLuint shader, GLsizei count, const char* const* string, const GLint* length);
typedef void (APIENTRY *PFNGLCOMPILESHADERPROC)(GLuint shader);
typedef void (APIENTRY *PFNGLGETSHADERIVPROC)(GLuint shader, GLenum pname, GLint* params);
typedef void (APIENTRY *PFNGLGETSHADERINFOLOGPROC)(GLuint shader, GLsizei bufSize, GLsizei* length, char* infoLog);
typedef GLuint (APIENTRY *PFNGLCREATEPROGRAMPROC)(void);
typedef void (APIENTRY *PFNGLATTACHSHADERPROC)(GLuint program, GLuint shader);
typedef void (APIENTRY *PFNGLLINKPROGRAMPROC)(GLuint program);
typedef void (APIENTRY *PFNGLGETPROGRAMIVPROC)(GLuint program, GLenum pname, GLint* params);
typedef void (APIENTRY *PFNGLGETPROGRAMINFOLOGPROC)(GLuint program, GLsizei bufSize, GLsizei* length, char* infoLog);
typedef GLint (APIENTRY *PFNGLGETUNIFORMLOCATIONPROC)(GLuint program, const char* name);
typedef void (APIENTRY *PFNGLDELETESHADERPROC)(GLuint shader);
typedef void (APIENTRY *PFNGLDELETEPROGRAMPROC)(GLuint program);

static PFNGLCREATESHADERPROC glCreateShader_ptr;
static PFNGLSHADERSOURCEPROC glShaderSource_ptr;
static PFNGLCOMPILESHADERPROC glCompileShader_ptr;
static PFNGLGETSHADERIVPROC glGetShaderiv_ptr;
static PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog_ptr;
static PFNGLCREATEPROGRAMPROC glCreateProgram_ptr;
static PFNGLATTACHSHADERPROC glAttachShader_ptr;
static PFNGLLINKPROGRAMPROC glLinkProgram_ptr;
static PFNGLGETPROGRAMIVPROC glGetProgramiv_ptr;
static PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog_ptr;
static PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation_ptr;
static PFNGLDELETESHADERPROC glDeleteShader_ptr;
static PFNGLDELETEPROGRAMPROC glDeleteProgram_ptr;

#define GLSL_HEAD \
    "precision highp float;\n" \
    "precision highp int;\n" \
    "precision highp usampler2D;\n" \
    "precision highp sampler2D;\n" \
    "#define WSEL(a, ph, k) ((ph) == 0 ? a[k] : a[TAPS + (k)])\n" \
    "#define BSEL(a, ph) ((ph) == 0 ? a[0] : a[1])\n"

#define GLSL_WORD \
    "uint word_at(sampler2D t, ivec2 p) { return packUnorm4x8(texelFetch(t, p, 0)); }\n" \
    "uint y_of(uint w) { return (w >> 20) & 1023u; }\n" \
    "uint cb_of(uint w) { return (w >> 10) & 1023u; }\n" \
    "uint cr_of(uint w) { return w & 1023u; }\n" \
    "vec4 pack_px(uint y, uint cb, uint cr) { return unpackUnorm4x8(cr | (cb << 10) | (y << 20)); }\n" \
    "uniform sampler2D lut;\n" \
    "float lut_at(int row, uint i) { return texelFetch(lut, ivec2(int(min(i, 1023u)), row), 0).x; }\n" \
    "uint out10(float v) { return uint(clamp(floor(v * 1024.0 + 0.5), 0.0, 1023.0)); }\n" \
    "uniform sampler2D dither;\n" \
    "uniform int dither_split;\n" \
    "uniform int pattern;\n" \
    "uniform int pass_h;\n" \
    "float dth(ivec2 p, ivec2 off) {\n" \
    "  bool on = pattern != 0 ? dither_split > 0 && ((p.y * 4 / pass_h) & 1) == 0\n" \
    "                         : p.x < dither_split;\n" \
    "  return on ? texelFetch(dither, (p + off) & 63, 0).x : 0.5;\n" \
    "}\n" \
    "float pattern_v(ivec2 p, ivec2 size) {\n" \
    "  float lo = p.y * 4 / size.y < 2 ? 160.0 : 330.0;\n" \
    "  return floor((lo + 80.0 * float(p.x) / float(size.x)) * 4.0) / 4096.0;\n" \
    "}\n" \
    "uint out10d(float v, float t) { return uint(clamp(floor(v * 1024.0 + t), 0.0, 1023.0)); }\n"

static const char *fs_el_vert1 =
    GLSL_HEAD GLSL_WORD
    "uniform sampler2D src_packed;\n"
    "uniform usampler2D src_planar;\n"
    "uniform bool is_packed;\n"
    "uniform float wt[12];\n"
    "uniform int base[2];\n"
    "out highp float o;\n"
    "void main() {\n"
    "  ivec2 p = ivec2(gl_FragCoord.xy);\n"
    "  int ph = p.y & 1;\n"
    "  int h = is_packed ? textureSize(src_packed, 0).y : textureSize(src_planar, 0).y;\n"
    "  int b = (p.y >> 1) + BSEL(base, ph);\n"
    "  highp float acc = 0.0;\n"
    "  for (int t = 0; t < TAPS; t++) {\n"
    "    int yy = clamp(b + t, 0, h - 1);\n"
    "    highp float s;\n"
    "    if (is_packed) {\n"
    "      uint w = word_at(src_packed, ivec2(p.x, yy));\n"
    "      s = float(y_of(w));\n"
    "    } else {\n"
    "      s = float(texelFetch(src_planar, ivec2(p.x, yy), 0).x);\n"
    "    }\n"
    "    acc += WSEL(wt, ph, t) * s;\n"
    "  }\n"
    "  o = acc;\n"
    "}\n";

static const char *fs_el_vert2 =
    GLSL_HEAD GLSL_WORD
    "uniform sampler2D src_packed;\n"
    "uniform usampler2D src0;\n"
    "uniform usampler2D src1;\n"
    "uniform bool is_packed;\n"
    "uniform float wt[12];\n"
    "uniform int base[2];\n"
    "out highp vec2 o;\n"
    "void main() {\n"
    "  ivec2 p = ivec2(gl_FragCoord.xy);\n"
    "  int ph = p.y & 1;\n"
    "  int h = is_packed ? (textureSize(src_packed, 0).y / 2) : textureSize(src0, 0).y;\n"
    "  int b = (p.y >> 1) + BSEL(base, ph);\n"
    "  highp vec2 acc = vec2(0.0);\n"
    "  for (int t = 0; t < TAPS; t++) {\n"
    "    int yy = clamp(b + t, 0, h - 1);\n"
    "    highp vec2 s;\n"
    "    if (is_packed) {\n"
    "      uint w = word_at(src_packed, ivec2(p.x * 2, yy * 2));\n"
    "      s = vec2(float(cb_of(w)), float(cr_of(w)));\n"
    "    } else {\n"
    "      s = vec2(float(texelFetch(src0, ivec2(p.x, yy), 0).x),\n"
    "               float(texelFetch(src1, ivec2(p.x, yy), 0).x));\n"
    "    }\n"
    "    acc += WSEL(wt, ph, t) * s;\n"
    "  }\n"
    "  o = acc;\n"
    "}\n";

static const char *fs_el_horiz1 =
    GLSL_HEAD
    "uniform sampler2D src;\n"
    "uniform float hwt[12];\n"
    "uniform int hbase[2];\n"
    "uniform float el_max;\n"
    "out highp float o;\n"
    "void main() {\n"
    "  ivec2 p = ivec2(gl_FragCoord.xy);\n"
    "  int ph = p.x & 1;\n"
    "  int w = textureSize(src, 0).x;\n"
    "  int b = (p.x >> 1) + BSEL(hbase, ph);\n"
    "  highp float acc = 0.0;\n"
    "  for (int k = 0; k < TAPS; k++) {\n"
    "    int xx = clamp(b + k, 0, w - 1);\n"
    "    acc += WSEL(hwt, ph, k) * texelFetch(src, ivec2(xx, p.y), 0).x;\n"
    "  }\n"
    "  o = clamp(floor(acc + 0.5), 0.0, el_max);\n"
    "}\n";

static const char *fs_el_horiz2 =
    GLSL_HEAD
    "uniform sampler2D src;\n"
    "uniform float hwt[12];\n"
    "uniform int hbase[2];\n"
    "uniform float el_max;\n"
    "out highp vec2 o;\n"
    "void main() {\n"
    "  ivec2 p = ivec2(gl_FragCoord.xy);\n"
    "  int ph = p.x & 1;\n"
    "  int w = textureSize(src, 0).x;\n"
    "  int b = (p.x >> 1) + BSEL(hbase, ph);\n"
    "  highp vec2 acc = vec2(0.0);\n"
    "  for (int k = 0; k < TAPS; k++) {\n"
    "    int xx = clamp(b + k, 0, w - 1);\n"
    "    acc += WSEL(hwt, ph, k) * texelFetch(src, ivec2(xx, p.y), 0).xy;\n"
    "  }\n"
    "  o = clamp(floor(acc + 0.5), 0.0, el_max);\n"
    "}\n";

static const char *vs_src =
    "#version 320 es\n"
    "void main() {\n"
    "  vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
    "  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";

static const char *fs_chroma =
    GLSL_HEAD GLSL_WORD
    "uniform sampler2D bl;\n"
    "uniform sampler2D elc;\n"
    "uniform float piv[27];\n"
    "uniform int npiv[3];\n"
    "uniform vec4 mmr[96];\n"
    "uniform float mmrc[16];\n"
    "uniform bool cpoly[2];\n"
    "uniform bool use_el;\n"
    "uniform float inv_bl;\n"
    "out uvec2 o;\n"
    "float mmr_eval(int b, float c0, vec3 sig) {\n"
    "  vec4 xx = vec4(sig.x * sig.y, sig.x * sig.z, sig.y * sig.z, sig.x * sig.y * sig.z);\n"
    "  vec3 x2 = sig * sig; vec4 xx2 = xx * xx;\n"
    "  return c0\n"
    "       + dot(mmr[b].xyz, sig) + dot(mmr[b + 1], xx)\n"
    "       + dot(mmr[b + 2].xyz, x2) + dot(mmr[b + 3], xx2)\n"
    "       + dot(mmr[b + 4].xyz, x2 * sig) + dot(mmr[b + 5], xx2 * xx);\n"
    "}\n"
    "float predict_mmr(int cc, float s, vec3 sig) {\n"
    "  int c = cc + 1, n = npiv[c], k = 0;\n"
    "  if (n == 2)\n"
    "    return mmr_eval(cc * 48, mmrc[cc * 8], sig);\n"
    "  s = clamp(s, piv[c * 9], piv[c * 9 + n - 1]);\n"
    "  for (int i = 1; i < 8; i++)\n"
    "    k += (i < n - 1 && s >= piv[c * 9 + i]) ? 1 : 0;\n"
    "  return mmr_eval((cc * 8 + k) * 6, mmrc[cc * 8 + k], sig);\n"
    "}\n"
    "void main() {\n"
    "  ivec2 c = ivec2(gl_FragCoord.xy);\n"
    "  ivec2 p = c * 2;\n"
    "  int W = textureSize(bl, 0).x;\n"
    "  uint w0 = word_at(bl, p);\n"
    "  uint cb = cb_of(w0), cr = cr_of(w0);\n"
    "  float vcb, vcr;\n"
    "  if (cpoly[0] && cpoly[1]) {\n"
    "    vcb = lut_at(4, cb);\n"
    "    vcr = lut_at(5, cr);\n"
    "  } else {\n"
    "    int xl = max(p.x - 1, 0), xr = min(p.x + 1, W - 1);\n"
    "    uint h0 = (y_of(word_at(bl, ivec2(xl, p.y))) + 2u * y_of(w0) +\n"
    "               y_of(word_at(bl, ivec2(xr, p.y))) + 2u) >> 2;\n"
    "    uint h1 = (y_of(word_at(bl, ivec2(xl, p.y + 1))) + 2u * y_of(word_at(bl, ivec2(p.x, p.y + 1))) +\n"
    "               y_of(word_at(bl, ivec2(xr, p.y + 1))) + 2u) >> 2;\n"
    "    vec3 sig = vec3(float(min((h0 + h1 + 1u) >> 1, 1023u)), float(cb), float(cr)) * inv_bl;\n"
    "    vcb = cpoly[0] ? lut_at(4, cb) : predict_mmr(0, sig.y, sig);\n"
    "    vcr = cpoly[1] ? lut_at(5, cr) : predict_mmr(1, sig.z, sig);\n"
    "  }\n"
    "  if (use_el) {\n"
    "    vec2 el = texelFetch(elc, c, 0).xy;\n"
    "    vcb += lut_at(2, uint(el.x));\n"
    "    vcr += lut_at(3, uint(el.y));\n"
    "  }\n"
    "  if (pattern != 0)\n"
    "    vcb = vcr = 0.5;\n"
    "  o = uvec2(out10d(vcb, dth(c, ivec2(17, 31))), out10d(vcr, dth(c, ivec2(41, 7))));\n"
    "}\n";

#define FELGPU_OVERLAY_LINES 3
#define FELGPU_OVERLAY_LEN 40
#define STR_(x) #x
#define STR(x) STR_(x)

#define GLSL_OVERLAY \
    "uniform bool ovl_on;\n" \
    "uniform int ovl_s;\n" \
    "uniform ivec4 ovl_box0;\n" \
    "uniform ivec4 ovl_box1;\n" \
    "uniform int ovl_len[2 * " STR(FELGPU_OVERLAY_LINES) "];\n" \
    "uniform int txt[2 * " STR(FELGPU_OVERLAY_LINES) " * " STR(FELGPU_OVERLAY_LEN) "];\n" \
    "uniform uvec2 font[32];\n" \
    "void overlay(ivec2 p, int W, inout uint y, inout uvec2 c) {\n" \
    "  bool right = p.x >= W / 2;\n" \
    "  ivec4 b = right ? ovl_box1 : ovl_box0;\n" \
    "  if (p.x < b.x || p.x >= b.z || p.y < b.y || p.y >= b.w)\n" \
    "    return;\n" \
    "  int side = right ? 1 : 0, s = ovl_s;\n" \
    "  ivec2 q = p - ivec2(side * (W / 2) + 16 * s, 8 * s);\n" \
    "  int yi = 64 + (int(y) - 64) * 3 / 10;\n" \
    "  int cb = 512 + (int(c.x) - 512) * 3 / 10, cr = 512 + (int(c.y) - 512) * 3 / 10;\n" \
    "  if (q.x >= 0 && q.y >= 0) {\n" \
    "    int line = q.y / (10 * s), col = q.x / (6 * s);\n" \
    "    int gx = (q.x - col * 6 * s) / s, gy = (q.y - line * 10 * s) / s;\n" \
    "    int l = side * " STR(FELGPU_OVERLAY_LINES) " + line;\n" \
    "    if (col < ovl_len[l] && gx < 5 && gy < 7) {\n" \
    "      uvec2 g = font[txt[l * " STR(FELGPU_OVERLAY_LEN) " + col]];\n" \
    "      uint bits = gy < 4 ? g.x >> uint(5 * (3 - gy)) : g.y >> uint(5 * (6 - gy));\n" \
    "      if (((bits >> uint(4 - gx)) & 1u) != 0u) {\n" \
    "        yi = 560; cb = 512; cr = 512;\n" \
    "      }\n" \
    "    }\n" \
    "  }\n" \
    "  y = uint(yi);\n" \
    "  c = uvec2(uint(cb), uint(cr));\n" \
    "}\n"

static const char *fs_luma =
    GLSL_HEAD GLSL_WORD GLSL_OVERLAY
    "uniform sampler2D bl;\n"
    "uniform sampler2D ely;\n"
    "uniform usampler2D chroma;\n"
    "uniform bool use_el;\n"
    "uniform int el_split;\n"
    "out vec4 o;\n"
    "void main() {\n"
    "  ivec2 p = ivec2(gl_FragCoord.xy);\n"
    "  uint w = word_at(bl, p);\n"
    "  uvec2 c = texelFetch(chroma, p >> 1, 0).xy;\n"
    "  float v = lut_at(0, y_of(w));\n"
    "  if (p.x >= el_split) {\n"
    "    v = float(y_of(w)) / 1024.0;\n"
    "    uint wc = word_at(bl, (p >> 1) << 1);\n"
    "    c = pattern != 0 ? uvec2(512u) : uvec2(cb_of(wc), cr_of(wc));\n"
    "  } else if (use_el) {\n"
    "    v += lut_at(1, uint(texelFetch(ely, p, 0).x));\n"
    "  }\n"
    "  if (pattern != 0)\n"
    "    v = pattern_v(p, textureSize(bl, 0));\n"
    "  uint y = out10d(v, dth(p, ivec2(0)));\n"
    "  if (ovl_on)\n"
    "    overlay(p, textureSize(bl, 0).x, y, c);\n"
    "  o = pack_px(y, c.x, c.y);\n"
    "}\n";

static GLuint compile_shader(GLenum type, const char *src, const char *name) {
    char full_src[16384];
    snprintf(full_src, sizeof(full_src), "#version 320 es\n#define TAPS 6\n%s", src);
    const char *p = full_src;
    GLuint s = glCreateShader_ptr(type);
    glShaderSource_ptr(s, 1, &p, NULL);
    glCompileShader_ptr(s);
    GLint ok = 0;
    glGetShaderiv_ptr(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096] = {0};
        glGetShaderInfoLog_ptr(s, sizeof(log), NULL, log);
        fprintf(stderr, "[-] Shader compile FAILED [%s]:\n%s\n", name, log);
        return 0;
    }
    printf("[+] Shader compile SUCCESS: %s\n", name);
    return s;
}

static GLuint link_prog(GLuint vs, GLuint fs, const char *name) {
    GLuint p = glCreateProgram_ptr();
    glAttachShader_ptr(p, vs);
    glAttachShader_ptr(p, fs);
    glLinkProgram_ptr(p);
    GLint ok = 0;
    glGetProgramiv_ptr(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096] = {0};
        glGetProgramInfoLog_ptr(p, sizeof(log), NULL, log);
        fprintf(stderr, "[-] Program link FAILED [%s]:\n%s\n", name, log);
        return 0;
    }
    printf("[+] Program link SUCCESS: %s\n", name);
    return p;
}

static int check_uniform(GLuint p, const char *name, const char *prog_name) {
    GLint loc = glGetUniformLocation_ptr(p, name);
    if (loc < 0) {
        fprintf(stderr, "    [-] UNIFORM MISSING: %s in %s (loc=%d)\n", name, prog_name, loc);
        return 0;
    }
    printf("    [+] Uniform '%s' resolved to loc %d in %s\n", name, loc, prog_name);
    return 1;
}

int main() {
    printf("=================================================================\n");
    printf("  Dolby Vision GLES 3.2 Pipeline: Shader Compile & Link Test\n");
    printf("=================================================================\n");

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = "ShaderTestWC";
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowA("ShaderTestWC", "ShaderTest", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, NULL, NULL, wc.hInstance, NULL);
    HDC hdc = GetDC(hwnd);
    PIXELFORMATDESCRIPTOR pfd = {sizeof(pfd), 1, PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER, PFD_TYPE_RGBA, 32};
    int pf = ChoosePixelFormat(hdc, &pfd);
    SetPixelFormat(hdc, pf, &pfd);
    HGLRC hglrc = wglCreateContext(hdc);
    if (!hglrc) {
        fprintf(stderr, "[-] Fatal: Failed to create host GL context\n");
        return 1;
    }
    wglMakeCurrent(hdc, hglrc);

    glCreateShader_ptr = (PFNGLCREATESHADERPROC)wglGetProcAddress("glCreateShader");
    glShaderSource_ptr = (PFNGLSHADERSOURCEPROC)wglGetProcAddress("glShaderSource");
    glCompileShader_ptr = (PFNGLCOMPILESHADERPROC)wglGetProcAddress("glCompileShader");
    glGetShaderiv_ptr = (PFNGLGETSHADERIVPROC)wglGetProcAddress("glGetShaderiv");
    glGetShaderInfoLog_ptr = (PFNGLGETSHADERINFOLOGPROC)wglGetProcAddress("glGetShaderInfoLog");
    glCreateProgram_ptr = (PFNGLCREATEPROGRAMPROC)wglGetProcAddress("glCreateProgram");
    glAttachShader_ptr = (PFNGLATTACHSHADERPROC)wglGetProcAddress("glAttachShader");
    glLinkProgram_ptr = (PFNGLLINKPROGRAMPROC)wglGetProcAddress("glLinkProgram");
    glGetProgramiv_ptr = (PFNGLGETPROGRAMIVPROC)wglGetProcAddress("glGetProgramiv");
    glGetProgramInfoLog_ptr = (PFNGLGETPROGRAMINFOLOGPROC)wglGetProcAddress("glGetProgramInfoLog");
    glGetUniformLocation_ptr = (PFNGLGETUNIFORMLOCATIONPROC)wglGetProcAddress("glGetUniformLocation");
    glDeleteShader_ptr = (PFNGLDELETESHADERPROC)wglGetProcAddress("glDeleteShader");
    glDeleteProgram_ptr = (PFNGLDELETEPROGRAMPROC)wglGetProcAddress("glDeleteProgram");

    if (!glCreateShader_ptr || !glLinkProgram_ptr || !glGetUniformLocation_ptr) {
        fprintf(stderr, "[-] Fatal: GL 3.2+ function pointers not available\n");
        return 1;
    }

    printf("[+] Host Context: %s | %s\n", (const char*)glGetString(GL_RENDERER), (const char*)glGetString(GL_VERSION));

    GLuint vs = compile_shader(GL_VERTEX_SHADER, "void main() { gl_Position = vec4(0.0); }", "vs_src");
    if (!vs) return 1;

    int all_ok = 1;

    // 1. fs_el_vert1
    GLuint fs_v1 = compile_shader(GL_FRAGMENT_SHADER, fs_el_vert1, "fs_el_vert1");
    GLuint p_v1 = link_prog(vs, fs_v1, "p_vert1 (Luma Vertical)");
    all_ok &= check_uniform(p_v1, "wt", "p_vert1");
    all_ok &= check_uniform(p_v1, "base", "p_vert1");
    all_ok &= check_uniform(p_v1, "is_packed", "p_vert1");
    all_ok &= check_uniform(p_v1, "src_packed", "p_vert1");
    all_ok &= check_uniform(p_v1, "src_planar", "p_vert1");

    // 2. fs_el_vert2
    GLuint fs_v2 = compile_shader(GL_FRAGMENT_SHADER, fs_el_vert2, "fs_el_vert2");
    GLuint p_v2 = link_prog(vs, fs_v2, "p_vert2 (Chroma Vertical)");
    all_ok &= check_uniform(p_v2, "wt", "p_vert2");
    all_ok &= check_uniform(p_v2, "base", "p_vert2");
    all_ok &= check_uniform(p_v2, "is_packed", "p_vert2");
    all_ok &= check_uniform(p_v2, "src_packed", "p_vert2");
    all_ok &= check_uniform(p_v2, "src0", "p_vert2");
    all_ok &= check_uniform(p_v2, "src1", "p_vert2");

    // 3. fs_el_horiz1
    GLuint fs_h1 = compile_shader(GL_FRAGMENT_SHADER, fs_el_horiz1, "fs_el_horiz1");
    GLuint p_h1 = link_prog(vs, fs_h1, "p_horiz_y (Luma Horizontal)");
    all_ok &= check_uniform(p_h1, "src", "p_horiz_y");
    all_ok &= check_uniform(p_h1, "hwt", "p_horiz_y");
    all_ok &= check_uniform(p_h1, "hbase", "p_horiz_y");
    all_ok &= check_uniform(p_h1, "el_max", "p_horiz_y");

    // 4. fs_el_horiz2
    GLuint fs_h2 = compile_shader(GL_FRAGMENT_SHADER, fs_el_horiz2, "fs_el_horiz2");
    GLuint p_h2 = link_prog(vs, fs_h2, "p_horiz_c (Chroma Horizontal)");
    all_ok &= check_uniform(p_h2, "src", "p_horiz_c");
    all_ok &= check_uniform(p_h2, "hwt", "p_horiz_c");
    all_ok &= check_uniform(p_h2, "hbase", "p_horiz_c");
    all_ok &= check_uniform(p_h2, "el_max", "p_horiz_c");

    // 5. fs_chroma
    GLuint fs_c = compile_shader(GL_FRAGMENT_SHADER, fs_chroma, "fs_chroma");
    GLuint p_c = link_prog(vs, fs_c, "p_chroma (Chroma MMR/Poly Composition)");
    all_ok &= check_uniform(p_c, "bl", "p_chroma");
    all_ok &= check_uniform(p_c, "elc", "p_chroma");
    all_ok &= check_uniform(p_c, "piv", "p_chroma");
    all_ok &= check_uniform(p_c, "npiv", "p_chroma");
    all_ok &= check_uniform(p_c, "mmr", "p_chroma");
    all_ok &= check_uniform(p_c, "mmrc", "p_chroma");
    all_ok &= check_uniform(p_c, "cpoly", "p_chroma");
    all_ok &= check_uniform(p_c, "use_el", "p_chroma");
    all_ok &= check_uniform(p_c, "inv_bl", "p_chroma");
    all_ok &= check_uniform(p_c, "lut", "p_chroma");
    all_ok &= check_uniform(p_c, "dither", "p_chroma");
    all_ok &= check_uniform(p_c, "dither_split", "p_chroma");

    // 6. fs_luma
    GLuint fs_l = compile_shader(GL_FRAGMENT_SHADER, fs_luma, "fs_luma");
    GLuint p_l = link_prog(vs, fs_l, "p_luma (4K Final Luma + DMA-buf Pack)");
    all_ok &= check_uniform(p_l, "bl", "p_luma");
    all_ok &= check_uniform(p_l, "ely", "p_luma");
    all_ok &= check_uniform(p_l, "chroma", "p_luma");
    all_ok &= check_uniform(p_l, "use_el", "p_luma");
    all_ok &= check_uniform(p_l, "lut", "p_luma");
    all_ok &= check_uniform(p_l, "dither", "p_luma");
    all_ok &= check_uniform(p_l, "dither_split", "p_luma");
    all_ok &= check_uniform(p_l, "el_split", "p_luma");

    printf("=================================================================\n");
    if (all_ok) {
        printf(">>> TEST 1 RESULT: ALL SHADERS AND UNIFORMS PASSED (PASS) <<<\n");
    } else {
        printf(">>> TEST 1 RESULT: SHADER TEST FAILED (FAIL) <<<\n");
    }
    printf("=================================================================\n");

    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(hglrc);
    ReleaseDC(hwnd, hdc);
    DestroyWindow(hwnd);
    return all_ok ? 0 : 1;
}
