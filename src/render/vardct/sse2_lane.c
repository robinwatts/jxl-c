// SPDX-License-Identifier: MIT OR Apache-2.0
#include "render/vardct/sse2_lane.h"

#include "render/vardct/dct_common.h"
#include "static_assert.h"

#include <emmintrin.h>

/* Lock shuffle immediates to Rust x86_64/dct/mod.rs bit patterns. */
JXL_STATIC_ASSERT(_MM_SHUFFLE(2, 0, 2, 0) == JXL_SSE2_SHUFFLE_A0B0, "A0B0 shuffle");
JXL_STATIC_ASSERT(_MM_SHUFFLE(3, 1, 3, 1) == JXL_SSE2_SHUFFLE_A1B1, "A1B1 shuffle");
JXL_STATIC_ASSERT(_MM_SHUFFLE(0, 1, 2, 3) == JXL_SSE2_SHUFFLE_REVERSE4, "reverse4 shuffle");
JXL_STATIC_ASSERT(_MM_SHUFFLE(1, 0, 3, 2) == JXL_SSE2_SHUFFLE_DCT4_FLIP, "dct4 flip shuffle");
JXL_STATIC_ASSERT(_MM_SHUFFLE(0, 2, 2, 0) == JXL_SSE2_SHUFFLE_DCT4_TMP_A, "dct4 tmp_a shuffle");
JXL_STATIC_ASSERT(_MM_SHUFFLE(1, 3, 3, 1) == JXL_SSE2_SHUFFLE_DCT4_TMP_B, "dct4 tmp_b shuffle");
JXL_STATIC_ASSERT(_MM_SHUFFLE(2, 1, 3, 0) == JXL_SSE2_SHUFFLE_DCT4_FWD_A, "dct4 fwd_a shuffle");
JXL_STATIC_ASSERT(_MM_SHUFFLE(3, 0, 2, 1) == JXL_SSE2_SHUFFLE_DCT4_FWD_B, "dct4 fwd_b shuffle");

static jxl_sse2_lane lane_splat(float v) {
    return _mm_set1_ps(v);
}

void jxl_sse2_transpose_lane(jxl_sse2_lane lanes[4]) {
    jxl_sse2_lane row0 = lanes[0];
    jxl_sse2_lane row1 = lanes[1];
    jxl_sse2_lane row2 = lanes[2];
    jxl_sse2_lane row3 = lanes[3];
    jxl_sse2_lane tmp0 = _mm_unpacklo_ps(row0, row1);
    jxl_sse2_lane tmp2 = _mm_unpacklo_ps(row2, row3);
    jxl_sse2_lane tmp1 = _mm_unpackhi_ps(row0, row1);
    jxl_sse2_lane tmp3 = _mm_unpackhi_ps(row2, row3);
    lanes[0] = _mm_movelh_ps(tmp0, tmp2);
    lanes[1] = _mm_movehl_ps(tmp2, tmp0);
    lanes[2] = _mm_movelh_ps(tmp1, tmp3);
    lanes[3] = _mm_movehl_ps(tmp3, tmp1);
}

void jxl_sse2_dct4_forward_lanes(jxl_sse2_lane out[4], const jxl_sse2_lane in[4]) {
    const jxl_sse2_lane sec0 = lane_splat(0.5411961f / 4.0f);
    const jxl_sse2_lane sec1 = lane_splat(1.306563f / 4.0f);
    const jxl_sse2_lane quarter = lane_splat(0.25f);
    const jxl_sse2_lane sqrt2 = lane_splat(1.4142135623730951f);

    const jxl_sse2_lane sum03 = _mm_add_ps(in[0], in[3]);
    const jxl_sse2_lane sum12 = _mm_add_ps(in[1], in[2]);
    const jxl_sse2_lane tmp0 = _mm_mul_ps(_mm_sub_ps(in[0], in[3]), sec0);
    const jxl_sse2_lane tmp1 = _mm_mul_ps(_mm_sub_ps(in[1], in[2]), sec1);
    const jxl_sse2_lane out1 = _mm_sub_ps(tmp0, tmp1);
    const jxl_sse2_lane out0 = _mm_add_ps(tmp0, tmp1);

    out[0] = _mm_mul_ps(_mm_add_ps(sum03, sum12), quarter);
    out[1] = _mm_add_ps(_mm_mul_ps(out0, sqrt2), out1);
    out[2] = _mm_mul_ps(_mm_sub_ps(sum03, sum12), quarter);
    out[3] = out1;
}

void jxl_sse2_dct4_inverse_lanes(jxl_sse2_lane out[4], const jxl_sse2_lane in[4]) {
    const jxl_sse2_lane sec0 = lane_splat(0.5411961f);
    const jxl_sse2_lane sec1 = lane_splat(1.306563f);
    const jxl_sse2_lane sqrt2 = lane_splat(1.4142135623730951f);

    const jxl_sse2_lane tmp0 = _mm_mul_ps(in[1], sqrt2);
    const jxl_sse2_lane tmp1 = _mm_add_ps(in[1], in[3]);
    const jxl_sse2_lane out0 = _mm_mul_ps(_mm_add_ps(tmp0, tmp1), sec0);
    const jxl_sse2_lane out1 = _mm_mul_ps(_mm_sub_ps(tmp0, tmp1), sec1);
    const jxl_sse2_lane sum02 = _mm_add_ps(in[0], in[2]);
    const jxl_sse2_lane sub02 = _mm_sub_ps(in[0], in[2]);

    out[0] = _mm_add_ps(sum02, out0);
    out[1] = _mm_add_ps(sub02, out1);
    out[2] = _mm_sub_ps(sub02, out1);
    out[3] = _mm_sub_ps(sum02, out0);
}

void jxl_sse2_dct4_inverse_4(jxl_sse2_lane lanes[4]) {
    jxl_sse2_lane in[4];
    in[0] = lanes[0];
    in[1] = lanes[1];
    in[2] = lanes[2];
    in[3] = lanes[3];
    jxl_sse2_dct4_inverse_lanes(lanes, in);
}

jxl_sse2_lane jxl_sse2_dct4_vec_inverse(jxl_sse2_lane v) {
    const jxl_sse2_lane v_flip = _mm_shuffle_ps(v, v, JXL_SSE2_SHUFFLE_DCT4_FLIP);
    const jxl_sse2_lane mul_a = _mm_set_ps(-1.306563f, -1.0f, (1.4142135623730951f + 1.0f) * 0.5411961f,
                                           1.0f);
    const jxl_sse2_lane mul_b = _mm_set_ps((1.4142135623730951f - 1.0f) * 1.306563f, 1.0f, 0.5411961f,
                                           1.0f);
    const jxl_sse2_lane tmp = _mm_add_ps(_mm_mul_ps(v, mul_a), _mm_mul_ps(v_flip, mul_b));

    const jxl_sse2_lane tmp_a = _mm_shuffle_ps(tmp, tmp, JXL_SSE2_SHUFFLE_DCT4_TMP_A);
    const jxl_sse2_lane tmp_b = _mm_shuffle_ps(tmp, tmp, JXL_SSE2_SHUFFLE_DCT4_TMP_B);
    const jxl_sse2_lane mul = _mm_set_ps(-1.0f, -1.0f, 1.0f, 1.0f);
    return _mm_add_ps(_mm_mul_ps(tmp_b, mul), tmp_a);
}

jxl_sse2_lane jxl_sse2_dct4_vec_forward(jxl_sse2_lane v) {
    const jxl_sse2_lane v_rev = _mm_shuffle_ps(v, v, JXL_SSE2_SHUFFLE_REVERSE4);
    const jxl_sse2_lane mul = _mm_set_ps(-1.0f, -1.0f, 1.0f, 1.0f);
    const jxl_sse2_lane addsub = _mm_add_ps(_mm_mul_ps(v, mul), v_rev);

    const jxl_sse2_lane a = _mm_shuffle_ps(addsub, addsub, JXL_SSE2_SHUFFLE_DCT4_FWD_A);
    const jxl_sse2_lane mul_a = _mm_set_ps(-0.25f * 1.306563f, -0.25f,
                                           (0.7071067811865476f / 2.0f + 0.25f) * 0.5411961f, 0.25f);
    const jxl_sse2_lane b = _mm_shuffle_ps(addsub, addsub, JXL_SSE2_SHUFFLE_DCT4_FWD_B);
    const jxl_sse2_lane mul_b = _mm_set_ps(0.25f * 0.5411961f, 0.25f,
                                           (0.7071067811865476f / 2.0f - 0.25f) * 1.306563f, 0.25f);
    return _mm_add_ps(_mm_mul_ps(a, mul_a), _mm_mul_ps(b, mul_b));
}

void jxl_sse2_dct8_vec_inverse(jxl_sse2_lane vl, jxl_sse2_lane vr, jxl_sse2_lane *out_l,
                               jxl_sse2_lane *out_r) {
    const jxl_sse2_lane sec_vec = _mm_set_ps(2.5629154477415055f, 0.8999762231364156f, 0.6013448869350453f,
                                             0.5097955791041592f);
    const jxl_sse2_lane input0 = _mm_shuffle_ps(vl, vr, JXL_SSE2_SHUFFLE_A0B0);
    const jxl_sse2_lane input1 = _mm_shuffle_ps(vl, vr, JXL_SSE2_SHUFFLE_A1B1);
    const jxl_sse2_lane input1_shifted = _mm_castsi128_ps(_mm_slli_si128(_mm_castps_si128(input1), 4));
    const jxl_sse2_lane input1_mul = _mm_set_ps(1.0f, 1.0f, 1.0f, 1.4142135623730951f);
    const jxl_sse2_lane input1_adj = _mm_add_ps(_mm_mul_ps(input1, input1_mul), input1_shifted);
    const jxl_sse2_lane output0 = jxl_sse2_dct4_vec_inverse(input0);
    const jxl_sse2_lane output1 = jxl_sse2_dct4_vec_inverse(input1_adj);
    const jxl_sse2_lane output1_scaled = _mm_mul_ps(output1, sec_vec);
    const jxl_sse2_lane sub = _mm_sub_ps(output0, output1_scaled);
    *out_l = _mm_add_ps(output0, output1_scaled);
    *out_r = _mm_shuffle_ps(sub, sub, JXL_SSE2_SHUFFLE_REVERSE4);
}

void jxl_sse2_dct8_vec_forward(jxl_sse2_lane vl, jxl_sse2_lane vr, jxl_sse2_lane *out_l,
                               jxl_sse2_lane *out_r) {
    const jxl_sse2_lane sec_vec = _mm_set_ps(1.2814577238707527f, 0.4499881115682078f,
                                             0.30067244346752264f, 0.2548977895520796f);
    const jxl_sse2_lane vr_rev = _mm_shuffle_ps(vr, vr, JXL_SSE2_SHUFFLE_REVERSE4);
    const jxl_sse2_lane half = lane_splat(0.5f);
    const jxl_sse2_lane input0 = _mm_mul_ps(_mm_add_ps(vl, vr_rev), half);
    const jxl_sse2_lane input1 = _mm_mul_ps(_mm_sub_ps(vl, vr_rev), sec_vec);
    jxl_sse2_lane output0 = jxl_sse2_dct4_vec_forward(input0);
    jxl_sse2_lane output1 = jxl_sse2_dct4_vec_forward(input1);
    const jxl_sse2_lane output1_shifted =
        _mm_castsi128_ps(_mm_srli_si128(_mm_castps_si128(output1), 4));
    const jxl_sse2_lane output1_mul = _mm_set_ps(1.0f, 1.0f, 1.0f, 1.4142135623730951f);
    output1 = _mm_add_ps(_mm_mul_ps(output1, output1_mul), output1_shifted);
    *out_l = _mm_unpacklo_ps(output0, output1);
    *out_r = _mm_unpackhi_ps(output0, output1);
}

void jxl_sse2_dct8_inverse_lanes(jxl_sse2_lane io[8]) {
    size_t idx;
    const jxl_sse2_lane sqrt2 = lane_splat(1.4142135623730951f);
    jxl_sse2_lane output0[4];
    jxl_sse2_lane output1[4];
    jxl_sse2_lane input0[4];
    jxl_sse2_lane input1[4];
    const float *sec = jxl_sec_half_small(8);

    input0[0] = io[0];
    input0[1] = io[2];
    input0[2] = io[4];
    input0[3] = io[6];
    input1[0] = _mm_mul_ps(io[1], sqrt2);
    input1[1] = _mm_add_ps(io[3], io[1]);
    input1[2] = _mm_add_ps(io[5], io[3]);
    input1[3] = _mm_add_ps(io[7], io[5]);
    jxl_sse2_dct4_inverse_lanes(output0, input0);
    jxl_sse2_dct4_inverse_lanes(output1, input1);
    for (idx = 0; idx < 4; ++idx) {
        const jxl_sse2_lane r = _mm_mul_ps(output1[idx], lane_splat(sec[idx]));
        io[idx] = _mm_add_ps(output0[idx], r);
        io[7 - idx] = _mm_sub_ps(output0[idx], r);
    }
}

void jxl_sse2_dct8_forward_lanes(jxl_sse2_lane io[8]) {
    const jxl_sse2_lane half = lane_splat(0.5f);
    const jxl_sse2_lane sqrt2 = lane_splat(1.4142135623730951f);
    jxl_sse2_lane output0[4];
    jxl_sse2_lane output1[4];
    jxl_sse2_lane input0[4];
    jxl_sse2_lane input1[4];
    const float *sec = jxl_sec_half_small(8);

    input0[0] = _mm_mul_ps(_mm_add_ps(io[0], io[7]), half);
    input0[1] = _mm_mul_ps(_mm_add_ps(io[1], io[6]), half);
    input0[2] = _mm_mul_ps(_mm_add_ps(io[2], io[5]), half);
    input0[3] = _mm_mul_ps(_mm_add_ps(io[3], io[4]), half);
    input1[0] = _mm_mul_ps(_mm_sub_ps(io[0], io[7]), lane_splat(sec[0] / 2.0f));
    input1[1] = _mm_mul_ps(_mm_sub_ps(io[1], io[6]), lane_splat(sec[1] / 2.0f));
    input1[2] = _mm_mul_ps(_mm_sub_ps(io[2], io[5]), lane_splat(sec[2] / 2.0f));
    input1[3] = _mm_mul_ps(_mm_sub_ps(io[3], io[4]), lane_splat(sec[3] / 2.0f));

    jxl_sse2_dct4_forward_lanes(output0, input0);
    jxl_sse2_dct4_forward_lanes(output1, input1);

    io[0] = output0[0];
    io[2] = output0[1];
    io[4] = output0[2];
    io[6] = output0[3];

    output1[0] = _mm_mul_ps(output1[0], sqrt2);
    io[1] = _mm_add_ps(output1[0], output1[1]);
    io[3] = _mm_add_ps(output1[1], output1[2]);
    io[5] = _mm_add_ps(output1[2], output1[3]);
    io[7] = output1[3];
}
