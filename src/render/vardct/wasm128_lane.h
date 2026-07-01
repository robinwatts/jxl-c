// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_RENDER_VARDCT_WASM128_LANE_H_
#define JXL_RENDER_VARDCT_WASM128_LANE_H_

#if defined(JXL_HAVE_SIMD_WASM128)

#include <wasm_simd128.h>

typedef v128_t jxl_wasm128_lane;

void jxl_wasm128_transpose_lane(jxl_wasm128_lane lanes[4]);

void jxl_wasm128_dct4_forward_lanes(jxl_wasm128_lane out[4], const jxl_wasm128_lane in[4]);

void jxl_wasm128_dct4_inverse_lanes(jxl_wasm128_lane out[4], const jxl_wasm128_lane in[4]);

void jxl_wasm128_dct4_inverse_4(jxl_wasm128_lane lanes[4]);

jxl_wasm128_lane jxl_wasm128_dct4_vec_inverse(jxl_wasm128_lane v);

jxl_wasm128_lane jxl_wasm128_dct4_vec_forward(jxl_wasm128_lane v);

void jxl_wasm128_dct8_vec_inverse(jxl_wasm128_lane vl, jxl_wasm128_lane vr, jxl_wasm128_lane *out_l,
                                  jxl_wasm128_lane *out_r);

void jxl_wasm128_dct8_vec_forward(jxl_wasm128_lane vl, jxl_wasm128_lane vr, jxl_wasm128_lane *out_l,
                                  jxl_wasm128_lane *out_r);

void jxl_wasm128_dct8_inverse_lanes(jxl_wasm128_lane io[8]);

void jxl_wasm128_dct8_forward_lanes(jxl_wasm128_lane io[8]);

#endif /* JXL_HAVE_SIMD_WASM128 */

#endif /* JXL_RENDER_VARDCT_WASM128_LANE_H_ */
