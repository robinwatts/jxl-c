// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_MODULAR_TRANSFORM_SQUEEZE_INTERNAL_H_
#define JXL_MODULAR_TRANSFORM_SQUEEZE_INTERNAL_H_

#include "allocator.h"

#include <stddef.h>
#include "jxl_oxide/jxl_types.h"

void jxl_squeeze_inverse_h_i16_base(jxl_allocator_state *alloc, int16_t *merged, size_t width,
                                    size_t height, size_t row_stride);
void jxl_squeeze_inverse_v_i16_base(jxl_allocator_state *alloc, int16_t *merged, size_t width,
                                      size_t height, size_t row_stride);

#if defined(JXL_HAVE_SIMD_SSE41)
void jxl_squeeze_inverse_h_i16_x86_sse41(jxl_allocator_state *alloc, int16_t *merged, size_t width,
                                         size_t height, size_t row_stride);
void jxl_squeeze_inverse_v_i16_x86_sse41(jxl_allocator_state *alloc, int16_t *merged, size_t width,
                                         size_t height, size_t row_stride);
#endif

#if defined(JXL_HAVE_SIMD_AVX2)
void jxl_squeeze_inverse_h_i16_x86_avx2(jxl_allocator_state *alloc, int16_t *merged, size_t width,
                                        size_t height, size_t row_stride);
void jxl_squeeze_inverse_v_i16_x86_avx2(jxl_allocator_state *alloc, int16_t *merged, size_t width,
                                        size_t height, size_t row_stride);
#endif

#if defined(JXL_HAVE_SIMD_NEON)
void jxl_squeeze_inverse_h_i16_neon(jxl_allocator_state *alloc, int16_t *merged, size_t width,
                                    size_t height, size_t row_stride);
void jxl_squeeze_inverse_v_i16_neon(jxl_allocator_state *alloc, int16_t *merged, size_t width,
                                    size_t height, size_t row_stride);
#endif

#if defined(JXL_HAVE_SIMD_WASM128)
void jxl_squeeze_inverse_h_i16_wasm128(jxl_allocator_state *alloc, int16_t *merged, size_t width,
                                        size_t height, size_t row_stride);
void jxl_squeeze_inverse_v_i16_wasm128(jxl_allocator_state *alloc, int16_t *merged, size_t width,
                                       size_t height, size_t row_stride);
#endif

#endif /* JXL_MODULAR_TRANSFORM_SQUEEZE_INTERNAL_H_ */
