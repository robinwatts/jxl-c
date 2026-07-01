// SPDX-License-Identifier: MIT OR Apache-2.0
#include "render/vardct/neon_lane.h"

#include "render/vardct/dct_common.h"

#if defined(JXL_HAVE_SIMD_NEON)

static jxl_neon_lane lane_splat(float v) {
    return vdupq_n_f32(v);
}

void jxl_neon_transpose_lane(jxl_neon_lane lanes[4]) {
    const float32x4x2_t zip01 = vzipq_f32(lanes[0], lanes[1]);
    const float32x4x2_t zip23 = vzipq_f32(lanes[2], lanes[3]);
    lanes[0] = vcombine_f32(vget_low_f32(zip01.val[0]), vget_low_f32(zip23.val[0]));
    lanes[1] = vcombine_f32(vget_high_f32(zip01.val[0]), vget_high_f32(zip23.val[0]));
    lanes[2] = vcombine_f32(vget_low_f32(zip01.val[1]), vget_low_f32(zip23.val[1]));
    lanes[3] = vcombine_f32(vget_high_f32(zip01.val[1]), vget_high_f32(zip23.val[1]));
}

void jxl_neon_dct4_forward_lanes(jxl_neon_lane out[4], const jxl_neon_lane in[4]) {
    const jxl_neon_lane sec0 = lane_splat(0.5411961f / 4.0f);
    const jxl_neon_lane sec1 = lane_splat(1.306563f / 4.0f);
    const jxl_neon_lane quarter = lane_splat(0.25f);
    const jxl_neon_lane sqrt2 = lane_splat(1.4142135623730951f);

    const jxl_neon_lane sum03 = vaddq_f32(in[0], in[3]);
    const jxl_neon_lane sum12 = vaddq_f32(in[1], in[2]);
    const jxl_neon_lane tmp0 = vmulq_f32(vsubq_f32(in[0], in[3]), sec0);
    const jxl_neon_lane tmp1 = vmulq_f32(vsubq_f32(in[1], in[2]), sec1);
    const jxl_neon_lane out1 = vsubq_f32(tmp0, tmp1);
    const jxl_neon_lane out0 = vaddq_f32(tmp0, tmp1);

    out[0] = vmulq_f32(vaddq_f32(sum03, sum12), quarter);
    out[1] = vaddq_f32(vmulq_f32(out0, sqrt2), out1);
    out[2] = vmulq_f32(vsubq_f32(sum03, sum12), quarter);
    out[3] = out1;
}

void jxl_neon_dct4_inverse_lanes(jxl_neon_lane out[4], const jxl_neon_lane in[4]) {
    const jxl_neon_lane sec0 = lane_splat(0.5411961f);
    const jxl_neon_lane sec1 = lane_splat(1.306563f);
    const jxl_neon_lane sqrt2 = lane_splat(1.4142135623730951f);

    const jxl_neon_lane tmp0 = vmulq_f32(in[1], sqrt2);
    const jxl_neon_lane tmp1 = vaddq_f32(in[1], in[3]);
    const jxl_neon_lane out0 = vmulq_f32(vaddq_f32(tmp0, tmp1), sec0);
    const jxl_neon_lane out1 = vmulq_f32(vsubq_f32(tmp0, tmp1), sec1);
    const jxl_neon_lane sum02 = vaddq_f32(in[0], in[2]);
    const jxl_neon_lane sub02 = vsubq_f32(in[0], in[2]);

    out[0] = vaddq_f32(sum02, out0);
    out[1] = vaddq_f32(sub02, out1);
    out[2] = vsubq_f32(sub02, out1);
    out[3] = vsubq_f32(sum02, out0);
}

void jxl_neon_dct4_inverse_4(jxl_neon_lane lanes[4]) {
    jxl_neon_lane in[4];
    in[0] = lanes[0];
    in[1] = lanes[1];
    in[2] = lanes[2];
    in[3] = lanes[3];
    jxl_neon_dct4_inverse_lanes(lanes, in);
}

jxl_neon_lane jxl_neon_dct4_vec_inverse(jxl_neon_lane v) {
    const jxl_neon_lane v_flip = vextq_f32(v, v, 2);
    const jxl_neon_lane mul_a =
        vsetq_lane_f32(-1.306563f, vsetq_lane_f32((1.4142135623730951f + 1.0f) * 0.5411961f,
                                                  vsetq_lane_f32(-1.0f, vdupq_n_f32(1.0f), 2), 1),
                       3);
    const jxl_neon_lane mul_b =
        vsetq_lane_f32((1.4142135623730951f - 1.0f) * 1.306563f,
                       vsetq_lane_f32(0.5411961f, vsetq_lane_f32(1.0f, vdupq_n_f32(1.0f), 1), 2), 3);
    const jxl_neon_lane tmp = vaddq_f32(vmulq_f32(v, mul_a), vmulq_f32(v_flip, mul_b));

    const float32x4x2_t uz = vuzpq_f32(tmp, vextq_f32(tmp, tmp, 2));
    const jxl_neon_lane mul = vcombine_f32(vdup_n_f32(1.0f), vdup_n_f32(-1.0f));
    return vaddq_f32(vmulq_f32(uz.val[1], mul), uz.val[0]);
}

jxl_neon_lane jxl_neon_dct4_vec_forward(jxl_neon_lane v) {
    const jxl_neon_lane v_rev =
        vcombine_f32(vrev64_f32(vget_high_f32(v)), vrev64_f32(vget_low_f32(v)));
    const jxl_neon_lane mul =
        vsetq_lane_f32(-1.0f, vsetq_lane_f32(-1.0f, vdupq_n_f32(1.0f), 1), 2);
    const jxl_neon_lane addsub = vaddq_f32(vmulq_f32(v, mul), v_rev);

    const jxl_neon_lane addsub3012 = vextq_f32(addsub, addsub, 3);
    const float32x2_t addsub03 = vrev64_f32(vget_low_f32(addsub3012));
    const float32x2_t addsub12 = vget_high_f32(addsub3012);

    const jxl_neon_lane a = vcombine_f32(addsub03, addsub12);
    const jxl_neon_lane mul_a =
        vsetq_lane_f32(-0.25f * 1.306563f,
                       vsetq_lane_f32(-0.25f,
                                      vsetq_lane_f32((0.7071067811865476f / 2.0f + 0.25f) * 0.5411961f,
                                                     vdupq_n_f32(0.25f), 1),
                                      2),
                       3);
    const jxl_neon_lane b = vcombine_f32(addsub12, addsub03);
    const jxl_neon_lane mul_b =
        vsetq_lane_f32(0.25f * 0.5411961f,
                       vsetq_lane_f32(0.25f,
                                      vsetq_lane_f32((0.7071067811865476f / 2.0f - 0.25f) * 1.306563f,
                                                     vdupq_n_f32(0.25f), 1),
                                      2),
                       3);
    return vaddq_f32(vmulq_f32(a, mul_a), vmulq_f32(b, mul_b));
}

void jxl_neon_dct8_vec_inverse(jxl_neon_lane vl, jxl_neon_lane vr, jxl_neon_lane *out_l,
                               jxl_neon_lane *out_r) {
    const jxl_neon_lane sec_vec =
        vsetq_lane_f32(2.5629154477415055f,
                       vsetq_lane_f32(0.8999762231364156f,
                                      vsetq_lane_f32(0.6013448869350453f,
                                                     vdupq_n_f32(0.5097955791041592f), 1),
                                      2),
                       3);
    const float32x4x2_t uz = vuzpq_f32(vl, vr);
    const jxl_neon_lane input0 = uz.val[0];
    jxl_neon_lane input1 = uz.val[1];
    input1 = vaddq_f32(vmulq_f32(input1, vsetq_lane_f32(1.4142135623730951f, vdupq_n_f32(1.0f), 0)),
                       vextq_f32(vdupq_n_f32(0.0f), input1, 3));
    const jxl_neon_lane output0 = jxl_neon_dct4_vec_inverse(input0);
    const jxl_neon_lane output1 = jxl_neon_dct4_vec_inverse(input1);
    const jxl_neon_lane output1_scaled = vmulq_f32(output1, sec_vec);
    const jxl_neon_lane sub = vsubq_f32(output0, output1_scaled);
    *out_l = vaddq_f32(output0, output1_scaled);
    *out_r = vrev64q_f32(vextq_f32(sub, sub, 2));
}

void jxl_neon_dct8_vec_forward(jxl_neon_lane vl, jxl_neon_lane vr, jxl_neon_lane *out_l,
                               jxl_neon_lane *out_r) {
    const jxl_neon_lane sec_vec =
        vsetq_lane_f32(1.2814577238707527f,
                       vsetq_lane_f32(0.4499881115682078f,
                                      vsetq_lane_f32(0.30067244346752264f,
                                                     vdupq_n_f32(0.2548977895520796f), 1),
                                      2),
                       3);
    const jxl_neon_lane vr_rev = vrev64q_f32(vextq_f32(vr, vr, 2));
    const jxl_neon_lane half = lane_splat(0.5f);
    const jxl_neon_lane input0 = vmulq_f32(vaddq_f32(vl, vr_rev), half);
    const jxl_neon_lane input1 = vmulq_f32(vsubq_f32(vl, vr_rev), sec_vec);
    jxl_neon_lane output0 = jxl_neon_dct4_vec_forward(input0);
    jxl_neon_lane output1 = jxl_neon_dct4_vec_forward(input1);
    output1 = vaddq_f32(vmulq_f32(output1, vsetq_lane_f32(1.4142135623730951f, vdupq_n_f32(1.0f), 0)),
                        vextq_f32(output1, vdupq_n_f32(0.0f), 1));
    *out_l = vzip1q_f32(output0, output1);
    *out_r = vzip2q_f32(output0, output1);
}

void jxl_neon_dct8_inverse_lanes(jxl_neon_lane io[8]) {
    size_t idx;
    const jxl_neon_lane sqrt2 = lane_splat(1.4142135623730951f);
    jxl_neon_lane output0[4];
    jxl_neon_lane output1[4];
    jxl_neon_lane input0[4];
    jxl_neon_lane input1[4];
    const float *sec = jxl_sec_half_small(8);

    input0[0] = io[0];
    input0[1] = io[2];
    input0[2] = io[4];
    input0[3] = io[6];
    input1[0] = vmulq_f32(io[1], sqrt2);
    input1[1] = vaddq_f32(io[3], io[1]);
    input1[2] = vaddq_f32(io[5], io[3]);
    input1[3] = vaddq_f32(io[7], io[5]);
    jxl_neon_dct4_inverse_lanes(output0, input0);
    jxl_neon_dct4_inverse_lanes(output1, input1);
    for (idx = 0; idx < 4; ++idx) {
        const jxl_neon_lane r = vmulq_f32(output1[idx], lane_splat(sec[idx]));
        io[idx] = vaddq_f32(output0[idx], r);
        io[7 - idx] = vsubq_f32(output0[idx], r);
    }
}

void jxl_neon_dct8_forward_lanes(jxl_neon_lane io[8]) {
    const jxl_neon_lane half = lane_splat(0.5f);
    const jxl_neon_lane sqrt2 = lane_splat(1.4142135623730951f);
    jxl_neon_lane output0[4];
    jxl_neon_lane output1[4];
    jxl_neon_lane input0[4];
    jxl_neon_lane input1[4];
    const float *sec = jxl_sec_half_small(8);

    input0[0] = vmulq_f32(vaddq_f32(io[0], io[7]), half);
    input0[1] = vmulq_f32(vaddq_f32(io[1], io[6]), half);
    input0[2] = vmulq_f32(vaddq_f32(io[2], io[5]), half);
    input0[3] = vmulq_f32(vaddq_f32(io[3], io[4]), half);
    input1[0] = vmulq_f32(vsubq_f32(io[0], io[7]), lane_splat(sec[0] / 2.0f));
    input1[1] = vmulq_f32(vsubq_f32(io[1], io[6]), lane_splat(sec[1] / 2.0f));
    input1[2] = vmulq_f32(vsubq_f32(io[2], io[5]), lane_splat(sec[2] / 2.0f));
    input1[3] = vmulq_f32(vsubq_f32(io[3], io[4]), lane_splat(sec[3] / 2.0f));

    jxl_neon_dct4_forward_lanes(output0, input0);
    jxl_neon_dct4_forward_lanes(output1, input1);

    io[0] = output0[0];
    io[2] = output0[1];
    io[4] = output0[2];
    io[6] = output0[3];

    output1[0] = vmulq_f32(output1[0], sqrt2);
    io[1] = vaddq_f32(output1[0], output1[1]);
    io[3] = vaddq_f32(output1[1], output1[2]);
    io[5] = vaddq_f32(output1[2], output1[3]);
    io[7] = output1[3];
}

#endif
