// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_MODULAR_SUBIMAGE_DECODE_H_
#define JXL_MODULAR_SUBIMAGE_DECODE_H_

#include "allocator.h"
#include "context.h"
#include "bitstream/bitstream.h"
#include "modular/error.h"
#include "modular/image.h"
#include "modular/modular_parse.h"
#include "modular/group_subimage.h"

jxl_modular_status_t jxl_modular_subimage_decode(jxl_context *ctx, jxl_allocator_state *alloc,
                                                 jxl_bs *bs, jxl_modular_image_destination *dest,
                                                 uint32_t stream_index, int allow_partial);

/* Decode global-modular channels (stream 0) that fit in one group. */
jxl_modular_status_t jxl_modular_gmodular_decode(jxl_context *ctx, jxl_allocator_state *alloc,
                                                 jxl_bs *bs, jxl_modular_image_destination *dest,
                                                 int allow_partial);

jxl_modular_status_t jxl_modular_dest_apply_local_header(jxl_allocator_state *alloc, jxl_bs *bs,
                                                         const jxl_modular_parse_ctx *ctx,
                                                         jxl_modular_image_destination *dest);

/* Entropy decode after recursive().prepare_subimage() (Rust subimage.decode). */
jxl_modular_status_t jxl_modular_pass_group_decode(jxl_context *ctx, jxl_bs *bs,
                                                   jxl_modular_transformed_subimage *sub,
                                                   uint32_t stream_index, int allow_partial,
                                                   int *out_partial);

#endif /* JXL_MODULAR_SUBIMAGE_DECODE_H_ */
