// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_CODING_DECODER_H_
#define JXL_CODING_DECODER_H_

#include "allocator.h"
#include "bitstream/bitstream.h"
#include "coding/error.h"

#include <stddef.h>

typedef struct jxl_context jxl_context;
#include "jxl_oxide/jxl_types.h"

typedef struct jxl_coding_decoder jxl_coding_decoder;

void jxl_coding_decoder_attach_context(jxl_coding_decoder *dec, jxl_context *ctx);

jxl_context *jxl_coding_decoder_context(const jxl_coding_decoder *dec);

jxl_coding_status_t jxl_coding_decoder_parse(jxl_allocator_state *alloc, jxl_bs *bs,
                                             uint32_t num_dist, jxl_coding_decoder **out);

void jxl_coding_decoder_destroy(jxl_allocator_state *alloc, jxl_coding_decoder *dec);

jxl_coding_status_t jxl_coding_decoder_clone(jxl_allocator_state *alloc,
                                             const jxl_coding_decoder *src,
                                             jxl_coding_decoder **out);

jxl_coding_status_t jxl_coding_decoder_begin(jxl_coding_decoder *dec, jxl_bs *bs);

jxl_coding_status_t jxl_coding_decoder_finalize(const jxl_coding_decoder *dec);

jxl_coding_status_t jxl_coding_decoder_read_varint(jxl_coding_decoder *dec, jxl_bs *bs,
                                                   uint32_t ctx, uint32_t *out);

jxl_coding_status_t jxl_coding_decoder_read_varint_with_multiplier(jxl_coding_decoder *dec,
                                                                   jxl_bs *bs, uint32_t ctx,
                                                                   uint32_t dist_multiplier,
                                                                   uint32_t *out);

jxl_coding_status_t jxl_coding_decoder_read_varint_clustered(jxl_coding_decoder *dec, jxl_bs *bs,
                                                             uint8_t cluster, uint32_t dist_multiplier,
                                                             uint32_t *out);

/* Returns 1 if decoder always emits a single token for cluster (no LZ77). */
int jxl_coding_decoder_single_token(const jxl_coding_decoder *dec, uint8_t cluster,
                                    uint32_t *token_out);

/* True when LZ77 is enabled in libjxl RLE mode (distance cluster single sym 1, split_exp 0). */
int jxl_coding_decoder_is_rle_mode(const jxl_coding_decoder *dec);

typedef enum {
    JXL_CODING_RLE_VALUE = 0,
    JXL_CODING_RLE_REPEAT = 1,
} jxl_coding_rle_token_kind;

typedef struct {
    jxl_coding_rle_token_kind kind;
    uint32_t value;
    uint32_t repeat;
} jxl_coding_rle_token;

jxl_coding_status_t jxl_coding_decoder_read_rle_token(jxl_coding_decoder *dec, jxl_bs *bs,
                                                      uint8_t cluster,
                                                      jxl_coding_rle_token *out);

/* Fused prefix+RLE read for modular decode_fast_lossless (mirrors Rust DecoderRleMode). */
typedef struct jxl_coding_prefix_rle_fast jxl_coding_prefix_rle_fast;

int jxl_coding_prefix_rle_fast_available(jxl_coding_decoder *dec, uint8_t cluster);

int jxl_coding_prefix_rle_fast_init(jxl_coding_decoder *dec, uint8_t cluster,
                                    jxl_coding_prefix_rle_fast *out);

jxl_coding_status_t jxl_coding_decoder_prepare_fast_rle(jxl_coding_decoder *dec);

const jxl_coding_prefix_rle_fast *jxl_coding_decoder_fast_rle_cluster(
    const jxl_coding_decoder *dec, uint8_t cluster);

jxl_coding_status_t jxl_coding_prefix_rle_next_raw(jxl_coding_prefix_rle_fast *fast, jxl_bs *bs,
                                                   uint32_t *repeat, int16_t *last_value);

/* Rust decode_fast_lossless: single noinline entry; pass fast_rle config by pointer. */
#if defined(__GNUC__) || defined(__clang__)
#define JXL_CODING_FAST_DECODE_NOINLINE __attribute__((noinline))
#else
#define JXL_CODING_FAST_DECODE_NOINLINE
#endif

jxl_coding_status_t jxl_coding_decode_fast_lossless_gradient_i16(
    jxl_bs *bs, const jxl_coding_prefix_rle_fast *fast, uint32_t *repeat, int16_t *last_value,
    jxl_coding_status_t *defer_err, int16_t * jxl_restrict pixels, size_t width, size_t height,
    size_t row_stride) JXL_ATTRIBUTE_HOT JXL_CODING_FAST_DECODE_NOINLINE;

const uint8_t *jxl_coding_decoder_cluster_map(const jxl_coding_decoder *dec, size_t *len_out);

jxl_coding_status_t jxl_coding_read_clusters(jxl_allocator_state *alloc, jxl_bs *bs,
                                             uint32_t num_dist, uint32_t *num_clusters_out,
                                             uint8_t **clusters_out, size_t *clusters_len_out);

jxl_coding_status_t jxl_coding_read_permutation(jxl_allocator_state *alloc, jxl_bs *bs,
                                                jxl_coding_decoder *dec, uint32_t size,
                                                uint32_t skip, size_t **permutation_out,
                                                size_t *permutation_len_out);

void jxl_coding_permutation_destroy(jxl_allocator_state *alloc, size_t *permutation);

#endif /* JXL_CODING_DECODER_H_ */
