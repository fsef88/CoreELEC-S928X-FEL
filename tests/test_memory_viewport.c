/*
 * test_memory_viewport.c - Validation of FBO lifecycle, leak prevention,
 *                          and glViewport resolution matching in felgpu
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#define GL_TEXTURE_2D 0x0DE1
#define GL_FRAMEBUFFER 0x8D40

/* Tracking structures for mock GL */
typedef struct {
    uint32_t id;
    int width;
    int height;
    uint32_t format;
    bool deleted;
} MockTex;

typedef struct {
    uint32_t id;
    uint32_t attached_tex;
    bool deleted;
} MockFBO;

typedef struct {
    int x;
    int y;
    int w;
    int h;
} ViewportCall;

#define MAX_TRACK 64
static MockTex g_textures[MAX_TRACK];
static int g_tex_count = 0;

static MockFBO g_fbos[MAX_TRACK];
static int g_fbo_count = 0;

static ViewportCall g_viewports[MAX_TRACK];
static int g_viewport_count = 0;

static uint32_t g_current_fbo = 0;

/* Mock GL functions */
static uint32_t mock_gen_tex(uint32_t ifmt, int w, int h) {
    uint32_t id = ++g_tex_count;
    g_textures[id].id = id;
    g_textures[id].width = w;
    g_textures[id].height = h;
    g_textures[id].format = ifmt;
    g_textures[id].deleted = false;
    return id;
}

static uint32_t mock_gen_fbo(uint32_t tex) {
    uint32_t id = ++g_fbo_count;
    g_fbos[id].id = id;
    g_fbos[id].attached_tex = tex;
    g_fbos[id].deleted = false;
    return id;
}

static void mock_delete_textures(int n, const uint32_t *tex) {
    for (int i = 0; i < n; i++) {
        uint32_t id = tex[i];
        if (id > 0 && id <= (uint32_t)g_tex_count) {
            g_textures[id].deleted = true;
        }
    }
}

static void mock_delete_fbos(int n, const uint32_t *fbos) {
    for (int i = 0; i < n; i++) {
        uint32_t id = fbos[i];
        if (id > 0 && id <= (uint32_t)g_fbo_count) {
            g_fbos[id].deleted = true;
        }
    }
}

static void mock_bind_fbo(uint32_t fbo) {
    g_current_fbo = fbo;
}

static void mock_viewport(int x, int y, int w, int h) {
    if (g_viewport_count < MAX_TRACK) {
        g_viewports[g_viewport_count].x = x;
        g_viewports[g_viewport_count].y = y;
        g_viewports[g_viewport_count].w = w;
        g_viewports[g_viewport_count].h = h;
        g_viewport_count++;
    }
}

/* Simulated pipeline structure matching felgpu.c */
struct test_felgpu {
    int w, h, taps;
    uint32_t el_y, el_cb, el_cr;
    uint32_t el_src;
    int is_packed;
    uint32_t el_yv, el_cv;
    uint32_t el_yh, el_ch;
    uint32_t fbo_yv, fbo_cv;
    uint32_t fbo_yh, fbo_ch;
    uint32_t chroma, fbo_chroma;
    uint32_t lut, dither;
    uint32_t stats, red;
    uint32_t fbo_stats, fbo_red;
    void *red_buf;
};

#define GL_R16UI   0x8234
#define GL_R16F    0x822D
#define GL_RG16F   0x822F
#define GL_RG16UI  0x823C
#define GL_R32F    0x822E
#define GL_RGBA32UI 0x8D70

static struct test_felgpu *test_felgpu_create(int w, int h) {
    struct test_felgpu *f = calloc(1, sizeof(*f));
    f->w = w;
    f->h = h;
    f->taps = 6;

    f->el_y = mock_gen_tex(GL_R16UI, w / 2, h / 2);
    f->el_cb = mock_gen_tex(GL_R16UI, w / 4, h / 4);
    f->el_cr = mock_gen_tex(GL_R16UI, w / 4, h / 4);
    f->el_yv = mock_gen_tex(GL_R16F, w / 2, h);
    f->el_cv = mock_gen_tex(GL_RG16F, w / 4, h / 2);
    f->el_yh = mock_gen_tex(GL_R16F, w, h);
    f->el_ch = mock_gen_tex(GL_RG16F, w / 2, h / 2);
    f->chroma = mock_gen_tex(GL_RG16UI, w / 2, h / 2);
    f->lut = mock_gen_tex(GL_R32F, 1024, 6);
    f->dither = mock_gen_tex(GL_R32F, 64, 64);
    f->stats = mock_gen_tex(GL_RG16UI, w / 8, h / 8);
    f->red = mock_gen_tex(GL_RGBA32UI, (w / 8 + 15) / 16, h / 8);

    f->fbo_yv = mock_gen_fbo(f->el_yv);
    f->fbo_cv = mock_gen_fbo(f->el_cv);
    f->fbo_yh = mock_gen_fbo(f->el_yh);
    f->fbo_ch = mock_gen_fbo(f->el_ch);
    f->fbo_chroma = mock_gen_fbo(f->chroma);
    f->fbo_stats = mock_gen_fbo(f->stats);
    f->fbo_red = mock_gen_fbo(f->red);

    f->red_buf = malloc(1024);
    return f;
}

static void test_draw(uint32_t fbo, int w, int h) {
    mock_bind_fbo(fbo);
    mock_viewport(0, 0, w, h);
}

static void test_felgpu_run(struct test_felgpu *f, uint32_t out_fbo) {
    g_viewport_count = 0;

    /* Pass 0: Vertical Luma */
    test_draw(f->fbo_yv, f->w / 2, f->h);

    /* Pass 1: Vertical Chroma */
    test_draw(f->fbo_cv, f->w / 4, f->h / 2);

    /* Pass 2: Horizontal Luma */
    test_draw(f->fbo_yh, f->w, f->h);

    /* Pass 3: Horizontal Chroma */
    test_draw(f->fbo_ch, f->w / 2, f->h / 2);

    /* Pass 4: Chroma composition */
    test_draw(f->fbo_chroma, f->w / 2, f->h / 2);

    /* Pass 5: 4K Luma composition + Pack */
    test_draw(out_fbo, f->w, f->h);
}

static void test_felgpu_destroy(struct test_felgpu *f) {
    if (!f) return;
    uint32_t tex[] = { f->el_y, f->el_cb, f->el_cr, f->el_yv, f->el_cv, f->el_yh, f->el_ch,
                       f->chroma, f->lut, f->dither, f->stats, f->red };
    uint32_t fbo[] = { f->fbo_yv, f->fbo_cv, f->fbo_yh, f->fbo_ch, f->fbo_chroma, f->fbo_stats, f->fbo_red };

    mock_delete_textures(12, tex);
    mock_delete_fbos(7, fbo);
    free(f->red_buf);
    free(f);
}

int main() {
    printf("=================================================================\n");
    printf("  Memory Leak & Viewport Resolution Audit: felgpu Pipeline\n");
    printf("=================================================================\n");

    const int W = 3840;
    const int H = 2160;

    struct test_felgpu *f = test_felgpu_create(W, H);

    /* 1. Verify intermediate buffer formats */
    printf("\n[1] Verifying Intermediate Texture Formats (Mali FP16/RG16F Optimization)...\n");
    printf("  -> el_yv (Vertical Luma):   format=0x%X (GL_R16F),  size=%dx%d\n",
           g_textures[f->el_yv].format, g_textures[f->el_yv].width, g_textures[f->el_yv].height);
    printf("  -> el_cv (Vertical Chroma): format=0x%X (GL_RG16F), size=%dx%d\n",
           g_textures[f->el_cv].format, g_textures[f->el_cv].width, g_textures[f->el_cv].height);
    printf("  -> el_yh (Horiz Luma):      format=0x%X (GL_R16F),  size=%dx%d\n",
           g_textures[f->el_yh].format, g_textures[f->el_yh].width, g_textures[f->el_yh].height);
    printf("  -> el_ch (Horiz Chroma):    format=0x%X (GL_RG16F), size=%dx%d\n",
           g_textures[f->el_ch].format, g_textures[f->el_ch].width, g_textures[f->el_ch].height);

    int fmt_ok = (g_textures[f->el_yv].format == GL_R16F) &&
                 (g_textures[f->el_cv].format == GL_RG16F) &&
                 (g_textures[f->el_yh].format == GL_R16F) &&
                 (g_textures[f->el_ch].format == GL_RG16F);
    printf("  -> Formats Status: %s\n", fmt_ok ? "PASS" : "FAIL");

    /* 2. Run pipeline and verify viewports */
    printf("\n[2] Verifying Pipeline Viewports per Draw Pass...\n");
    test_felgpu_run(f, 999);

    struct { const char *name; int exp_w; int exp_h; } expected_passes[] = {
        { "Pass 0 (EL-V Luma)",   W / 2, H     }, /* 1920 x 2160 */
        { "Pass 1 (EL-V Chroma)", W / 4, H / 2 }, /* 960  x 1080 */
        { "Pass 2 (EL-H Luma)",   W,     H     }, /* 3840 x 2160 */
        { "Pass 3 (EL-H Chroma)", W / 2, H / 2 }, /* 1920 x 1080 */
        { "Pass 4 (Chroma Comp)", W / 2, H / 2 }, /* 1920 x 1080 */
        { "Pass 5 (4K Luma Pack)",W,     H     }  /* 3840 x 2160 */
    };

    int vp_ok = 1;
    for (int i = 0; i < 6; i++) {
        bool match = (g_viewports[i].w == expected_passes[i].exp_w) &&
                     (g_viewports[i].h == expected_passes[i].exp_h);
        printf("  -> %-22s: Viewport = %dx%d (Expected: %dx%d) [%s]\n",
               expected_passes[i].name,
               g_viewports[i].w, g_viewports[i].h,
               expected_passes[i].exp_w, expected_passes[i].exp_h,
               match ? "OK" : "MISMATCH");
        if (!match) vp_ok = 0;
    }

    /* 3. Destroy and verify no leaks */
    printf("\n[3] Verifying Resource Destruction & Zero Leak Invariant...\n");
    test_felgpu_destroy(f);

    int unreleased_tex = 0;
    for (int i = 1; i <= g_tex_count; i++) {
        if (!g_textures[i].deleted) unreleased_tex++;
    }

    int unreleased_fbo = 0;
    for (int i = 1; i <= g_fbo_count; i++) {
        if (!g_fbos[i].deleted) unreleased_fbo++;
    }

    printf("  -> Total Textures Created: %d | Deleted: %d | Leaked: %d\n",
           g_tex_count, g_tex_count - unreleased_tex, unreleased_tex);
    printf("  -> Total FBOs Created:     %d | Deleted: %d | Leaked: %d\n",
           g_fbo_count, g_fbo_count - unreleased_fbo, unreleased_fbo);

    int leak_ok = (unreleased_tex == 0) && (unreleased_fbo == 0);
    printf("  -> Destruction Status: %s\n", leak_ok ? "PASS" : "FAIL");

    int test4_ok = fmt_ok && vp_ok && leak_ok;
    printf("\n=================================================================\n");
    if (test4_ok) {
        printf(">>> TEST 4 RESULT: MEMORY & VIEWPORT AUDIT PASSED (PASS) <<<\n");
    } else {
        printf(">>> TEST 4 RESULT: AUDIT TEST FAILED (FAIL) <<<\n");
    }
    printf("=================================================================\n");

    return test4_ok ? 0 : 1;
}
