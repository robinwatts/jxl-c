// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_RENDER_VARDCT_NEON_LANE_H_
#define JXL_RENDER_VARDCT_NEON_LANE_H_

#if defined(JXL_HAVE_SIMD_NEON)

#include <arm_neon.h>

typedef float32x4_t jxl_neon_lane;

void jxl_neon_transpose_lane(jxl_neon_lane lanes[4]);

void jxl_neon_dct4_forward_lanes(jxl_neon_lane out[4], const jxl_neon_lane in[4]);

void jxl_neon_dct4_inverse_lanes(jxl_neon_lane out[4], const jxl_neon_lane in[4]);

void jxl_neon_dct4_inverse_4(jxl_neon_lane lanes[4]);

jxl_neon_lane jxl_neon_dct4_vec_inverse(jxl_neon_lane v);

jxl_neon_lane jxl_neon_dct4_vec_forward(jxl_neon_lane v);

void jxl_neon_dct8_vec_inverse(jxl_neon_lane vl, jxl_neon_lane vr, jxl_neon_lane *out_l,
                               jxl_neon_lane *out_r);

void jxl_neon_dct8_vec_forward(jxl_neon_lane vl, jxl_neon_lane vr, jxl_neon_lane *out_l,
                               jxl_neon_lane *out_r);

void jxl_neon_dct8_inverse_lanes(jxl_neon_lane io[8]);

void jxl_neon_dct8_forward_lanes(jxl_neon_lane io[8]);

#endif/* JXL_HAVE_SIMD_NEON */

#endif /* JXL_RENDER_VARDCT_NEON_LANE_H_ */
