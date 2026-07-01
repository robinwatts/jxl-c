// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_CODING_ANS_READ_INLINE_H_
#define JXL_CODING_ANS_READ_INLINE_H_

#include "coding/ans.h"
#include "coding/error.h"

#include <string.h>

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define JXL_ANS_LITTLE_ENDIAN 1
#elif defined(__BYTE_ORDER) && __BYTE_ORDER == __LITTLE_ENDIAN
#define JXL_ANS_LITTLE_ENDIAN 1
#elif defined(_WIN32) || defined(__LITTLE_ENDIAN__) || defined(__ARMEL__) || defined(__THUMBEL__) || \
    defined(__AARCH64EL__) || defined(__MIPSEL__) || defined(__i386__) || defined(__x86_64__)
#define JXL_ANS_LITTLE_ENDIAN 1
#else
#define JXL_ANS_LITTLE_ENDIAN 0
#endif

JXL_ALWAYS_INLINE jxl_coding_status_t jxl_ans_histogram_read_symbol_inline(const jxl_ans_histogram *hist,
                                                                             jxl_bs *bs, uint32_t *state,
                                                                             uint32_t *symbol_out) {
    const uint32_t idx = *state & 0xfffu;
    const size_t i = (size_t)(idx >> hist->log_bucket_size);
    const uint32_t pos = idx & hist->bucket_mask;

    if (i >= hist->bucket_count) {
        return JXL_CODING_INVALID_ANS_HISTOGRAM;
    }

#if JXL_ANS_LITTLE_ENDIAN
    {
        uint64_t bucket_int;
        jxl_ans_bucket bucket;
        size_t alias_symbol;
        uint32_t alias_cutoff;
        uint32_t dist;
        int map_to_alias;
        uint64_t cond_bucket;
        uint32_t offset;
        uint32_t dist_xor;
        size_t symbol;
        uint32_t sym_offset;
        uint32_t next_state;
        uint32_t peeked;
        uint32_t appended_state;
        int select_appended;

        bucket = hist->buckets[i];
        bucket_int = 0;
        memcpy(&bucket_int, &bucket, sizeof(bucket));

        alias_symbol = (size_t)(bucket_int & 0xffu);
        alias_cutoff = (uint32_t)((bucket_int >> 8) & 0xffu);
        dist = (uint32_t)((bucket_int >> 16) & 0xffffu);

        map_to_alias = pos >= alias_cutoff;
        cond_bucket = map_to_alias ? bucket_int : 0;
        offset = (uint32_t)((cond_bucket >> 32) & 0xffffu);
        dist_xor = (uint32_t)(cond_bucket >> 48);

        dist ^= dist_xor;
        symbol = map_to_alias ? alias_symbol : i;

        sym_offset = offset + pos;
        next_state = (*state >> 12) * dist + sym_offset;

        jxl_bs_refill(bs);
        peeked = (uint32_t)(bs->buf & 0xffffu);
        appended_state = (next_state << 16) | peeked;
        select_appended = next_state < (1u << 16);
        *state = select_appended ? appended_state : next_state;
        if (select_appended) {
            if (bs->remaining_buf_bits < 16) {
                return JXL_CODING_BITSTREAM_ERROR;
            }
            bs->remaining_buf_bits -= 16;
            bs->num_read_bits += 16;
            bs->buf >>= 16;
        }

        *symbol_out = (uint32_t)symbol;
        return JXL_CODING_OK;
    }
#else
    {
        jxl_ans_bucket bucket;
        int map_to_alias;
        uint32_t dist;
        uint32_t offset;
        uint32_t dist_xor;
        size_t symbol;
        uint32_t sym_offset;
        uint32_t next_state;
        uint32_t peeked;
        uint32_t appended_state;
        int select_appended;

        bucket = hist->buckets[i];
        map_to_alias = pos >= bucket.alias_cutoff;
        dist = bucket.dist;
        offset = 0;
        dist_xor = 0;
        if (map_to_alias) {
            offset = bucket.alias_offset;
            dist_xor = bucket.alias_dist_xor;
        }
        dist ^= dist_xor;
        symbol = map_to_alias ? (size_t)bucket.alias_symbol : i;

        sym_offset = offset + pos;
        next_state = (*state >> 12) * dist + sym_offset;

        JXL_CODING_TRY_BS(jxl_bs_peek_bits(bs, 16, &peeked));
        appended_state = (next_state << 16) | peeked;
        select_appended = next_state < (1u << 16);
        *state = select_appended ? appended_state : next_state;
        JXL_CODING_TRY_BS(jxl_bs_consume_bits(bs, select_appended ? 16 : 0));

        *symbol_out = (uint32_t)symbol;
        return JXL_CODING_OK;
    }
#endif
}

#endif /* JXL_CODING_ANS_READ_INLINE_H_ */
