// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_RENDER_COLOR_FASTMATH_INTERNAL_H_
#define JXL_RENDER_COLOR_FASTMATH_INTERNAL_H_

#include <immintrin.h>

#if defined(JXL_HAVE_SIMD_SSE41)
__m128 jxl_fastmath_powf_vec_x86_sse41(__m128 base, __m128 exp);
__m128 jxl_fastmath_rational_eval5_x86_sse2(__m128 x, const float p[5], const float q[5]);
#endif

#if defined(JXL_HAVE_SIMD_AVX2)
__m256 jxl_fastmath_powf_vec_x86_avx2(__m256 base, __m256 exp);
__m128 jxl_fastmath_powf_vec_x86_fma(__m128 base, __m128 exp);
__m128 jxl_fastmath_rational_eval5_x86_fma(__m128 x, const float p[5], const float q[5]);
__m256 jxl_fastmath_rational_eval5_x86_avx2(__m256 x, const float p[5], const float q[5]);
#endif

#endif /* JXL_RENDER_COLOR_FASTMATH_INTERNAL_H_ */
