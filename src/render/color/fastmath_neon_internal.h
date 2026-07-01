// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_RENDER_COLOR_FASTMATH_NEON_INTERNAL_H_
#define JXL_RENDER_COLOR_FASTMATH_NEON_INTERNAL_H_

#if defined(JXL_HAVE_SIMD_NEON)
#include <arm_neon.h>

float32x4_t jxl_fastmath_rational_eval3_neon(float32x4_t x, const float p[3], const float q[3]);
float32x4_t jxl_fastmath_rational_eval5_neon(float32x4_t x, const float p[5], const float q[5]);
float32x4_t jxl_fastmath_powf_vec_neon(float32x4_t base, float exponent);

void jxl_fastmath_powf_f32_neon(float *samples, size_t n, float exponent);

#endif /* defined(JXL_HAVE_SIMD_NEON) */

#endif /* JXL_RENDER_COLOR_FASTMATH_NEON_INTERNAL_H_ */
