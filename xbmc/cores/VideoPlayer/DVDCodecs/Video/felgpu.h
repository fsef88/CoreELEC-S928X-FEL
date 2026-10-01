/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * felgpu - Dolby Vision FEL composition on OpenGL ES 3.2
 *
 * Composes a decoded base layer (dvfel linear 10-bit 4:4:4 buffer, see
 * dvfel_uapi.h) with the enhancement layer and the RPU mapping into the
 * same buffer format:
 *
 *   VDR = predict(BL) + NLQ^-1(upsample(EL))
 */
#ifndef _FELGPU_H
#define _FELGPU_H

#include <stdint.h>
#include <GLES3/gl32.h>

#define FELGPU_MAX_PIECES       8
#define FELGPU_OVERLAY_LINES    3
#define FELGPU_OVERLAY_LEN      40

struct felgpu;

struct felgpu_meta {
    int use_el;
    int bl_bits;
    int el_bits;
    int npiv[3];
    int pivots[3][9];
    int mapping[3][FELGPU_MAX_PIECES];
    int mmr_order[3][FELGPU_MAX_PIECES];
    double poly[3][FELGPU_MAX_PIECES][3];
    double mmr[3][FELGPU_MAX_PIECES][3][7];            /* [order][term] */
    double mmr_const[3][FELGPU_MAX_PIECES];
    int nlq_offset[3];                                 /* EL codewords */
    double nlq_slope[3], nlq_thresh[3], nlq_max[3];    /* normalized */
    int l1_max_pq, l1_avg_pq;                          /* RPU level 1, 12-bit PQ; -1 if absent */
    int l5[4];                                         /* RPU level 5 active area offsets left,
                                                          right, top, bottom in pixels; -1 if absent */
};

/*
 * demo mode measurement: the L1 of the composed picture and of the base layer
 * in the active area, from every 8th pixel - max and average of
 * max(R', G', B') in PQ, as cd/m^2: nits[0] composed max, [1] composed avg,
 * [2] BL max, [3] BL avg.
 */
enum { FELGPU_AREA_FULL, FELGPU_AREA_L5, FELGPU_AREA_DETECTED };
struct felgpu_stats {
    float nits[4];
    int x0, y0, x1, y1;
    int source;                                        /* FELGPU_AREA_* */
};

struct felgpu *felgpu_create(int w, int h, int taps);
void felgpu_destroy(struct felgpu *f);

/* EL planes, yuv420p10 (16-bit samples), strides in samples */
void felgpu_set_el(struct felgpu *f, const uint16_t *y, int y_stride,
                   const uint16_t *cb, const uint16_t *cr, int c_stride);
/*
 * hardware decoded EL: the EL picture as a (w/2 x h/2) texture of packed
 * words laid out like the BL (see dvfel_uapi.h), instead of felgpu_set_el
 */
void felgpu_set_el_tex(struct felgpu *f, GLuint el);
void felgpu_set_meta(struct felgpu *f, const struct felgpu_meta *m);

/* compose bl_tex (dma-buf import, RGBA8) into out_fbo (dma-buf, RGBA8) */
void felgpu_run(struct felgpu *f, GLuint bl_tex, GLuint out_fbo);

/* 0: normal, 1: dither everywhere, 2: left half dithered (split demo) */
void felgpu_set_dither(struct felgpu *f, int mode);
/* 1: EL applied to the left half only */
void felgpu_set_demo(struct felgpu *f, int on);

/* active area of next frames: offsets from left, right, top, bottom (RPU L5) */
void felgpu_set_active_area(struct felgpu *f, const int offsets[4]);
/* 0 if the last felgpu_run did not measure (demo off) */
int felgpu_get_stats(struct felgpu *f, struct felgpu_stats *s);

/* 1: replaces video with test pattern */
void felgpu_set_pattern(struct felgpu *f, int pattern);

/* demo mode text: lines 0-2 on left half, 3-5 on right half */
void felgpu_set_overlay(struct felgpu *f, int line, const char *text);

void felgpu_set_profile(struct felgpu *f, int on);
void felgpu_print_profile(struct felgpu *f);

#endif
