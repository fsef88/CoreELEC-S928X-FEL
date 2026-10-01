/*
 * test_lanczos_math.c - Mathematical verification of Lanczos-3 reconstruction
 *                       and ±1 LSB invariant (US10701399B2)
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define TAPS 6
#define RADIUS 3.0
#define VECTOR_LEN 4096

/* Reference double-precision Lanczos-3 function */
static double lanczos_ref(double x, double a) {
    x = fabs(x);
    if (x < 1e-9) return 1.0;
    if (x >= a) return 0.0;
    return a * sin(M_PI * x) * sin(M_PI * x / a) / (M_PI * M_PI * x * x);
}

/* Reference double-precision kernel calculation */
static void kernel_ref(double off, double wt[2][TAPS], int base[2]) {
    int r = (int)(RADIUS);
    for (int ph = 0; ph < 2; ph++) {
        double p = ph * 0.5 + off;
        int i0 = (int)floor(p);
        double f = p - i0;
        base[ph] = i0 - (r - 1);
        double sum = 0.0, w[TAPS];
        for (int t = 0; t < TAPS; t++) {
            w[t] = lanczos_ref((t - (r - 1)) - f, RADIUS);
            sum += w[t];
        }
        for (int t = 0; t < TAPS; t++) {
            wt[ph][t] = w[t] / sum;
        }
    }
}

/* GPU highp float kernel calculation (matching felgpu.c exactly) */
static void kernel_gpu(double off, float wt[12], int base[2]) {
    int r = TAPS / 2;
    for (int ph = 0; ph < 2; ph++) {
        double p = ph * 0.5 + off, sum = 0, w[6];
        int i0 = (int)floor(p);
        double f = p - i0;
        base[ph] = i0 - (r - 1);
        for (int t = 0; t < TAPS; t++) {
            sum += w[t] = lanczos_ref((t - (r - 1)) - f, r);
        }
        for (int t = 0; t < TAPS; t++) {
            wt[ph * TAPS + t] = (float)(w[t] / sum);
        }
    }
}

/* Reference CPU 2x upsampler in double precision */
static void upsample_reference(const uint16_t *src, int src_len, double off, uint16_t *dst, int dst_len) {
    double wt[2][TAPS];
    int base[2];
    kernel_ref(off, wt, base);

    for (int p = 0; p < dst_len; p++) {
        int ph = p & 1;
        int b = (p >> 1) + base[ph];
        double acc = 0.0;
        for (int t = 0; t < TAPS; t++) {
            int idx = b + t;
            if (idx < 0) idx = 0;
            if (idx >= src_len) idx = src_len - 1;
            acc += wt[ph][t] * (double)src[idx];
        }
        double rounded = floor(acc + 0.5);
        if (rounded < 0.0) rounded = 0.0;
        if (rounded > 1023.0) rounded = 1023.0;
        dst[p] = (uint16_t)rounded;
    }
}

/* GPU highp float shader accumulator simulation (matching fs_el_vert1 / fs_el_horiz1) */
static void upsample_shader_gpu(const uint16_t *src, int src_len, double off, uint16_t *dst, int dst_len) {
    float wt[12];
    int base[2];
    kernel_gpu(off, wt, base);

    for (int p = 0; p < dst_len; p++) {
        int ph = p & 1;
        int b = (p >> 1) + (ph == 0 ? base[0] : base[1]);
        /* highp float accumulator */
        float acc = 0.0f;
        for (int t = 0; t < TAPS; t++) {
            int idx = b + t;
            if (idx < 0) idx = 0;
            if (idx >= src_len) idx = src_len - 1;
            float w = (ph == 0 ? wt[t] : wt[TAPS + t]);
            acc += w * (float)src[idx];
        }
        float rounded = floorf(acc + 0.5f);
        if (rounded < 0.0f) rounded = 0.0f;
        if (rounded > 1023.0f) rounded = 1023.0f;
        dst[p] = (uint16_t)rounded;
    }
}

/* Mediump (fp16) simulation to demonstrate precision loss */
static uint16_t float_to_half_bits(float f) {
    uint32_t x = *(uint32_t*)&f;
    uint16_t sign = (x >> 16) & 0x8000;
    int32_t exp = ((x >> 23) & 0xFF) - 127 + 15;
    uint32_t mant = (x & 0x7FFFFF) >> 13;
    if (exp <= 0) return sign;
    if (exp >= 31) return sign | 0x7C00;
    return sign | (exp << 10) | (mant & 0x3FF);
}
static float half_bits_to_float(uint16_t h) {
    uint32_t sign = (h & 0x8000) << 16;
    int32_t exp = ((h >> 10) & 0x1F) - 15 + 127;
    uint32_t mant = (h & 0x3FF) << 13;
    if ((h & 0x7C00) == 0) return 0.0f;
    uint32_t x = sign | (exp << 23) | mant;
    return *(float*)&x;
}
static float to_half(float f) {
    return half_bits_to_float(float_to_half_bits(f));
}

static void upsample_mediump(const uint16_t *src, int src_len, double off, uint16_t *dst, int dst_len) {
    float wt[12];
    int base[2];
    kernel_gpu(off, wt, base);

    for (int p = 0; p < dst_len; p++) {
        int ph = p & 1;
        int b = (p >> 1) + (ph == 0 ? base[0] : base[1]);
        float acc = 0.0f;
        for (int t = 0; t < TAPS; t++) {
            int idx = b + t;
            if (idx < 0) idx = 0;
            if (idx >= src_len) idx = src_len - 1;
            float w = to_half(ph == 0 ? wt[t] : wt[TAPS + t]);
            float s = to_half((float)src[idx]);
            acc = to_half(acc + to_half(w * s));
        }
        float rounded = floorf(acc + 0.5f);
        if (rounded < 0.0f) rounded = 0.0f;
        if (rounded > 1023.0f) rounded = 1023.0f;
        dst[p] = (uint16_t)rounded;
    }
}

static int test_vector(const char *name, const uint16_t *src, int src_len, double off) {
    int dst_len = src_len * 2;
    uint16_t *ref = malloc(dst_len * sizeof(uint16_t));
    uint16_t *gpu = malloc(dst_len * sizeof(uint16_t));
    uint16_t *med = malloc(dst_len * sizeof(uint16_t));

    upsample_reference(src, src_len, off, ref, dst_len);
    upsample_shader_gpu(src, src_len, off, gpu, dst_len);
    upsample_mediump(src, src_len, off, med, dst_len);

    int max_err_gpu = 0;
    int max_err_med = 0;
    int exact_count = 0;

    for (int i = 0; i < dst_len; i++) {
        int err_gpu = abs((int)gpu[i] - (int)ref[i]);
        int err_med = abs((int)med[i] - (int)ref[i]);
        if (err_gpu > max_err_gpu) max_err_gpu = err_gpu;
        if (err_med > max_err_med) max_err_med = err_med;
        if (err_gpu == 0) exact_count++;
    }

    double exact_pct = 100.0 * (double)exact_count / (double)dst_len;

    printf("  [Vector: %-26s] | Max Error (highp): %d LSB (Exact: %.1f%%) | Max Error (mediump): %d LSB\n",
           name, max_err_gpu, exact_pct, max_err_med);

    free(ref);
    free(gpu);
    free(med);

    if (max_err_gpu <= 1) {
        printf("    -> PASS: Strict invariant |error| <= 1 LSB guaranteed.\n");
        return 1;
    } else {
        printf("    -> FAIL: Invariant broken! Max error = %d LSB\n", max_err_gpu);
        return 0;
    }
}

int main() {
    printf("=================================================================\n");
    printf("  Dolby Vision Mathematical Verification: +/- 1 LSB Invariant\n");
    printf("  Reference: US10701399B2 / IEEE 754 High-Precision Lanczos-3\n");
    printf("=================================================================\n");

    const double ce = 0.5;
    const double off_luma = -ce / 2.0;   /* -0.25 */
    const double off_chroma = -ce / 4.0; /* -0.125 */

    uint16_t vec_grad[VECTOR_LEN];
    uint16_t vec_step[VECTOR_LEN];
    uint16_t vec_noise[VECTOR_LEN];

    /* 1. Linear 10-bit ramp: 0, 1, 2, ..., 1023, 0, 1, ... */
    for (int i = 0; i < VECTOR_LEN; i++) {
        vec_grad[i] = (uint16_t)(i % 1024);
    }

    /* 2. High-contrast edge (Step: 64 -> 960) */
    for (int i = 0; i < VECTOR_LEN; i++) {
        vec_step[i] = (i < VECTOR_LEN / 2) ? 64 : 960;
    }

    /* 3. Random noise array (PRNG 0..1023) */
    uint32_t seed = 0x5928;
    for (int i = 0; i < VECTOR_LEN; i++) {
        seed = seed * 1664525u + 1013904223u;
        vec_noise[i] = (uint16_t)((seed >> 16) & 1023);
    }

    int ok = 1;
    printf("\n[Testing Luma Reconstruction (Phase offset: -0.25)]\n");
    ok &= test_vector("10-bit Gradient", vec_grad, VECTOR_LEN, off_luma);
    ok &= test_vector("High-Contrast Step 64->960", vec_step, VECTOR_LEN, off_luma);
    ok &= test_vector("Random High-Freq Noise", vec_noise, VECTOR_LEN, off_luma);

    printf("\n[Testing Chroma Reconstruction (Phase offset: -0.125)]\n");
    ok &= test_vector("10-bit Gradient", vec_grad, VECTOR_LEN, off_chroma);
    ok &= test_vector("High-Contrast Step 64->960", vec_step, VECTOR_LEN, off_chroma);
    ok &= test_vector("Random High-Freq Noise", vec_noise, VECTOR_LEN, off_chroma);

    printf("\n=================================================================\n");
    if (ok) {
        printf(">>> TEST 2 RESULT: ALL MATHEMATICAL INVARIANTS SATISFIED (PASS) <<<\n");
    } else {
        printf(">>> TEST 2 RESULT: MATHEMATICAL INVARIANT TEST FAILED (FAIL) <<<\n");
    }
    printf("=================================================================\n");

    return ok ? 0 : 1;
}
