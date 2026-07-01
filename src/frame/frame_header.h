// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_FRAME_FRAME_HEADER_H_
#define JXL_FRAME_FRAME_HEADER_H_

#include "bitstream/bitstream.h"
#include "frame/error.h"
#include "frame/filter.h"
#include "image/image_internal.h"
#include "allocator.h"

#include "jxl_oxide/jxl_types.h"

typedef enum {
    JXL_FRAME_TYPE_REGULAR = 0,
    JXL_FRAME_TYPE_LF = 1,
    JXL_FRAME_TYPE_REFERENCE_ONLY = 2,
    JXL_FRAME_TYPE_SKIP_PROGRESSIVE = 3,
} jxl_frame_type;

typedef enum {
    JXL_FRAME_ENCODING_VARDCT = 0,
    JXL_FRAME_ENCODING_MODULAR = 1,
} jxl_frame_encoding;

typedef struct {
    uint64_t flags;
} jxl_frame_flags;

typedef enum {
    JXL_BLEND_REPLACE = 0,
    JXL_BLEND_ADD = 1,
    JXL_BLEND_BLEND = 2,
    JXL_BLEND_MUL_ADD = 3,
    JXL_BLEND_MUL = 4,
} jxl_blend_mode;

typedef struct {
    jxl_blend_mode mode;
    uint32_t alpha_channel;
    int clamp;
    uint32_t source;
} jxl_blending_info;

typedef struct {
    uint32_t num_passes;
    uint32_t num_ds;
    uint32_t *shift;
    size_t shift_len;
    uint32_t *downsample;
    size_t downsample_len;
    uint32_t *last_pass;
    size_t last_pass_len;
} jxl_frame_passes;

typedef struct {
    jxl_frame_type frame_type;
    jxl_frame_encoding encoding;
    jxl_frame_flags flags;
    int do_ycbcr;
    uint32_t jpeg_upsampling[3];
    uint32_t upsampling;
    uint32_t *ec_upsampling;
    size_t ec_upsampling_len;
    uint32_t group_size_shift;
    uint32_t x_qm_scale;
    uint32_t b_qm_scale;
    jxl_frame_passes passes;
    uint32_t lf_level;
    int have_crop;
    int32_t x0;
    int32_t y0;
    uint32_t width;
    uint32_t height;
    uint32_t duration;
    uint32_t timecode;
    int is_last;
    uint32_t save_as_reference;
    int resets_canvas;
    int save_before_ct;
    uint32_t encoded_color_channels;
    jxl_blending_info blending_info;
    jxl_blending_info *ec_blending_info;
    size_t ec_blending_info_len;
    jxl_restoration_filter restoration;
} jxl_frame_header;

void jxl_frame_passes_free(jxl_allocator_state *alloc, jxl_frame_passes *p);
void jxl_frame_header_init(jxl_frame_header *h);
void jxl_frame_header_free(jxl_allocator_state *alloc, jxl_frame_header *h);

jxl_frame_status_t jxl_frame_header_parse(jxl_allocator_state *alloc, jxl_bs *bs,
                                          const jxl_parsed_image_header *image,
                                          jxl_frame_header *out);

uint32_t jxl_frame_header_num_groups(const jxl_frame_header *h);
uint32_t jxl_frame_header_num_lf_groups(const jxl_frame_header *h);
uint32_t jxl_frame_header_groups_per_row(const jxl_frame_header *h);
uint32_t jxl_frame_header_lf_group_dim(const jxl_frame_header *h);
uint32_t jxl_frame_header_lf_groups_per_row(const jxl_frame_header *h);
uint32_t jxl_frame_header_lf_group_idx_from_group_idx(const jxl_frame_header *h, uint32_t group_idx);
uint32_t jxl_frame_header_group_idx_from_coord(const jxl_frame_header *h, uint32_t x, uint32_t y);
void jxl_frame_header_group_size_for(const jxl_frame_header *h, uint32_t group_idx, uint32_t *width_out,
                                     uint32_t *height_out);
void jxl_frame_header_lf_group_size_for(const jxl_frame_header *h, uint32_t lf_group_idx,
                                      uint32_t *width_out, uint32_t *height_out);
uint32_t jxl_frame_header_pass_group_modular_stream_index(const jxl_frame_header *h, uint32_t pass_idx,
                                                          uint32_t group_idx);
uint32_t jxl_frame_header_group_dim(const jxl_frame_header *h);
uint32_t jxl_frame_header_color_sample_width(const jxl_frame_header *h);
uint32_t jxl_frame_header_color_sample_height(const jxl_frame_header *h);

int jxl_frame_header_is_normal_frame(const jxl_frame_header *h);
int jxl_frame_header_is_keyframe(const jxl_frame_header *h);
int jxl_frame_flags_use_lf_frame(const jxl_frame_flags *f);
int jxl_frame_flags_skip_adaptive_lf_smoothing(const jxl_frame_flags *f);
int jxl_frame_flags_noise(const jxl_frame_flags *f);
int jxl_frame_flags_patches(const jxl_frame_flags *f);
int jxl_frame_flags_splines(const jxl_frame_flags *f);

#endif /* JXL_FRAME_FRAME_HEADER_H_ */
