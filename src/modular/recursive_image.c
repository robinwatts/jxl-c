// SPDX-License-Identifier: MIT OR Apache-2.0
#include "recursive_image.h"

#include "modular/transform/transform.h"

#include "allocator.h"
#include <string.h>

static int bs_near_end(const jxl_bs *bs) {
    if (bs == NULL) {
        return 0;
    }
    return bs->bytes_len == 0 && bs->remaining_buf_bits <= 8;
}

void jxl_modular_recursive_image_init(jxl_modular_recursive_image *img) {
    if (img != NULL) {
        memset(img, 0, sizeof(*img));
        jxl_modular_channels_init(&img->channels);
        jxl_modular_header_ma_init(&img->hm);
    }
}

void jxl_modular_recursive_image_teardown(jxl_allocator_state *alloc,
                                        jxl_modular_recursive_image *img) {
    if (img == NULL) {
        return;
    }
    if (img->meta_channels != NULL) {
        size_t i;
        for (i = 0; i < img->meta_channels_len; ++i) {
            jxl_modular_grid_i32_destroy(alloc, &img->meta_channels[i]);
        }
        jxl_free(alloc, img->meta_channels);
        img->meta_channels = NULL;
        img->meta_channels_len = 0;
    }
    jxl_modular_channels_free(alloc, &img->channels);
    jxl_modular_channels_init(&img->channels);
    if (alloc != NULL) {
        jxl_modular_header_ma_free(alloc, &img->hm);
    } else if (img->hm.header.transform != NULL || img->hm.ma_owns) {
        jxl_modular_header_free(alloc, &img->hm.header);
        if (img->hm.ma_owns) {
            jxl_ma_config_init(&img->hm.ma_ctx);
        }
        img->hm.ma_owns = 0;
    } else {
        jxl_modular_header_ma_init(&img->hm);
    }
    img->valid = 0;
}

int jxl_modular_recursive_image_is_valid(const jxl_modular_recursive_image *img) {
    return img != NULL && img->valid;
}

jxl_modular_status_t jxl_modular_subimage_recursive(
    jxl_allocator_state *alloc, jxl_bs *bs, const jxl_modular_transformed_subimage *sub,
    jxl_modular_image_destination *dest, const jxl_modular_params *mod_params,
    const jxl_ma_config *global_ma, int allow_partial, jxl_modular_recursive_image *out) {
    size_t ti;
    jxl_modular_channels channels;
    jxl_modular_parse_ctx ctx = {0};
    jxl_modular_status_t st;
    if (alloc == NULL || bs == NULL || dest == NULL || mod_params == NULL || sub == NULL ||
        out == NULL) {
        return JXL_MODULAR_BITSTREAM_ERROR;
    }
    jxl_modular_recursive_image_teardown(alloc, out);

    if (jxl_modular_transformed_subimage_is_empty(sub)) {
        return JXL_MODULAR_OK;
    }

    ctx.params = mod_params;
    ctx.global_ma = global_ma;
    ctx.tracker = NULL;
    ctx.retain_pretransform_channels = 1;


    jxl_modular_channels_init(&channels);
    for (ti = 0; ti < sub->tile_count; ++ti) {
        jxl_modular_status_t pst = jxl_modular_channels_push(alloc, &channels, sub->tiles[ti].info);
        if (pst != JXL_MODULAR_OK) {
            jxl_modular_channels_free(alloc, &channels);
            return pst;
        }
    }

    st = jxl_modular_read_local_header(alloc, bs, &ctx, &out->hm, &channels);
    if (st != JXL_MODULAR_OK) {
        jxl_modular_channels_free(alloc, &channels);
        jxl_modular_recursive_image_teardown(alloc, out);
        if (st == JXL_MODULAR_BITSTREAM_ERROR && allow_partial && bs_near_end(bs)) {
            return JXL_MODULAR_OK;
        }
        return st;
    }

    out->meta_channels = NULL;
    out->meta_channels_len = 0;
    for (ti = 0; ti < out->hm.header.transform_len; ++ti) {
        size_t meta_w = 0;
        size_t meta_h = 0;
        size_t new_len;
        jxl_modular_grid_i32 *grown;
        jxl_modular_sample_kind grid_kind;
        if (jxl_transform_prepare_meta_channels(&out->hm.header.transform[ti], &meta_w, &meta_h) !=
            JXL_MODULAR_OK) {
            jxl_modular_channels_free(alloc, &channels);
            jxl_modular_recursive_image_teardown(alloc, out);
            return JXL_MODULAR_BITSTREAM_ERROR;
        }
        if (meta_h == 0 ||
            (meta_w == 0 && out->hm.header.transform[ti].kind != JXL_TRANSFORM_KIND_PALETTE)) {
            continue;
        }
        new_len = out->meta_channels_len + 1;
        grown = jxl_realloc(alloc, out->meta_channels, new_len * sizeof(*out->meta_channels));
        if (grown == NULL) {
            jxl_modular_channels_free(alloc, &channels);
            jxl_modular_recursive_image_teardown(alloc, out);
            return JXL_MODULAR_OUT_OF_MEMORY;
        }
        out->meta_channels = grown;
        if (out->meta_channels_len > 0) {
            memmove(&out->meta_channels[1], &out->meta_channels[0],
                    out->meta_channels_len * sizeof(*out->meta_channels));
        }
        jxl_modular_grid_i32_init_empty(&out->meta_channels[0]);
        grid_kind = dest->sample_kind;
        if (!jxl_modular_grid_create(alloc, meta_w, meta_h, NULL, grid_kind, &out->meta_channels[0])) {
            jxl_modular_channels_free(alloc, &channels);
            jxl_modular_recursive_image_teardown(alloc, out);
            return JXL_MODULAR_OUT_OF_MEMORY;
        }
        out->meta_channels_len = new_len;
    }

    out->channels = channels;
    out->valid = 1;
    return JXL_MODULAR_OK;
}
