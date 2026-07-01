// SPDX-License-Identifier: MIT OR Apache-2.0
#include "prefix.h"

#include "coding/internal.h"
#include "coding/util.h"
#include "static_assert.h"

#include <string.h>

JXL_STATIC_ASSERT(sizeof(jxl_prefix_entry) == 4, "jxl_prefix_entry must be 4 bytes");

/* Reverse index bits for power-of-two len (matches Rust vec_reverse_bits). */
static size_t reverse_bits_idx(size_t idx, size_t bits) {
    size_t b;
    size_t rev = 0;
    for (b = 0; b < bits; ++b) {
        rev = (rev << 1) | ((idx >> b) & 1u);
    }
    return rev;
}

static void prefix_vec_reverse_bits_fixed(const jxl_prefix_entry *v, size_t len,
                                          jxl_prefix_entry *out) {
                                              size_t t;
                                              size_t idx;
    size_t bits = 0;
    for (t = len; t > 1; t >>= 1) {
        bits += 1;
    }
    for (idx = 0; idx < len; ++idx) {
        out[idx] = v[reverse_bits_idx(idx, bits)];
    }
}

static jxl_coding_status_t prefix_with_single_symbol(jxl_allocator_state *alloc, uint16_t symbol,
                                                     jxl_prefix_histogram *out) {
    memset(out, 0, sizeof(*out));
    out->toplevel_bits = 0;
    out->toplevel_mask = 0;
    out->toplevel_len = 1;
    out->toplevel_entries = jxl_alloc(alloc, sizeof(jxl_prefix_entry));
    if (out->toplevel_entries == NULL) {
        return JXL_CODING_OUT_OF_MEMORY;
    }
    out->toplevel_entries[0].nested = 0;
    out->toplevel_entries[0].bits_or_mask = 0;
    out->toplevel_entries[0].symbol_or_offset = symbol;
    out->second_level_entries = NULL;
    out->second_level_len = 0;
    return JXL_CODING_OK;
}

typedef struct {
    uint16_t *syms;
    size_t len;
    size_t cap;
} jxl_prefix_sym_list;

static jxl_coding_status_t prefix_sym_list_push(jxl_allocator_state *alloc, jxl_prefix_sym_list *list,
                                                uint16_t sym) {
    if (list->len == list->cap) {
        size_t new_cap = list->cap == 0 ? 4 : list->cap * 2;
        uint16_t *grown = jxl_alloc(alloc, new_cap * sizeof(uint16_t));
        if (grown == NULL) {
            return JXL_CODING_OUT_OF_MEMORY;
        }
        if (list->syms != NULL) {
            memcpy(grown, list->syms, list->len * sizeof(uint16_t));
            jxl_free(alloc, list->syms);
        }
        list->syms = grown;
        list->cap = new_cap;
    }
    list->syms[list->len++] = sym;
    return JXL_CODING_OK;
}

static void prefix_sym_list_clear(jxl_allocator_state *alloc, jxl_prefix_sym_list *lists,
                                  size_t count) {
                                      size_t i;
    for (i = 0; i < count; ++i) {
        jxl_free(alloc, lists[i].syms);
        lists[i].syms = NULL;
        lists[i].len = 0;
        lists[i].cap = 0;
    }
}

static jxl_coding_status_t prefix_with_code_lengths(jxl_allocator_state *alloc,
                                                    const uint8_t *code_lengths, size_t len,
                                                    jxl_prefix_histogram *out) {
                                                        size_t sym;
                                                        size_t i;
                                                        size_t idx;
    jxl_prefix_sym_list syms_for_length[JXL_PREFIX_MAX_PREFIX_BITS] = {0};
    size_t max_len;
    uint16_t current_bits;
    size_t second_level_len;
    size_t second_level_cap;
    size_t toplevel_bits;
    size_t entries_len;
    jxl_prefix_entry *entries;
    jxl_prefix_entry *second_level_entries;
    jxl_prefix_entry *toplevel_entries;

    for (sym = 0; sym < len; ++sym) {
        const uint8_t clen = code_lengths[sym];
        if (clen > 0) {
            jxl_coding_status_t st;
            if (clen > JXL_PREFIX_MAX_PREFIX_BITS) {
                prefix_sym_list_clear(alloc, syms_for_length, JXL_PREFIX_MAX_PREFIX_BITS);
                return JXL_CODING_INVALID_PREFIX_HISTOGRAM;
            }
            st = prefix_sym_list_push(alloc, &syms_for_length[clen - 1], (uint16_t)sym);
            if (st != JXL_CODING_OK) {
                prefix_sym_list_clear(alloc, syms_for_length, JXL_PREFIX_MAX_PREFIX_BITS);
                return st;
            }
        }
    }

    max_len = 0;
    for (i = 0; i < JXL_PREFIX_MAX_PREFIX_BITS; ++i) {
        if (syms_for_length[i].len > 0) {
            max_len = i + 1;
        }
    }

    toplevel_bits =
        max_len < JXL_PREFIX_MAX_TOPLEVEL_BITS ? max_len : JXL_PREFIX_MAX_TOPLEVEL_BITS;
    entries_len = (size_t)1u << toplevel_bits;
    entries = jxl_alloc(alloc, entries_len * sizeof(jxl_prefix_entry));
    if (entries == NULL) {
        prefix_sym_list_clear(alloc, syms_for_length, JXL_PREFIX_MAX_PREFIX_BITS);
        return JXL_CODING_OUT_OF_MEMORY;
    }
    memset(entries, 0, entries_len * sizeof(jxl_prefix_entry));

    current_bits = 0;
    for (idx = 0; idx < toplevel_bits; ++idx) {
        size_t s;
        const size_t shifts = toplevel_bits - 1 - idx;
        const jxl_prefix_sym_list *syms = &syms_for_length[idx];
        for (s = 0; s < syms->len; ++s) {
            size_t off;
            jxl_prefix_entry entry;
            size_t span;
            entry.nested = 0;
            entry.bits_or_mask = (uint8_t)(idx + 1);
            entry.symbol_or_offset = syms->syms[s];

            span = (size_t)1u << shifts;
            for (off = 0; off < span; ++off) {
                entries[current_bits + off] = entry;
            }
            current_bits = (uint16_t)(current_bits + (uint16_t)span);
        }
    }

    second_level_entries = NULL;
    second_level_len = 0;
    second_level_cap = 0;

    if (toplevel_bits < max_len) {
        size_t idx;
        size_t remaining_len;
        size_t remaining_entry_bits;
        jxl_prefix_entry *remaining_entries = NULL;
        remaining_len = 0;
        remaining_entry_bits = 0;

        for (idx = toplevel_bits; idx < max_len; ++idx) {
            size_t s;
            size_t chunk_len;
            size_t chunk_size_bits;
            size_t chunk_size;
            jxl_prefix_entry *chunk;
            const jxl_prefix_sym_list *syms = &syms_for_length[idx];
            if (syms->len == 0) {
                continue;
            }

            chunk_size_bits = idx + 1 - toplevel_bits;
            chunk_size = (size_t)1u << chunk_size_bits;
            chunk = jxl_alloc(alloc, chunk_size * sizeof(jxl_prefix_entry));
            if (chunk == NULL) {
                jxl_free(alloc, entries);
                jxl_free(alloc, remaining_entries);
                jxl_free(alloc, second_level_entries);
                prefix_sym_list_clear(alloc, syms_for_length, JXL_PREFIX_MAX_PREFIX_BITS);
                return JXL_CODING_OUT_OF_MEMORY;
            }
            chunk_len = 0;

            if (remaining_len > 0) {
                size_t r;
                const size_t mult = (size_t)1u << (chunk_size_bits - remaining_entry_bits);
                for (r = 0; r < remaining_len; ++r) {
                    size_t m;
                    for (m = 0; m < mult; ++m) {
                        chunk[chunk_len++] = remaining_entries[r];
                    }
                }
            }

            for (s = 0; s < syms->len; ++s) {
                jxl_prefix_entry entry;
                entry.nested = 0;
                entry.bits_or_mask = (uint8_t)(idx + 1);
                entry.symbol_or_offset = syms->syms[s];

                chunk[chunk_len++] = entry;
                if (chunk_len == chunk_size) {
                    jxl_prefix_entry compound_tmp_1;
                    if (current_bits >= entries_len) {
                        jxl_free(alloc, chunk);
                        jxl_free(alloc, entries);
                        jxl_free(alloc, remaining_entries);
                        jxl_free(alloc, second_level_entries);
                        prefix_sym_list_clear(alloc, syms_for_length, JXL_PREFIX_MAX_PREFIX_BITS);
                        return JXL_CODING_INVALID_PREFIX_HISTOGRAM;
                    }
                    compound_tmp_1.nested = 1;
                    compound_tmp_1.bits_or_mask = (uint8_t)(chunk_size - 1);
                    compound_tmp_1.symbol_or_offset = (uint16_t)second_level_len;
                    entries[current_bits] = compound_tmp_1;

                    if (second_level_len + chunk_size > second_level_cap) {
                        jxl_prefix_entry *grown;
                        size_t new_cap =
                            second_level_cap == 0 ? chunk_size : second_level_cap + chunk_size;
                        while (new_cap < second_level_len + chunk_size) {
                            new_cap *= 2;
                        }
                        grown =
                            jxl_alloc(alloc, new_cap * sizeof(jxl_prefix_entry));
                        if (grown == NULL) {
                            jxl_free(alloc, chunk);
                            jxl_free(alloc, entries);
                            jxl_free(alloc, remaining_entries);
                            jxl_free(alloc, second_level_entries);
                            prefix_sym_list_clear(alloc, syms_for_length,
                                                  JXL_PREFIX_MAX_PREFIX_BITS);
                            return JXL_CODING_OUT_OF_MEMORY;
                        }
                        if (second_level_entries != NULL) {
                            memcpy(grown, second_level_entries,
                                   second_level_len * sizeof(jxl_prefix_entry));
                            jxl_free(alloc, second_level_entries);
                        }
                        second_level_entries = grown;
                        second_level_cap = new_cap;
                    }
                    prefix_vec_reverse_bits_fixed(chunk, chunk_size,
                                                  second_level_entries + second_level_len);
                    second_level_len += chunk_size;
                    current_bits += 1;
                    chunk_len = 0;
                }
            }

            jxl_free(alloc, remaining_entries);
            remaining_entries = chunk;
            remaining_len = chunk_len;
            remaining_entry_bits = chunk_size_bits;
        }

        if (remaining_len > 0) {
            jxl_free(alloc, entries);
            jxl_free(alloc, remaining_entries);
            jxl_free(alloc, second_level_entries);
            prefix_sym_list_clear(alloc, syms_for_length, JXL_PREFIX_MAX_PREFIX_BITS);
            return JXL_CODING_INVALID_PREFIX_HISTOGRAM;
        }
        jxl_free(alloc, remaining_entries);
    }

    prefix_sym_list_clear(alloc, syms_for_length, JXL_PREFIX_MAX_PREFIX_BITS);

    if (current_bits != entries_len) {
        jxl_free(alloc, entries);
        jxl_free(alloc, second_level_entries);
        return JXL_CODING_INVALID_PREFIX_HISTOGRAM;
    }

    toplevel_entries = jxl_alloc(alloc, entries_len * sizeof(jxl_prefix_entry));
    if (toplevel_entries == NULL) {
        jxl_free(alloc, entries);
        jxl_free(alloc, second_level_entries);
        return JXL_CODING_OUT_OF_MEMORY;
    }
    prefix_vec_reverse_bits_fixed(entries, entries_len, toplevel_entries);
    jxl_free(alloc, entries);

    memset(out, 0, sizeof(*out));
    out->toplevel_bits = toplevel_bits;
    out->toplevel_mask = (uint32_t)((1u << toplevel_bits) - 1u);
    out->toplevel_entries = toplevel_entries;
    out->toplevel_len = entries_len;
    out->second_level_entries = second_level_entries;
    out->second_level_len = second_level_len;
    return JXL_CODING_OK;
}

void jxl_prefix_histogram_destroy(jxl_allocator_state *alloc, jxl_prefix_histogram *hist) {
    if (hist == NULL) {
        return;
    }
    jxl_free(alloc, hist->toplevel_entries);
    jxl_free(alloc, hist->second_level_entries);
    hist->toplevel_entries = NULL;
    hist->second_level_entries = NULL;
    hist->toplevel_len = 0;
    hist->second_level_len = 0;
}

static jxl_coding_status_t prefix_parse_simple(jxl_allocator_state *alloc, jxl_bs *bs,
                                               uint32_t alphabet_size, jxl_prefix_histogram *out) {
                                                   size_t i;
    uint32_t pow2 = 1;
    size_t alphabet_bits;
    uint32_t nsym;
    size_t pair_count;
    uint8_t *code_lengths;
    jxl_coding_status_t st;
    typedef struct {
        size_t sym;
        uint8_t len;
    } sym_len_t;
    sym_len_t pairs[4];

    while (pow2 < alphabet_size) {
        pow2 <<= 1;
    }
    alphabet_bits = 0;
    while (((size_t)1u << alphabet_bits) < pow2) {
        alphabet_bits += 1;
    }

    nsym = 0;
    JXL_CODING_TRY_BS(jxl_bs_read_bits(bs, 2, &nsym));
    nsym += 1;

    code_lengths = jxl_alloc(alloc, alphabet_size * sizeof(uint8_t));
    if (code_lengths == NULL) {
        return JXL_CODING_OUT_OF_MEMORY;
    }
    memset(code_lengths, 0, alphabet_size * sizeof(uint8_t));

    st = JXL_CODING_OK;

    if (nsym == 1) {
        uint32_t sym = 0;
        JXL_CODING_TRY_BS(jxl_bs_read_bits(bs, alphabet_bits, &sym));
        jxl_free(alloc, code_lengths);
        if (sym >= alphabet_size) {
            return JXL_CODING_INVALID_PREFIX_HISTOGRAM;
        }
        return prefix_with_single_symbol(alloc, (uint16_t)sym, out);
    }

    pair_count = 0;

    if (nsym == 2) {
        uint32_t s2 = 0;
        uint32_t s3 = 0;
        sym_len_t compound_tmp_2 = {0, 0};
        sym_len_t compound_tmp_3 = {0, 0};
        sym_len_t compound_tmp_4;
        sym_len_t compound_tmp_5;
        JXL_CODING_TRY_BS(jxl_bs_read_bits(bs, alphabet_bits, &s2));
        JXL_CODING_TRY_BS(jxl_bs_read_bits(bs, alphabet_bits, &s3));
        pairs[0] = compound_tmp_2;

        pairs[1] = compound_tmp_3;

        compound_tmp_4.sym = s2;
        compound_tmp_4.len = 1;

        pairs[2] = compound_tmp_4;

        compound_tmp_5.sym = s3;
        compound_tmp_5.len = 1;

        pairs[3] = compound_tmp_5;

        pair_count = 4;
    } else if (nsym == 3) {
        uint32_t s1 = 0;
        uint32_t s2 = 0;
        uint32_t s3 = 0;
        sym_len_t compound_tmp_6 = {0, 0};
        sym_len_t compound_tmp_7;
        sym_len_t compound_tmp_8;
        sym_len_t compound_tmp_9;
        JXL_CODING_TRY_BS(jxl_bs_read_bits(bs, alphabet_bits, &s1));
        JXL_CODING_TRY_BS(jxl_bs_read_bits(bs, alphabet_bits, &s2));
        JXL_CODING_TRY_BS(jxl_bs_read_bits(bs, alphabet_bits, &s3));
        pairs[0] = compound_tmp_6;

        compound_tmp_7.sym = s1;
        compound_tmp_7.len = 1;

        pairs[1] = compound_tmp_7;

        compound_tmp_8.sym = s2;
        compound_tmp_8.len = 2;

        pairs[2] = compound_tmp_8;

        compound_tmp_9.sym = s3;
        compound_tmp_9.len = 2;

        pairs[3] = compound_tmp_9;

        pair_count = 4;
    } else if (nsym == 4) {
        uint32_t s0 = 0;
        uint32_t s1 = 0;
        uint32_t s2 = 0;
        uint32_t s3 = 0;
        int tree_selector;
        JXL_CODING_TRY_BS(jxl_bs_read_bits(bs, alphabet_bits, &s0));
        JXL_CODING_TRY_BS(jxl_bs_read_bits(bs, alphabet_bits, &s1));
        JXL_CODING_TRY_BS(jxl_bs_read_bits(bs, alphabet_bits, &s2));
        JXL_CODING_TRY_BS(jxl_bs_read_bits(bs, alphabet_bits, &s3));
        tree_selector = 0;
        JXL_CODING_TRY_BS(jxl_bs_read_bool(bs, &tree_selector));
        if (tree_selector) {
            sym_len_t compound_tmp_10;
            sym_len_t compound_tmp_11;
            sym_len_t compound_tmp_12;
            sym_len_t compound_tmp_13;
            compound_tmp_10.sym = s0;
            compound_tmp_10.len = 1;

            pairs[0] = compound_tmp_10;

            compound_tmp_11.sym = s1;
            compound_tmp_11.len = 2;

            pairs[1] = compound_tmp_11;

            compound_tmp_12.sym = s2;
            compound_tmp_12.len = 3;

            pairs[2] = compound_tmp_12;

            compound_tmp_13.sym = s3;
            compound_tmp_13.len = 3;

            pairs[3] = compound_tmp_13;

        } else {
            sym_len_t compound_tmp_14;
            sym_len_t compound_tmp_15;
            sym_len_t compound_tmp_16;
            sym_len_t compound_tmp_17;
            compound_tmp_14.sym = s0;
            compound_tmp_14.len = 2;

            pairs[0] = compound_tmp_14;

            compound_tmp_15.sym = s1;
            compound_tmp_15.len = 2;

            pairs[1] = compound_tmp_15;

            compound_tmp_16.sym = s2;
            compound_tmp_16.len = 2;

            pairs[2] = compound_tmp_16;

            compound_tmp_17.sym = s3;
            compound_tmp_17.len = 2;

            pairs[3] = compound_tmp_17;

        }
        pair_count = 4;
    } else {
        jxl_free(alloc, code_lengths);
        return JXL_CODING_INVALID_PREFIX_HISTOGRAM;
    }

    for (i = 0; i < pair_count; ++i) {
        if (pairs[i].sym >= alphabet_size) {
            st = JXL_CODING_INVALID_PREFIX_HISTOGRAM;
            break;
        }
        code_lengths[pairs[i].sym] = pairs[i].len;
    }
    if (st == JXL_CODING_OK) {
        st = prefix_with_code_lengths(alloc, code_lengths, alphabet_size, out);
    }
    jxl_free(alloc, code_lengths);
    return st;
}

static jxl_coding_status_t prefix_parse_complex(jxl_allocator_state *alloc, jxl_bs *bs,
                                                uint32_t alphabet_size, uint32_t hskip,
                                                jxl_prefix_histogram *out) {
                                                    size_t oi;
                                                    size_t i;
    uint8_t code_length_code_lengths[18] = {0};
    size_t bitacc;
    size_t nonzero_count;
    size_t nonzero_sym;
    jxl_prefix_histogram code_length_histogram = {0};
    jxl_coding_status_t st;
    size_t cbitacc;
    uint8_t prev_sym;
    uint8_t last_nonzero_sym;
    size_t last_repeat_count;
    size_t repeat_count;
    uint8_t repeat_sym;
    static const size_t CODE_LENGTH_ORDER[18] = {1,  2,  3,  4,  0,  5,  17, 6,  16,
                                                 7,  8,  9,  10, 11, 12, 13, 14, 15};
    uint8_t *code_lengths;

    bitacc = 0;
    nonzero_count = 0;
    nonzero_sym = 0;

    for (oi = hskip; oi < 18; ++oi) {
        const size_t idx = CODE_LENGTH_ORDER[oi];
        uint32_t base;
        const jxl_u32_spec specs[4] = {JXL_U32_C(0), JXL_U32_C(4), JXL_U32_C(3),
                                       JXL_U32_C(8)};
        uint8_t len;
        base = 0;
        JXL_CODING_TRY_BS(jxl_bs_read_u32(bs, specs, &base));
        len = (uint8_t)base;
        if (base == 8) {
            int b0 = 0;
            JXL_CODING_TRY_BS(jxl_bs_read_bool(bs, &b0));
            if (b0) {
                int b1 = 0;
                JXL_CODING_TRY_BS(jxl_bs_read_bool(bs, &b1));
                len = b1 ? 5 : 1;
            } else {
                len = 2;
            }
        }
        code_length_code_lengths[idx] = len;
        if (len != 0) {
            nonzero_count += 1;
            nonzero_sym = idx;
            bitacc += (size_t)32 >> len;
            if (bitacc > 32) {
                return JXL_CODING_INVALID_PREFIX_HISTOGRAM;
            }
            if (bitacc == 32) {
                break;
            }
        }
    }

    if (nonzero_count == 1) {
        st = prefix_with_single_symbol(alloc, (uint16_t)nonzero_sym, &code_length_histogram);
    } else if (bitacc != 32) {
        return JXL_CODING_INVALID_PREFIX_HISTOGRAM;
    } else {
        st = prefix_with_code_lengths(alloc, code_length_code_lengths, 18, &code_length_histogram);
    }
    if (st != JXL_CODING_OK) {
        return st;
    }

    code_lengths = jxl_alloc(alloc, alphabet_size * sizeof(uint8_t));
    if (code_lengths == NULL) {
        jxl_prefix_histogram_destroy(alloc, &code_length_histogram);
        return JXL_CODING_OUT_OF_MEMORY;
    }
    memset(code_lengths, 0, alphabet_size * sizeof(uint8_t));

    cbitacc = 0;
    prev_sym = 8;
    last_nonzero_sym = 8;
    last_repeat_count = 0;
    repeat_count = 0;
    repeat_sym = 0;

    for (i = 0; i < alphabet_size; ++i) {
        if (repeat_count > 0) {
            code_lengths[i] = repeat_sym;
            repeat_count -= 1;
        } else {
            uint32_t sym = 0;
            st = jxl_prefix_histogram_read_symbol(&code_length_histogram, bs, &sym);
            if (st != JXL_CODING_OK) {
                jxl_free(alloc, code_lengths);
                jxl_prefix_histogram_destroy(alloc, &code_length_histogram);
                return st;
            }
            switch (sym) {
            case 0:
                break;
            case 1:
            case 2:
            case 3:
            case 4:
            case 5:
            case 6:
            case 7:
            case 8:
            case 9:
            case 10:
            case 11:
            case 12:
            case 13:
            case 14:
            case 15:
                code_lengths[i] = (uint8_t)sym;
                last_nonzero_sym = (uint8_t)sym;
                break;
            case 16: {
                uint32_t peeked = 0;
                JXL_CODING_TRY_BS(jxl_bs_peek_bits_prefilled(bs, 2, &peeked));
                repeat_count = (size_t)peeked + 3;
                JXL_CODING_TRY_BS(jxl_bs_consume_bits(bs, 2));
                if (prev_sym == 16) {
                    repeat_count += last_repeat_count * 3 - 8;
                    last_repeat_count += repeat_count;
                } else {
                    last_repeat_count = repeat_count;
                }
                repeat_sym = last_nonzero_sym;
                code_lengths[i] = repeat_sym;
                repeat_count -= 1;
                break;
            }
            case 17: {
                uint32_t peeked = 0;
                JXL_CODING_TRY_BS(jxl_bs_peek_bits_prefilled(bs, 3, &peeked));
                repeat_count = (size_t)peeked + 3;
                JXL_CODING_TRY_BS(jxl_bs_consume_bits(bs, 3));
                if (prev_sym == 17) {
                    repeat_count += last_repeat_count * 7 - 16;
                    last_repeat_count += repeat_count;
                } else {
                    last_repeat_count = repeat_count;
                }
                repeat_sym = 0;
                code_lengths[i] = repeat_sym;
                repeat_count -= 1;
                break;
            }
            default:
                jxl_free(alloc, code_lengths);
                jxl_prefix_histogram_destroy(alloc, &code_length_histogram);
                return JXL_CODING_INVALID_PREFIX_HISTOGRAM;
            }
            prev_sym = (uint8_t)sym;
        }

        if (code_lengths[i] != 0) {
            const size_t add = (size_t)1u << (JXL_PREFIX_MAX_PREFIX_BITS - code_lengths[i]);
            cbitacc += add;
            if (cbitacc > (size_t)1u << JXL_PREFIX_MAX_PREFIX_BITS) {
                jxl_free(alloc, code_lengths);
                jxl_prefix_histogram_destroy(alloc, &code_length_histogram);
                return JXL_CODING_PREFIX_SYMBOL_TOO_LARGE;
            }
            if (cbitacc == (size_t)1u << JXL_PREFIX_MAX_PREFIX_BITS && repeat_count == 0) {
                break;
            }
        }
    }

    jxl_prefix_histogram_destroy(alloc, &code_length_histogram);

    if (cbitacc != (size_t)1u << JXL_PREFIX_MAX_PREFIX_BITS || repeat_count > 0) {
        jxl_free(alloc, code_lengths);
        return JXL_CODING_INVALID_PREFIX_HISTOGRAM;
    }
    st = prefix_with_code_lengths(alloc, code_lengths, alphabet_size, out);
    jxl_free(alloc, code_lengths);
    return st;
}

jxl_coding_status_t jxl_prefix_histogram_parse(jxl_allocator_state *alloc, jxl_bs *bs,
                                               uint32_t alphabet_size,
                                               jxl_prefix_histogram *out) {
    uint32_t hskip;
    memset(out, 0, sizeof(*out));
    if (alphabet_size == 1) {
        return prefix_with_single_symbol(alloc, 0, out);
    }
    if (alphabet_size > (1u << JXL_PREFIX_MAX_PREFIX_BITS)) {
        return JXL_CODING_PREFIX_SYMBOL_TOO_LARGE;
    }

    hskip = 0;
    JXL_CODING_TRY_BS(jxl_bs_read_bits(bs, 2, &hskip));
    if (hskip == 1) {
        return prefix_parse_simple(alloc, bs, alphabet_size, out);
    }
    return prefix_parse_complex(alloc, bs, alphabet_size, hskip, out);
}

jxl_coding_status_t jxl_prefix_histogram_read_symbol(const jxl_prefix_histogram *hist, jxl_bs *bs,
                                                     uint32_t *symbol_out) {
    uint32_t peeked = 0;
    uint32_t toplevel_offset;
    jxl_prefix_entry toplevel_entry;
    JXL_CODING_TRY_BS(jxl_bs_peek_bits(bs, JXL_PREFIX_MAX_PREFIX_BITS, &peeked));
    toplevel_offset = peeked & hist->toplevel_mask;
    if (toplevel_offset >= hist->toplevel_len) {
        return JXL_CODING_INVALID_PREFIX_HISTOGRAM;
    }
    toplevel_entry = hist->toplevel_entries[toplevel_offset];
    if (toplevel_entry.nested) {
        const uint32_t chunk_offset =
            (peeked >> hist->toplevel_bits) & toplevel_entry.bits_or_mask;
        const size_t second_level_offset =
            (size_t)toplevel_entry.symbol_or_offset + (size_t)chunk_offset;
        jxl_prefix_entry second_level_entry;
        if (second_level_offset >= hist->second_level_len) {
            return JXL_CODING_INVALID_PREFIX_HISTOGRAM;
        }
        second_level_entry =
            hist->second_level_entries[second_level_offset];
        JXL_CODING_TRY_BS(jxl_bs_consume_bits(bs, second_level_entry.bits_or_mask));
        *symbol_out = second_level_entry.symbol_or_offset;
        return JXL_CODING_OK;
    }
    JXL_CODING_TRY_BS(jxl_bs_consume_bits(bs, toplevel_entry.bits_or_mask));
    *symbol_out = toplevel_entry.symbol_or_offset;
    return JXL_CODING_OK;
}

int jxl_prefix_histogram_single_symbol(const jxl_prefix_histogram *hist, uint32_t *symbol_out) {
    if (hist->toplevel_len == 1 && hist->toplevel_entries != NULL &&
        hist->toplevel_entries[0].nested == 0 && hist->toplevel_entries[0].bits_or_mask == 0) {
        *symbol_out = hist->toplevel_entries[0].symbol_or_offset;
        return 1;
    }
    return 0;
}
