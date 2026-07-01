// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_MODULAR_GROUP_SUBIMAGE_H_
#define JXL_MODULAR_GROUP_SUBIMAGE_H_

#include "allocator.h"
#include "context.h"
#include "frame/frame_header.h"
#include "modular/image.h"
#include "modular/ma.h"
#include "modular/param.h"
#include "modular/modular_parse.h"
#include "modular/transformed_grid.h"

#include <stddef.h>
#include "jxl_oxide/jxl_types.h"

typedef struct {
    size_t dest_channel_idx;
    jxl_modular_channel_info info;
    size_t tile_x;
    size_t tile_y;
} jxl_modular_pg_tile;

/* Per-group modular decode context (Rust TransformedModularSubimage). */
typedef struct {
    jxl_modular_header_ma hm;
    jxl_modular_channels channels;
    size_t *channel_indices;
    size_t channel_indices_len;
    jxl_modular_pg_tile *tiles;
    size_t tile_count;
    size_t tile_cap;
    int partial;
    int prepared;
    jxl_transformed_grid *grids;
    size_t grids_len;
} jxl_modular_transformed_subimage;

/* LF + pass group subimages (Rust TransformedGlobalModular). */
typedef struct jxl_modular_global_groups {
    jxl_modular_transformed_subimage *lf_groups;
    size_t num_lf_groups;
    jxl_modular_transformed_subimage *pass_groups;
    size_t num_passes;
    size_t num_groups;
} jxl_modular_global_groups;

void jxl_modular_transformed_subimage_init(jxl_modular_transformed_subimage *sub);
void jxl_modular_transformed_subimage_free(jxl_allocator_state *alloc,
                                           jxl_modular_transformed_subimage *sub);
void jxl_modular_subimage_teardown_prepared(jxl_allocator_state *alloc,
                                            jxl_modular_transformed_subimage *sub);
int jxl_modular_transformed_subimage_is_empty(const jxl_modular_transformed_subimage *sub);
int jxl_modular_transformed_subimage_is_partial(const jxl_modular_transformed_subimage *sub);
int jxl_modular_transformed_subimage_is_prepared(const jxl_modular_transformed_subimage *sub);

void jxl_modular_global_groups_init(jxl_modular_global_groups *groups);
void jxl_modular_global_groups_free(jxl_allocator_state *alloc,
                                    jxl_modular_global_groups *groups);

jxl_modular_status_t jxl_modular_prepare_global_groups(jxl_allocator_state *alloc,
                                                     jxl_modular_image_destination *dest,
                                                       const jxl_frame_header *frame_header,
                                                       jxl_modular_global_groups *out);

jxl_modular_transformed_subimage *jxl_modular_global_lf_group(jxl_modular_global_groups *groups,
                                                              uint32_t lf_group_idx);

jxl_modular_transformed_subimage *
jxl_modular_global_pass_group(jxl_modular_global_groups *groups, uint32_t pass_idx,
                              uint32_t group_idx);

jxl_modular_status_t jxl_modular_ensure_group_layout(jxl_allocator_state *alloc,
                                                   jxl_modular_image_destination *dest,
                                                   const jxl_frame_header *frame_header);

void jxl_modular_clear_group_layout(jxl_allocator_state *alloc,
                                    jxl_modular_image_destination *dest);

jxl_modular_global_groups *jxl_modular_dest_group_layout(jxl_modular_image_destination *dest);

/* Rust recursive().prepare_subimage().decode().finish() on one group subimage. */
jxl_modular_status_t jxl_modular_subimage_recursive_decode(
    jxl_context *ctx, jxl_allocator_state *alloc, jxl_bs *bs, jxl_modular_transformed_subimage *sub,
    jxl_modular_image_destination *dest, const jxl_modular_params *mod_params,
    const jxl_ma_config *global_ma, uint32_t stream_index, int allow_partial, int *out_complete);

#endif /* JXL_MODULAR_GROUP_SUBIMAGE_H_ */
