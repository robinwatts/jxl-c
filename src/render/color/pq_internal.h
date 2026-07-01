// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_RENDER_COLOR_PQ_INTERNAL_H_
#define JXL_RENDER_COLOR_PQ_INTERNAL_H_

#include <stddef.h>

#if defined(JXL_HAVE_SIMD_SSE41)
#include <immintrin.h>
#endif

float jxl_color_linear_to_pq_sample(float s, float intensity_target);
float jxl_color_pq_to_linear_sample(float s, float intensity_target);

void jxl_color_linear_to_pq_base(float *samples, size_t n, float intensity_target);
void jxl_color_pq_to_linear_base(float *samples, size_t n, float intensity_target);

#if defined(JXL_HAVE_SIMD_SSE41)
__m128 jxl_color_linear_to_pq_vec_x86_sse2(__m128 v, float intensity_target);
__m128 jxl_color_pq_to_linear_vec_x86_sse2(__m128 v, float intensity_target);
void jxl_color_linear_to_pq_x86_sse2(float *samples, size_t n, float intensity_target);
void jxl_color_pq_to_linear_x86_sse2(float *samples, size_t n, float intensity_target);
#endif

#if defined(JXL_HAVE_SIMD_AVX2)
__m128 jxl_color_linear_to_pq_vec_x86_fma(__m128 v, float intensity_target);
__m128 jxl_color_pq_to_linear_vec_x86_fma(__m128 v, float intensity_target);
__m256 jxl_color_linear_to_pq_vec_x86_avx2(__m256 v, float intensity_target);
__m256 jxl_color_pq_to_linear_vec_x86_avx2(__m256 v, float intensity_target);
void jxl_color_linear_to_pq_x86_fma(float *samples, size_t n, float intensity_target);
void jxl_color_pq_to_linear_x86_fma(float *samples, size_t n, float intensity_target);
void jxl_color_linear_to_pq_x86_avx2(float *samples, size_t n, float intensity_target);
void jxl_color_pq_to_linear_x86_avx2(float *samples, size_t n, float intensity_target);
#endif

#if defined(JXL_HAVE_SIMD_NEON)
#include <arm_neon.h>

float32x4_t jxl_color_linear_to_pq_vec_neon(float32x4_t v, float intensity_target);
float32x4_t jxl_color_pq_to_linear_vec_neon(float32x4_t v, float intensity_target);
void jxl_color_linear_to_pq_neon(float *samples, size_t n, float intensity_target);
void jxl_color_pq_to_linear_neon(float *samples, size_t n, float intensity_target);
#endif

#endif /* JXL_RENDER_COLOR_PQ_INTERNAL_H_ */
