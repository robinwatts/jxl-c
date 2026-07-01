// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_CODING_CDECODER_MODULAR_INTERNAL_H_
#define JXL_CODING_CDECODER_MODULAR_INTERNAL_H_

#include "coding/decoder.h"

/* Used by modular decode_slow hoisted entropy reads. */
typedef struct jxl_coding_hoist_slot {
    uint32_t ans_state;
    uint32_t lz_num_to_copy;
    uint32_t lz_copy_pos;
    uint32_t lz_num_decoded;
} jxl_coding_hoist_slot;

int jxl_coding_decoder_hoist_available(const jxl_coding_decoder *dec);
int jxl_coding_decoder_hoist_begin(jxl_coding_decoder *dec, jxl_coding_hoist_slot *out);
void jxl_coding_decoder_hoist_publish(jxl_coding_decoder *dec, const jxl_coding_hoist_slot *slot);
void jxl_coding_decoder_hoist_capture(jxl_coding_decoder *dec, jxl_coding_hoist_slot *slot);
void jxl_coding_decoder_hoist_commit(jxl_coding_decoder *dec, const jxl_coding_hoist_slot *slot);

jxl_coding_status_t jxl_coding_decoder_lz77_store_at(jxl_coding_decoder *dec, uint32_t *num_decoded,
                                                     uint32_t r);

jxl_coding_status_t jxl_coding_decoder_lz77_from_repeat_token_hoisted(jxl_coding_decoder *dec,
                                                                      jxl_bs *bs,
                                                                      jxl_coding_hoist_slot *slot,
                                                                      uint32_t dist_multiplier,
                                                                      uint32_t repeat_token,
                                                                      uint32_t *out);

jxl_coding_status_t jxl_coding_decoder_lz77_store(jxl_coding_decoder *dec, uint32_t r);

jxl_coding_status_t jxl_coding_decoder_lz77_from_repeat_token(jxl_coding_decoder *dec,
                                                                jxl_bs *bs,
                                                                uint32_t dist_multiplier,
                                                                uint32_t repeat_token,
                                                                uint32_t *out);

#endif /* JXL_CODING_CDECODER_MODULAR_INTERNAL_H_ */
