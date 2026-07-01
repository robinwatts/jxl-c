// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_MODULAR_TRANSFORM_INVERSE_H_
#define JXL_MODULAR_TRANSFORM_INVERSE_H_

#include "allocator.h"
#include "context.h"
#include "modular/error.h"
#include "modular/image.h"
#include "modular/transformed_grid.h"
#include "modular/modular_parse.h"
#include "modular/param.h"

#include <stddef.h>
#include "jxl_oxide/jxl_types.h"

/* Rust TransformedModularSubimage::finish — reverse transforms on a grid array. */
jxl_modular_status_t jxl_modular_subimage_finish(jxl_context *ctx, jxl_allocator_state *alloc,
                                                 const jxl_modular_header *header,
                                                 jxl_transformed_grid **grids, size_t *grids_len,
                                                 uint32_t bit_depth,
                                                 jxl_modular_image_destination *dest,
                                                 const jxl_modular_params *mod_params);

jxl_modular_status_t jxl_modular_image_apply_inverse_transforms(
    jxl_context *ctx, jxl_allocator_state *alloc, jxl_modular_image_destination *dest,
    uint32_t frame_width, uint32_t frame_height, uint32_t bit_depth,
    const jxl_modular_params *mod_params);

#endif /* JXL_MODULAR_TRANSFORM_INVERSE_H_ */
