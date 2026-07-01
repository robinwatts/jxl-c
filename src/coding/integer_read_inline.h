// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_CODING_INTEGER_READ_INLINE_H_
#define JXL_CODING_INTEGER_READ_INLINE_H_

#include "bitstream/bitstream.h"
#include "coding/cdecoder_private.h"

#include "jxl_oxide/jxl_types.h"

#if defined(__GNUC__) || defined(__clang__)
#define JXL_INTEGER_READ_INLINE static __attribute__((always_inline)) inline
#else
#define JXL_INTEGER_READ_INLINE static inline
#endif

JXL_INTEGER_READ_INLINE uint32_t jxl_integer_read_uint(jxl_bs *bs, uint32_t split,
                                                       uint32_t msb_in_token, uint32_t lsb_in_token,
                                                       uint32_t split_exponent, uint32_t token) {
    uint32_t rest_bits;
    uint32_t n;
    uint32_t tok;
    if (token < split) {
        return token;
    }

    n = split_exponent - (msb_in_token + lsb_in_token) +
        ((token - split) >> (msb_in_token + lsb_in_token));
    n &= 31u;

    rest_bits = (uint32_t)(bs->buf & ((1ull << n) - 1ull));
    jxl_bs_consume_bits_inline(bs, n);

    {
        const uint32_t low_mask = lsb_in_token == 0 ? 0u : ((1u << lsb_in_token) - 1u);
        const uint64_t low_bits = token & low_mask;
        tok = token >> lsb_in_token;
        {
            const uint32_t msb_mask = msb_in_token == 0 ? 0u : ((1u << msb_in_token) - 1u);
            tok &= msb_mask;
            tok |= 1u << msb_in_token;
            return (uint32_t)(((((uint64_t)tok << n) | rest_bits) << lsb_in_token) | low_bits);
        }
    }
}

JXL_INTEGER_READ_INLINE uint32_t jxl_integer_read_uint_config(jxl_bs *bs,
                                                              const jxl_integer_config *config,
                                                              uint32_t token) {
    return jxl_integer_read_uint(bs, config->split, config->msb_in_token, config->lsb_in_token,
                                 config->split_exponent, token);
}

#endif /* JXL_CODING_INTEGER_READ_INLINE_H_ */
