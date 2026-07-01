// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_JBR_BIT_WRITER_H_
#define JXL_JBR_BIT_WRITER_H_

#include "allocator.h"
#include "jbr/error.h"
#include "jbr/output.h"

#include <stddef.h>
#include "jxl_oxide/jxl_types.h"

typedef struct {
    jxl_jbr_output *output;
    uint64_t buf;
    size_t valid_buf_bits;
} jxl_jbr_bit_writer;

void jxl_jbr_bit_writer_init(jxl_jbr_bit_writer *w, jxl_jbr_output *output);
void jxl_jbr_bit_writer_write_huffman(jxl_jbr_bit_writer *w, jxl_allocator_state *alloc,
                                      uint64_t bits, uint8_t len);
void jxl_jbr_bit_writer_write_raw(jxl_jbr_bit_writer *w, jxl_allocator_state *alloc, uint64_t bits,
                                  uint8_t len);
size_t jxl_jbr_bit_writer_padding_bits(const jxl_jbr_bit_writer *w);
jxl_jbr_status jxl_jbr_bit_writer_finalize(jxl_jbr_bit_writer *w, jxl_allocator_state *alloc);

#endif /* JXL_JBR_BIT_WRITER_H_ */
