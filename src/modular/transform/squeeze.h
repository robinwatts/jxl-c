// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_MODULAR_TRANSFORM_SQUEEZE_H_
#define JXL_MODULAR_TRANSFORM_SQUEEZE_H_

#include "allocator.h"
#include "context.h"
#include "modular/image.h"

#include <stddef.h>

void jxl_squeeze_inverse_h_i32(jxl_allocator_state *alloc, int32_t *merged, size_t width, size_t height, size_t row_stride);
void jxl_squeeze_inverse_v_i32(jxl_allocator_state *alloc, int32_t *merged, size_t width, size_t height, size_t row_stride);
void jxl_squeeze_inverse_h_i16(jxl_context *ctx, jxl_allocator_state *alloc, int16_t *merged,
                               size_t width, size_t height, size_t row_stride);
void jxl_squeeze_inverse_v_i16(jxl_context *ctx, jxl_allocator_state *alloc, int16_t *merged,
                               size_t width, size_t height, size_t row_stride);

#endif /* JXL_MODULAR_TRANSFORM_SQUEEZE_H_ */
