// SPDX-License-Identifier: MIT OR Apache-2.0
#include "jbr/scan.h"

#include "frame/frame_header.h"
#include "vardct/hf_pass.h"

#include <string.h>

static const jxl_jbr_huffman_table *ac_table_or_empty(const jxl_jbr_reconstructor *recon,
                                                      uint8_t idx) {
    if (idx < 4 && recon->has_ac_table[idx]) {
        return &recon->ac_tables[idx];
    }
    return &jxl_jbr_empty_huffman_table;
}

static const jxl_jbr_huffman_table *dc_table_or_empty(const jxl_jbr_reconstructor *recon,
                                                      uint8_t idx) {
    if (idx < 4 && recon->has_dc_table[idx]) {
        return &recon->dc_tables[idx];
    }
    return &jxl_jbr_empty_huffman_table;
}

static jxl_jbr_status huffman_lookup(const jxl_jbr_huffman_table *table, uint8_t symbol,
                                     uint8_t *len_out, uint64_t *bits_out) {
    jxl_jbr_status st = jxl_jbr_huffman_table_lookup(table, symbol, len_out, bits_out);
    if (st != JXL_JBR_OK) {
        return JXL_JBR_HUFFMAN_LOOKUP;
    }
    return JXL_JBR_OK;
}

static uint32_t leading_zeros_u32(uint32_t v) {
    if (v == 0) {
        return 32;
    }
#if defined(__GNUC__) || defined(__clang__)
    return (uint32_t)__builtin_clz(v);
#else
    {
        uint32_t n;

        n = 0;
        while ((v & 0x80000000u) == 0) {
            ++n;
            v <<= 1;
        }
        return n;
    }
#endif
}

static uint8_t coeff_bitlen_u16(uint16_t bits) {
    if (bits == 0) {
        return 0;
    }
    /* Match Rust `16 - bits.leading_zeros()` on i16 (16-bit magnitude). */
    return (uint8_t)(32 - leading_zeros_u32(bits));
}

static int32_t subgrid_i32_get(const jxl_subgrid_i32 *sg, size_t x, size_t y) {
    return sg->data[y * sg->stride + x];
}

static jxl_jbr_status refinement_grow(jxl_jbr_scan_state *state, jxl_allocator_state *alloc) {
    if (state->refinement_len < state->refinement_cap) {
        return JXL_JBR_OK;
    }
    size_t new_cap = state->refinement_cap == 0 ? 16 : state->refinement_cap * 2;
    uint8_t *new_lens = jxl_realloc(alloc, state->refinement_bitlen, new_cap);
    uint64_t *new_bits = jxl_realloc(alloc, state->refinement_bits, new_cap * sizeof(uint64_t));
    if (new_lens == NULL || new_bits == NULL) {
        jxl_free(alloc, new_lens);
        jxl_free(alloc, new_bits);
        return JXL_JBR_OUT_OF_MEMORY;
    }
    state->refinement_bitlen = new_lens;
    state->refinement_bits = new_bits;
    state->refinement_cap = new_cap;
    return JXL_JBR_OK;
}

void jxl_jbr_scan_state_init(jxl_jbr_scan_state *state, jxl_allocator_state *alloc, size_t num_comps) {
    if (state == NULL) {
        return;
    }
    memset(state, 0, sizeof(*state));
    state->dc_pred_len = num_comps;
    if (num_comps > 0 && alloc != NULL) {
        state->dc_pred = jxl_calloc(alloc, num_comps, sizeof(int16_t));
    }
}

void jxl_jbr_scan_state_free(jxl_allocator_state *alloc, jxl_jbr_scan_state *state) {
    if (state == NULL) {
        return;
    }
    jxl_free(alloc, state->dc_pred);
    jxl_free(alloc, state->refinement_bitlen);
    jxl_free(alloc, state->refinement_bits);
    memset(state, 0, sizeof(*state));
}

static void try_init_ac_table(jxl_jbr_scan_state *state, const jxl_jbr_huffman_table *ac_table) {
    if (!state->has_last_ac_table) {
        state->last_ac_table = ac_table;
        state->has_last_ac_table = 1;
    }
}

static void update_ac_table(jxl_jbr_scan_state *state, const jxl_jbr_huffman_table *ac_table) {
    state->last_ac_table = ac_table;
    state->has_last_ac_table = 1;
}

static int16_t update_dc_pred(jxl_jbr_scan_state *state, size_t comp_idx, int16_t coeff) {
    int16_t diff = (int16_t)(coeff - state->dc_pred[comp_idx]);
    state->dc_pred[comp_idx] = coeff;
    return diff;
}

static jxl_jbr_status buffer_refinement_bits(jxl_jbr_scan_state *state, jxl_allocator_state *alloc,
                                             uint64_t bits, uint8_t bitlen) {
    jxl_jbr_status st = refinement_grow(state, alloc);
    if (st != JXL_JBR_OK) {
        return st;
    }
    state->refinement_bits[state->refinement_len] = bits;
    state->refinement_bitlen[state->refinement_len] = bitlen;
    state->refinement_len += 1;
    return JXL_JBR_OK;
}

static jxl_jbr_status emit_eobrun(jxl_jbr_scan_state *state, jxl_allocator_state *alloc) {
    size_t i;
    uint8_t len;
    uint64_t bits;
    uint32_t mask;
    if (state->eobrun == 0) {
        return JXL_JBR_OK;
    }
    if (!state->has_last_ac_table) {
        return JXL_JBR_INVALID_DATA;
    }
    const jxl_jbr_huffman_table *ac_table = state->last_ac_table;
    uint32_t eobn = 31 - leading_zeros_u32(state->eobrun);
    len = 0;
    bits = 0;
    jxl_jbr_status st = huffman_lookup(ac_table, (uint8_t)(eobn << 4), &len, &bits);
    if (st != JXL_JBR_OK) {
        return st;
    }
    jxl_jbr_bit_writer_write_huffman(&state->bit_writer, alloc, bits, len);
    mask = eobn == 32 ? 0xffffffffu : ((1u << eobn) - 1u);
    jxl_jbr_bit_writer_write_raw(&state->bit_writer, alloc, state->eobrun & mask, (uint8_t)eobn);

    state->eobrun = 0;
    for (i = 0; i < state->refinement_len; ++i) {
        jxl_jbr_bit_writer_write_raw(&state->bit_writer, alloc, state->refinement_bits[i],
                                     state->refinement_bitlen[i]);
    }
    state->refinement_len = 0;
    return JXL_JBR_OK;
}

static jxl_jbr_status flush_bit_writer(jxl_jbr_scan_state *state, jxl_jbr_reconstructor *recon,
                                       jxl_allocator_state *alloc, jxl_jbr_output *out) {
    jxl_jbr_status st = emit_eobrun(state, alloc);
    if (st != JXL_JBR_OK) {
        return st;
    }

    size_t padding_needed = jxl_jbr_bit_writer_padding_bits(&state->bit_writer);
    if (padding_needed != 0) {
        uint64_t bits = (uint64_t)(uint32_t)(-1);
        if (recon->has_padding_bs) {
            uint32_t read = 0;
            if (jxl_bs_read_bits(&recon->padding_bs, padding_needed, &read) != JXL_BS_OK) {
                return JXL_JBR_INVALID_DATA;
            }
            bits = read;
        }
        jxl_jbr_bit_writer_write_raw(&state->bit_writer, alloc, bits, (uint8_t)padding_needed);
    }

    st = jxl_jbr_bit_writer_finalize(&state->bit_writer, alloc);
    if (st != JXL_JBR_OK) {
        return st;
    }

    jxl_jbr_bit_writer_init(&state->bit_writer, out);
    return JXL_JBR_OK;
}

static jxl_jbr_status restart_scan(jxl_jbr_scan_state *state, jxl_jbr_reconstructor *recon,
                                   jxl_allocator_state *alloc, jxl_jbr_output *out) {
    uint8_t rst[2];
    if (state->dc_pred != NULL) {
        memset(state->dc_pred, 0, state->dc_pred_len * sizeof(int16_t));
    }
    jxl_jbr_status st = flush_bit_writer(state, recon, alloc, out);
    if (st != JXL_JBR_OK) {
        return st;
    }

    rst[0] = 0xff;
    rst[1] = (uint8_t)(0xd0 + state->rst_m);

    st = jxl_jbr_output_write(alloc, out, rst, 2);
    if (st != JXL_JBR_OK) {
        return st;
    }
    state->rst_m = (uint8_t)((state->rst_m + 1) % 8);
    return JXL_JBR_OK;
}

jxl_jbr_status jxl_jbr_process_sequential(jxl_jbr_scan_state *state, jxl_allocator_state *alloc,
                                          size_t component_idx, const jxl_jbr_huffman_table *dc_table,
                                          const jxl_jbr_huffman_table *ac_table, int16_t dc,
                                          const int16_t *ac, size_t ac_len, int has_extra_zero_runs,
                                          uint32_t extra_zero_runs) {
    uint8_t hlen;
    uint64_t hbits;
    size_t pos;
    int16_t diff = update_dc_pred(state, component_idx, dc);
    int is_neg = diff < 0;
    uint16_t bits = (uint16_t)(is_neg ? -diff : diff);
    uint8_t bitlen = coeff_bitlen_u16(bits);
    uint16_t raw_bits = (uint16_t)(is_neg ? (uint16_t)(-bits - 1) : bits);

    hlen = 0;
    hbits = 0;
    jxl_jbr_status st = huffman_lookup(dc_table, bitlen, &hlen, &hbits);
    if (st != JXL_JBR_OK) {
        return st;
    }
    jxl_jbr_bit_writer_write_huffman(&state->bit_writer, alloc, hbits, hlen);
    jxl_jbr_bit_writer_write_raw(&state->bit_writer, alloc, raw_bits, bitlen);

    pos = 0;
    while (pos < ac_len) {
        size_t rel = 0;
        size_t nonzero_idx;
        uint16_t raw_ac;
        uint8_t ac_bitlen;
        while (rel < ac_len - pos && ac[pos + rel] == 0) {
            ++rel;
        }
        if (rel >= ac_len - pos) {
            break;
        }
        nonzero_idx = rel;
        int16_t coeff = ac[pos + rel];
        pos += rel + 1;

        while (nonzero_idx >= 16) {
            st = huffman_lookup(ac_table, 0xf0, &hlen, &hbits);
            if (st != JXL_JBR_OK) {
                return st;
            }
            jxl_jbr_bit_writer_write_huffman(&state->bit_writer, alloc, hbits, hlen);
            nonzero_idx -= 16;
        }

        raw_ac = 0;
        ac_bitlen = 0;
        if (coeff < 0) {
            uint16_t ac_abs = (uint16_t)(-coeff);
            raw_ac = (uint16_t)(~ac_abs);
            ac_bitlen = coeff_bitlen_u16(ac_abs);
        } else {
            raw_ac = (uint16_t)coeff;
            ac_bitlen = coeff_bitlen_u16(raw_ac);
        }

        uint8_t sym = (uint8_t)((nonzero_idx << 4) | ac_bitlen);
        st = huffman_lookup(ac_table, sym, &hlen, &hbits);
        if (st != JXL_JBR_OK) {
            return st;
        }
        jxl_jbr_bit_writer_write_huffman(&state->bit_writer, alloc, hbits, hlen);
        jxl_jbr_bit_writer_write_raw(&state->bit_writer, alloc, raw_ac, ac_bitlen);
    }

    int32_t num_zeros = (int32_t)(ac_len - pos);
    if (has_extra_zero_runs) {
        uint32_t i;
        st = huffman_lookup(ac_table, 0xf0, &hlen, &hbits);
        if (st != JXL_JBR_OK) {
            return st;
        }
        for (i = 0; i < extra_zero_runs; ++i) {
            jxl_jbr_bit_writer_write_huffman(&state->bit_writer, alloc, hbits, hlen);
        }
        num_zeros -= (int32_t)extra_zero_runs * 16;
    }

    if (num_zeros > 0) {
        st = huffman_lookup(ac_table, 0, &hlen, &hbits);
        if (st != JXL_JBR_OK) {
            return st;
        }
        jxl_jbr_bit_writer_write_huffman(&state->bit_writer, alloc, hbits, hlen);
    }
    return JXL_JBR_OK;
}

jxl_jbr_status jxl_jbr_process_progressive_first(jxl_jbr_scan_state *state,
                                                 jxl_allocator_state *alloc, size_t component_idx,
                                                 const jxl_jbr_huffman_table *dc_table,
                                                 const jxl_jbr_huffman_table *ac_table, int has_dc,
                                                 int16_t dc, const int16_t *ac, size_t ac_len,
                                                 int has_extra_zero_runs, uint32_t extra_zero_runs) {
    jxl_jbr_status st = JXL_JBR_OK;
    size_t pos;
    if (has_dc) {
        uint8_t hlen;
        uint64_t hbits;
        int16_t diff = update_dc_pred(state, component_idx, dc);
        int is_neg = diff < 0;
        uint16_t bits = (uint16_t)(is_neg ? -diff : diff);
        uint8_t bitlen = coeff_bitlen_u16(bits);
        uint16_t raw_bits = (uint16_t)(is_neg ? (uint16_t)(-bits - 1) : bits);

        st = emit_eobrun(state, alloc);
        if (st != JXL_JBR_OK) {
            return st;
        }

        hlen = 0;
        hbits = 0;
        st = huffman_lookup(dc_table, bitlen, &hlen, &hbits);
        if (st != JXL_JBR_OK) {
            return st;
        }
        jxl_jbr_bit_writer_write_huffman(&state->bit_writer, alloc, hbits, hlen);
        jxl_jbr_bit_writer_write_raw(&state->bit_writer, alloc, raw_bits, bitlen);
    }

    pos = 0;
    while (pos < ac_len) {
        size_t rel = 0;
        size_t nonzero_idx;
        uint16_t raw_ac;
        uint8_t ac_bitlen;
        uint8_t hlen;
        uint64_t hbits;
        while (rel < ac_len - pos && ac[pos + rel] == 0) {
            ++rel;
        }
        if (rel >= ac_len - pos) {
            break;
        }
        st = emit_eobrun(state, alloc);
        if (st != JXL_JBR_OK) {
            return st;
        }

        nonzero_idx = rel;
        int16_t coeff = ac[pos + rel];
        pos += rel + 1;

        while (nonzero_idx >= 16) {
            hlen = 0;
            hbits = 0;
            st = huffman_lookup(ac_table, 0xf0, &hlen, &hbits);
            if (st != JXL_JBR_OK) {
                return st;
            }
            jxl_jbr_bit_writer_write_huffman(&state->bit_writer, alloc, hbits, hlen);
            nonzero_idx -= 16;
        }

        raw_ac = 0;
        ac_bitlen = 0;
        if (coeff < 0) {
            uint16_t ac_abs = (uint16_t)(-coeff);
            raw_ac = (uint16_t)(~ac_abs);
            ac_bitlen = coeff_bitlen_u16(ac_abs);
        } else {
            raw_ac = (uint16_t)coeff;
            ac_bitlen = coeff_bitlen_u16(raw_ac);
        }

        uint8_t sym = (uint8_t)((nonzero_idx << 4) | ac_bitlen);
        hlen = 0;
        hbits = 0;
        st = huffman_lookup(ac_table, sym, &hlen, &hbits);
        if (st != JXL_JBR_OK) {
            return st;
        }
        jxl_jbr_bit_writer_write_huffman(&state->bit_writer, alloc, hbits, hlen);
        jxl_jbr_bit_writer_write_raw(&state->bit_writer, alloc, raw_ac, ac_bitlen);
    }

    int32_t num_zeros = (int32_t)(ac_len - pos);
    if (has_extra_zero_runs) {
        uint32_t i;
        uint8_t hlen;
        uint64_t hbits;
        st = emit_eobrun(state, alloc);
        if (st != JXL_JBR_OK) {
            return st;
        }
        hlen = 0;
        hbits = 0;
        st = huffman_lookup(ac_table, 0xf0, &hlen, &hbits);
        if (st != JXL_JBR_OK) {
            return st;
        }
        for (i = 0; i < extra_zero_runs; ++i) {
            jxl_jbr_bit_writer_write_huffman(&state->bit_writer, alloc, hbits, hlen);
        }
        num_zeros -= (int32_t)extra_zero_runs * 16;
    }

    if (state->eobrun == 0) {
        update_ac_table(state, ac_table);
    }

    if (num_zeros > 0) {
        state->eobrun += 1;
        if (state->eobrun >= 32767) {
            st = emit_eobrun(state, alloc);
            if (st != JXL_JBR_OK) {
                return st;
            }
        }
    }
    return JXL_JBR_OK;
}

jxl_jbr_status jxl_jbr_process_progressive_refinement(jxl_jbr_scan_state *state,
                                                      jxl_allocator_state *alloc,
                                                      const jxl_jbr_huffman_table *ac_table,
                                                      int has_dc, int16_t dc, const int16_t *ac,
                                                      size_t ac_len, int has_extra_zero_runs,
                                                      uint32_t extra_zero_runs) {
                                                          size_t i;
    jxl_jbr_status st = JXL_JBR_OK;
    size_t pos;
    uint8_t zrl_len;
    uint64_t zrl_bits;
    uint8_t zero_runs;
    uint8_t refinement_bitlen;
    uint64_t refinement_bits;
    uint32_t remaining_zrl;
    if (has_dc) {
        st = emit_eobrun(state, alloc);
        if (st != JXL_JBR_OK) {
            return st;
        }
        jxl_jbr_bit_writer_write_raw(&state->bit_writer, alloc, (uint64_t)(uint16_t)dc, 1);
    }

    pos = 0;
    while (pos < ac_len) {
        size_t i;
        size_t rel = pos;
        uint8_t zero_runs;
        uint8_t refinement_bitlen;
        uint64_t refinement_bits;
        uint8_t hlen;
        uint64_t hbits;
        while (rel < ac_len && ac[rel] != 1 && ac[rel] != -1) {
            ++rel;
        }
        if (rel >= ac_len) {
            break;
        }

        st = emit_eobrun(state, alloc);
        if (st != JXL_JBR_OK) {
            return st;
        }

        zero_runs = 0;
        refinement_bitlen = 0;
        refinement_bits = 0;
        for (i = pos; i < rel; ++i) {
            int16_t coeff = ac[i];
            if (coeff == 0) {
                zero_runs += 1;
                if (zero_runs == 16) {
                    hlen = 0;
                    hbits = 0;
                    st = huffman_lookup(ac_table, 0xf0, &hlen, &hbits);
                    if (st != JXL_JBR_OK) {
                        return st;
                    }
                    jxl_jbr_bit_writer_write_huffman(&state->bit_writer, alloc, hbits, hlen);
                    jxl_jbr_bit_writer_write_raw(&state->bit_writer, alloc, refinement_bits,
                                                 refinement_bitlen);
                    zero_runs = 0;
                    refinement_bitlen = 0;
                    refinement_bits = 0;
                }
            } else {
                refinement_bits = (refinement_bits << 1) | (uint64_t)(coeff & 1);
                refinement_bitlen += 1;
            }
        }

        int16_t coeff = ac[rel];
        pos = rel + 1;

        uint64_t bit = coeff == 1 ? 1 : 0;
        uint8_t sym = (uint8_t)((zero_runs << 4) | 1);
        hlen = 0;
        hbits = 0;
        st = huffman_lookup(ac_table, sym, &hlen, &hbits);
        if (st != JXL_JBR_OK) {
            return st;
        }
        jxl_jbr_bit_writer_write_huffman(&state->bit_writer, alloc, hbits, hlen);
        jxl_jbr_bit_writer_write_raw(&state->bit_writer, alloc, bit, 1);
        jxl_jbr_bit_writer_write_raw(&state->bit_writer, alloc, refinement_bits, refinement_bitlen);
    }

    remaining_zrl = has_extra_zero_runs ? extra_zero_runs : 0;
    if (remaining_zrl > 0) {
        st = emit_eobrun(state, alloc);
        if (st != JXL_JBR_OK) {
            return st;
        }
    }

    zrl_len = 0;
    zrl_bits = 0;
    if (remaining_zrl > 0) {
        st = huffman_lookup(ac_table, 0xf0, &zrl_len, &zrl_bits);
        if (st != JXL_JBR_OK) {
            return st;
        }
    }

    zero_runs = 0;
    refinement_bitlen = 0;
    refinement_bits = 0;
    for (i = pos; i < ac_len; ++i) {
        int16_t coeff = ac[i];
        if (coeff == 0) {
            zero_runs += 1;
            if (remaining_zrl > 0 && zero_runs == 16) {
                jxl_jbr_bit_writer_write_huffman(&state->bit_writer, alloc, zrl_bits, zrl_len);
                jxl_jbr_bit_writer_write_raw(&state->bit_writer, alloc, refinement_bits,
                                             refinement_bitlen);
                zero_runs = 0;
                refinement_bitlen = 0;
                refinement_bits = 0;
                remaining_zrl -= 1;
            }
        } else {
            refinement_bits = (refinement_bits << 1) | (uint64_t)(coeff & 1);
            refinement_bitlen += 1;
        }
    }

    for (i = 0; i < remaining_zrl; ++i) {
        jxl_jbr_bit_writer_write_huffman(&state->bit_writer, alloc, zrl_bits, zrl_len);
        jxl_jbr_bit_writer_write_raw(&state->bit_writer, alloc, refinement_bits, refinement_bitlen);
        zero_runs = 0;
        refinement_bitlen = 0;
        refinement_bits = 0;
    }

    if (state->eobrun == 0) {
        update_ac_table(state, ac_table);
    }

    if (zero_runs > 0 || refinement_bitlen > 0) {
        state->eobrun += 1;
        st = buffer_refinement_bits(state, alloc, refinement_bits, refinement_bitlen);
        if (st != JXL_JBR_OK) {
            return st;
        }
        if (state->eobrun >= 32767) {
            st = emit_eobrun(state, alloc);
            if (st != JXL_JBR_OK) {
                return st;
            }
        }
    }
    return JXL_JBR_OK;
}

static int16_t clamp_i16(int32_t v, int32_t lo, int32_t hi) {
    if (v < lo) {
        return (int16_t)lo;
    }
    if (v > hi) {
        return (int16_t)hi;
    }
    return (int16_t)v;
}

static int16_t saturate_sub_i16(int16_t a, int16_t b) {
    int32_t r = (int32_t)a - (int32_t)b;
    if (r < -32768) {
        return (int16_t)-32768;
    }
    if (r > 32767) {
        return (int16_t)32767;
    }
    return (int16_t)r;
}

jxl_jbr_status jxl_jbr_process_scan(jxl_jbr_reconstructor *recon, jxl_allocator_state *alloc,
                                    int scan_type, const jxl_jbr_scan_params *params,
                                    jxl_jbr_output *out) {
                                        uint32_t y8;
    size_t dct_len;
    jxl_jbr_scan_state state;
    uint32_t block_idx;
    if (recon == NULL || alloc == NULL || params == NULL || out == NULL || params->si == NULL ||
        params->smi == NULL || recon->frame == NULL) {
        return JXL_JBR_INVALID_DATA;
    }
    if (scan_type < 0 || scan_type > 2) {
        return JXL_JBR_INVALID_DATA;
    }

    const jxl_jbr_scan_info *si = params->si;
    const jxl_jbr_scan_more_info *smi = params->smi;
    const jxl_frame_header *fh = &recon->frame->header;
    const int16_t *dc_offset = recon->parsed.dc_offset;

    uint8_t ss = si->ss > 0 ? si->ss : 1;
    uint8_t se = (uint8_t)(si->se + 1);
    uint8_t al = si->al;
    uint32_t group_dim = jxl_frame_header_group_dim(fh);

    dct_len = 0;
    const jxl_coeff_order *dct_order =
        jxl_hf_pass_dct8_natural_order(recon->ctx, &dct_len);
    if (dct_order == NULL || se > dct_len) {
        return JXL_JBR_INVALID_DATA;
    }

    jxl_jbr_scan_state_init(&state, alloc, params->num_comps);
    if (params->num_comps > 0 && state.dc_pred == NULL) {
        return JXL_JBR_OUT_OF_MEMORY;
    }
    jxl_jbr_bit_writer_init(&state.bit_writer, out);

    jxl_jbr_status st = JXL_JBR_OK;
    block_idx = 0;
    for (y8 = 0; y8 < params->h8; ++y8) {
        uint32_t x8;
        for (x8 = 0; x8 < params->w8; ++x8) {
            size_t cidx;
            uint32_t mcu_idx = x8 + params->w8 * y8;
            jxl_lf_group_view lf_view;
            if (recon->restart_interval != 0 && mcu_idx != 0 &&
                mcu_idx % recon->restart_interval == 0) {
                st = restart_scan(&state, recon, alloc, out);
                if (st != JXL_JBR_OK) {
                    goto done;
                }
            }

            uint32_t group_idx = jxl_frame_header_group_idx_from_coord(
                fh, x8 << (3 + params->max_hsample), y8 << (3 + params->max_vsample));
            uint32_t lf_group_idx = jxl_frame_header_lf_group_idx_from_group_idx(fh, group_idx);
            if (group_idx >= recon->parsed.num_groups || lf_group_idx >= recon->parsed.num_lf_groups) {
                st = JXL_JBR_INVALID_DATA;
                goto done;
            }

            jxl_lf_group_fill_view(&recon->parsed.lf_groups[lf_group_idx], &lf_view);
            jxl_jbr_group_coeff_bufs *pg = &recon->parsed.pass_groups[group_idx];

            for (cidx = 0; cidx < params->num_comps; ++cidx) {
                uint32_t dy8;
                size_t idx;
                uint32_t group_width;
                uint32_t group_height;
                jxl_subgrid_i32 hf_coeff;
                const jxl_jbr_scan_component_info *c = &si->component_info[cidx];
                const jxl_jbr_huffman_table *dc_table =
                    dc_table_or_empty(recon, c->dc_tbl_idx);
                const jxl_jbr_huffman_table *ac_table =
                    ac_table_or_empty(recon, c->ac_tbl_idx);
                try_init_ac_table(&state, ac_table);

                idx = 0;
                if (fh->do_ycbcr) {
                    idx = c->comp_idx;
                } else {
                    static const size_t k_map[3] = {1, 0, 2};
                    idx = k_map[c->comp_idx % 3];
                }

                hf_coeff.data = pg->data[idx];
                hf_coeff.width = pg->width[idx];
                hf_coeff.height = pg->height[idx];
                hf_coeff.stride = pg->stride[idx];


                jxl_channel_shift shift = params->upsampling_shifts_ycbcr[idx];
                group_width = 0;
                group_height = 0;
                jxl_channel_shift_shift_size(&shift, group_dim, group_dim, &group_width,
                                             &group_height);
                size_t group_width_mask = (size_t)group_width - 1;
                size_t group_height_mask = (size_t)group_height - 1;

                uint32_t hs = params->hsamples[cidx];
                uint32_t vs = params->vsamples[cidx];

                for (dy8 = 0; dy8 < vs; ++dy8) {
                    uint32_t dx8;
                    uint32_t y_dc = y8 * vs + dy8;
                    uint32_t y_ac_start = y_dc * 8;
                    size_t y_dc_m = (size_t)(y_dc & group_height_mask);
                    size_t y_ac_m = (size_t)(y_ac_start & group_height_mask);

                    for (dx8 = 0; dx8 < hs; ++dx8) {
                        size_t ai;
                        uint32_t x_dc = x8 * hs + dx8;
                        uint32_t x_ac_start = x_dc * 8;
                        size_t x_dc_m = (size_t)(x_dc & group_width_mask);
                        size_t x_ac_m = (size_t)(x_ac_start & group_width_mask);

                        int has_dc = si->ss == 0;
                        int has_ezr;
                        uint32_t ezr;
                        int16_t dc_val = 0;
                        if (has_dc) {
                            int32_t raw = jxl_lf_quant_subgrid_sample(&lf_view.lf_quant[idx],
                                                                      x_dc_m, y_dc_m);
                            raw = saturate_sub_i16((int16_t)raw, dc_offset[idx]);
                            dc_val = clamp_i16(raw, -2047, 2047);
                            dc_val = (int16_t)(dc_val >> al);
                        }

                        size_t ac_count = (size_t)(se - ss);
                        int16_t *ac_coeffs = NULL;
                        if (ac_count > 0) {
                            ac_coeffs = jxl_alloc(alloc, ac_count * sizeof(int16_t));
                            if (ac_coeffs == NULL) {
                                st = JXL_JBR_OUT_OF_MEMORY;
                                goto done;
                            }
                            for (ai = 0; ai < ac_count; ++ai) {
                                const jxl_coeff_order *ord = &dct_order[ss + ai];
                                int32_t coeff =
                                    subgrid_i32_get(&hf_coeff, x_ac_m + ord->x, y_ac_m + ord->y);
                                if (coeff < 0) {
                                    ac_coeffs[ai] = (int16_t)(-((-coeff) >> al));
                                } else {
                                    ac_coeffs[ai] = (int16_t)(coeff >> al);
                                }
                            }
                        }

                        has_ezr = 0;
                        ezr = 0;
                        has_ezr =
                            jxl_jbr_scan_more_info_has_extra_zero_runs(smi, block_idx, &ezr);

                        if (jxl_jbr_scan_more_info_has_reset(smi, block_idx)) {
                            st = emit_eobrun(&state, alloc);
                            if (st != JXL_JBR_OK) {
                                jxl_free(alloc, ac_coeffs);
                                goto done;
                            }
                        }

                        switch (scan_type) {
                        case 0:
                            if (!has_dc) {
                                st = JXL_JBR_INVALID_DATA;
                            } else {
                                st = jxl_jbr_process_sequential(
                                    &state, alloc, cidx, dc_table, ac_table, dc_val, ac_coeffs,
                                    ac_count, has_ezr, ezr);
                            }
                            break;
                        case 1:
                            st = jxl_jbr_process_progressive_first(
                                &state, alloc, cidx, dc_table, ac_table, has_dc, dc_val,
                                ac_coeffs, ac_count, has_ezr, ezr);
                            break;
                        case 2:
                            st = jxl_jbr_process_progressive_refinement(
                                &state, alloc, ac_table, has_dc, dc_val, ac_coeffs, ac_count,
                                has_ezr, ezr);
                            break;
                        default:
                            st = JXL_JBR_INVALID_DATA;
                            break;
                        }

                        jxl_free(alloc, ac_coeffs);
                        if (st != JXL_JBR_OK) {
                            goto done;
                        }
                        block_idx += 1;
                    }
                }
            }
        }
    }

    st = flush_bit_writer(&state, recon, alloc, out);

done:
    jxl_jbr_scan_state_free(alloc, &state);
    return st;
}
