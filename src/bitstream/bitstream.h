// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_BITSTREAM_BITSTREAM_H_
#define JXL_BITSTREAM_BITSTREAM_H_

#include "error.h"

#include <stddef.h>
#include <string.h>
#include "jxl_oxide/jxl_types.h"

typedef struct {
    const uint8_t *bytes;
    size_t bytes_len;
    uint64_t buf;
    size_t num_read_bits;
    size_t remaining_buf_bits;
} jxl_bs;

typedef enum {
    JXL_U32_CONST = 0,
    JXL_U32_BITS = 1,
} jxl_u32_spec_kind;

typedef struct {
    jxl_u32_spec_kind kind;
    uint32_t value;
    uint8_t bits;
} jxl_u32_spec;

void jxl_bs_init(jxl_bs *bs, const uint8_t *bytes, size_t len);
/* Re-initialize and skip to a bit offset (for repositioning within a fixed buffer). */
void jxl_bs_init_at_bit(jxl_bs *bs, const uint8_t *bytes, size_t len, size_t bit_offset);
size_t jxl_bs_num_read_bits(const jxl_bs *bs);

/* Refill the bit buffer from the byte stream (hot-path helper). */
jxl_inline void jxl_bs_refill(jxl_bs *bs) {
    if (bs->bytes_len >= 8) {
        uint64_t bits;
        memcpy(&bits, bs->bytes, sizeof(bits));
        bs->buf |= bits << bs->remaining_buf_bits;
        size_t read_bytes = (63 - bs->remaining_buf_bits) >> 3;
        bs->remaining_buf_bits |= 56;
        bs->bytes += read_bytes;
        bs->bytes_len -= read_bytes;
        return;
    }

    while (bs->remaining_buf_bits < 56 && bs->bytes_len > 0) {
        bs->buf |= (uint64_t)bs->bytes[0] << bs->remaining_buf_bits;
        bs->remaining_buf_bits += 8;
        bs->bytes += 1;
        bs->bytes_len -= 1;
    }
}

jxl_inline jxl_bs_status_t jxl_bs_consume_bits_inline(jxl_bs *bs, size_t n) {
    if (n > bs->remaining_buf_bits) {
        return JXL_BS_EOF;
    }
    bs->remaining_buf_bits -= n;
    bs->num_read_bits += n;
    bs->buf >>= n;
    return JXL_BS_OK;
}

jxl_bs_status_t jxl_bs_peek_bits(jxl_bs *bs, size_t n, uint32_t *out);
/* Peek without refilling (caller must ensure enough bits are buffered). */
jxl_bs_status_t jxl_bs_peek_bits_prefilled(jxl_bs *bs, size_t n, uint32_t *out);
jxl_bs_status_t jxl_bs_consume_bits(jxl_bs *bs, size_t n);
jxl_bs_status_t jxl_bs_read_bits(jxl_bs *bs, size_t n, uint32_t *out);
jxl_bs_status_t jxl_bs_skip_bits(jxl_bs *bs, size_t n);
jxl_bs_status_t jxl_bs_zero_pad_to_byte(jxl_bs *bs);

jxl_bs_status_t jxl_bs_read_u32(jxl_bs *bs, const jxl_u32_spec specs[4], uint32_t *out);
jxl_bs_status_t jxl_bs_read_u64(jxl_bs *bs, uint64_t *out);
jxl_bs_status_t jxl_bs_read_bool(jxl_bs *bs, int *out);
jxl_bs_status_t jxl_bs_read_f16_as_f32(jxl_bs *bs, float *out);

jxl_bs_status_t jxl_bs_read_enum(jxl_bs *bs, uint32_t *out);

#define JXL_U32_C(x) {JXL_U32_CONST, (uint32_t)(x), 0}
#define JXL_U32_BITS(offset, nbits) {JXL_U32_BITS, (uint32_t)(offset), (uint8_t)(nbits)}

#endif /* JXL_BITSTREAM_BITSTREAM_H_ */
