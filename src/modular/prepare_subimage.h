// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_MODULAR_PREPARE_SUBIMAGE_H_
#define JXL_MODULAR_PREPARE_SUBIMAGE_H_

#include "allocator.h"
#include "context.h"
#include "modular/group_subimage.h"
#include "modular/recursive_image.h"
#include "modular/image.h"

/* Channel layout used for decode/grouping after prepare_subimage. */
const jxl_modular_channels *jxl_modular_dest_subimage_channels(
    const jxl_modular_image_destination *dest);

jxl_modular_status_t jxl_modular_image_prepare_subimage_grids(jxl_allocator_state *alloc,
                                                              jxl_modular_image_destination *dest);
jxl_modular_status_t jxl_modular_channels_transform_info(
    jxl_allocator_state *alloc, jxl_modular_image_destination *dest,
    jxl_modular_channels *out);
jxl_modular_status_t jxl_modular_prepare_gmodular(jxl_allocator_state *alloc,
                                                  jxl_modular_image_destination *dest);
jxl_modular_status_t jxl_modular_gmodular_finish(jxl_context *ctx, jxl_allocator_state *alloc,
                                                 jxl_modular_image_destination *dest,
                                                 uint32_t frame_width, uint32_t frame_height,
                                                 uint32_t bit_depth,
                                                 const jxl_modular_params *mod_params);
size_t jxl_modular_gmodular_channel_count(jxl_modular_image_destination *dest);
jxl_modular_status_t jxl_modular_dest_sync_image_channels(jxl_allocator_state *alloc,
                                                          jxl_modular_image_destination *dest);

/* Resolve the grid backing a transformed channel (handles palette merged members). */
jxl_modular_grid *jxl_modular_dest_channel_grid(jxl_modular_image_destination *dest,
                                                size_t channel_idx);

/* Active decode/inverse grid array (transformed views when prepared, else backing). */
jxl_transformed_grid *jxl_modular_dest_work_grids(jxl_modular_image_destination *dest);
size_t jxl_modular_dest_work_grids_len(const jxl_modular_image_destination *dest);
jxl_transformed_grid **jxl_modular_dest_work_grids_storage(jxl_modular_image_destination *dest);
size_t *jxl_modular_dest_work_grids_len_storage(jxl_modular_image_destination *dest);

void jxl_modular_transformed_grids_teardown(jxl_allocator_state *alloc,
                                            jxl_modular_image_destination *dest);
void jxl_modular_dest_finalize_after_inverse(jxl_allocator_state *alloc,
                                             jxl_modular_image_destination *dest);

/* Rust RecursiveModularImage::prepare_subimage(). */
jxl_modular_status_t jxl_modular_recursive_image_prepare_subimage(
    jxl_allocator_state *alloc, jxl_modular_recursive_image *recursive,
    jxl_modular_transformed_subimage *sub, jxl_modular_image_destination *dest);

#endif /* JXL_MODULAR_PREPARE_SUBIMAGE_H_ */
