// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_CODING_CDECODER_PRIVATE_H_
#define JXL_CODING_CDECODER_PRIVATE_H_

#include "coding/ans.h"
#include "coding/decoder.h"
#include "coding/prefix.h"

typedef struct jxl_integer_config {
    uint32_t split_exponent;
    uint32_t split;
    uint32_t msb_in_token;
    uint32_t lsb_in_token;
} jxl_integer_config;

typedef enum {
    JXL_CODER_KIND_PREFIX = 0,
    JXL_CODER_KIND_ANS = 1,
} jxl_coder_kind;

typedef struct jxl_coder {
    jxl_coder_kind kind;
    union {
        struct {
            jxl_prefix_histogram *histograms;
            size_t count;
        } prefix;
        struct {
            jxl_ans_histogram *histograms;
            size_t count;
            uint32_t state;
            int initial;
        } ans;
    } u;
} jxl_coder;

typedef struct jxl_lz77_state {
    jxl_integer_config lz_len_conf;
    uint32_t *window;
    size_t window_cap;
    uint32_t num_to_copy;
    uint32_t copy_pos;
    uint32_t num_decoded;
} jxl_lz77_state;

struct jxl_coding_decoder {
    jxl_allocator_state *alloc;
    jxl_context *ctx;
    int lz77_enabled;
    uint32_t lz_min_symbol;
    uint32_t lz_min_length;
    jxl_lz77_state lz;

    uint8_t *clusters;
    size_t num_dist;
    jxl_integer_config *configs;
    size_t num_clusters;
    jxl_coder code;

    jxl_coding_prefix_rle_fast *fast_rle_cache;
    size_t fast_rle_cache_len;
    int fast_rle_cache_ready;
};

#endif /* JXL_CODING_CDECODER_PRIVATE_H_ */
