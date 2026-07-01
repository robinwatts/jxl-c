// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_JBR_HUFFMAN_H_
#define JXL_JBR_HUFFMAN_H_

#include "allocator.h"
#include "bitstream/bitstream.h"
#include "jbr/error.h"

#include <stddef.h>
#include "jxl_oxide/jxl_types.h"

typedef struct {
    int is_ac;
    uint8_t id;
    int is_last;
    uint8_t counts[17];
    uint8_t *values;
    size_t values_len;
} jxl_jbr_huffman_code;

typedef struct {
    uint8_t lengths[256];
    uint64_t bits[256];
} jxl_jbr_huffman_table;

void jxl_jbr_huffman_code_init(jxl_jbr_huffman_code *hc);
void jxl_jbr_huffman_code_free(jxl_allocator_state *alloc, jxl_jbr_huffman_code *hc);
size_t jxl_jbr_huffman_code_encoded_len(const jxl_jbr_huffman_code *hc);
jxl_jbr_status jxl_jbr_huffman_code_parse(jxl_allocator_state *alloc, jxl_bs *bs,
                                          jxl_jbr_huffman_code *out);
jxl_jbr_status jxl_jbr_huffman_code_build(jxl_allocator_state *alloc, const jxl_jbr_huffman_code *hc,
                                          jxl_jbr_huffman_table *out);
jxl_jbr_status jxl_jbr_huffman_table_lookup(const jxl_jbr_huffman_table *table, uint8_t symbol,
                                              uint8_t *len_out, uint64_t *bits_out);

extern const jxl_jbr_huffman_table jxl_jbr_empty_huffman_table;

#endif /* JXL_JBR_HUFFMAN_H_ */
