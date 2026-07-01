// SPDX-License-Identifier: MIT OR Apache-2.0
#include "jbr/huffman.h"

#include <string.h>

const jxl_jbr_huffman_table jxl_jbr_empty_huffman_table = {{0}, {0}};

static const jxl_u32_spec k_huff_count_specs[4] = {JXL_U32_C(0), JXL_U32_C(1), JXL_U32_BITS(2, 3),
                                                   JXL_U32_BITS(0, 8)};
static const jxl_u32_spec k_huff_value_specs[4] = {JXL_U32_BITS(0, 2), JXL_U32_BITS(4, 2),
                                                   JXL_U32_BITS(8, 4), JXL_U32_BITS(1, 8)};

void jxl_jbr_huffman_code_init(jxl_jbr_huffman_code *hc) {
    if (hc != NULL) {
        memset(hc, 0, sizeof(*hc));
    }
}

void jxl_jbr_huffman_code_free(jxl_allocator_state *alloc, jxl_jbr_huffman_code *hc) {
    if (hc == NULL) {
        return;
    }
    jxl_free(alloc, hc->values);
    hc->values = NULL;
    hc->values_len = 0;
}

size_t jxl_jbr_huffman_code_encoded_len(const jxl_jbr_huffman_code *hc) {
    if (hc == NULL || hc->values_len == 0) {
        return 17;
    }
    return 1 + 16 + hc->values_len - 1;
}

jxl_jbr_status jxl_jbr_huffman_code_parse(jxl_allocator_state *alloc, jxl_bs *bs,
                                          jxl_jbr_huffman_code *out) {
                                              size_t i;
    int is_ac;
    uint32_t id;
    int is_last;
    uint32_t sum_counts;
    if (alloc == NULL || bs == NULL || out == NULL) {
        return JXL_JBR_BITSTREAM_ERROR;
    }
    jxl_jbr_huffman_code_free(alloc, out);
    jxl_jbr_huffman_code_init(out);

    is_ac = 0;
    id = 0;
    is_last = 0;
    if (jxl_bs_read_bool(bs, &is_ac) != JXL_BS_OK || jxl_bs_read_bits(bs, 2, &id) != JXL_BS_OK ||
        jxl_bs_read_bool(bs, &is_last) != JXL_BS_OK) {
        return JXL_JBR_BITSTREAM_ERROR;
    }
    out->is_ac = is_ac;
    out->id = (uint8_t)id;
    out->is_last = is_last;

    sum_counts = 0;
    for (i = 0; i < 17; ++i) {
        uint32_t x = 0;
        if (jxl_bs_read_u32(bs, k_huff_count_specs, &x) != JXL_BS_OK) {
            return JXL_JBR_BITSTREAM_ERROR;
        }
        out->counts[i] = (uint8_t)x;
        sum_counts += x;
    }
    if (sum_counts == 0) {
        return JXL_JBR_OK;
    }
    out->values = jxl_alloc(alloc, sum_counts);
    if (out->values == NULL) {
        return JXL_JBR_OUT_OF_MEMORY;
    }
    out->values_len = sum_counts;
    for (i = 0; i < sum_counts; ++i) {
        uint32_t v = 0;
        if (jxl_bs_read_u32(bs, k_huff_value_specs, &v) != JXL_BS_OK) {
            jxl_jbr_huffman_code_free(alloc, out);
            return JXL_JBR_BITSTREAM_ERROR;
        }
        out->values[i] = (uint8_t)v;
    }
    return JXL_JBR_OK;
}

jxl_jbr_status jxl_jbr_huffman_code_build(jxl_allocator_state *alloc, const jxl_jbr_huffman_code *hc,
                                          jxl_jbr_huffman_table *out) {
                                              size_t len_idx;
                                              size_t i;
    size_t pos;
    uint64_t next_code;
    if (hc == NULL || out == NULL || hc->values_len == 0) {
        return JXL_JBR_INVALID_DATA;
    }
    memset(out, 0, sizeof(*out));

    uint8_t *lengths = jxl_alloc(alloc, hc->values_len);
    uint64_t *code_bits = jxl_alloc(alloc, hc->values_len * sizeof(uint64_t));
    if (lengths == NULL || code_bits == NULL) {
        jxl_free(alloc, lengths);
        jxl_free(alloc, code_bits);
        return JXL_JBR_OUT_OF_MEMORY;
    }

    pos = 0;
    for (len_idx = 0; len_idx < 17; ++len_idx) {
        uint8_t j;
        uint8_t len = (uint8_t)len_idx;
        uint8_t count = hc->counts[len_idx];
        for (j = 0; j < count && pos < hc->values_len; ++j) {
            lengths[pos++] = len;
        }
    }
    if (pos > 0) {
        pos -= 1; /* pop last like Rust */
    }

    next_code = 0;
    uint8_t prev_len = lengths[0];
    for (i = 0; i < pos; ++i) {
        uint8_t len = lengths[i];
        if (len != prev_len) {
            next_code <<= (len - prev_len);
            prev_len = len;
        }
        code_bits[i] = next_code << (64 - len);
        next_code += 1;
    }

    for (i = 0; i < pos; ++i) {
        uint8_t sym = hc->values[i];
        out->lengths[sym] = lengths[i];
        out->bits[sym] = code_bits[i];
    }

    jxl_free(alloc, lengths);
    jxl_free(alloc, code_bits);
    return JXL_JBR_OK;
}

jxl_jbr_status jxl_jbr_huffman_table_lookup(const jxl_jbr_huffman_table *table, uint8_t symbol,
                                              uint8_t *len_out, uint64_t *bits_out) {
    if (table == NULL || len_out == NULL || bits_out == NULL) {
        return JXL_JBR_HUFFMAN_LOOKUP;
    }
    uint8_t len = table->lengths[symbol];
    if (len == 0) {
        return JXL_JBR_HUFFMAN_LOOKUP;
    }
    *len_out = len;
    *bits_out = table->bits[symbol];
    return JXL_JBR_OK;
}
