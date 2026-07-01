// SPDX-License-Identifier: MIT OR Apache-2.0
#include "subimage_decode.h"

#include "modular/channel_decode.h"
#include "coding/decoder.h"
#include "modular/prepare_subimage.h"
#include "modular/transformed_grid.h"
#include "modular/transform/transform.h"
#include "modular/ma.h"
#include "modular/ma_flat.h"
#include "modular/predictor_state.h"
#include "modular/util.h"

#include "allocator.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

static int channel_matches(const jxl_modular_channel_info *a, const jxl_modular_channel_info *b) {
    return a->width == b->width && a->height == b->height && a->hshift == b->hshift &&
           a->vshift == b->vshift;
}

/* Rust TransformedModularSubimage::decode_inner — one entropy loop for all callers. */
static jxl_modular_status_t transformed_subimage_decode(
    jxl_context *ctx, jxl_bs *bs, const jxl_ma_config *ma, const jxl_wp_header *wp,
    const jxl_modular_channel_info *infos, jxl_modular_grid_i32 **grids, size_t n,
    uint32_t stream_index, int allow_partial, int *out_partial) {
        size_t i;
    uint32_t dist_multiplier;
    int skip_finalize;
    const jxl_coding_decoder *decoder_src;
    jxl_coding_decoder *decoder;
    jxl_ma_flat_tree *trees;
    jxl_modular_grid_i32 **decoded;
    jxl_modular_batch_rle batch_rle;
    jxl_modular_status_t overall;
    int fast_lossless_rle;
    if (out_partial != NULL) {
        *out_partial = 0;
    }
    if (n == 0) {
        return JXL_MODULAR_OK;
    }
    if (bs == NULL || ma == NULL || wp == NULL || infos == NULL || grids == NULL) {
        return JXL_MODULAR_BITSTREAM_ERROR;
    }

    decoder_src = jxl_ma_config_decoder(ma);
    if (decoder_src == NULL || ma->alloc == NULL) {
        return JXL_MODULAR_DECODER_ERROR;
    }
    decoder = NULL;
    if (jxl_coding_decoder_clone(ma->alloc, decoder_src, &decoder) != JXL_CODING_OK) {
        return JXL_MODULAR_OUT_OF_MEMORY;
    }
    jxl_coding_decoder_attach_context(decoder, ctx);
    if (jxl_coding_decoder_begin(decoder, bs) != JXL_CODING_OK) {
        jxl_coding_decoder_destroy(ma->alloc, decoder);
        return allow_partial ? JXL_MODULAR_OK : JXL_MODULAR_DECODER_ERROR;
    }

    dist_multiplier = 0;
    for (i = 0; i < n; ++i) {
        if (infos[i].width > dist_multiplier) {
            dist_multiplier = infos[i].width;
        }
    }

    trees = jxl_calloc(ma->alloc, n, sizeof(*trees));
    decoded = jxl_calloc(ma->alloc, n, sizeof(*decoded));
    if (trees == NULL || decoded == NULL) {
        jxl_free(ma->alloc, trees);
        jxl_free(ma->alloc, decoded);
        jxl_coding_decoder_destroy(ma->alloc, decoder);
        return JXL_MODULAR_OUT_OF_MEMORY;
    }

    overall = JXL_MODULAR_OK;
    fast_lossless_rle = jxl_coding_decoder_is_rle_mode(decoder);
    skip_finalize = 0;

    for (i = 0; i < n && overall == JXL_MODULAR_OK; ++i) {
        size_t j;
        size_t filtered_prev;
        const jxl_modular_channel_info *info = &infos[i];
        if (info->width == 0 || info->height == 0) {
            continue;
        }
        if (grids[i] == NULL) {
            overall = JXL_MODULAR_DECODER_ERROR;
            break;
        }

        jxl_ma_flat_tree_init(&trees[i]);
        filtered_prev = 0;
        for (j = 0; j < i; ++j) {
            if (infos[j].width == 0 || infos[j].height == 0) {
                continue;
            }
            if (channel_matches(info, &infos[j])) {
                ++filtered_prev;
            }
        }
        overall = jxl_ma_flat_tree_build(ma, (uint32_t)i, stream_index, (uint32_t)filtered_prev,
                                         &trees[i]);
        if (overall != JXL_MODULAR_OK) {
            break;
        }
        if (!jxl_ma_flat_tree_is_fast_lossless_gradient(&trees[i])) {
            fast_lossless_rle = 0;
        }
    }

    if (overall == JXL_MODULAR_OK && fast_lossless_rle) {
        size_t i;
        jxl_modular_batch_rle_begin(&batch_rle, decoder);
        for (i = 0; i < n && overall == JXL_MODULAR_OK; ++i) {
            const jxl_modular_channel_info *info = &infos[i];
            const jxl_ma_tree_leaf_clustered *leaf;
            if (info->width == 0 || info->height == 0) {
                continue;
            }
            leaf = jxl_ma_flat_tree_single_leaf(&trees[i]);
            if (leaf == NULL) {
                overall = JXL_MODULAR_DECODER_ERROR;
                break;
            }
            jxl_modular_debug_tokens_set_channel(ctx, i);
            overall = jxl_modular_decode_fast_lossless_i16_rle(bs, decoder, leaf->cluster,
                                                               grids[i], &batch_rle.state);
            if (overall != JXL_MODULAR_OK) {
                break;
            }
            decoded[i] = grids[i];
        }
        if (overall == JXL_MODULAR_OK &&
            jxl_modular_batch_rle_error(&batch_rle) != JXL_CODING_OK) {
            overall = JXL_MODULAR_DECODER_ERROR;
        }
        skip_finalize = 1;
    } else if (overall == JXL_MODULAR_OK) {
        size_t i;
        jxl_modular_predictor_state shared_pred;
        jxl_modular_predictor_state_init(&shared_pred);
        for (i = 0; i < n && overall == JXL_MODULAR_OK; ++i) {
            size_t j;
            size_t prev_count;
            jxl_modular_channel_decode_params cp = {0};
            const jxl_modular_channel_info *info = &infos[i];
            const jxl_modular_grid_i32 *prev[16];
            jxl_modular_status_t st;
            if (info->width == 0 || info->height == 0) {
                continue;
            }

            cp.ctx = ctx;
            cp.alloc = ma->alloc;
            cp.ma_tree = &trees[i];
            cp.wp_header = wp;
            cp.dist_multiplier = dist_multiplier;
            cp.predictor = &shared_pred;


            prev_count = 0;
            for (j = i; j > 0 && prev_count < 16;) {
                --j;
                if (infos[j].width == 0 || infos[j].height == 0) {
                    continue;
                }
                if (channel_matches(info, &infos[j])) {
                    prev[prev_count++] = decoded[j];
                }
            }

            jxl_modular_debug_tokens_set_channel(ctx, i);
            jxl_modular_debug_pixel_set_channel(ctx, i);
            st = jxl_modular_decode_channel(bs, decoder, &cp, grids[i], prev, prev_count);
            if (st != JXL_MODULAR_OK) {
                overall = allow_partial ? JXL_MODULAR_OK : st;
                if (allow_partial && out_partial != NULL) {
                    *out_partial = 1;
                }
                break;
            }
            decoded[i] = grids[i];
            {
                if (JXL_DEBUG_FLAG(ctx, debug_pg_fail) && stream_index == 21 && i == 1 &&
                    grids[1] != NULL && grids[1]->width == 256 && grids[1]->height == 256 &&
                    grids[1]->buf != NULL) {
                        size_t row;
                    int64_t full = 0;
                    int ch0_at;
                    for (row = 0; row < 256; ++row) {
                        size_t x;
                        for (x = 0; x < 256; ++x) {
                            full += jxl_modular_grid_sample_as_i32(grids[1], x, row);
                        }
                    }
                    ch0_at = 0;
                    if (grids[0] != NULL && grids[0]->buf != NULL && grids[0]->width > 171 &&
                        grids[0]->height > 191) {
                        ch0_at = (int)jxl_modular_grid_sample_as_i32(grids[0], 171, 191);
                    }
                    fprintf(stderr,
                            "pg after ch1 ch0(191,171)=%d ch1(191,171)=%d full_sum=%lld bits=%zu\n",
                            ch0_at,
                            (int)jxl_modular_grid_sample_as_i32(grids[1], 171, 191),
                            (long long)full, bs->num_read_bits);
                }
            }
        }
        jxl_modular_predictor_state_free(ma->alloc, &shared_pred);
    }

    if (overall == JXL_MODULAR_OK && !skip_finalize &&
        jxl_coding_decoder_finalize(decoder) != JXL_CODING_OK) {
        overall = allow_partial ? JXL_MODULAR_OK : JXL_MODULAR_DECODER_ERROR;
        if (allow_partial && out_partial != NULL) {
            *out_partial = 1;
        }
    }

    for (i = 0; i < n; ++i) {
        jxl_ma_flat_tree_free(ma->alloc, &trees[i]);
    }
    jxl_free(ma->alloc, trees);
    jxl_free(ma->alloc, decoded);
    jxl_coding_decoder_destroy(ma->alloc, decoder);
    return overall;
}

static jxl_modular_status_t dest_decode_transformed_channels(
    jxl_context *ctx, jxl_allocator_state *alloc, jxl_bs *bs, jxl_modular_image_destination *dest,
    size_t channel_end, uint32_t stream_index, int allow_partial) {
    size_t i;
    const jxl_modular_channels *layout;
    jxl_modular_grid_i32 **grid_ptrs;
    jxl_modular_status_t overall;
    if (channel_end == 0) {
        return JXL_MODULAR_OK;
    }
    if (!dest->subimage_grids_prepared) {
        jxl_modular_status_t pst = jxl_modular_image_prepare_subimage_grids(alloc, dest);
        if (pst != JXL_MODULAR_OK) {
            return pst;
        }
    }
    if (dest->transformed_grids == NULL || dest->transformed_grids_len < channel_end) {
        return JXL_MODULAR_DECODER_ERROR;
    }

    layout = jxl_modular_dest_subimage_channels(dest);
    if (layout == NULL || layout->info_len < channel_end) {
        return JXL_MODULAR_DECODER_ERROR;
    }

    grid_ptrs = jxl_calloc(alloc, channel_end, sizeof(*grid_ptrs));
    if (grid_ptrs == NULL) {
        return JXL_MODULAR_OUT_OF_MEMORY;
    }

    overall = JXL_MODULAR_OK;
    for (i = 0; i < channel_end; ++i) {
        grid_ptrs[i] = jxl_modular_dest_channel_grid(dest, i);
        if (grid_ptrs[i] == NULL) {
            overall = JXL_MODULAR_DECODER_ERROR;
            break;
        }
    }

    if (overall == JXL_MODULAR_OK) {
        overall = transformed_subimage_decode(ctx, bs, &dest->ma_ctx, &dest->header.wp_params,
                                              layout->info, grid_ptrs, channel_end, stream_index,
                                              allow_partial, NULL);
    }
    jxl_free(alloc, grid_ptrs);
    return overall;
}

jxl_modular_status_t jxl_modular_subimage_decode(jxl_context *ctx, jxl_allocator_state *alloc,
                                                 jxl_bs *bs, jxl_modular_image_destination *dest,
                                                 uint32_t stream_index, int allow_partial) {
    const jxl_modular_channels *layout;
    if (alloc == NULL || bs == NULL || dest == NULL) {
        return JXL_MODULAR_BITSTREAM_ERROR;
    }
    if (dest->channels.info_len == 0) {
        return JXL_MODULAR_OK;
    }
    if (dest->image_channels == NULL) {
        return JXL_MODULAR_DECODER_ERROR;
    }
    layout = jxl_modular_dest_subimage_channels(dest);
    if (layout == NULL) {
        return JXL_MODULAR_DECODER_ERROR;
    }
    return dest_decode_transformed_channels(ctx, alloc, bs, dest, layout->info_len, stream_index,
                                            allow_partial);
}

jxl_modular_status_t jxl_modular_gmodular_decode(jxl_context *ctx, jxl_allocator_state *alloc,
                                                 jxl_bs *bs, jxl_modular_image_destination *dest,
                                                 int allow_partial) {
    const jxl_modular_channels *layout;
    size_t n;
    jxl_modular_status_t overall;
    if (alloc == NULL || bs == NULL || dest == NULL) {
        return JXL_MODULAR_BITSTREAM_ERROR;
    }
    layout = jxl_modular_dest_subimage_channels(dest);
    if (layout == NULL) {
        return JXL_MODULAR_DECODER_ERROR;
    }
    n = jxl_modular_gmodular_channel_count(dest);
    if (n == 0) {
        return JXL_MODULAR_OK;
    }

    overall = dest_decode_transformed_channels(ctx, alloc, bs, dest, n, 0,
                                               allow_partial);

    if (overall == JXL_MODULAR_OK && allow_partial && bs->bytes_len == 0 &&
        bs->remaining_buf_bits <= 8) {
        jxl_modular_image_set_partial(dest, 1);
    }
    return overall;
}

jxl_modular_status_t jxl_modular_pass_group_decode(jxl_context *ctx, jxl_bs *bs,
                                                   jxl_modular_transformed_subimage *sub,
                                                   uint32_t stream_index, int allow_partial,
                                                   int *out_partial) {
    size_t i;
    const jxl_ma_config *ma;
    jxl_allocator_state *alloc;
    const jxl_modular_header *header;
    const jxl_modular_channels *channels;
    jxl_modular_grid_i32 **grid_ptrs;
    jxl_modular_status_t st;
    if (bs == NULL || sub == NULL || !jxl_modular_transformed_subimage_is_prepared(sub)) {
        return JXL_MODULAR_BITSTREAM_ERROR;
    }
    ma = &sub->hm.ma_ctx;
    alloc = ma != NULL ? ma->alloc : NULL;
    if (alloc == NULL) {
        return JXL_MODULAR_DECODER_ERROR;
    }
    header = &sub->hm.header;
    channels = &sub->channels;
    if (channels->info_len == 0) {
        if (out_partial != NULL) {
            *out_partial = 0;
        }
        return JXL_MODULAR_OK;
    }
    if (sub->grids == NULL || sub->grids_len < channels->info_len) {
        return JXL_MODULAR_DECODER_ERROR;
    }

    {
        if (JXL_DEBUG_FLAG(ctx, debug_bits)) {
            size_t ci;
            fprintf(stderr, "pg decode prepared ch=%zu stream=%u bits=%zu\n", channels->info_len,
                    stream_index, bs->num_read_bits);
            for (ci = 0; ci < channels->info_len; ++ci) {
                fprintf(stderr, "  prep ch%zu %ux%u hs=%d vs=%d\n", ci, channels->info[ci].width,
                        channels->info[ci].height, channels->info[ci].hshift,
                        channels->info[ci].vshift);
            }
        }
    }

    grid_ptrs =
        jxl_calloc(alloc, channels->info_len, sizeof(*grid_ptrs));
    if (grid_ptrs == NULL) {
        return JXL_MODULAR_OUT_OF_MEMORY;
    }
    for (i = 0; i < channels->info_len; ++i) {
        grid_ptrs[i] = jxl_transformed_grid_leader(&sub->grids[i]);
        if (grid_ptrs[i] == NULL) {
            jxl_free(alloc, grid_ptrs);
            return JXL_MODULAR_DECODER_ERROR;
        }
    }

    st = transformed_subimage_decode(ctx, bs, ma, &header->wp_params, channels->info, grid_ptrs,
                                     channels->info_len, stream_index, allow_partial, out_partial);
    jxl_free(alloc, grid_ptrs);
    return st;
}

jxl_modular_status_t jxl_modular_dest_apply_local_header(jxl_allocator_state *alloc, jxl_bs *bs,
                                                         const jxl_modular_parse_ctx *ctx,
                                                         jxl_modular_image_destination *dest) {
    jxl_modular_channels channels;
    jxl_modular_header_ma hm;
    jxl_modular_status_t st;
    jxl_modular_sample_kind sample_kind;
    if (alloc == NULL || bs == NULL || ctx == NULL || dest == NULL) {
        return JXL_MODULAR_BITSTREAM_ERROR;
    }
    jxl_modular_channels_init(&channels);
    jxl_modular_header_ma_init(&hm);

    st = jxl_modular_read_local_header(alloc, bs, ctx, &hm, &channels);
    if (st != JXL_MODULAR_OK) {
        jxl_modular_channels_free(alloc, &channels);
        jxl_modular_header_ma_free(alloc, &hm);
        return st;
    }

    if (ctx->params == NULL) {
        jxl_modular_channels_free(alloc, &channels);
        jxl_modular_header_ma_free(alloc, &hm);
        return JXL_MODULAR_BITSTREAM_ERROR;
    }

    sample_kind =
        ctx->params->narrow_buffer ? JXL_MODULAR_SAMPLE_I16 : JXL_MODULAR_SAMPLE_I32;
    st = jxl_modular_image_destination_create(alloc, &hm, ctx->params->group_dim,
                                              ctx->params->bit_depth, sample_kind, &channels,
                                              ctx->tracker, dest);
    jxl_modular_channels_free(alloc, &channels);
    return st;
}
