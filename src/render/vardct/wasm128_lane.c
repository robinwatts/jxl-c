// SPDX-License-Identifier: MIT OR Apache-2.0
#include "render/vardct/wasm128_lane.h"

#include "render/vardct/dct_common.h"

#if defined(JXL_HAVE_SIMD_WASM128)

static jxl_wasm128_lane lane_splat(float v) {
    return wasm_f32x4_splat(v);
}

static jxl_wasm128_lane lane_set(float x0, float x1, float x2, float x3) {
    return wasm_f32x4_make(x0, x1, x2, x3);
}

void jxl_wasm128_transpose_lane(jxl_wasm128_lane lanes[4]) {
    jxl_wasm128_lane out[4];
    out[0] = wasm_i32x4_shuffle(lanes[0], lanes[1], 0, 1, 4, 5);
    out[1] = wasm_i32x4_shuffle(lanes[2], lanes[3], 0, 1, 4, 5);
    out[2] = wasm_i32x4_shuffle(lanes[0], lanes[1], 2, 3, 6, 7);
    out[3] = wasm_i32x4_shuffle(lanes[2], lanes[3], 2, 3, 6, 7);

    {
        jxl_wasm128_lane a = wasm_i32x4_shuffle(out[0], out[1], 0, 2, 4, 6);
        jxl_wasm128_lane b = wasm_i32x4_shuffle(out[0], out[1], 1, 3, 5, 7);
        lanes[0] = a;
        lanes[1] = b;
    }
    {
        jxl_wasm128_lane a = wasm_i32x4_shuffle(out[2], out[3], 0, 2, 4, 6);
        jxl_wasm128_lane b = wasm_i32x4_shuffle(out[2], out[3], 1, 3, 5, 7);
        lanes[2] = a;
        lanes[3] = b;
    }
}

void jxl_wasm128_dct4_forward_lanes(jxl_wasm128_lane out[4], const jxl_wasm128_lane in[4]) {
    const jxl_wasm128_lane sec0 = lane_splat(0.5411961f / 4.0f);
    const jxl_wasm128_lane sec1 = lane_splat(1.306563f / 4.0f);
    const jxl_wasm128_lane quarter = lane_splat(0.25f);
    const jxl_wasm128_lane sqrt2 = lane_splat(1.4142135623730951f);

    const jxl_wasm128_lane sum03 = wasm_f32x4_add(in[0], in[3]);
    const jxl_wasm128_lane sum12 = wasm_f32x4_add(in[1], in[2]);
    const jxl_wasm128_lane tmp0 = wasm_f32x4_mul(wasm_f32x4_sub(in[0], in[3]), sec0);
    const jxl_wasm128_lane tmp1 = wasm_f32x4_mul(wasm_f32x4_sub(in[1], in[2]), sec1);
    const jxl_wasm128_lane out1 = wasm_f32x4_sub(tmp0, tmp1);
    const jxl_wasm128_lane out0 = wasm_f32x4_add(tmp0, tmp1);

    out[0] = wasm_f32x4_mul(wasm_f32x4_add(sum03, sum12), quarter);
    out[1] = wasm_f32x4_add(wasm_f32x4_mul(out0, sqrt2), out1);
    out[2] = wasm_f32x4_mul(wasm_f32x4_sub(sum03, sum12), quarter);
    out[3] = out1;
}

void jxl_wasm128_dct4_inverse_lanes(jxl_wasm128_lane out[4], const jxl_wasm128_lane in[4]) {
    const jxl_wasm128_lane sec0 = lane_splat(0.5411961f);
    const jxl_wasm128_lane sec1 = lane_splat(1.306563f);
    const jxl_wasm128_lane sqrt2 = lane_splat(1.4142135623730951f);

    const jxl_wasm128_lane tmp0 = wasm_f32x4_mul(in[1], sqrt2);
    const jxl_wasm128_lane tmp1 = wasm_f32x4_add(in[1], in[3]);
    const jxl_wasm128_lane out0 = wasm_f32x4_mul(wasm_f32x4_add(tmp0, tmp1), sec0);
    const jxl_wasm128_lane out1 = wasm_f32x4_mul(wasm_f32x4_sub(tmp0, tmp1), sec1);
    const jxl_wasm128_lane sum02 = wasm_f32x4_add(in[0], in[2]);
    const jxl_wasm128_lane sub02 = wasm_f32x4_sub(in[0], in[2]);

    out[0] = wasm_f32x4_add(sum02, out0);
    out[1] = wasm_f32x4_add(sub02, out1);
    out[2] = wasm_f32x4_sub(sub02, out1);
    out[3] = wasm_f32x4_sub(sum02, out0);
}

void jxl_wasm128_dct4_inverse_4(jxl_wasm128_lane lanes[4]) {
    jxl_wasm128_lane in[4];
    in[0] = lanes[0];
    in[1] = lanes[1];
    in[2] = lanes[2];
    in[3] = lanes[3];
    jxl_wasm128_dct4_inverse_lanes(lanes, in);
}

jxl_wasm128_lane jxl_wasm128_dct4_vec_inverse(jxl_wasm128_lane v) {
    const jxl_wasm128_lane v_flip = wasm_i32x4_shuffle(v, v, 2, 3, 0, 1);
    const jxl_wasm128_lane mul_a =
        lane_set(1.0f, (1.4142135623730951f + 1.0f) * 0.5411961f, -1.0f, -1.306563f);
    const jxl_wasm128_lane mul_b =
        lane_set(1.0f, 0.5411961f, 1.0f, (1.4142135623730951f - 1.0f) * 1.306563f);
    const jxl_wasm128_lane tmp =
        wasm_f32x4_add(wasm_f32x4_mul(v, mul_a), wasm_f32x4_mul(v_flip, mul_b));
    const jxl_wasm128_lane tmp_neg = wasm_f32x4_neg(tmp);
    const jxl_wasm128_lane tmp_a = wasm_i32x4_shuffle(tmp, tmp, 0, 2, 2, 0);
    const jxl_wasm128_lane tmp_b = wasm_i32x4_shuffle(tmp, tmp_neg, 1, 3, 7, 5);
    return wasm_f32x4_add(tmp_a, tmp_b);
}

jxl_wasm128_lane jxl_wasm128_dct4_vec_forward(jxl_wasm128_lane v) {
    const jxl_wasm128_lane vrev = wasm_i32x4_shuffle(v, v, 3, 2, 1, 0);
    const jxl_wasm128_lane vneg = wasm_f32x4_neg(v);
    const jxl_wasm128_lane vadd = wasm_i32x4_shuffle(v, vneg, 0, 1, 6, 7);
    const jxl_wasm128_lane addsub = wasm_f32x4_add(vrev, vadd);

    const jxl_wasm128_lane a = wasm_i32x4_shuffle(addsub, addsub, 0, 3, 1, 2);
    const jxl_wasm128_lane mul_a = lane_set(
        0.25f, (0.7071067811865476f / 2.0f + 0.25f) * 0.5411961f, -0.25f, -0.25f * 1.306563f);
    const jxl_wasm128_lane b = wasm_i32x4_shuffle(addsub, addsub, 1, 2, 0, 3);
    const jxl_wasm128_lane mul_b = lane_set(
        0.25f, (0.7071067811865476f / 2.0f - 0.25f) * 1.306563f, 0.25f, 0.25f * 0.5411961f);
    return wasm_f32x4_add(wasm_f32x4_mul(a, mul_a), wasm_f32x4_mul(b, mul_b));
}

void jxl_wasm128_dct8_vec_inverse(jxl_wasm128_lane vl, jxl_wasm128_lane vr, jxl_wasm128_lane *out_l,
                                  jxl_wasm128_lane *out_r) {
    const jxl_wasm128_lane sec_vec =
        lane_set(0.5097955791041592f, 0.6013448869350453f, 0.8999762231364156f, 2.5629154477415055f);
    jxl_wasm128_lane input0 = wasm_i32x4_shuffle(vl, vr, 0, 2, 4, 6);
    jxl_wasm128_lane input1 = wasm_i32x4_shuffle(vl, vr, 1, 3, 5, 7);
    const jxl_wasm128_lane zero = wasm_f32x4_splat(0.0f);
    jxl_wasm128_lane input1_shifted = wasm_i32x4_shuffle(zero, input1, 3, 4, 5, 6);
    const jxl_wasm128_lane input1_mul = lane_set(1.4142135623730951f, 1.0f, 1.0f, 1.0f);
    input1 = wasm_f32x4_add(wasm_f32x4_mul(input1, input1_mul), input1_shifted);
    const jxl_wasm128_lane output0 = jxl_wasm128_dct4_vec_inverse(input0);
    jxl_wasm128_lane output1 = jxl_wasm128_dct4_vec_inverse(input1);
    output1 = wasm_f32x4_mul(output1, sec_vec);
    const jxl_wasm128_lane sub = wasm_f32x4_sub(output0, output1);
    *out_l = wasm_f32x4_add(output0, output1);
    *out_r = wasm_i32x4_shuffle(sub, sub, 3, 2, 1, 0);
}

void jxl_wasm128_dct8_vec_forward(jxl_wasm128_lane vl, jxl_wasm128_lane vr, jxl_wasm128_lane *out_l,
                                  jxl_wasm128_lane *out_r) {
    const jxl_wasm128_lane sec_vec =
        lane_set(0.2548977895520796f, 0.30067244346752264f, 0.4499881115682078f, 1.2814577238707527f);
    const jxl_wasm128_lane vr_rev = wasm_i32x4_shuffle(vr, vr, 3, 2, 1, 0);
    const jxl_wasm128_lane half = lane_splat(0.5f);
    const jxl_wasm128_lane input0 = wasm_f32x4_mul(wasm_f32x4_add(vl, vr_rev), half);
    const jxl_wasm128_lane input1 = wasm_f32x4_mul(wasm_f32x4_sub(vl, vr_rev), sec_vec);
    jxl_wasm128_lane output0 = jxl_wasm128_dct4_vec_forward(input0);
    jxl_wasm128_lane output1 = jxl_wasm128_dct4_vec_forward(input1);
    const jxl_wasm128_lane zero = wasm_f32x4_splat(0.0f);
    jxl_wasm128_lane output1_shifted = wasm_i32x4_shuffle(output1, zero, 1, 2, 3, 4);
    const jxl_wasm128_lane output1_mul = lane_set(1.4142135623730951f, 1.0f, 1.0f, 1.0f);
    output1 = wasm_f32x4_add(wasm_f32x4_mul(output1, output1_mul), output1_shifted);
    *out_l = wasm_i32x4_shuffle(output0, output1, 0, 4, 1, 5);
    *out_r = wasm_i32x4_shuffle(output0, output1, 2, 6, 3, 7);
}

void jxl_wasm128_dct8_inverse_lanes(jxl_wasm128_lane io[8]) {
    size_t idx;
    const jxl_wasm128_lane sqrt2 = lane_splat(1.4142135623730951f);
    jxl_wasm128_lane output0[4];
    jxl_wasm128_lane output1[4];
    jxl_wasm128_lane input0[4];
    jxl_wasm128_lane input1[4];
    const float *sec = jxl_sec_half_small(8);

    input0[0] = io[0];
    input0[1] = io[2];
    input0[2] = io[4];
    input0[3] = io[6];
    input1[0] = wasm_f32x4_mul(io[1], sqrt2);
    input1[1] = wasm_f32x4_add(io[3], io[1]);
    input1[2] = wasm_f32x4_add(io[5], io[3]);
    input1[3] = wasm_f32x4_add(io[7], io[5]);
    jxl_wasm128_dct4_inverse_lanes(output0, input0);
    jxl_wasm128_dct4_inverse_lanes(output1, input1);
    for (idx = 0; idx < 4; ++idx) {
        const jxl_wasm128_lane r = wasm_f32x4_mul(output1[idx], lane_splat(sec[idx]));
        io[idx] = wasm_f32x4_add(output0[idx], r);
        io[7 - idx] = wasm_f32x4_sub(output0[idx], r);
    }
}

void jxl_wasm128_dct8_forward_lanes(jxl_wasm128_lane io[8]) {
    const jxl_wasm128_lane half = lane_splat(0.5f);
    const jxl_wasm128_lane sqrt2 = lane_splat(1.4142135623730951f);
    jxl_wasm128_lane output0[4];
    jxl_wasm128_lane output1[4];
    jxl_wasm128_lane input0[4];
    jxl_wasm128_lane input1[4];
    const float *sec = jxl_sec_half_small(8);

    input0[0] = wasm_f32x4_mul(wasm_f32x4_add(io[0], io[7]), half);
    input0[1] = wasm_f32x4_mul(wasm_f32x4_add(io[1], io[6]), half);
    input0[2] = wasm_f32x4_mul(wasm_f32x4_add(io[2], io[5]), half);
    input0[3] = wasm_f32x4_mul(wasm_f32x4_add(io[3], io[4]), half);
    input1[0] = wasm_f32x4_mul(wasm_f32x4_sub(io[0], io[7]), lane_splat(sec[0] / 2.0f));
    input1[1] = wasm_f32x4_mul(wasm_f32x4_sub(io[1], io[6]), lane_splat(sec[1] / 2.0f));
    input1[2] = wasm_f32x4_mul(wasm_f32x4_sub(io[2], io[5]), lane_splat(sec[2] / 2.0f));
    input1[3] = wasm_f32x4_mul(wasm_f32x4_sub(io[3], io[4]), lane_splat(sec[3] / 2.0f));

    jxl_wasm128_dct4_forward_lanes(output0, input0);
    jxl_wasm128_dct4_forward_lanes(output1, input1);

    io[0] = output0[0];
    io[2] = output0[1];
    io[4] = output0[2];
    io[6] = output0[3];

    output1[0] = wasm_f32x4_mul(output1[0], sqrt2);
    io[1] = wasm_f32x4_add(output1[0], output1[1]);
    io[3] = wasm_f32x4_add(output1[1], output1[2]);
    io[5] = wasm_f32x4_add(output1[2], output1[3]);
    io[7] = output1[3];
}

#endif
