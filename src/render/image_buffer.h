// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_RENDER_IMAGE_BUFFER_H_
#define JXL_RENDER_IMAGE_BUFFER_H_

#include "allocator.h"
#include "modular/image.h"

#include "jxl_oxide/jxl_status.h"

#include <stddef.h>
#include <stdint.h>

/* Rust ImageBuffer: F32 / I16 / I32 modular grids. */
typedef enum {
    JXL_IMAGE_BUFFER_F32 = 0,
    JXL_IMAGE_BUFFER_I16,
    JXL_IMAGE_BUFFER_I32,
} jxl_image_buffer_kind;

typedef struct jxl_image_buffer {
    jxl_image_buffer_kind kind;
    union {
        struct {
            float *data;
            /* When 0, data points into a parent jxl_render samples block. */
            int owns;
        } f32;
        jxl_modular_grid_i32 grid;
    } u;
} jxl_image_buffer;

void jxl_image_buffer_init_empty(jxl_image_buffer *buf);
void jxl_image_buffer_bind_f32(jxl_allocator_state *alloc, jxl_image_buffer *buf, float *data);
void jxl_image_buffer_take_grid(jxl_allocator_state *alloc, jxl_image_buffer *buf,
                                jxl_modular_grid_i32 *src);
void jxl_image_buffer_destroy(jxl_allocator_state *alloc, jxl_image_buffer *buf);

size_t jxl_image_buffer_width(const jxl_image_buffer *buf);
size_t jxl_image_buffer_height(const jxl_image_buffer *buf);

/*
 * Convert integer modular storage to owned f32 in out_data (w*h samples).
 * On success, buf becomes an F32 view into out_data (non-owning).
 */
jxl_status_t jxl_image_buffer_convert_to_float_modular(jxl_allocator_state *alloc,
                                                       jxl_image_buffer *buf,
                                                       uint32_t bit_depth_bits, float *out_data,
                                                       uint32_t out_stride, uint32_t out_height);

#endif /* JXL_RENDER_IMAGE_BUFFER_H_ */
