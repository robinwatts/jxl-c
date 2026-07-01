// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_MODULAR_CHANNEL_DECODE_H_
#define JXL_MODULAR_CHANNEL_DECODE_H_

#include "context.h"
#include "bitstream/bitstream.h"
#include "coding/decoder.h"
#include "modular/error.h"
#include "modular/image.h"
#include "modular/ma_flat.h"
#include "modular/predictor.h"
#include "modular/predictor_state.h"

typedef struct {
    uint32_t last_raw;
    int16_t last_i16;
    uint32_t repeat;
    jxl_coding_status_t error;
} jxl_modular_rle_state;

typedef struct {
    jxl_modular_rle_state state;
} jxl_modular_batch_rle;

typedef struct {
    jxl_context *ctx;
    jxl_allocator_state *alloc;
    const jxl_ma_flat_tree *ma_tree;
    const jxl_wp_header *wp_header;
    uint32_t dist_multiplier;
    jxl_modular_predictor_state *predictor;
    jxl_modular_rle_state *rle;
} jxl_modular_channel_decode_params;

jxl_modular_status_t jxl_modular_decode_channel(jxl_bs *bs, jxl_coding_decoder *decoder,
                                                const jxl_modular_channel_decode_params *params,
                                                jxl_modular_grid_i32 *grid,
                                                const jxl_modular_grid_i32 *const *prev_channels,
                                                size_t prev_channel_count);

/* Share one RLE state across multiple channel decodes (pass-group / subimage batch). */
void jxl_modular_batch_rle_begin(jxl_modular_batch_rle *batch, jxl_coding_decoder *decoder);
jxl_coding_status_t jxl_modular_batch_rle_error(const jxl_modular_batch_rle *batch);

#ifdef NDEBUG
#define jxl_modular_debug_tokens_set_pg(C,P,G) while (0) {}
#define jxl_modular_debug_tokens_set_channel(C,D) while (0) {}
#define jxl_modular_debug_pixel_set_channel(C,D) while (0) {}
#else
void jxl_modular_debug_tokens_set_pg(jxl_context *ctx, unsigned pass_idx, unsigned group_idx);
void jxl_modular_debug_tokens_set_channel(jxl_context *ctx, size_t dest_channel);
void jxl_modular_debug_pixel_set_channel(jxl_context *ctx, size_t dest_channel);
#endif

jxl_modular_status_t jxl_modular_decode_fast_lossless_i16_rle(jxl_bs *bs, jxl_coding_decoder *decoder,
                                                              uint8_t cluster,
                                                              jxl_modular_grid_i32 *grid,
                                                              jxl_modular_rle_state *rle);

jxl_modular_status_t jxl_modular_decode_fast_lossless_grad(jxl_bs *bs, jxl_coding_decoder *decoder,
                                                           uint8_t cluster,
                                                           jxl_modular_grid_i32 *grid,
                                                           jxl_modular_rle_state *rle);

#endif /* JXL_MODULAR_CHANNEL_DECODE_H_ */
