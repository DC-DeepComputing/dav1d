/*
 * Copyright © 2024, VideoLAN and dav1d authors
 * Copyright © 2024, Luca Barbato
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <string.h>

#include "common/attributes.h"
#include "src/ppc/mc.h"
#include "src/tables.h"
#include "src/ppc/dav1d_types.h"

#if BITDEPTH == 8

#define blend_px(a, b, m) (((a * (64 - m) + b * m) + 32) >> 6)

typedef void (*blend_line)(uint8_t *dst, const uint8_t *tmp, const uint8_t *mask, int stride);

#define BLEND_LINES4(d0_u16, d1_u16, d2_u16, d3_u16, ab0, ab1, ab2, ab3, nm_m0, nm_m1, nm_m2, nm_m3) \
{ \
    u16x8 anm0 = vec_mule(ab0, nm_m0); \
    u16x8 anm1 = vec_mule(ab1, nm_m1); \
    u16x8 anm2 = vec_mule(ab2, nm_m2); \
    u16x8 anm3 = vec_mule(ab3, nm_m3); \
\
    u16x8 bm0 = vec_mulo(ab0, nm_m0); \
    u16x8 bm1 = vec_mulo(ab1, nm_m1); \
    u16x8 bm2 = vec_mulo(ab2, nm_m2); \
    u16x8 bm3 = vec_mulo(ab3, nm_m3); \
\
    d0_u16 = vec_add(anm0, bm0); \
    d1_u16 = vec_add(anm1, bm1); \
    d2_u16 = vec_add(anm2, bm2); \
    d3_u16 = vec_add(anm3, bm3); \
\
    d0_u16 = vec_add(d0_u16, vec_splats((uint16_t)32)); \
    d1_u16 = vec_add(d1_u16, vec_splats((uint16_t)32)); \
    d2_u16 = vec_add(d2_u16, vec_splats((uint16_t)32)); \
    d3_u16 = vec_add(d3_u16, vec_splats((uint16_t)32)); \
\
    d0_u16 = vec_sr(d0_u16, vec_splat_u16(6)); \
    d1_u16 = vec_sr(d1_u16, vec_splat_u16(6)); \
    d2_u16 = vec_sr(d2_u16, vec_splat_u16(6)); \
    d3_u16 = vec_sr(d3_u16, vec_splat_u16(6)); \
}

#define BLEND_LINES3(d0_u16, d1_u16, d2_u16, ab0, ab1, ab2, nm_m0, nm_m1, nm_2) \
{ \
    u16x8 anm0 = vec_mule(ab0, nm_m0); \
    u16x8 anm1 = vec_mule(ab1, nm_m1); \
    u16x8 anm2 = vec_mule(ab2, nm_m2); \
\
    u16x8 bm0 = vec_mulo(ab0, nm_m0); \
    u16x8 bm1 = vec_mulo(ab1, nm_m1); \
    u16x8 bm2 = vec_mulo(ab2, nm_m2); \
\
    d0_u16 = vec_add(anm0, bm0); \
    d1_u16 = vec_add(anm1, bm1); \
    d2_u16 = vec_add(anm2, bm2); \
\
    d0_u16 = vec_add(d0_u16, vec_splats((uint16_t)32)); \
    d1_u16 = vec_add(d1_u16, vec_splats((uint16_t)32)); \
    d2_u16 = vec_add(d2_u16, vec_splats((uint16_t)32)); \
\
    d0_u16 = vec_sr(d0_u16, vec_splat_u16(6)); \
    d1_u16 = vec_sr(d1_u16, vec_splat_u16(6)); \
    d2_u16 = vec_sr(d2_u16, vec_splat_u16(6)); \
}

#define BLEND_LINES2(d0_u16, d1_u16, ab0, ab1, nm_m0, nm_m1) \
{ \
    u16x8 anm0 = vec_mule(ab0, nm_m0); \
    u16x8 anm1 = vec_mule(ab1, nm_m1); \
\
    u16x8 bm0 = vec_mulo(ab0, nm_m0); \
    u16x8 bm1 = vec_mulo(ab1, nm_m1); \
\
    d0_u16 = vec_add(anm0, bm0); \
    d1_u16 = vec_add(anm1, bm1); \
\
    d0_u16 = vec_add(d0_u16, vec_splats((uint16_t)32)); \
    d1_u16 = vec_add(d1_u16, vec_splats((uint16_t)32)); \
\
    d0_u16 = vec_sr(d0_u16, vec_splat_u16(6)); \
    d1_u16 = vec_sr(d1_u16, vec_splat_u16(6)); \
}

static void blend4(uint8_t *dst, const uint8_t *tmp, const uint8_t *mask, int stride)
{
    u8x16 v64u8 = vec_splats((uint8_t)64);
    u8x16 a0 = vec_xl(0, dst);
    u8x16 a1 = vec_xl(0, dst + stride);
    u8x16 a2 = vec_xl(0, dst + 2 * stride);
    u8x16 a3 = vec_xl(0, dst + 3 * stride);
    u8x16 m0 = vec_xl(0, mask);
    u8x16 m1 = vec_xl(0, mask + 4);
    u8x16 m2 = vec_xl(0, mask + 2 * 4);
    u8x16 m3 = vec_xl(0, mask + 3 * 4);
    u8x16 b0 = vec_xl(0, tmp);
    u8x16 b1 = vec_xl(0, tmp + 4);
    u8x16 b2 = vec_xl(0, tmp + 2 * 4);
    u8x16 b3 = vec_xl(0, tmp + 3 * 4);

    u8x16 nm0 = vec_sub(v64u8, m0);
    u8x16 nm1 = vec_sub(v64u8, m1);
    u8x16 nm2 = vec_sub(v64u8, m2);
    u8x16 nm3 = vec_sub(v64u8, m3);

    u8x16 ab0 = vec_mergeh(a0, b0); // a even, b odd
    u8x16 ab1 = vec_mergeh(a1, b1); // a even, b odd
    u8x16 ab2 = vec_mergeh(a2, b2); // a even, b odd
    u8x16 ab3 = vec_mergeh(a3, b3); // a even, b odd
    u8x16 nm_m0 = vec_mergeh(nm0, m0);
    u8x16 nm_m1 = vec_mergeh(nm1, m1);
    u8x16 nm_m2 = vec_mergeh(nm2, m2);
    u8x16 nm_m3 = vec_mergeh(nm3, m3);

    u16x8 d0_u16, d1_u16, d2_u16, d3_u16;

    BLEND_LINES4(d0_u16, d1_u16, d2_u16, d3_u16, ab0, ab1, ab2, ab3, nm_m0, nm_m1, nm_m2, nm_m3);

    u8x16 d0 = (u8x16)vec_pack(d0_u16, d0_u16);
    u8x16 d1 = (u8x16)vec_pack(d1_u16, d1_u16);
    u8x16 d2 = (u8x16)vec_pack(d2_u16, d2_u16);
    u8x16 d3 = (u8x16)vec_pack(d3_u16, d3_u16);

    uint32_t r0, r1, r2, r3;
    memcpy(&r0, &d0, 4);
    memcpy(&r1, &d1, 4);
    memcpy(&r2, &d2, 4);
    memcpy(&r3, &d3, 4);
    memcpy(dst, &r0, 4);
    memcpy(dst + stride, &r1, 4);
    memcpy(dst + 2 * stride, &r2, 4);
    memcpy(dst + 3 * stride, &r3, 4);
}

static void blend8(uint8_t *dst, const uint8_t *tmp, const uint8_t *mask, int stride)
{
    u8x16 v64u8 = vec_splats((uint8_t)64);
    u8x16 a0 = vec_xl(0, dst);
    u8x16 a1 = vec_xl(0, dst + stride);
    u8x16 a2 = vec_xl(0, dst + 2 * stride);
    u8x16 a3 = vec_xl(0, dst + 3 * stride);
    u8x16 m0 = vec_xl(0, mask);
    u8x16 m1 = vec_xl(0, mask + 8);
    u8x16 m2 = vec_xl(0, mask + 2 * 8);
    u8x16 m3 = vec_xl(0, mask + 3 * 8);
    u8x16 b0 = vec_xl(0, tmp);
    u8x16 b1 = vec_xl(0, tmp + 8);
    u8x16 b2 = vec_xl(0, tmp + 2 * 8);
    u8x16 b3 = vec_xl(0, tmp + 3 * 8);

    u8x16 nm0 = vec_sub(v64u8, m0);
    u8x16 nm1 = vec_sub(v64u8, m1);
    u8x16 nm2 = vec_sub(v64u8, m2);
    u8x16 nm3 = vec_sub(v64u8, m3);

    u8x16 ab0 = vec_mergeh(a0, b0); // a even, b odd
    u8x16 ab1 = vec_mergeh(a1, b1); // a even, b odd
    u8x16 ab2 = vec_mergeh(a2, b2); // a even, b odd
    u8x16 ab3 = vec_mergeh(a3, b3); // a even, b odd
    u8x16 nm_m0 = vec_mergeh(nm0, m0);
    u8x16 nm_m1 = vec_mergeh(nm1, m1);
    u8x16 nm_m2 = vec_mergeh(nm2, m2);
    u8x16 nm_m3 = vec_mergeh(nm3, m3);

    u16x8 d0_u16, d1_u16, d2_u16, d3_u16;

    BLEND_LINES4(d0_u16, d1_u16, d2_u16, d3_u16, ab0, ab1, ab2, ab3, nm_m0, nm_m1, nm_m2, nm_m3);

    u8x16 d0 = (u8x16)vec_pack(d0_u16, d0_u16);
    u8x16 d1 = (u8x16)vec_pack(d1_u16, d1_u16);
    u8x16 d2 = (u8x16)vec_pack(d2_u16, d2_u16);
    u8x16 d3 = (u8x16)vec_pack(d3_u16, d3_u16);

    uint64_t s0, s1, s2, s3;
    memcpy(&s0, &d0, 8);
    memcpy(&s1, &d1, 8);
    memcpy(&s2, &d2, 8);
    memcpy(&s3, &d3, 8);
    memcpy(dst, &s0, 8);
    memcpy(dst + stride, &s1, 8);
    memcpy(dst + 2 * stride, &s2, 8);
    memcpy(dst + 3 * stride, &s3, 8);
}

static inline void blend16_lines(uint8_t *dst, const uint8_t *tmp, const uint8_t *mask, int stride, int mstride)
{
    u8x16 v64u8 = vec_splats((uint8_t)64);
    u8x16 a0 = vec_xl(0, dst);
    u8x16 a1 = vec_xl(0, dst + stride);
    u8x16 a2 = vec_xl(0, dst + 2 * stride);
    u8x16 a3 = vec_xl(0, dst + 3 * stride);
    u8x16 m0 = vec_xl(0, mask);
    u8x16 m1 = vec_xl(0, mask + mstride);
    u8x16 m2 = vec_xl(0, mask + 2 * mstride);
    u8x16 m3 = vec_xl(0, mask + 3 * mstride);
    u8x16 b0 = vec_xl(0, tmp);
    u8x16 b1 = vec_xl(0, tmp + mstride);
    u8x16 b2 = vec_xl(0, tmp + 2 * mstride);
    u8x16 b3 = vec_xl(0, tmp + 3 * mstride);

    u8x16 nm0 = vec_sub(v64u8, m0);
    u8x16 nm1 = vec_sub(v64u8, m1);
    u8x16 nm2 = vec_sub(v64u8, m2);
    u8x16 nm3 = vec_sub(v64u8, m3);

    u8x16 ab0 = vec_mergeh(a0, b0);
    u8x16 ab1 = vec_mergeh(a1, b1);
    u8x16 ab2 = vec_mergeh(a2, b2);
    u8x16 ab3 = vec_mergeh(a3, b3);

    u8x16 nm_m0 = vec_mergeh(nm0, m0);
    u8x16 nm_m1 = vec_mergeh(nm1, m1);
    u8x16 nm_m2 = vec_mergeh(nm2, m2);
    u8x16 nm_m3 = vec_mergeh(nm3, m3);

    u16x8 d0h_u16, d1h_u16, d2h_u16, d3h_u16;
    u16x8 d0l_u16, d1l_u16, d2l_u16, d3l_u16;

    BLEND_LINES4(d0h_u16, d1h_u16, d2h_u16, d3h_u16, ab0, ab1, ab2, ab3, nm_m0, nm_m1, nm_m2, nm_m3)

    ab0 = vec_mergel(a0, b0);
    ab1 = vec_mergel(a1, b1);
    ab2 = vec_mergel(a2, b2);
    ab3 = vec_mergel(a3, b3);

    nm_m0 = vec_mergel(nm0, m0);
    nm_m1 = vec_mergel(nm1, m1);
    nm_m2 = vec_mergel(nm2, m2);
    nm_m3 = vec_mergel(nm3, m3);

    BLEND_LINES4(d0l_u16, d1l_u16, d2l_u16, d3l_u16, ab0, ab1, ab2, ab3, nm_m0, nm_m1, nm_m2, nm_m3)

    u8x16 d0 = (u8x16)vec_pack(d0h_u16, d0l_u16);
    u8x16 d1 = (u8x16)vec_pack(d1h_u16, d1l_u16);
    u8x16 d2 = (u8x16)vec_pack(d2h_u16, d2l_u16);
    u8x16 d3 = (u8x16)vec_pack(d3h_u16, d3l_u16);

    vec_xst(d0, 0,dst);
    vec_xst(d1, 0,dst + stride);
    vec_xst(d2, 0,dst + 2 * stride);
    vec_xst(d3, 0,dst + 3 * stride);
}

static void blend16(uint8_t *dst, const uint8_t *tmp, const uint8_t *mask, int stride)
{
    blend16_lines(dst, tmp, mask, stride, 16);
}

static void blend32(uint8_t *dst, const uint8_t *tmp, const uint8_t *mask, int stride)
{
    for (int i = 0; i < 2; i++, dst += 16, tmp += 16, mask += 16) {
        blend16_lines(dst, tmp, mask, stride, 32);
    }
}

static blend_line blend_funcs[4] = {
    blend4, blend8, blend16, blend32
};

void dav1d_blend_8bpc_pwr9(pixel *dst, const ptrdiff_t dst_stride, const pixel *tmp,
                           const int w, int h, const uint8_t *mask)
{
    assert(w <= 32);
    blend_line blend = blend_funcs[ctz(w) - 2];

    for (int y = 0; y < h; y+=4) {
        blend(dst, tmp, mask, PXSTRIDE(dst_stride));
        dst += 4 * PXSTRIDE(dst_stride);
        tmp += 4 * w;
        mask += 4 * w;
    }
}

static inline void blend_v_h(uint8_t *dst, const uint8_t *tmp, const uint8_t *mask, int stride, int mstride, int l)
{
    u8x16 v64u8 = vec_splats((uint8_t)64);
    u8x16 a0 = vec_xl(0, dst);
    u8x16 a1 = vec_xl(0, dst + stride);
    u8x16 m0 = vec_xl(0, mask);
    u8x16 b0 = vec_xl(0, tmp);
    u8x16 b1 = vec_xl(0, tmp + mstride);

    u8x16 nm0 = vec_sub(v64u8, m0);

    u8x16 ab0 = vec_mergeh(a0, b0); // a even, b odd
    u8x16 ab1 = vec_mergeh(a1, b1); // a even, b odd
    u8x16 nm_m0 = vec_mergeh(nm0, m0);

    u16x8 d0_u16, d1_u16;

    BLEND_LINES2(d0_u16, d1_u16, ab0, ab1, nm_m0, nm_m0);

    u8x16 d0 = (u8x16)vec_pack(d0_u16, d0_u16);
    u8x16 d1 = (u8x16)vec_pack(d1_u16, d1_u16);

    vec_xst_len(d0, dst, l);
    vec_xst_len(d1, dst + stride, l);
}

static inline void blend_v_hl(uint8_t *dst, const uint8_t *tmp, const uint8_t *mask, int stride, int mstride, int l)
{
    u8x16 v64u8 = vec_splats((uint8_t)64);
    u8x16 a0 = vec_xl(0, dst);
    u8x16 a1 = vec_xl(0, dst + stride);
    u8x16 m0 = vec_xl(0, mask);
    u8x16 b0 = vec_xl(0, tmp);
    u8x16 b1 = vec_xl(0, tmp + mstride);

    u8x16 nm0 = vec_sub(v64u8, m0);

    u8x16 ab0 = vec_mergeh(a0, b0);
    u8x16 ab1 = vec_mergeh(a1, b1);

    u8x16 nm_m0 = vec_mergeh(nm0, m0);

    u16x8 d0h_u16, d1h_u16;
    u16x8 d0l_u16, d1l_u16;

    BLEND_LINES2(d0h_u16, d1h_u16, ab0, ab1, nm_m0, nm_m0)

    ab0 = vec_mergel(a0, b0);
    ab1 = vec_mergel(a1, b1);

    nm_m0 = vec_mergel(nm0, m0);

    BLEND_LINES2(d0l_u16, d1l_u16, ab0, ab1,nm_m0, nm_m0)

    u8x16 d0 = (u8x16)vec_pack(d0h_u16, d0l_u16);
    u8x16 d1 = (u8x16)vec_pack(d1h_u16, d1l_u16);

    vec_xst_len(d0, dst, l);
    vec_xst_len(d1, dst + stride, l);
}

static void blend_v3(uint8_t *dst, const uint8_t *tmp, const uint8_t *mask, int stride)
{
    blend_v_h(dst, tmp, mask, stride, 4, 3);
}

static void blend_v6(uint8_t *dst, const uint8_t *tmp, const uint8_t *mask, int stride)
{
    blend_v_h(dst, tmp, mask, stride, 8, 6);
}

static void blend_v12(uint8_t *dst, const uint8_t *tmp, const uint8_t *mask, int stride)
{
    blend_v_hl(dst, tmp, mask, stride, 16, 12);
}

static void blend_v24(uint8_t *dst, const uint8_t *tmp, const uint8_t *mask, int stride)
{
    blend_v_hl(dst, tmp, mask, stride, 32, 16);
    blend_v_h(dst + 16, tmp + 16, mask + 16, stride, 32, 8);
}

static void blend_v1(uint8_t *dst, const uint8_t *tmp, const uint8_t *mask, int stride)
{
    dst[0] = blend_px(dst[0], tmp[0], mask[0]);
    dst[stride] = blend_px(dst[stride], tmp[2], mask[0]);
}

static blend_line blend_v_funcs[5] = {
    blend_v1, blend_v3, blend_v6, blend_v12, blend_v24
};

void dav1d_blend_v_8bpc_pwr9(pixel *dst, const ptrdiff_t dst_stride, const pixel *tmp,
                             const int w, int h)
{
    const uint8_t *const mask = &dav1d_obmc_masks[w];

    assert(w <= 32);
    blend_line blend = blend_v_funcs[ctz(w) - 1];

    for (int y = 0; y < h; y+=2) {
        blend(dst, tmp, mask, PXSTRIDE(dst_stride));

        dst += 2 * PXSTRIDE(dst_stride);
        tmp += 2 * w;
    }
}

static inline void blend_h_h(uint8_t *dst, const uint8_t *tmp, const uint8_t *mask, int stride, int mstride, int l)
{
    u8x16 v64u8 = vec_splats((uint8_t)64);
    u8x16 a0 = vec_xl(0, dst);
    u8x16 a1 = vec_xl(0, dst + stride);
    u8x16 a2 = vec_xl(0, dst + 2 * stride);
    u8x16 m = vec_xl(0, mask);
    u8x16 b0 = vec_xl(0, tmp);
    u8x16 b1 = vec_xl(0, tmp + mstride);
    u8x16 b2 = vec_xl(0, tmp + 2 * mstride);
    u8x16 m0 = vec_splat(m, 0);
    u8x16 m1 = vec_splat(m, 1);
    u8x16 m2 = vec_splat(m, 2);

    u8x16 nm0 = vec_sub(v64u8, m0);
    u8x16 nm1 = vec_sub(v64u8, m1);
    u8x16 nm2 = vec_sub(v64u8, m2);

    u8x16 ab0 = vec_mergeh(a0, b0); // a even, b odd
    u8x16 ab1 = vec_mergeh(a1, b1); // a even, b odd
    u8x16 ab2 = vec_mergeh(a2, b2); // a even, b odd
    u8x16 nm_m0 = vec_mergeh(nm0, m0);
    u8x16 nm_m1 = vec_mergeh(nm1, m1);
    u8x16 nm_m2 = vec_mergeh(nm2, m2);

    u16x8 d0_u16, d1_u16, d2_u16;

    BLEND_LINES3(d0_u16, d1_u16, d2_u16, ab0, ab1, ab2, nm_m0, nm_m1, nm_m2);

    u8x16 d0 = (u8x16)vec_pack(d0_u16, d0_u16);
    u8x16 d1 = (u8x16)vec_pack(d1_u16, d1_u16);
    u8x16 d2 = (u8x16)vec_pack(d2_u16, d2_u16);

    vec_xst_len(d0, dst, l);
    vec_xst_len(d1, dst + stride, l);
    vec_xst_len(d2, dst + 2 * stride, l);
}

static inline void blend_h_hl(uint8_t *dst, const uint8_t *tmp, const uint8_t *mask, int stride, int mstride)
{
    u8x16 v64u8 = vec_splats((uint8_t)64);
    u8x16 a0 = vec_xl(0, dst);
    u8x16 a1 = vec_xl(0, dst + stride);
    u8x16 a2 = vec_xl(0, dst + 2 * stride);
    u8x16 m = vec_xl(0, mask);
    u8x16 b0 = vec_xl(0, tmp);
    u8x16 b1 = vec_xl(0, tmp + mstride);
    u8x16 b2 = vec_xl(0, tmp + 2 * mstride);
    u8x16 m0 = vec_splat(m, 0);
    u8x16 m1 = vec_splat(m, 1);
    u8x16 m2 = vec_splat(m, 2);

    u8x16 nm0 = vec_sub(v64u8, m0);
    u8x16 nm1 = vec_sub(v64u8, m1);
    u8x16 nm2 = vec_sub(v64u8, m2);

    u8x16 ab0 = vec_mergeh(a0, b0);
    u8x16 ab1 = vec_mergeh(a1, b1);
    u8x16 ab2 = vec_mergeh(a2, b2);

    u8x16 nm_m0 = vec_mergeh(nm0, m0);
    u8x16 nm_m1 = vec_mergeh(nm1, m1);
    u8x16 nm_m2 = vec_mergeh(nm2, m2);

    u16x8 d0h_u16, d1h_u16, d2h_u16;
    u16x8 d0l_u16, d1l_u16, d2l_u16;

    BLEND_LINES3(d0h_u16, d1h_u16, d2h_u16,  ab0, ab1, ab2, nm_m0, nm_m1, nm_m2)

    ab0 = vec_mergel(a0, b0);
    ab1 = vec_mergel(a1, b1);
    ab2 = vec_mergel(a2, b2);

    nm_m0 = vec_mergel(nm0, m0);
    nm_m1 = vec_mergel(nm1, m1);
    nm_m2 = vec_mergel(nm2, m2);

    BLEND_LINES3(d0l_u16, d1l_u16, d2l_u16, ab0, ab1, ab2, nm_m0, nm_m1, nm_m2)

    u8x16 d0 = (u8x16)vec_pack(d0h_u16, d0l_u16);
    u8x16 d1 = (u8x16)vec_pack(d1h_u16, d1l_u16);
    u8x16 d2 = (u8x16)vec_pack(d2h_u16, d2l_u16);

    vec_xst(d0, 0, dst);
    vec_xst(d1, 0,dst + stride);
    vec_xst(d2, 0,dst + 2 * stride);
}

static void blend_h2(uint8_t *dst, const uint8_t *tmp, const uint8_t *mask, int stride) {
    for (int y = 0; y < 3; y++) {
        const int m = *mask++;
        for (int x = 0; x < 2; x++) {
            dst[x] = blend_px(dst[x], tmp[x], m);
        }
        dst += stride;
        tmp += 2;
    }
}

static void blend_h4(uint8_t *dst, const uint8_t *tmp, const uint8_t *mask, int stride) {
    blend_h_h(dst, tmp, mask, stride, 4, 4);
}

static void blend_h8(uint8_t *dst, const uint8_t *tmp, const uint8_t *mask, int stride) {
    blend_h_h(dst, tmp, mask, stride, 8, 8);
}

static void blend_h16(uint8_t *dst, const uint8_t *tmp, const uint8_t *mask, int stride) {
    blend_h_hl(dst, tmp, mask, stride, 16);
}

static void blend_h32(uint8_t *dst, const uint8_t *tmp, const uint8_t *mask, int stride) {
    blend_h_hl(dst, tmp, mask, stride, 32);
    blend_h_hl(dst + 16, tmp + 16, mask, stride, 32);
}

static void blend_h64(uint8_t *dst, const uint8_t *tmp, const uint8_t *mask, int stride) {
    blend_h_hl(dst, tmp, mask, stride, 64);
    blend_h_hl(dst + 16, tmp + 16, mask, stride, 64);
    blend_h_hl(dst + 32, tmp + 32, mask, stride, 64);
    blend_h_hl(dst + 48, tmp + 48, mask, stride, 64);
}

static void blend_h128(uint8_t *dst, const uint8_t *tmp, const uint8_t *mask, int stride) {
    for (int i = 0; i < 2; i++, dst += 64, tmp += 64) {
        blend_h_hl(dst, tmp, mask, stride, 128);
        blend_h_hl(dst + 16, tmp + 16, mask, stride, 128);
        blend_h_hl(dst + 32, tmp + 32, mask, stride, 128);
        blend_h_hl(dst + 48, tmp + 48, mask, stride, 128);
    }
}

static blend_line blend_h_funcs[7] = {
    blend_h2, blend_h4, blend_h8, blend_h16, blend_h32, blend_h64, blend_h128
};

void dav1d_blend_h_8bpc_pwr9(pixel *dst, const ptrdiff_t dst_stride, const pixel *tmp,
                             const int w, int h)
{
    const uint8_t *mask = &dav1d_obmc_masks[h];
    h = (h * 3) >> 2;

    assert(w <= 128);
    blend_line blend = blend_h_funcs[ctz(w) - 1];

    if (h == 1) {
        const int m = *mask++;
        for (int x = 0; x < w; x++) {
            dst[x] = blend_px(dst[x], tmp[x], m);
        }
    } else
    for (int y = 0; y < h; y+=3) {
        blend(dst, tmp, mask, PXSTRIDE(dst_stride));
        dst += 3 * PXSTRIDE(dst_stride);
        tmp += 3 * w;
        mask += 3;
    }
}

void dav1d_avg_8bpc_pwr9(pixel *dst, const ptrdiff_t dst_stride,
                         const int16_t *tmp1, const int16_t *tmp2,
                         const int w, int h)
{
    const i16x8 v16 = vec_splats((int16_t)16);
    const u16x8 v5  = vec_splat_u16(5);

    if (w == 4) {
        while (h >= 2) {
            const i16x8 t1 = vec_xl(0, tmp1);
            const i16x8 t2 = vec_xl(0, tmp2);
            const i16x8 s  = vec_sra(vec_add(vec_add(t1, t2), v16), v5);
            const u8x16 res = vec_packsu(s, s);
            uint32_t r0, r1;
            memcpy(&r0, (const char*)&res + 0, 4);
            memcpy(&r1, (const char*)&res + 4, 4);
            memcpy(dst, &r0, 4);
            memcpy(dst + PXSTRIDE(dst_stride), &r1, 4);
            tmp1 += 8;
            tmp2 += 8;
            dst += 2 * PXSTRIDE(dst_stride);
            h -= 2;
        }
        if (h == 1) {
            const i16x8 t1 = vec_xl(0, tmp1);
            const i16x8 t2 = vec_xl(0, tmp2);
            const i16x8 s  = vec_sra(vec_add(vec_add(t1, t2), v16), v5);
            const u8x16 res = vec_packsu(s, s);
            uint32_t r0;
            memcpy(&r0, (const char*)&res + 0, 4);
            memcpy(dst, &r0, 4);
        }
    } else if (w == 8) {
        while (h >= 2) {
            const i16x8 t1_0 = vec_xl(0, tmp1);
            const i16x8 t2_0 = vec_xl(0, tmp2);
            const i16x8 t1_1 = vec_xl(0, tmp1 + 8);
            const i16x8 t2_1 = vec_xl(0, tmp2 + 8);
            const i16x8 s0 = vec_sra(vec_add(vec_add(t1_0, t2_0), v16), v5);
            const i16x8 s1 = vec_sra(vec_add(vec_add(t1_1, t2_1), v16), v5);
            const u8x16 res = vec_packsu(s0, s1);
            uint64_t r0, r1;
            memcpy(&r0, (const char*)&res + 0, 8);
            memcpy(&r1, (const char*)&res + 8, 8);
            memcpy(dst, &r0, 8);
            memcpy(dst + PXSTRIDE(dst_stride), &r1, 8);
            tmp1 += 16;
            tmp2 += 16;
            dst += 2 * PXSTRIDE(dst_stride);
            h -= 2;
        }
        if (h == 1) {
            const i16x8 t1_0 = vec_xl(0, tmp1);
            const i16x8 t2_0 = vec_xl(0, tmp2);
            const i16x8 s0 = vec_sra(vec_add(vec_add(t1_0, t2_0), v16), v5);
            const u8x16 res = vec_packsu(s0, s0);
            uint64_t r0;
            memcpy(&r0, (const char*)&res + 0, 8);
            memcpy(dst, &r0, 8);
        }
    } else if (w == 16) {
        while (h >= 2) {
            const i16x8 t1_0 = vec_xl(0, tmp1);
            const i16x8 t1_1 = vec_xl(0, tmp1 + 8);
            const i16x8 t2_0 = vec_xl(0, tmp2);
            const i16x8 t2_1 = vec_xl(0, tmp2 + 8);
            const i16x8 t1_2 = vec_xl(0, tmp1 + 16);
            const i16x8 t1_3 = vec_xl(0, tmp1 + 24);
            const i16x8 t2_2 = vec_xl(0, tmp2 + 16);
            const i16x8 t2_3 = vec_xl(0, tmp2 + 24);

            const i16x8 s0 = vec_sra(vec_add(vec_add(t1_0, t2_0), v16), v5);
            const i16x8 s1 = vec_sra(vec_add(vec_add(t1_1, t2_1), v16), v5);
            const i16x8 s2 = vec_sra(vec_add(vec_add(t1_2, t2_2), v16), v5);
            const i16x8 s3 = vec_sra(vec_add(vec_add(t1_3, t2_3), v16), v5);

            const u8x16 res0 = vec_packsu(s0, s1);
            const u8x16 res1 = vec_packsu(s2, s3);
            vec_xst(res0, 0, dst);
            vec_xst(res1, 0, dst + PXSTRIDE(dst_stride));

            tmp1 += 32;
            tmp2 += 32;
            dst += 2 * PXSTRIDE(dst_stride);
            h -= 2;
        }
        if (h == 1) {
            const i16x8 t1_0 = vec_xl(0, tmp1);
            const i16x8 t1_1 = vec_xl(0, tmp1 + 8);
            const i16x8 t2_0 = vec_xl(0, tmp2);
            const i16x8 t2_1 = vec_xl(0, tmp2 + 8);
            const i16x8 s0 = vec_sra(vec_add(vec_add(t1_0, t2_0), v16), v5);
            const i16x8 s1 = vec_sra(vec_add(vec_add(t1_1, t2_1), v16), v5);
            const u8x16 res = vec_packsu(s0, s1);
            vec_xst(res, 0, dst);
        }
    } else if (w == 32) {
        do {
            const i16x8 t1_0 = vec_xl(0, tmp1);
            const i16x8 t1_1 = vec_xl(0, tmp1 + 8);
            const i16x8 t1_2 = vec_xl(0, tmp1 + 16);
            const i16x8 t1_3 = vec_xl(0, tmp1 + 24);

            const i16x8 t2_0 = vec_xl(0, tmp2);
            const i16x8 t2_1 = vec_xl(0, tmp2 + 8);
            const i16x8 t2_2 = vec_xl(0, tmp2 + 16);
            const i16x8 t2_3 = vec_xl(0, tmp2 + 24);

            const i16x8 s0 = vec_sra(vec_add(vec_add(t1_0, t2_0), v16), v5);
            const i16x8 s1 = vec_sra(vec_add(vec_add(t1_1, t2_1), v16), v5);
            const i16x8 s2 = vec_sra(vec_add(vec_add(t1_2, t2_2), v16), v5);
            const i16x8 s3 = vec_sra(vec_add(vec_add(t1_3, t2_3), v16), v5);

            const u8x16 res0 = vec_packsu(s0, s1);
            const u8x16 res1 = vec_packsu(s2, s3);

            vec_xst(res0, 0, dst);
            vec_xst(res1, 0, dst + 16);

            tmp1 += 32;
            tmp2 += 32;
            dst += PXSTRIDE(dst_stride);
        } while (--h);
    } else {
        do {
            for (int x = 0; x < w; x += 64) {
                const i16x8 t1_0 = vec_xl(0, tmp1 + x);
                const i16x8 t1_1 = vec_xl(0, tmp1 + x + 8);
                const i16x8 t1_2 = vec_xl(0, tmp1 + x + 16);
                const i16x8 t1_3 = vec_xl(0, tmp1 + x + 24);
                const i16x8 t1_4 = vec_xl(0, tmp1 + x + 32);
                const i16x8 t1_5 = vec_xl(0, tmp1 + x + 40);
                const i16x8 t1_6 = vec_xl(0, tmp1 + x + 48);
                const i16x8 t1_7 = vec_xl(0, tmp1 + x + 56);

                const i16x8 t2_0 = vec_xl(0, tmp2 + x);
                const i16x8 t2_1 = vec_xl(0, tmp2 + x + 8);
                const i16x8 t2_2 = vec_xl(0, tmp2 + x + 16);
                const i16x8 t2_3 = vec_xl(0, tmp2 + x + 24);
                const i16x8 t2_4 = vec_xl(0, tmp2 + x + 32);
                const i16x8 t2_5 = vec_xl(0, tmp2 + x + 40);
                const i16x8 t2_6 = vec_xl(0, tmp2 + x + 48);
                const i16x8 t2_7 = vec_xl(0, tmp2 + x + 56);

                const i16x8 s0 = vec_sra(vec_add(vec_add(t1_0, t2_0), v16), v5);
                const i16x8 s1 = vec_sra(vec_add(vec_add(t1_1, t2_1), v16), v5);
                const i16x8 s2 = vec_sra(vec_add(vec_add(t1_2, t2_2), v16), v5);
                const i16x8 s3 = vec_sra(vec_add(vec_add(t1_3, t2_3), v16), v5);
                const i16x8 s4 = vec_sra(vec_add(vec_add(t1_4, t2_4), v16), v5);
                const i16x8 s5 = vec_sra(vec_add(vec_add(t1_5, t2_5), v16), v5);
                const i16x8 s6 = vec_sra(vec_add(vec_add(t1_6, t2_6), v16), v5);
                const i16x8 s7 = vec_sra(vec_add(vec_add(t1_7, t2_7), v16), v5);

                const u8x16 res0 = vec_packsu(s0, s1);
                const u8x16 res1 = vec_packsu(s2, s3);
                const u8x16 res2 = vec_packsu(s4, s5);
                const u8x16 res3 = vec_packsu(s6, s7);

                vec_xst(res0, 0, dst + x);
                vec_xst(res1, 0, dst + x + 16);
                vec_xst(res2, 0, dst + x + 32);
                vec_xst(res3, 0, dst + x + 48);
            }
            tmp1 += w;
            tmp2 += w;
            dst += PXSTRIDE(dst_stride);
        } while (--h);
    }
}

#endif // BITDEPTH
