// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_MODULAR_CHANNEL_DECODE_INTERNAL_H_
#define JXL_MODULAR_CHANNEL_DECODE_INTERNAL_H_

#include "channel_decode.h"

jxl_modular_status_t jxl_modular_channel_read_token(jxl_modular_rle_state *rle,
                                                    jxl_coding_decoder *decoder, jxl_bs *bs,
                                                    uint8_t cluster, uint32_t dist_multiplier,
                                                    uint32_t *out);

jxl_modular_status_t jxl_modular_channel_decode_slow(jxl_context *ctx, jxl_bs *bs,
                                                     jxl_coding_decoder *decoder,
                                                     uint32_t dist_multiplier,
                                                     const jxl_ma_flat_tree *tree,
                                                     jxl_modular_predictor_state *pred,
                                                     jxl_modular_grid_i32 *grid,
                                                     jxl_modular_rle_state *rle);

#endif /* JXL_MODULAR_CHANNEL_DECODE_INTERNAL_H_ */
