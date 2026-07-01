// SPDX-License-Identifier: MIT OR Apache-2.0
#include "jbr/bit_writer.h"

#include <string.h>

static int has_ff_byte(uint64_t val) {
    return (((~val) - 0x0101010101010101ull) & val & 0x8080808080808080ull) != 0;
}

static jxl_jbr_status emit_byte(jxl_jbr_bit_writer *w, jxl_allocator_state *alloc, uint8_t b) {
    jxl_jbr_status st = jxl_jbr_output_write(alloc, w->output, &b, 1);
    if (st != JXL_JBR_OK) {
        return st;
    }
    if (b == 0xff) {
        uint8_t zero = 0;
        return jxl_jbr_output_write(alloc, w->output, &zero, 1);
    }
    return JXL_JBR_OK;
}

static jxl_jbr_status flush_buf(jxl_jbr_bit_writer *w, jxl_allocator_state *alloc, uint64_t next_buf) {
    size_t i;
    uint64_t out = w->buf;
    w->valid_buf_bits -= 64;
    w->buf = next_buf;
    if (!has_ff_byte(out)) {
        size_t i;
        uint8_t bytes[8];
        for (i = 0; i < 8; ++i) {
            bytes[i] = (uint8_t)(out >> ((7 - i) * 8));
        }
        return jxl_jbr_output_write(alloc, w->output, bytes, 8);
    }
    for (i = 0; i < 8; ++i) {
        jxl_jbr_status st = emit_byte(w, alloc, (uint8_t)(out >> ((7 - i) * 8)));
        if (st != JXL_JBR_OK) {
            return st;
        }
    }
    return JXL_JBR_OK;
}

void jxl_jbr_bit_writer_init(jxl_jbr_bit_writer *w, jxl_jbr_output *output) {
    if (w != NULL) {
        memset(w, 0, sizeof(*w));
        w->output = output;
    }
}

void jxl_jbr_bit_writer_write_huffman(jxl_jbr_bit_writer *w, jxl_allocator_state *alloc,
                                      uint64_t bits, uint8_t len) {
    if (w == NULL || len == 0) {
        return;
    }
    uint64_t shifted = bits >> w->valid_buf_bits;
    w->buf |= shifted;
    w->valid_buf_bits += len;
    if (w->valid_buf_bits >= 64) {
        size_t extra = w->valid_buf_bits - 64;
        (void)flush_buf(w, alloc, bits << (len - extra));
    }
}

void jxl_jbr_bit_writer_write_raw(jxl_jbr_bit_writer *w, jxl_allocator_state *alloc, uint64_t bits,
                                  uint8_t len) {
    if (len == 0) {
        return;
    }
    jxl_jbr_bit_writer_write_huffman(w, alloc, bits << (64 - len), len);
}

size_t jxl_jbr_bit_writer_padding_bits(const jxl_jbr_bit_writer *w) {
    if (w == NULL) {
        return 0;
    }
    return (8 - w->valid_buf_bits % 8) % 8;
}

jxl_jbr_status jxl_jbr_bit_writer_finalize(jxl_jbr_bit_writer *w, jxl_allocator_state *alloc) {
    size_t i;
    if (w == NULL) {
        return JXL_JBR_WRITE_ERROR;
    }
    uint64_t out = w->buf;
    size_t valid_bytes = (w->valid_buf_bits + 7) / 8;
    if (valid_bytes == 0) {
        return JXL_JBR_OK;
    }
    if (!has_ff_byte(out)) {
        size_t i;
        uint8_t bytes[8];
        for (i = 0; i < 8; ++i) {
            bytes[i] = (uint8_t)(out >> ((7 - i) * 8));
        }
        return jxl_jbr_output_write(alloc, w->output, bytes, valid_bytes);
    }
    for (i = 0; i < valid_bytes; ++i) {
        jxl_jbr_status st = emit_byte(w, alloc, (uint8_t)(out >> ((7 - i) * 8)));
        if (st != JXL_JBR_OK) {
            return st;
        }
    }
    return JXL_JBR_OK;
}
