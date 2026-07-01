// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_FRAME_TOC_H_
#define JXL_FRAME_TOC_H_

#include "allocator.h"
#include "bitstream/bitstream.h"
#include "frame/error.h"
#include "frame/frame_header.h"

#include <stddef.h>
#include "jxl_oxide/jxl_types.h"

typedef enum {
    JXL_TOC_KIND_ALL = 0,
    JXL_TOC_KIND_LF_GLOBAL,
    JXL_TOC_KIND_LF_GROUP,
    JXL_TOC_KIND_HF_GLOBAL,
    JXL_TOC_KIND_GROUP_PASS,
} jxl_toc_group_kind;

typedef struct {
    jxl_toc_group_kind kind;
    uint32_t lf_group_idx;
    uint32_t pass_idx;
    uint32_t group_idx;
    size_t offset;
    uint32_t size;
} jxl_toc_group;

typedef struct {
    uint32_t num_lf_groups;
    uint32_t num_groups;
    jxl_toc_group *groups;
    size_t groups_len;
    size_t *bitstream_to_original;
    size_t bitstream_to_original_len;
    size_t *original_to_bitstream;
    size_t original_to_bitstream_len;
    size_t total_size;
    size_t bookmark;
} jxl_toc;

void jxl_toc_init(jxl_toc *t);
void jxl_toc_free(jxl_allocator_state *alloc, jxl_toc *t);

jxl_frame_status_t jxl_toc_parse(jxl_allocator_state *alloc, jxl_bs *bs,
                                 const jxl_frame_header *header, jxl_toc *out);

void jxl_toc_adjust_offsets(jxl_toc *t, size_t global_frame_offset);

int jxl_toc_is_single_entry(const jxl_toc *t);
size_t jxl_toc_group_index_bitstream_order(const jxl_toc *t, jxl_toc_group_kind kind,
                                           uint32_t index);

#endif /* JXL_FRAME_TOC_H_ */
