// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_RENDER_VARDCT_SSE2_LANE_H_
#define JXL_RENDER_VARDCT_SSE2_LANE_H_

#include <emmintrin.h>

typedef __m128 jxl_sse2_lane;

/* _mm_shuffle_ps immediates — keep in sync with
 * crates/jxl-render/src/vardct/x86_64/dct/mod.rs and transform.rs */
#define JXL_SSE2_SHUFFLE_A0B0 0x88u /* 0b10001000: even lanes from a/b (_MM_SHUFFLE(2,0,2,0)) */
#define JXL_SSE2_SHUFFLE_A1B1 0xDDu /* 0b11011101: odd lanes from a/b (_MM_SHUFFLE(3,1,3,1)) */
#define JXL_SSE2_SHUFFLE_REVERSE4 0x1Bu /* 0b00011011: reverse lane (_MM_SHUFFLE(0,1,2,3)) */
#define JXL_SSE2_SHUFFLE_DCT4_FLIP 0x4Eu /* 0b01001110: dct4_vec flip */
#define JXL_SSE2_SHUFFLE_DCT4_TMP_A 0x28u /* 0b00101000 */
#define JXL_SSE2_SHUFFLE_DCT4_TMP_B 0x7Du /* 0b01111101 */
#define JXL_SSE2_SHUFFLE_DCT4_FWD_A 0x9Cu /* 0b10011100: dct4_vec_forward */
#define JXL_SSE2_SHUFFLE_DCT4_FWD_B 0xC9u /* 0b11001001: dct4_vec_forward */

void jxl_sse2_transpose_lane(jxl_sse2_lane lanes[4]);

void jxl_sse2_dct4_forward_lanes(jxl_sse2_lane out[4], const jxl_sse2_lane in[4]);

void jxl_sse2_dct4_inverse_lanes(jxl_sse2_lane out[4], const jxl_sse2_lane in[4]);

void jxl_sse2_dct4_inverse_4(jxl_sse2_lane lanes[4]);

jxl_sse2_lane jxl_sse2_dct4_vec_inverse(jxl_sse2_lane v);

jxl_sse2_lane jxl_sse2_dct4_vec_forward(jxl_sse2_lane v);

void jxl_sse2_dct8_vec_inverse(jxl_sse2_lane vl, jxl_sse2_lane vr, jxl_sse2_lane *out_l,
                               jxl_sse2_lane *out_r);

void jxl_sse2_dct8_vec_forward(jxl_sse2_lane vl, jxl_sse2_lane vr, jxl_sse2_lane *out_l,
                               jxl_sse2_lane *out_r);

void jxl_sse2_dct8_inverse_lanes(jxl_sse2_lane io[8]);

void jxl_sse2_dct8_forward_lanes(jxl_sse2_lane io[8]);

#endif /* JXL_RENDER_VARDCT_SSE2_LANE_H_ */
