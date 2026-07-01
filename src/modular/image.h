// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_MODULAR_IMAGE_H_
#define JXL_MODULAR_IMAGE_H_

#include "allocator.h"
#include "grid/alloc_tracker.h"
#include "modular/channel.h"
#include "modular/ma.h"
#include "modular/float_export.h"
#include "modular/modular_parse.h"

typedef struct jxl_modular_grid_i32 jxl_modular_grid_i32;
/* Unified modular grid; `jxl_modular_grid_i32` name kept for minimal churn during migration. */
typedef jxl_modular_grid_i32 jxl_modular_grid;
typedef struct jxl_transformed_grid jxl_transformed_grid;
typedef struct jxl_modular_global_groups jxl_modular_global_groups;

typedef enum {
    JXL_MODULAR_SAMPLE_I16 = 0,
    JXL_MODULAR_SAMPLE_I32 = 1,
} jxl_modular_sample_kind;

struct jxl_modular_grid_i32 {
    void *buf;
    jxl_modular_sample_kind kind;
    size_t width;
    size_t height;
    size_t stride;
    size_t offset;
    size_t buf_len;
    jxl_grid_alloc_handle *handle;
};

void jxl_modular_grid_i32_init_empty(jxl_modular_grid_i32 *g);
void jxl_modular_grid_normalize_stride(jxl_modular_grid *g);
int jxl_modular_grid_create(jxl_allocator_state *alloc, size_t width, size_t height,
                            jxl_grid_alloc_tracker *tracker,
                            jxl_modular_sample_kind kind, jxl_modular_grid_i32 *out);
int jxl_modular_grid_i32_create(jxl_allocator_state *alloc, size_t width, size_t height,
                                jxl_grid_alloc_tracker *tracker,
                                jxl_modular_grid_i32 *out);
int jxl_modular_grid_i16_create(jxl_allocator_state *alloc, size_t width, size_t height,
                                jxl_grid_alloc_tracker *tracker,
                                jxl_modular_grid_i32 *out);
int jxl_modular_grid_clone(jxl_allocator_state *alloc, const jxl_modular_grid_i32 *src,
                           jxl_modular_grid_i32 *dst);
void jxl_modular_grid_i32_destroy(jxl_allocator_state *alloc, jxl_modular_grid_i32 *g);

jxl_modular_sample_kind jxl_modular_grid_sample_kind(const jxl_modular_grid_i32 *g);
size_t jxl_modular_grid_elem_size(const jxl_modular_grid_i32 *g);
#define jxl_modular_grid_row_stride(g) (((g) != NULL) ? (g)->stride : (size_t)0)

int32_t jxl_modular_grid_sample_as_i32(const jxl_modular_grid_i32 *g, size_t x, size_t y);
void jxl_modular_grid_store_i32(jxl_modular_grid_i32 *g, size_t x, size_t y, int32_t sample);

int16_t *jxl_modular_grid_row_i16(jxl_modular_grid_i32 *g, size_t y);
int32_t *jxl_modular_grid_row_i32(jxl_modular_grid_i32 *g, size_t y);
const int16_t *jxl_modular_grid_row_i16_const(const jxl_modular_grid_i32 *g, size_t y);
const int32_t *jxl_modular_grid_row_i32_const(const jxl_modular_grid_i32 *g, size_t y);

/* Subgrid views: offset/stride are in sample elements (i16 or i32), not bytes. */
jxl_modular_grid jxl_modular_grid_tile_view(jxl_modular_grid *parent, size_t tile_x, size_t tile_y,
                                            size_t tile_w, size_t tile_h);
jxl_modular_grid jxl_modular_grid_split_horizontal_in_place(jxl_modular_grid *g);
jxl_modular_grid jxl_modular_grid_split_vertical_in_place(jxl_modular_grid *g);
void jxl_modular_grid_split_h_in_place(jxl_modular_grid *g);
void jxl_modular_grid_split_v_in_place(jxl_modular_grid *g);
int jxl_modular_grid_group_view_at(jxl_modular_grid *parent, size_t group_width,
                                   size_t group_height, size_t num_cols, size_t num_rows,
                                   size_t group_idx, jxl_modular_grid *out);

typedef struct {
    jxl_modular_header header;
    jxl_ma_config ma_ctx;
    int ma_owns;
    uint32_t group_dim;
    uint32_t bit_depth;
    jxl_modular_sample_kind sample_kind;
    jxl_modular_channels channels;
    /* Channel layout after prepare_subimage (matches squeezed grid buffers). */
    jxl_modular_channels transformed_channels;
    jxl_modular_grid *meta_channels;
    size_t meta_channels_len;
    jxl_modular_grid *image_channels;
    size_t image_channels_len;
    /* Transformed views into backing image_channels (Rust prepare_subimage grids). */
    jxl_transformed_grid *transformed_grids;
    size_t transformed_grids_len;
    int channel_info_transformed;
    int subimage_grids_prepared;
    /* Cached per-group subimages (Rust TransformedGlobalModular). */
    jxl_modular_global_groups *group_layout;
    int group_layout_valid;
    int gmodular_partial;
    jxl_modular_float_export_ctx float_export;
} jxl_modular_image_destination;

void jxl_modular_image_destination_init(jxl_modular_image_destination *dest);
void jxl_modular_image_destination_free(jxl_allocator_state *alloc,
                                       jxl_modular_image_destination *dest);

jxl_modular_status_t jxl_modular_image_destination_create(
    jxl_allocator_state *alloc, jxl_modular_header_ma *header_ma, uint32_t group_dim,
    uint32_t bit_depth, jxl_modular_sample_kind sample_kind, const jxl_modular_channels *channels,
    jxl_grid_alloc_tracker *tracker, jxl_modular_image_destination *out);

int jxl_modular_image_has_palette(const jxl_modular_image_destination *dest);
int jxl_modular_image_has_squeeze(const jxl_modular_image_destination *dest);

int jxl_modular_image_is_partial(const jxl_modular_image_destination *dest);
void jxl_modular_image_set_partial(jxl_modular_image_destination *dest, int partial);

int64_t jxl_modular_dest_sample_sum(const jxl_modular_image_destination *dest, size_t max_px);

#endif /* JXL_MODULAR_IMAGE_H_ */
