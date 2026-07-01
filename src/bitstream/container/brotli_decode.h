// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_CONTAINER_BROTLI_DECODE_H_
#define JXL_CONTAINER_BROTLI_DECODE_H_

#include "allocator.h"
#include "../error.h"

#include <stddef.h>
#include "jxl_oxide/jxl_types.h"

typedef struct jxl_brotli_decoder jxl_brotli_decoder;

jxl_brotli_decoder *jxl_brotli_decoder_create(jxl_allocator_state *alloc);
void jxl_brotli_decoder_destroy(jxl_allocator_state *alloc, jxl_brotli_decoder *dec);

jxl_bs_status_t jxl_brotli_decoder_feed(jxl_brotli_decoder *dec, const uint8_t *data, size_t len);
jxl_bs_status_t jxl_brotli_decoder_finish(jxl_brotli_decoder *dec);

const uint8_t *jxl_brotli_decoder_output(const jxl_brotli_decoder *dec, size_t *len);
int jxl_brotli_decoder_is_finished(const jxl_brotli_decoder *dec);
void jxl_brotli_decoder_reset(jxl_brotli_decoder *dec);

#endif /* JXL_CONTAINER_BROTLI_DECODE_H_ */
