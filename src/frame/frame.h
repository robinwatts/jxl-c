// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_FRAME_FRAME_H_
#define JXL_FRAME_FRAME_H_

#include "allocator.h"
#include "bitstream/bitstream.h"
#include "frame/error.h"
#include "frame/frame_header.h"
#include "frame/pass_group.h"
#include "frame/toc.h"
#include "image/image_internal.h"

#include <stddef.h>
#include "jxl_oxide/jxl_types.h"

typedef struct {
    jxl_toc_group toc_group;
    uint8_t *bytes;
    size_t bytes_len;
    size_t bytes_cap;
} jxl_frame_group_data;

typedef struct {
    jxl_allocator_state *alloc;
    jxl_frame_header header;
    jxl_toc toc;
    jxl_frame_group_data *data;
    size_t data_len;
    size_t reading_data_index;
} jxl_frame;

void jxl_frame_init(jxl_frame *f);
void jxl_frame_free(jxl_allocator_state *alloc, jxl_frame *f);

jxl_frame_status_t jxl_frame_parse(jxl_allocator_state *alloc, jxl_bs *bs,
                                   const jxl_parsed_image_header *image, jxl_frame *out);

jxl_frame_status_t jxl_frame_parse_keyframe(jxl_allocator_state *alloc, jxl_bs *bs,
                                            const jxl_parsed_image_header *image,
                                            const uint8_t *codestream, size_t cs_len,
                                            jxl_frame *out);

jxl_frame_status_t jxl_frame_parse_nth_keyframe(jxl_allocator_state *alloc, jxl_bs *bs,
                                                const jxl_parsed_image_header *image,
                                                const uint8_t *codestream, size_t cs_len,
                                                uint32_t keyframe_index, jxl_frame *out);

jxl_frame_status_t jxl_count_keyframes(jxl_allocator_state *alloc, jxl_bs *bs,
                                       const jxl_parsed_image_header *image,
                                       const uint8_t *codestream, size_t cs_len,
                                       uint32_t *count_out);

const uint8_t *jxl_frame_feed_bytes(jxl_frame *f, const uint8_t *buf, size_t buf_len,
                                    size_t *consumed_out);

int jxl_frame_is_loading_done(const jxl_frame *f);
size_t jxl_frame_total_group_bytes(const jxl_frame *f);

const jxl_frame_group_data *jxl_frame_group_by_kind(const jxl_frame *frame,
                                                    jxl_toc_group_kind kind, uint32_t index);

/* Rust PassGroupBitstream::partial — TOC payload not fully received yet. */
int jxl_frame_group_allow_partial(const jxl_frame_group_data *group);

#endif /* JXL_FRAME_FRAME_H_ */
