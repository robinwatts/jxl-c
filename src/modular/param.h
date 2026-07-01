// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_MODULAR_PARAM_H_
#define JXL_MODULAR_PARAM_H_

#include "allocator.h"
#include "context.h"
#include "frame/frame_header.h"
#include "image/image_internal.h"

#include <stddef.h>
#include "jxl_oxide/jxl_types.h"

typedef enum {
    JXL_CHANNEL_SHIFT_JPEG_UPSAMPLING = 0,
    JXL_CHANNEL_SHIFT_SHIFTS = 1,
    JXL_CHANNEL_SHIFT_RAW = 2,
} jxl_channel_shift_kind;

typedef struct {
    jxl_channel_shift_kind kind;
    /* JPEG upsampling */
    int has_h_subsample;
    int h_subsample;
    int has_v_subsample;
    int v_subsample;
    /* Shifts / raw */
    uint32_t shift;
    int32_t raw_h;
    int32_t raw_v;
} jxl_channel_shift;

typedef struct {
    uint32_t width;
    uint32_t height;
    jxl_channel_shift shift;
} jxl_modular_channel_params;

typedef struct {
    uint32_t group_dim;
    uint32_t bit_depth;
    jxl_modular_channel_params *channels;
    size_t num_channels;
    int narrow_buffer;
} jxl_modular_params;

jxl_channel_shift jxl_channel_shift_from_shift(uint32_t shift);
jxl_channel_shift jxl_channel_shift_from_jpeg_upsampling(const uint32_t jpeg_upsampling[3],
                                                           size_t idx);
jxl_channel_shift jxl_channel_shift_raw(int32_t h, int32_t v);

int32_t jxl_channel_shift_hshift(const jxl_channel_shift *s);
int32_t jxl_channel_shift_vshift(const jxl_channel_shift *s);
void jxl_channel_shift_shift_size(const jxl_channel_shift *s, uint32_t width, uint32_t height,
                                  uint32_t *out_w, uint32_t *out_h);

void jxl_modular_params_init(jxl_modular_params *p);
void jxl_modular_params_free(jxl_allocator_state *alloc, jxl_modular_params *p);

int jxl_modular_params_set_channels(jxl_allocator_state *alloc, jxl_modular_params *p,
                                    uint32_t width, uint32_t height, uint32_t group_dim,
                                    uint32_t bit_depth, const jxl_channel_shift *shifts,
                                    size_t num_shifts);

/* Build channel shifts for a modular-encoded frame (matches Rust GlobalModular). */
int jxl_modular_params_set_for_modular_frame(jxl_allocator_state *alloc, jxl_context *ctx,
                                             jxl_modular_params *p,
                                             const jxl_parsed_image_header *image,
                                             const jxl_frame_header *frame);
int jxl_modular_params_set_for_vardct_frame(jxl_allocator_state *alloc, jxl_context *ctx,
                                              jxl_modular_params *p,
                                              const jxl_parsed_image_header *image,
                                              const jxl_frame_header *frame);

#endif /* JXL_MODULAR_PARAM_H_ */
