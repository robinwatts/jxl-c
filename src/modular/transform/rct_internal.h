// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_MODULAR_TRANSFORM_RCT_INTERNAL_H_
#define JXL_MODULAR_TRANSFORM_RCT_INTERNAL_H_

#include "render/simd/features.h"

#include <stddef.h>
#include "jxl_oxide/jxl_types.h"

void jxl_rct_inverse_row_i16_base(uint32_t ty, int16_t *ra, int16_t *rb, int16_t *rc, size_t width);
void jxl_rct_inverse_row_i32_base(uint32_t ty, int32_t *ra, int32_t *rb, int32_t *rc, size_t width);

void jxl_rct_inverse_row_i16_pixel(uint32_t ty, int16_t *ra, int16_t *rb, int16_t *rc);
void jxl_rct_inverse_row_i32_pixel(uint32_t ty, int32_t *ra, int32_t *rb, int32_t *rc);

void jxl_rct_inverse_row_i16(uint32_t ty, int16_t *ra, int16_t *rb, int16_t *rc, size_t width,
                             const jxl_cpu_features *feat);
void jxl_rct_inverse_row_i32(uint32_t ty, int32_t *ra, int32_t *rb, int32_t *rc, size_t width,
                             const jxl_cpu_features *feat);

#if defined(JXL_HAVE_SIMD_AVX2)
void jxl_rct_inverse_row_i16_avx2(uint32_t ty, int16_t *ra, int16_t *rb, int16_t *rc, size_t width);
void jxl_rct_inverse_export_row3_i16_avx2(uint32_t ty, int16_t *ra, int16_t *rb, int16_t *rc,
                                          size_t width, int export_row, uint32_t export_x0,
                                          uint32_t export_w, float scale, float *dst0,
                                          float *dst1, float *dst2);
void jxl_rct_inverse_row_i32_avx2(uint32_t ty, int32_t *ra, int32_t *rb, int32_t *rc, size_t width);
#endif

#if defined(JXL_HAVE_SIMD_NEON)
void jxl_rct_inverse_row_i16_neon(uint32_t ty, int16_t *ra, int16_t *rb, int16_t *rc, size_t width);
void jxl_rct_inverse_row_i32_neon(uint32_t ty, int32_t *ra, int32_t *rb, int32_t *rc, size_t width);
#endif

#endif /* JXL_MODULAR_TRANSFORM_RCT_INTERNAL_H_ */
