// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_MODULAR_GROUP_DECODE_H_
#define JXL_MODULAR_GROUP_DECODE_H_

#include "frame/frame.h"
#include "image/image_internal.h"
#include "jxl_oxide/jxl_status.h"
#include "modular/image.h"
#include "modular/ma.h"
#include "modular/param.h"
#include "modular/region.h"

#include "allocator.h"
#include "context.h"

/* Decode all LF-group modular tile payloads (Rust load_lf_groups / mlf_group). */
jxl_status_t jxl_modular_decode_frame_lf_groups(jxl_context *ctx, jxl_allocator_state *alloc, const jxl_frame *frame,
                                                const jxl_ma_config *global_ma, int has_ma,
                                                jxl_modular_image_destination *dest,
                                                int allow_partial,
                                                const jxl_modular_region *filter_region);

/* Decode all pass-group modular tile payloads (Rust decode_pass_group_modular). */
jxl_status_t jxl_modular_decode_frame_pass_groups(jxl_context *ctx, jxl_allocator_state *alloc,
                                                  const jxl_frame *frame,
                                                  const jxl_ma_config *global_ma, int has_ma,
                                                  const jxl_modular_params *mod_params,
                                                  jxl_modular_image_destination *dest,
                                                  int allow_partial,
                                                  const jxl_modular_region *filter_region);

/* Largest pass-group modular payload when no multi-group TOC (squeeze-only frames). */
jxl_status_t jxl_modular_decode_pass_group_fallback(jxl_context *ctx, jxl_allocator_state *alloc,
                                                    const jxl_parsed_image_header *parsed,
                                                    const jxl_frame *frame, int has_ma,
                                                    jxl_ma_config *global_ma,
                                                    jxl_modular_params *mod_params,
                                                    jxl_modular_image_destination *dest);

/* LF + pass group modular decode after gmodular stream-0 (Rust render_modular groups phase). */
jxl_status_t jxl_modular_decode_frame_group_coefficients(
    jxl_context *ctx, jxl_allocator_state *alloc, const jxl_frame *frame, const jxl_parsed_image_header *parsed,
    const jxl_ma_config *global_ma, int has_ma, const jxl_modular_params *mod_params,
    jxl_modular_image_destination *dest, int multi_group, int allow_partial,
    const jxl_modular_region *filter_region);

#endif /* JXL_MODULAR_GROUP_DECODE_H_ */
