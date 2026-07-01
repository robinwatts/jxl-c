// SPDX-License-Identifier: MIT OR Apache-2.0
#include "bitstream.h"

void jxl_bs_init(jxl_bs *bs, const uint8_t *bytes, size_t len) {
    bs->bytes = bytes;
    bs->bytes_len = len;
    bs->buf = 0;
    bs->num_read_bits = 0;
    bs->remaining_buf_bits = 0;
}

void jxl_bs_init_at_bit(jxl_bs *bs, const uint8_t *bytes, size_t len, size_t bit_offset) {
    jxl_bs_init(bs, bytes, len);
    if (bit_offset > 0) {
        jxl_bs_skip_bits(bs, bit_offset);
    }
}

size_t jxl_bs_num_read_bits(const jxl_bs *bs) { return bs->num_read_bits; }

jxl_bs_status_t jxl_bs_peek_bits(jxl_bs *bs, size_t n, uint32_t *out) {
    if (n > 32) {
        return JXL_BS_VALIDATION_FAILED;
    }
    jxl_bs_refill(bs);
    *out = (uint32_t)(bs->buf & ((1ull << n) - 1ull));
    return JXL_BS_OK;
}

jxl_bs_status_t jxl_bs_peek_bits_prefilled(jxl_bs *bs, size_t n, uint32_t *out) {
    if (n > 32) {
        return JXL_BS_VALIDATION_FAILED;
    }
    *out = (uint32_t)(bs->buf & ((1ull << n) - 1ull));
    return JXL_BS_OK;
}

jxl_bs_status_t jxl_bs_consume_bits(jxl_bs *bs, size_t n) {
    if (n > bs->remaining_buf_bits) {
        return JXL_BS_EOF;
    }
    bs->remaining_buf_bits -= n;
    bs->num_read_bits += n;
    bs->buf >>= n;
    return JXL_BS_OK;
}

jxl_bs_status_t jxl_bs_read_bits(jxl_bs *bs, size_t n, uint32_t *out) {
    jxl_bs_status_t st = jxl_bs_peek_bits(bs, n, out);
    if (st != JXL_BS_OK) {
        return st;
    }
    return jxl_bs_consume_bits(bs, n);
}

jxl_bs_status_t jxl_bs_skip_bits(jxl_bs *bs, size_t n) {
    if (n <= bs->remaining_buf_bits) {
        bs->num_read_bits += n;
        bs->remaining_buf_bits -= n;
        bs->buf >>= n;
        return JXL_BS_OK;
    }

    n -= bs->remaining_buf_bits;
    bs->num_read_bits += bs->remaining_buf_bits;
    bs->buf = 0;
    bs->remaining_buf_bits = 0;

    if (n > bs->bytes_len * 8) {
        bs->num_read_bits += bs->bytes_len * 8;
        return JXL_BS_EOF;
    }

    bs->num_read_bits += n;
    bs->bytes += n / 8;
    bs->bytes_len -= n / 8;
    n %= 8;
    jxl_bs_refill(bs);
    if (n > bs->remaining_buf_bits) {
        return JXL_BS_EOF;
    }
    bs->remaining_buf_bits -= n;
    bs->buf >>= n;
    return JXL_BS_OK;
}

jxl_bs_status_t jxl_bs_zero_pad_to_byte(jxl_bs *bs) {
    size_t byte_boundary = (bs->num_read_bits + 7u) / 8u * 8u;
    size_t n = byte_boundary - bs->num_read_bits;
    uint32_t bits = 0;
    jxl_bs_status_t st = jxl_bs_read_bits(bs, n, &bits);
    if (st != JXL_BS_OK) {
        return st;
    }
    return bits == 0 ? JXL_BS_OK : JXL_BS_NON_ZERO_PADDING;
}

jxl_bs_status_t jxl_bs_read_u32(jxl_bs *bs, const jxl_u32_spec specs[4], uint32_t *out) {
    uint32_t selector = 0;
    jxl_bs_status_t st = jxl_bs_read_bits(bs, 2, &selector);
    uint32_t bits;
    jxl_u32_spec d;
    if (st != JXL_BS_OK) {
        return st;
    }
    d = specs[selector];
    if (d.kind == JXL_U32_CONST) {
        *out = d.value;
        return JXL_BS_OK;
    }
    bits = 0;
    st = jxl_bs_read_bits(bs, d.bits, &bits);
    if (st != JXL_BS_OK) {
        return st;
    }
    *out = bits + d.value;
    return JXL_BS_OK;
}

jxl_bs_status_t jxl_bs_read_u64(jxl_bs *bs, uint64_t *out) {
    uint32_t selector = 0;
    jxl_bs_status_t st = jxl_bs_read_bits(bs, 2, &selector);
    if (st != JXL_BS_OK) {
        return st;
    }
    switch (selector) {
    case 0:
        *out = 0;
        return JXL_BS_OK;
    case 1: {
        uint32_t bits = 0;
        st = jxl_bs_read_bits(bs, 4, &bits);
        if (st != JXL_BS_OK) {
            return st;
        }
        *out = (uint64_t)bits + 1u;
        return JXL_BS_OK;
    }
    case 2: {
        uint32_t bits = 0;
        st = jxl_bs_read_bits(bs, 8, &bits);
        if (st != JXL_BS_OK) {
            return st;
        }
        *out = (uint64_t)bits + 17u;
        return JXL_BS_OK;
    }
    case 3: {
        uint32_t low = 0;
        uint64_t value;
        uint32_t shift;
        st = jxl_bs_read_bits(bs, 12, &low);
        if (st != JXL_BS_OK) {
            return st;
        }
        value = low;
        shift = 12;
        for (;;) {
            uint32_t more = 0;
            uint32_t chunk;
            st = jxl_bs_read_bits(bs, 1, &more);
            if (st != JXL_BS_OK) {
                return st;
            }
            if (more == 0) {
                break;
            }
            if (shift == 60) {
                uint32_t tail = 0;
                st = jxl_bs_read_bits(bs, 4, &tail);
                if (st != JXL_BS_OK) {
                    return st;
                }
                value |= (uint64_t)tail << shift;
                break;
            }
            chunk = 0;
            st = jxl_bs_read_bits(bs, 8, &chunk);
            if (st != JXL_BS_OK) {
                return st;
            }
            value |= (uint64_t)chunk << shift;
            shift += 8;
        }
        *out = value;
        return JXL_BS_OK;
    }
    default:
        return JXL_BS_VALIDATION_FAILED;
    }
}

jxl_bs_status_t jxl_bs_read_bool(jxl_bs *bs, int *out) {
    uint32_t bit = 0;
    jxl_bs_status_t st = jxl_bs_read_bits(bs, 1, &bit);
    if (st != JXL_BS_OK) {
        return st;
    }
    *out = bit != 0;
    return JXL_BS_OK;
}

jxl_bs_status_t jxl_bs_read_enum(jxl_bs *bs, uint32_t *out) {
    const jxl_u32_spec specs[4] = {JXL_U32_C(0), JXL_U32_C(1), JXL_U32_BITS(2, 4),
                                   JXL_U32_BITS(18, 6)};
    return jxl_bs_read_u32(bs, specs, out);
}

jxl_bs_status_t jxl_bs_read_f16_as_f32(jxl_bs *bs, float *out) {
    uint32_t v = 0;
    jxl_bs_status_t st = jxl_bs_read_bits(bs, 16, &v);
    uint32_t neg_bit;
    uint32_t mantissa;
    uint32_t exponent;
    uint32_t bitpattern;
    union {
        uint32_t u;
        float f;
    } bits;
    if (st != JXL_BS_OK) {
        return st;
    }

    neg_bit = (v & 0x8000u) << 16;
    if ((v & 0x7fffu) == 0) {
        union {
            uint32_t u;
            float f;
        } bits;
        bits.u = neg_bit;
        *out = bits.f;
        return JXL_BS_OK;
    }

    mantissa = v & 0x3ffu;
    exponent = (v >> 10) & 0x1fu;
    if (exponent == 0x1fu) {
        return JXL_BS_INVALID_FLOAT;
    }
    if (exponent == 0) {
        float val = (1.0f / 16384.0f) * ((float)mantissa / 1024.0f);
        *out = neg_bit != 0 ? -val : val;
        return JXL_BS_OK;
    }

    bitpattern = (mantissa << 13) | ((exponent + 112u) << 23) | neg_bit;
    bits.u = bitpattern;
    *out = bits.f;
    return JXL_BS_OK;
}
