// SPDX-License-Identifier: MIT OR Apache-2.0
#include "toc.h"

#include "coding/decoder.h"
#include "frame/util.h"

#include <stdlib.h>
#include <string.h>

static const jxl_u32_spec k_toc_size[4] = {JXL_U32_BITS(0, 10), JXL_U32_BITS(1024, 14),
                                           JXL_U32_BITS(17408, 22), JXL_U32_BITS(4211712, 30)};

void jxl_toc_init(jxl_toc *t) {
    if (t != NULL) {
        memset(t, 0, sizeof(*t));
    }
}

void jxl_toc_free(jxl_allocator_state *alloc, jxl_toc *t) {
    if (t == NULL) {
        return;
    }
    jxl_free(alloc, t->groups);
    jxl_free(alloc, t->bitstream_to_original);
    jxl_free(alloc, t->original_to_bitstream);
    jxl_toc_init(t);
}

static void assign_kind(jxl_toc_group *g, size_t entry_count, size_t idx, uint32_t num_lf_groups,
                        uint32_t num_groups, uint32_t num_passes) {
    size_t rel;
    size_t pass_base;
    memset(g, 0, sizeof(*g));
    if (entry_count == 1) {
        g->kind = JXL_TOC_KIND_ALL;
        return;
    }
    if (idx == 0) {
        g->kind = JXL_TOC_KIND_LF_GLOBAL;
        return;
    }
    if (idx <= (size_t)num_lf_groups) {
        g->kind = JXL_TOC_KIND_LF_GROUP;
        g->lf_group_idx = (uint32_t)(idx - 1);
        return;
    }
    if (idx == (size_t)num_lf_groups + 1) {
        g->kind = JXL_TOC_KIND_HF_GLOBAL;
        return;
    }
    pass_base = (size_t)num_lf_groups + 2;
    rel = idx - pass_base;
    g->kind = JXL_TOC_KIND_GROUP_PASS;
    g->group_idx = (uint32_t)(rel % (size_t)num_groups);
    g->pass_idx = (uint32_t)(rel / (size_t)num_groups);
    (void)num_passes;
}

jxl_frame_status_t jxl_toc_parse(jxl_allocator_state *alloc, jxl_bs *bs,
                                 const jxl_frame_header *header, jxl_toc *out) {
                                     size_t i;
    size_t entry_count;
    int permutated;
    size_t permutation_len;
    size_t total_size;
    uint32_t num_groups;
    uint32_t num_passes;
    uint32_t num_lf_groups;
    size_t *permutation = NULL;
    uint32_t *sizes;
    size_t *offsets;
    size_t acc;
    uint32_t *sizes_out;
    size_t *offsets_out;
    uint32_t *sizes_perm;
    size_t *offsets_perm;
    if (alloc == NULL || bs == NULL || header == NULL || out == NULL) {
        return JXL_FRAME_BITSTREAM_ERROR;
    }
    jxl_toc_free(alloc, out);

    num_groups = jxl_frame_header_num_groups(header);
    num_passes = header->passes.num_passes;
    num_lf_groups = jxl_frame_header_num_lf_groups(header);

    entry_count = 1;
    if (!(num_groups == 1 && num_passes == 1)) {
        entry_count = 1 + (size_t)num_lf_groups + 1 + (size_t)num_groups * (size_t)num_passes;
    }
    if (entry_count > 65536) {
        return JXL_FRAME_VALIDATION_ERROR;
    }

    permutated = 0;
    JXL_FRAME_TRY_BS(jxl_bs_read_bool(bs, &permutated));

    permutation_len = 0;
    if (permutated) {
        jxl_coding_decoder *perm_dec = NULL;
        JXL_FRAME_TRY_CODING(jxl_coding_decoder_parse(alloc, bs, 8, &perm_dec));
        JXL_FRAME_TRY_CODING(jxl_coding_decoder_begin(perm_dec, bs));
        JXL_FRAME_TRY_CODING(jxl_coding_read_permutation(alloc, bs, perm_dec, (uint32_t)entry_count,
                                                         0, &permutation, &permutation_len));
        JXL_FRAME_TRY_CODING(jxl_coding_decoder_finalize(perm_dec));
        jxl_coding_decoder_destroy(alloc, perm_dec);
        if (permutation_len != entry_count) {
            jxl_coding_permutation_destroy(alloc, permutation);
            return JXL_FRAME_DECODER_ERROR;
        }
    }

    JXL_FRAME_TRY_BS(jxl_bs_zero_pad_to_byte(bs));

    sizes = jxl_alloc(alloc, entry_count * sizeof(uint32_t));
    if (sizes == NULL && entry_count > 0) {
        jxl_coding_permutation_destroy(alloc, permutation);
        return JXL_FRAME_OUT_OF_MEMORY;
    }
    for (i = 0; i < entry_count; ++i) {
        JXL_FRAME_TRY_BS(jxl_bs_read_u32(bs, k_toc_size, &sizes[i]));
    }

    JXL_FRAME_TRY_BS(jxl_bs_zero_pad_to_byte(bs));

    offsets = jxl_alloc(alloc, entry_count * sizeof(size_t));
    if (offsets == NULL && entry_count > 0) {
        jxl_free(alloc, sizes);
        jxl_coding_permutation_destroy(alloc, permutation);
        return JXL_FRAME_OUT_OF_MEMORY;
    }

    acc = bs->num_read_bits / 8;
    total_size = 0;
    for (i = 0; i < entry_count; ++i) {
        offsets[i] = acc;
        acc += sizes[i];
        total_size += sizes[i];
    }

    sizes_out = sizes;
    offsets_out = offsets;
    sizes_perm = NULL;
    offsets_perm = NULL;

    if (permutated) {
        size_t idx;
        sizes_perm = jxl_alloc(alloc, entry_count * sizeof(uint32_t));
        offsets_perm = jxl_alloc(alloc, entry_count * sizeof(size_t));
        out->bitstream_to_original = jxl_alloc(alloc, entry_count * sizeof(size_t));
        out->original_to_bitstream = jxl_alloc(alloc, entry_count * sizeof(size_t));
        if ((sizes_perm == NULL || offsets_perm == NULL || out->bitstream_to_original == NULL ||
             out->original_to_bitstream == NULL) &&
            entry_count > 0) {
            jxl_free(alloc, sizes);
            jxl_free(alloc, offsets);
            jxl_free(alloc, sizes_perm);
            jxl_free(alloc, offsets_perm);
            jxl_coding_permutation_destroy(alloc, permutation);
            return JXL_FRAME_OUT_OF_MEMORY;
        }
        out->bitstream_to_original_len = entry_count;
        out->original_to_bitstream_len = entry_count;
        for (idx = 0; idx < entry_count; ++idx) {
            size_t perm = permutation[idx];
            offsets_perm[idx] = offsets[perm];
            sizes_perm[idx] = sizes[perm];
            out->bitstream_to_original[perm] = idx;
            out->original_to_bitstream[idx] = perm;
        }
        sizes_out = sizes_perm;
        offsets_out = offsets_perm;
    }

    out->groups = jxl_alloc(alloc, entry_count * sizeof(jxl_toc_group));
    if (out->groups == NULL && entry_count > 0) {
        jxl_toc_free(alloc, out);
        jxl_free(alloc, sizes);
        jxl_free(alloc, offsets);
        jxl_free(alloc, sizes_perm);
        jxl_free(alloc, offsets_perm);
        jxl_coding_permutation_destroy(alloc, permutation);
        return JXL_FRAME_OUT_OF_MEMORY;
    }
    out->groups_len = entry_count;
    out->num_lf_groups = num_lf_groups;
    out->num_groups = num_groups;
    out->total_size = total_size;

    for (i = 0; i < entry_count; ++i) {
        size_t kind_idx = i;
        if (out->original_to_bitstream_len > 0 && i < out->original_to_bitstream_len) {
            kind_idx = out->original_to_bitstream[i];
        }
        assign_kind(&out->groups[i], entry_count, kind_idx, num_lf_groups, num_groups, num_passes);
        out->groups[i].offset = offsets_out[i];
        out->groups[i].size = sizes_out[i];
    }

    if (entry_count > 0) {
        size_t idx = 0;
        if (out->bitstream_to_original_len > 0) {
            idx = out->bitstream_to_original[0];
        }
        if (idx < entry_count) {
            out->bookmark = out->groups[idx].offset;
        }
    }

    jxl_free(alloc, sizes);
    jxl_free(alloc, offsets);
    jxl_free(alloc, sizes_perm);
    jxl_free(alloc, offsets_perm);
    jxl_coding_permutation_destroy(alloc, permutation);
    return JXL_FRAME_OK;
}

void jxl_toc_adjust_offsets(jxl_toc *t, size_t global_frame_offset) {
    size_t i;
    if (t == NULL || global_frame_offset == 0) {
        return;
    }
    for (i = 0; i < t->groups_len; ++i) {
        if (t->groups[i].offset >= global_frame_offset) {
            t->groups[i].offset -= global_frame_offset;
        }
    }
    if (t->bookmark >= global_frame_offset) {
        t->bookmark -= global_frame_offset;
    }
}

int jxl_toc_is_single_entry(const jxl_toc *t) {
    return t != NULL && t->groups_len <= 1;
}

size_t jxl_toc_group_index_bitstream_order(const jxl_toc *t, jxl_toc_group_kind kind,
                                           uint32_t index) {
    size_t original_order;
    if (t == NULL) {
        return 0;
    }
    original_order = 0;
    if (kind == JXL_TOC_KIND_ALL && jxl_toc_is_single_entry(t)) {
        original_order = 0;
    } else if (kind == JXL_TOC_KIND_LF_GLOBAL) {
        original_order = 0;
    } else if (kind == JXL_TOC_KIND_LF_GROUP) {
        original_order = 1 + index;
    } else if (kind == JXL_TOC_KIND_HF_GLOBAL) {
        original_order = 1 + t->num_lf_groups;
    } else if (kind == JXL_TOC_KIND_GROUP_PASS) {
        uint32_t pass_idx = index / (t->num_groups ? t->num_groups : 1);
        uint32_t group_idx = index % (t->num_groups ? t->num_groups : 1);
        original_order = 1 + t->num_lf_groups + 1 + (size_t)pass_idx * t->num_groups + group_idx;
    }
    if (t->original_to_bitstream_len == 0) {
        return original_order;
    }
    if (original_order < t->original_to_bitstream_len) {
        return t->original_to_bitstream[original_order];
    }
    return original_order;
}
