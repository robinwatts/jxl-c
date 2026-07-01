// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_MODULAR_RECURSIVE_IMAGE_H_
#define JXL_MODULAR_RECURSIVE_IMAGE_H_

#include "modular/error.h"
#include "modular/group_subimage.h"
#include "modular/image.h"
#include "modular/ma.h"
#include "modular/modular_parse.h"
#include "modular/param.h"

#include <stddef.h>

/* Rust RecursiveModularImage — local header + meta backing between recursive() and prepare. */
typedef struct jxl_modular_recursive_image {
    jxl_modular_header_ma hm;
    jxl_modular_channels channels;
    jxl_modular_grid_i32 *meta_channels;
    size_t meta_channels_len;
    int valid;
} jxl_modular_recursive_image;

void jxl_modular_recursive_image_init(jxl_modular_recursive_image *img);
void jxl_modular_recursive_image_teardown(jxl_allocator_state *alloc,
                                        jxl_modular_recursive_image *img);

int jxl_modular_recursive_image_is_valid(const jxl_modular_recursive_image *img);

/* Rust TransformedModularSubimage::recursive(). */
jxl_modular_status_t jxl_modular_subimage_recursive(
    jxl_allocator_state *alloc, jxl_bs *bs, const jxl_modular_transformed_subimage *sub,
    jxl_modular_image_destination *dest, const jxl_modular_params *mod_params,
    const jxl_ma_config *global_ma, int allow_partial, jxl_modular_recursive_image *out);

#endif /* JXL_MODULAR_RECURSIVE_IMAGE_H_ */
