// SPDX-License-Identifier: MIT OR Apache-2.0
#include "ans.h"

#include "coding/internal.h"
#include "coding/util.h"
#include "static_assert.h"

#include <string.h>

JXL_STATIC_ASSERT(sizeof(jxl_ans_bucket) == 8, "jxl_ans_bucket must be 8 bytes");

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

typedef struct {
    uint16_t dist;
    uint16_t alias_symbol;
    uint16_t alias_offset;
    uint16_t alias_cutoff;
} jxl_ans_working_bucket;

typedef struct {
    size_t start;
    size_t end;
} jxl_ans_repeat_range;

static jxl_coding_status_t ans_read_u8(jxl_bs *bs, uint8_t *out) {
    int flag = 0;
    uint32_t n;
    uint32_t extra;
    JXL_CODING_TRY_BS(jxl_bs_read_bool(bs, &flag));
    if (!flag) {
        *out = 0;
        return JXL_CODING_OK;
    }
    n = 0;
    JXL_CODING_TRY_BS(jxl_bs_read_bits(bs, 3, &n));
    extra = 0;
    JXL_CODING_TRY_BS(jxl_bs_read_bits(bs, n, &extra));
    *out = (uint8_t)((1u << n) + extra);
    return JXL_CODING_OK;
}

static jxl_coding_status_t ans_read_prefix(jxl_bs *bs, uint16_t *out) {
    uint32_t tag = 0;
    JXL_CODING_TRY_BS(jxl_bs_read_bits(bs, 3, &tag));
    switch (tag) {
    case 0:
        *out = 10;
        return JXL_CODING_OK;
    case 1: {
        size_t i;
        static const uint16_t vals[] = {4, 0, 11, 13};
        for (i = 0; i < 4; ++i) {
            int flag = 0;
            JXL_CODING_TRY_BS(jxl_bs_read_bool(bs, &flag));
            if (flag) {
                *out = vals[i];
                return JXL_CODING_OK;
            }
        }
        *out = 12;
        return JXL_CODING_OK;
    }
    case 2:
        *out = 7;
        return JXL_CODING_OK;
    case 3: {
        int flag = 0;
        JXL_CODING_TRY_BS(jxl_bs_read_bool(bs, &flag));
        *out = flag ? 1 : 3;
        return JXL_CODING_OK;
    }
    case 4:
        *out = 6;
        return JXL_CODING_OK;
    case 5:
        *out = 8;
        return JXL_CODING_OK;
    case 6:
        *out = 9;
        return JXL_CODING_OK;
    case 7: {
        int flag = 0;
        JXL_CODING_TRY_BS(jxl_bs_read_bool(bs, &flag));
        *out = flag ? 2 : 5;
        return JXL_CODING_OK;
    }
    default:
        return JXL_CODING_INVALID_ANS_HISTOGRAM;
    }
}

void jxl_ans_histogram_destroy(jxl_allocator_state *alloc, jxl_ans_histogram *hist) {
    if (hist == NULL) {
        return;
    }
    jxl_free(alloc, hist->buckets);
    hist->buckets = NULL;
    hist->bucket_count = 0;
}

jxl_coding_status_t jxl_ans_histogram_clone(jxl_allocator_state *alloc,
                                            const jxl_ans_histogram *src,
                                            jxl_ans_histogram *out) {
    if (src == NULL || out == NULL) {
        return JXL_CODING_BITSTREAM_ERROR;
    }
    memset(out, 0, sizeof(*out));
    *out = *src;
    out->buckets = NULL;
    if (src->bucket_count == 0 || src->buckets == NULL) {
        return JXL_CODING_OK;
    }
    out->buckets = jxl_alloc(alloc, src->bucket_count * sizeof(*src->buckets));
    if (out->buckets == NULL) {
        return JXL_CODING_OUT_OF_MEMORY;
    }
    memcpy(out->buckets, src->buckets, src->bucket_count * sizeof(*src->buckets));
    return JXL_CODING_OK;
}

jxl_coding_status_t jxl_ans_histogram_parse(jxl_allocator_state *alloc, jxl_bs *bs,
                                            uint32_t log_alphabet_size, jxl_ans_histogram *out) {
                                                size_t i;
                                                size_t idx;
    size_t alphabet_size;
    int flag0;
    size_t underfull_len;
    size_t overfull_len;
    const size_t table_size = (size_t)1u << log_alphabet_size;
    const uint32_t log_bucket_size = 12u - log_alphabet_size;
    const uint16_t bucket_size = (uint16_t)(1u << log_bucket_size);
    uint16_t *dist;
    jxl_coding_status_t st = JXL_CODING_OK;
    size_t single_sym_idx = SIZE_MAX;
    jxl_ans_working_bucket *wb;
    size_t *underfull;
    size_t *overfull;

    memset(out, 0, sizeof(*out));

    if (log_alphabet_size < 5 || log_alphabet_size > 8) {
        return JXL_CODING_INVALID_ANS_HISTOGRAM;
    }

    dist = jxl_alloc(alloc, table_size * sizeof(uint16_t));
    if (dist == NULL) {
        return JXL_CODING_OUT_OF_MEMORY;
    }
    memset(dist, 0, table_size * sizeof(uint16_t));

    alphabet_size = 0;

    flag0 = 0;
    JXL_CODING_TRY_BS(jxl_bs_read_bool(bs, &flag0));
    if (flag0) {
        int flag1 = 0;
        JXL_CODING_TRY_BS(jxl_bs_read_bool(bs, &flag1));
        if (flag1) {
            uint8_t v0 = 0;
            uint8_t v1 = 0;
            uint32_t prob;
            st = ans_read_u8(bs, &v0);
            if (st != JXL_CODING_OK) {
                goto cleanup;
            }
            st = ans_read_u8(bs, &v1);
            if (st != JXL_CODING_OK) {
                goto cleanup;
            }
            if (v0 == v1) {
                st = JXL_CODING_INVALID_ANS_HISTOGRAM;
                goto cleanup;
            }
            alphabet_size = (size_t)(v0 > v1 ? v0 : v1) + 1;
            if (alphabet_size > table_size) {
                st = JXL_CODING_INVALID_ANS_HISTOGRAM;
                goto cleanup;
            }
            prob = 0;
            JXL_CODING_TRY_BS(jxl_bs_read_bits(bs, 12, &prob));
            dist[v0] = (uint16_t)prob;
            dist[v1] = (uint16_t)((1u << 12) - prob);
        } else {
            uint8_t val = 0;
            st = ans_read_u8(bs, &val);
            if (st != JXL_CODING_OK) {
                goto cleanup;
            }
            alphabet_size = (size_t)val + 1;
            if (alphabet_size > table_size) {
                st = JXL_CODING_INVALID_ANS_HISTOGRAM;
                goto cleanup;
            }
            dist[val] = 1u << 12;
        }
    } else {
        int evenly = 0;
        JXL_CODING_TRY_BS(jxl_bs_read_bool(bs, &evenly));
        if (evenly) {
            size_t i;
            uint8_t val = 0;
            st = ans_read_u8(bs, &val);
            if (st != JXL_CODING_OK) {
                goto cleanup;
            }
            alphabet_size = (size_t)val + 1;
            if (alphabet_size > table_size) {
                st = JXL_CODING_INVALID_ANS_HISTOGRAM;
                goto cleanup;
            }
            const size_t base = ((size_t)1u << 12) / alphabet_size;
            const size_t leftover = ((size_t)1u << 12) % alphabet_size;
            for (i = 0; i < leftover; ++i) {
                dist[i] = (uint16_t)(base + 1);
            }
            for (i = leftover; i < alphabet_size; ++i) {
                dist[i] = (uint16_t)base;
            }
        } else {
            size_t i;
            size_t len = 0;
            uint32_t len_bits;
            uint8_t val;
            size_t repeat_range_count;
            size_t repeat_range_cap;
            int has_omit;
            uint16_t omit_log;
            size_t omit_pos;
            size_t idx;
            size_t repeat_range_idx;
            uint32_t acc;
            uint16_t prev_dist;
            int16_t shift;
            jxl_ans_repeat_range *repeat_ranges = NULL;
            while (len < 3) {
                int bit = 0;
                JXL_CODING_TRY_BS(jxl_bs_read_bool(bs, &bit));
                if (bit) {
                    len += 1;
                } else {
                    break;
                }
            }
            len_bits = 0;
            if (len > 0) {
                JXL_CODING_TRY_BS(jxl_bs_read_bits(bs, len, &len_bits));
            }
            shift = (int16_t)(len_bits + (int)((1u << len) - 1u));
            if (shift > 13) {
                st = JXL_CODING_INVALID_ANS_HISTOGRAM;
                goto cleanup;
            }
            val = 0;
            st = ans_read_u8(bs, &val);
            if (st != JXL_CODING_OK) {
                goto cleanup;
            }
            alphabet_size = (size_t)val + 3;
            if (alphabet_size > table_size) {
                st = JXL_CODING_INVALID_ANS_HISTOGRAM;
                goto cleanup;
            }

            repeat_range_count = 0;
            repeat_range_cap = 0;

            has_omit = 0;
            omit_log = 0;
            omit_pos = 0;

            idx = 0;
            while (idx < alphabet_size) {
                st = ans_read_prefix(bs, &dist[idx]);
                if (st != JXL_CODING_OK) {
                    goto cleanup_repeat;
                }
                if (dist[idx] == 13) {
                    uint8_t repeat_byte = 0;
                    size_t repeat_count;
                    st = ans_read_u8(bs, &repeat_byte);
                    if (st != JXL_CODING_OK) {
                        goto cleanup_repeat;
                    }
                    repeat_count = (size_t)repeat_byte + 4;
                    if (idx + repeat_count > alphabet_size) {
                        st = JXL_CODING_INVALID_ANS_HISTOGRAM;
                        goto cleanup_repeat;
                    }
                    if (repeat_range_count == repeat_range_cap) {
                        size_t new_cap = repeat_range_cap == 0 ? 4 : repeat_range_cap * 2;
                        jxl_ans_repeat_range *grown =
                            jxl_alloc(alloc, new_cap * sizeof(jxl_ans_repeat_range));
                        if (grown == NULL) {
                            st = JXL_CODING_OUT_OF_MEMORY;
                            goto cleanup_repeat;
                        }
                        if (repeat_ranges != NULL) {
                            memcpy(grown, repeat_ranges,
                                   repeat_range_count * sizeof(jxl_ans_repeat_range));
                            jxl_free(alloc, repeat_ranges);
                        }
                        repeat_ranges = grown;
                        repeat_range_cap = new_cap;
                    }
                    repeat_ranges[repeat_range_count].start = idx;
                    repeat_ranges[repeat_range_count].end = idx + repeat_count;
                    repeat_range_count += 1;
                    idx += repeat_count;
                    continue;
                }
                if (has_omit) {
                    if (dist[idx] > omit_log) {
                        omit_log = dist[idx];
                        omit_pos = idx;
                    }
                } else {
                    has_omit = 1;
                    omit_log = dist[idx];
                    omit_pos = idx;
                }
                idx += 1;
            }

            if (!has_omit) {
                st = JXL_CODING_INVALID_ANS_HISTOGRAM;
                goto cleanup_repeat;
            }
            if (omit_pos + 1 < alphabet_size && dist[omit_pos + 1] == 13) {
                st = JXL_CODING_INVALID_ANS_HISTOGRAM;
                goto cleanup_repeat;
            }

            repeat_range_idx = 0;
            acc = 0;
            prev_dist = 0;
            for (i = 0; i < table_size; ++i) {
                if (repeat_range_idx < repeat_range_count &&
                    repeat_ranges[repeat_range_idx].start <= i) {
                    if (repeat_ranges[repeat_range_idx].end == i) {
                        repeat_range_idx += 1;
                    } else {
                        dist[i] = prev_dist;
                        acc += dist[i];
                        if (acc > (1u << 12)) {
                            st = JXL_CODING_INVALID_ANS_HISTOGRAM;
                            goto cleanup_repeat;
                        }
                        continue;
                    }
                }

                if (dist[i] == 0) {
                    prev_dist = 0;
                    continue;
                }
                if (i == omit_pos) {
                    prev_dist = 0;
                    continue;
                }
                if (dist[i] > 1) {
                    uint32_t extra;
                    const int16_t zeros = (int16_t)(dist[i] - 1);
                    int16_t bitcount = shift - ((12 - zeros) >> 1);
                    if (bitcount < 0) {
                        bitcount = 0;
                    }
                    if (bitcount > zeros) {
                        bitcount = zeros;
                    }
                    extra = 0;
                    if (bitcount > 0) {
                        JXL_CODING_TRY_BS(jxl_bs_read_bits(bs, (size_t)bitcount, &extra));
                    }
                    dist[i] = (uint16_t)((1u << zeros) +
                                         ((uint16_t)extra << (zeros - bitcount)));
                }
                prev_dist = dist[i];
                acc += dist[i];
                if (acc > (1u << 12)) {
                    st = JXL_CODING_INVALID_ANS_HISTOGRAM;
                    goto cleanup_repeat;
                }
            }
            dist[omit_pos] = (uint16_t)((1u << 12) - acc);

        cleanup_repeat:
            jxl_free(alloc, repeat_ranges);
            if (st != JXL_CODING_OK) {
                goto cleanup;
            }
        }
    }

    for (i = 0; i < table_size; ++i) {
        if (dist[i] == (uint16_t)(1u << 12)) {
            single_sym_idx = i;
            break;
        }
    }

    out->log_bucket_size = log_bucket_size;
    out->bucket_mask = (1u << log_bucket_size) - 1u;
    out->bucket_count = table_size;
    out->buckets = jxl_alloc(alloc, table_size * sizeof(jxl_ans_bucket));
    if (out->buckets == NULL) {
        st = JXL_CODING_OUT_OF_MEMORY;
        goto cleanup;
    }

    if (single_sym_idx != SIZE_MAX) {
        size_t i;
        for (i = 0; i < table_size; ++i) {
            out->buckets[i].dist = dist[i];
            out->buckets[i].alias_symbol = (uint8_t)single_sym_idx;
            out->buckets[i].alias_offset = (uint16_t)(bucket_size * i);
            out->buckets[i].alias_cutoff = 0;
            out->buckets[i].alias_dist_xor = dist[i] ^ (uint16_t)(1u << 12);
        }
        out->has_single_symbol = 1;
        out->single_symbol = (uint32_t)single_sym_idx;
        jxl_free(alloc, dist);
        return JXL_CODING_OK;
    }

    wb = jxl_alloc(alloc, table_size * sizeof(jxl_ans_working_bucket));
    if (wb == NULL) {
        st = JXL_CODING_OUT_OF_MEMORY;
        goto cleanup_buckets;
    }
    for (i = 0; i < table_size; ++i) {
        wb[i].dist = dist[i];
        wb[i].alias_symbol = (uint16_t)(i < alphabet_size ? i : 0);
        wb[i].alias_offset = 0;
        wb[i].alias_cutoff = dist[i];
    }

    underfull = jxl_alloc(alloc, table_size * sizeof(size_t));
    overfull = jxl_alloc(alloc, table_size * sizeof(size_t));
    if (underfull == NULL || overfull == NULL) {
        st = JXL_CODING_OUT_OF_MEMORY;
        goto cleanup_working;
    }
    underfull_len = 0;
    overfull_len = 0;

    for (idx = 0; idx < table_size; ++idx) {
        if (wb[idx].dist < bucket_size) {
            underfull[underfull_len++] = idx;
        } else if (wb[idx].dist > bucket_size) {
            overfull[overfull_len++] = idx;
        }
    }

    while (overfull_len > 0 && underfull_len > 0) {
        const size_t o = overfull[--overfull_len];
        const size_t u = underfull[--underfull_len];
        const uint16_t by = bucket_size - wb[u].alias_cutoff;
        wb[o].alias_cutoff -= by;
        wb[u].alias_symbol = (uint16_t)o;
        wb[u].alias_offset = wb[o].alias_cutoff;
        if (wb[o].alias_cutoff < bucket_size) {
            underfull[underfull_len++] = o;
        } else if (wb[o].alias_cutoff > bucket_size) {
            overfull[overfull_len++] = o;
        }
    }

    for (idx = 0; idx < table_size; ++idx) {
        const jxl_ans_working_bucket *bucket = &wb[idx];
        if (bucket->alias_cutoff == bucket_size) {
            out->buckets[idx].dist = bucket->dist;
            out->buckets[idx].alias_symbol = (uint8_t)idx;
            out->buckets[idx].alias_offset = 0;
            out->buckets[idx].alias_cutoff = 0;
            out->buckets[idx].alias_dist_xor = 0;
        } else {
            const size_t alias_idx = bucket->alias_symbol;
            out->buckets[idx].dist = bucket->dist;
            out->buckets[idx].alias_symbol = (uint8_t)bucket->alias_symbol;
            out->buckets[idx].alias_offset = bucket->alias_offset - bucket->alias_cutoff;
            out->buckets[idx].alias_cutoff = (uint8_t)bucket->alias_cutoff;
            out->buckets[idx].alias_dist_xor =
                bucket->dist ^ wb[alias_idx].dist;
        }
    }

    out->has_single_symbol = 0;
    st = JXL_CODING_OK;

cleanup_working:
    jxl_free(alloc, underfull);
    jxl_free(alloc, overfull);
    jxl_free(alloc, wb);
    jxl_free(alloc, dist);
    if (st == JXL_CODING_OK) {
        return JXL_CODING_OK;
    }
    jxl_ans_histogram_destroy(alloc, out);
    return st;

cleanup_buckets:
    jxl_free(alloc, out->buckets);
    out->buckets = NULL;

cleanup:
    jxl_free(alloc, dist);
    return st;
}

jxl_coding_status_t jxl_ans_histogram_read_symbol(const jxl_ans_histogram *hist, jxl_bs *bs,
                                                  uint32_t *state, uint32_t *symbol_out) {
    const uint32_t idx = *state & 0xfffu;
    const size_t i = (size_t)(idx >> hist->log_bucket_size);
    const uint32_t pos = idx & hist->bucket_mask;

    uint64_t bucket_int;
    uint32_t peeked;
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
    uint32_t appended_state;
    int select_appended;

    if (i >= hist->bucket_count) {
        return JXL_CODING_INVALID_ANS_HISTOGRAM;
    }

    bucket = hist->buckets[i];

#if JXL_ANS_LITTLE_ENDIAN
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
#else
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
#endif

    sym_offset = offset + pos;
    next_state = (*state >> 12) * dist + sym_offset;

    peeked = 0;
    JXL_CODING_TRY_BS(jxl_bs_peek_bits(bs, 16, &peeked));
    appended_state = (next_state << 16) | peeked;
    select_appended = next_state < (1u << 16);
    *state = select_appended ? appended_state : next_state;
    JXL_CODING_TRY_BS(jxl_bs_consume_bits(bs, select_appended ? 16 : 0));

    *symbol_out = (uint32_t)symbol;
    return JXL_CODING_OK;
}

int jxl_ans_histogram_single_symbol(const jxl_ans_histogram *hist, uint32_t *symbol_out) {
    if (!hist->has_single_symbol) {
        return 0;
    }
    *symbol_out = hist->single_symbol;
    return 1;
}
