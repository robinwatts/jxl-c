// SPDX-License-Identifier: MIT OR Apache-2.0
#include "hf_global.h"

#include "context.h"
#include "frame/frame_header.h"

#include <string.h>

static jxl_frame_status_t vardct_to_frame(jxl_vardct_status_t st) {
    switch (st) {
    case JXL_VARDCT_OK:
        return JXL_FRAME_OK;
    case JXL_VARDCT_OUT_OF_MEMORY:
        return JXL_FRAME_OUT_OF_MEMORY;
    case JXL_VARDCT_BITSTREAM_ERROR:
        return JXL_FRAME_BITSTREAM_ERROR;
    default:
        return JXL_FRAME_DECODER_ERROR;
    }
}

static uint32_t next_power_of_two_u32(uint32_t v) {
    if (v <= 1u) {
        return 1u;
    }
    --v;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    return v + 1u;
}

static uint32_t trailing_zeros_u32(uint32_t v) {
    uint32_t n;
    if (v == 0) {
        return 0;
    }
#if defined(__GNUC__) || defined(__clang__)
    return (uint32_t)__builtin_ctz(v);
#else
    n = 0;
    while ((v & 1u) == 0) {
        v >>= 1;
        ++n;
    }
    return n;
#endif
}

void jxl_hf_global_init(jxl_hf_global *hf) {
    if (hf == NULL) {
        return;
    }
    memset(hf, 0, sizeof(*hf));
    jxl_dequant_matrix_set_init(&hf->dequant_matrices);
}

void jxl_hf_global_free(jxl_allocator_state *alloc, jxl_hf_global *hf) {
    if (hf == NULL) {
        return;
    }
    jxl_dequant_matrix_set_free(&hf->dequant_matrices);
    if (hf->hf_passes != NULL) {
        size_t i;
        for (i = 0; i < hf->hf_pass_count; ++i) {
            jxl_hf_pass_destroy(alloc, &hf->hf_passes[i]);
        }
        jxl_free(alloc, hf->hf_passes);
    }
    jxl_hf_global_init(hf);
}

jxl_frame_status_t jxl_hf_global_parse(jxl_context *ctx, jxl_allocator_state *alloc, jxl_bs *bs,
                                       const jxl_hf_global_params *params, jxl_hf_global *out) {
                                           size_t i;
    uint32_t preset_raw;
    jxl_dequant_matrix_set_params dparams = {0};
    jxl_hf_pass_params pass_params = {0};
    jxl_frame_status_t fst;
    uint32_t num_groups;
    uint32_t preset_bits;
    uint32_t num_passes;
    if (ctx == NULL || alloc == NULL || bs == NULL || params == NULL || params->image == NULL ||
        params->frame == NULL || params->hf_block_ctx == NULL || out == NULL) {
        return JXL_FRAME_BITSTREAM_ERROR;
    }

    dparams.ctx = ctx;
    dparams.bit_depth = params->image->bit_depth_bits;
    dparams.stream_index = jxl_dequant_matrix_set_stream_index(jxl_frame_header_num_lf_groups(params->frame));
    dparams.global_ma = params->global_ma;

    fst = vardct_to_frame(
        jxl_dequant_matrix_set_parse(ctx, alloc, bs, &dparams, &out->dequant_matrices));
    if (fst != JXL_FRAME_OK) {
        return fst;
    }

    num_groups = jxl_frame_header_num_groups(params->frame);
    preset_bits =
        trailing_zeros_u32(next_power_of_two_u32(num_groups > 0 ? num_groups : 1));
    preset_raw = 0;
    if (jxl_bs_read_bits(bs, preset_bits, &preset_raw) != JXL_BS_OK) {
        return JXL_FRAME_BITSTREAM_ERROR;
    }
    out->num_hf_presets = preset_raw + 1u;

    num_passes = params->frame->passes.num_passes;
    if (num_passes == 0) {
        return JXL_FRAME_OK;
    }

    out->hf_passes = jxl_calloc(alloc, num_passes, sizeof(*out->hf_passes));
    if (out->hf_passes == NULL) {
        return JXL_FRAME_OUT_OF_MEMORY;
    }
    out->hf_pass_count = num_passes;

    pass_params.hf_block_ctx = params->hf_block_ctx;
    pass_params.num_hf_presets = out->num_hf_presets;

    for (i = 0; i < out->hf_pass_count; ++i) {
        jxl_hf_pass_init(&out->hf_passes[i]);
        fst = vardct_to_frame(
            jxl_hf_pass_parse(ctx, alloc, bs, &pass_params, &out->hf_passes[i]));
        if (fst != JXL_FRAME_OK) {
            return fst;
        }
    }
    return JXL_FRAME_OK;
}
