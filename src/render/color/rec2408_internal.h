// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_RENDER_COLOR_REC2408_INTERNAL_H_
#define JXL_RENDER_COLOR_REC2408_INTERNAL_H_

#include "render/color/rec2408.h"

#include <stddef.h>

#if defined(JXL_HAVE_SIMD_SSE41)
#include <immintrin.h>
#endif

typedef struct {
    float min_source_luminance;
    float source_pq_diff;
    float min_luminance;
    float max_luminance;
    float ks;
    float b;
    float one_sub_ks;
} jxl_rec2408_eetf_params;

jxl_rec2408_eetf_params jxl_rec2408_eetf_prep(float intensity_target,
                                              jxl_luminance_nits_range from_luminance_range,
                                              jxl_luminance_nits_range to_luminance_range);

float jxl_rec2408_eetf_with_params(float from_pq_sample, const jxl_rec2408_eetf_params *params);

void jxl_rec2408_eetf_pq_base(float *samples, size_t n, const jxl_rec2408_eetf_params *params);

#if defined(JXL_HAVE_SIMD_SSE41)
__m128 jxl_rec2408_eetf_vec_x86_sse2(__m128 from_pq_sample, const jxl_rec2408_eetf_params *params);
void jxl_rec2408_eetf_pq_x86_sse2(float *samples, size_t n, const jxl_rec2408_eetf_params *params);
#endif

#if defined(JXL_HAVE_SIMD_AVX2)
__m128 jxl_rec2408_eetf_vec_x86_fma(__m128 from_pq_sample, const jxl_rec2408_eetf_params *params);
__m256 jxl_rec2408_eetf_vec_x86_avx2(__m256 from_pq_sample, const jxl_rec2408_eetf_params *params);
void jxl_rec2408_eetf_pq_x86_fma(float *samples, size_t n, const jxl_rec2408_eetf_params *params);
void jxl_rec2408_eetf_pq_x86_avx2(float *samples, size_t n, const jxl_rec2408_eetf_params *params);
#endif

#if defined(JXL_HAVE_SIMD_NEON)
#include <arm_neon.h>

float32x4_t jxl_rec2408_eetf_vec_neon(float32x4_t from_pq_sample, const jxl_rec2408_eetf_params *params);
void jxl_rec2408_eetf_pq_neon(float *samples, size_t n, const jxl_rec2408_eetf_params *params);
#endif

#endif /* JXL_RENDER_COLOR_REC2408_INTERNAL_H_ */
