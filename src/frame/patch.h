// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_FRAME_PATCH_H_
#define JXL_FRAME_PATCH_H_

#include "frame/error.h"
#include "frame/frame_header.h"
#include "image/image_internal.h"

#include "allocator.h"
#include "bitstream/bitstream.h"

#include <stddef.h>
#include "jxl_oxide/jxl_types.h"

typedef enum {
    JXL_PATCH_BLEND_NONE = 0,
    JXL_PATCH_BLEND_REPLACE,
    JXL_PATCH_BLEND_ADD,
    JXL_PATCH_BLEND_MUL,
    JXL_PATCH_BLEND_BLEND_ABOVE,
    JXL_PATCH_BLEND_BLEND_BELOW,
    JXL_PATCH_BLEND_MULADD_ABOVE,
    JXL_PATCH_BLEND_MULADD_BELOW,
} jxl_patch_blend_mode;

typedef struct {
    jxl_patch_blend_mode mode;
    uint32_t alpha_channel;
    int clamp;
} jxl_patch_blending_info;

typedef struct {
    int32_t x;
    int32_t y;
    jxl_patch_blending_info *blending;
    size_t blending_len;
} jxl_patch_target;

typedef struct {
    uint32_t ref_idx;
    uint32_t x0;
    uint32_t y0;
    uint32_t width;
    uint32_t height;
    jxl_patch_target *targets;
    size_t targets_len;
} jxl_patch_ref;

typedef struct {
    jxl_patch_ref *refs;
    size_t refs_len;
} jxl_patches;

void jxl_patches_init(jxl_patches *p);
void jxl_patches_free(jxl_allocator_state *alloc, jxl_patches *p);

jxl_frame_status_t jxl_patches_parse(jxl_allocator_state *alloc, jxl_bs *bs,
                                     const jxl_parsed_image_header *image,
                                     const jxl_frame_header *frame, jxl_patches *out);

int jxl_frame_header_can_reference(const jxl_frame_header *h);

#endif /* JXL_FRAME_PATCH_H_ */
