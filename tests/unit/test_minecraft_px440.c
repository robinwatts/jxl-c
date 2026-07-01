// SPDX-License-Identifier: MIT OR Apache-2.0
#include "test_paths.h"

/* Debug: inspect VarDCT state at global pixel 440 / block bx=23 for minecraft_vardct_e7. */
#include "allocator.h"
#include "bitstream/bitstream.h"
#include "codestream_collect.h"
#include "context.h"
#include "frame/frame.h"
#include "frame/frame_header.h"
#include "frame/hf_global.h"
#include "frame/lf_global.h"
#include "frame/lf_group.h"
#include "frame/pass_group.h"
#include "frame/toc.h"
#include "image/image_internal.h"
#include "render/vardct/cfl_hf.h"
#include "render/vardct/dequant_hf.h"
#include "render/vardct/group_pipeline.h"
#include "render/vardct/transform.h"
#include "render/vardct/varblocks.h"
#include "vardct/lf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static const jxl_frame_group_data *group_by_kind(const jxl_frame *frame, jxl_toc_group_kind kind,
                                                 uint32_t index) {
    size_t idx = jxl_toc_group_index_bitstream_order(&frame->toc, kind, index);
    if (idx >= frame->data_len) {
        return NULL;
    }
    return &frame->data[idx];
}

static int read_file(const char *path, uint8_t **out, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return -1;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc((size_t)sz);
    if (buf == NULL) {
        fclose(f);
        return -1;
    }
    fread(buf, 1, (size_t)sz, f);
    fclose(f);
    *out = buf;
    *len = (size_t)sz;
    return 0;
}

static float i32_bits_to_f32(int32_t v) {
    union {
        int32_t i;
        float f;
    } u;
    u.i = v;
    return u.f;
}

static void dump_block_coeffs(const char *label, float *buf, size_t gw, size_t bx0, size_t by0,
                              size_t bw, size_t bh) {
                                  size_t y;
    printf("%s block (%zu,%zu) %zux%zu:\n", label, bx0 / 8, by0 / 8, bw, bh);
    for (y = 0; y < bh && y < 4; ++y) {
        size_t x;
        for (x = 0; x < bw && x < 8; ++x) {
            printf(" %8.4g", buf[(by0 + y) * gw + (bx0 + x)]);
        }
        printf("\n");
    }
}

int main(void) {
    uint32_t i;
    int c;
    char path[512];
    size_t file_len;
    jxl_allocator_state alloc;
    size_t cs_len;
    jxl_bs bs;
    jxl_parsed_image_header parsed;
    jxl_frame frame;
    size_t consumed;
    jxl_lf_global lf_global;
    jxl_hf_global hf_global;
    jxl_bs lf_bs;
    jxl_bs hf_bs;
    jxl_lf_group_view lf_view;
    jxl_block_info_subgrid bi;
    jxl_bs pgbs;
    jxl_subgrid_i32 sg_out[3];
    size_t bx0;
    size_t by0;
    jxl_subgrid_f32 coeff_sg[3];
    jxl_subgrid_f32 coeff2[3];
    jxl_subgrid_f32 block[3];
    size_t in_bx;
    size_t in_by;
    jxl_lf_global_params lp = {0};
    jxl_hf_global_params hp = {0};
    jxl_hf_global_view hf_view = {0};
    jxl_pass_group_vardct_params vparams = {0};
    jxl_opsin_inverse_matrix opsin;
    jxl_hf_global_dequant hf_dequant;
    snprintf(path, sizeof(path), "%s/minecraft_vardct_e7/input.jxl", JXL_OXIDE_FIXTURES_DIR);

    uint8_t *file = NULL;
    file_len = 0;
    if (read_file(path, &file, &file_len) != 0) {
        fprintf(stderr, "read failed\n");
        return 1;
    }

    jxl_allocator_init(&alloc, NULL);
    jxl_context *library_ctx = NULL;
    if (jxl_context_create(NULL, &library_ctx) != JXL_OK) {
        fprintf(stderr, "context create failed\n");
        return 1;
    }

    uint8_t *cs = NULL;
    cs_len = 0;
    if (jxl_collect_codestream(&alloc, file, file_len, &cs, &cs_len) != JXL_OK) {
        fprintf(stderr, "codestream failed\n");
        return 1;
    }
    free(file);

    jxl_bs_init(&bs, cs, cs_len);
    memset(&parsed, 0, sizeof(parsed));
    if (jxl_image_header_parse(&bs, &parsed) != JXL_BS_OK) {
        fprintf(stderr, "header parse failed\n");
        return 1;
    }

    jxl_frame_init(&frame);
    if (jxl_frame_parse(&alloc, &bs, &parsed, &frame) != JXL_FRAME_OK) {
        fprintf(stderr, "frame parse failed\n");
        return 1;
    }

    size_t meta_end = bs.num_read_bits / 8;
    consumed = 0;
    jxl_frame_feed_bytes(&frame, cs + meta_end, cs_len - meta_end, &consumed);

    const uint32_t target_px = 440;
    const uint32_t target_py = 0;
    const uint32_t group_dim = jxl_frame_header_group_dim(&frame.header);
    const uint32_t groups_per_row = jxl_frame_header_groups_per_row(&frame.header);
    const uint32_t group_idx = (target_py / group_dim) * groups_per_row + (target_px / group_dim);
    const uint32_t local_x = target_px % group_dim;
    const uint32_t local_y = target_py % group_dim;
    const uint32_t lf_idx = jxl_frame_header_lf_group_idx_from_group_idx(&frame.header, group_idx);

    printf("px=(%u,%u) group=%u local=(%u,%u) lf_idx=%u\n", target_px, target_py, group_idx,
           local_x, local_y, lf_idx);

    jxl_lf_global_init(&lf_global);
    jxl_hf_global_init(&hf_global);

    const jxl_frame_group_data *lf_src = group_by_kind(&frame, JXL_TOC_KIND_LF_GLOBAL, 0);
    jxl_bs_init(&lf_bs, lf_src->bytes, lf_src->bytes_len);
    lp.ctx = library_ctx;
    lp.image = &parsed;
    lp.frame = &frame.header;

    if (jxl_lf_global_consume(&alloc, &lf_bs, &lp, &lf_global) != JXL_FRAME_OK) {
        fprintf(stderr, "lf global failed\n");
        return 1;
    }

    const jxl_frame_group_data *hf_src = group_by_kind(&frame, JXL_TOC_KIND_HF_GLOBAL, 0);
    jxl_bs_init(&hf_bs, hf_src->bytes, hf_src->bytes_len);
    hp.image = &parsed;
    hp.frame = &frame.header;
    hp.global_ma = lf_global.has_global_ma ? &lf_global.global_ma : NULL;
    hp.hf_block_ctx = &lf_global.hf_block_ctx;

    if (jxl_hf_global_parse(library_ctx, &alloc, &hf_bs, &hp, &hf_global) != JXL_FRAME_OK) {
        fprintf(stderr, "hf global failed\n");
        return 1;
    }

    uint32_t num_lf_groups = jxl_frame_header_num_lf_groups(&frame.header);
    jxl_lf_group *lf_groups = calloc(num_lf_groups, sizeof(*lf_groups));
    for (i = 0; i < num_lf_groups; ++i) {
        jxl_lf_group_init(&lf_groups[i]);
    }

    for (i = 0; i < num_lf_groups; ++i) {
        jxl_bs lg_bs;
        jxl_lf_group_params lgp = {0};
        const jxl_frame_group_data *lg_src = group_by_kind(&frame, JXL_TOC_KIND_LF_GROUP, i);
        jxl_bs_init(&lg_bs, lg_src->bytes, lg_src->bytes_len);
        lgp.ctx = library_ctx;
        lgp.image = &parsed;
        lgp.frame = &frame.header;
        lgp.quantizer = &lf_global.quantizer;
        lgp.global_ma = lf_global.has_global_ma ? &lf_global.global_ma : NULL;
        lgp.lf_group_idx = i;

        if (jxl_lf_group_parse(&alloc, &lg_bs, &lgp, &lf_groups[i]) != JXL_FRAME_OK) {
            fprintf(stderr, "lf group %u failed\n", i);
            return 1;
        }
    }

    jxl_lf_group_fill_view(&lf_groups[lf_idx], &lf_view);

    if (!jxl_pass_group_block_info_subgrid(&frame.header, group_idx, &lf_view, NULL, &bi)) {
        fprintf(stderr, "block_info failed\n");
        return 1;
    }

    size_t bx = (size_t)(local_x / 8);
    size_t by = (size_t)(local_y / 8);
    const jxl_block_info *info = &bi.data[by * bi.stride + bx];
    printf("block (%zu,%zu): kind=%d dct=%d hf_mul=%d\n", bx, by, (int)info->kind,
           (int)info->dct_select, info->hf_mul);

    uint32_t gw = 0, gh = 0;
    jxl_frame_header_group_size_for(&frame.header, group_idx, &gw, &gh);

    int32_t *i32_coeff[3];
    for (c = 0; c < 3; ++c) {
        i32_coeff[c] = calloc((size_t)gw * (size_t)gh, sizeof(int32_t));
    }

    const jxl_frame_group_data *pgd = group_by_kind(&frame, JXL_TOC_KIND_GROUP_PASS, group_idx);
    jxl_bs_init(&pgbs, pgd->bytes, pgd->bytes_len);

    hf_view.num_hf_presets = hf_global.num_hf_presets;
    hf_view.hf_block_ctx = &lf_global.hf_block_ctx;
    hf_view.hf_passes = hf_global.hf_passes;
    hf_view.hf_pass_count = hf_global.hf_pass_count;


    vparams.ctx = library_ctx;
    vparams.frame_header = &frame.header;
    vparams.lf_group = &lf_view;
    vparams.hf_global = &hf_view;
    vparams.group_idx = group_idx;
    vparams.pass_idx = 0;

    for (c = 0; c < 3; ++c) {
        jxl_subgrid_i32 compound_tmp;
        compound_tmp.data = i32_coeff[c];
        compound_tmp.width = gw;
        compound_tmp.height = gh;
        compound_tmp.stride = gw;
        sg_out[c] = compound_tmp;

        vparams.hf_coeff_out[c] = sg_out[c];
    }
    if (jxl_decode_pass_group_vardct(&pgbs, &vparams) != JXL_FRAME_OK) {
        fprintf(stderr, "vardct decode failed\n");
        return 1;
    }

    printf("i32 bits at local (%u,%u): %d %d %d -> f32 %.6g %.6g %.6g\n", local_x, local_y,
           i32_coeff[0][local_y * gw + local_x], i32_coeff[1][local_y * gw + local_x],
           i32_coeff[2][local_y * gw + local_x],
           i32_bits_to_f32(i32_coeff[0][local_y * gw + local_x]),
           i32_bits_to_f32(i32_coeff[1][local_y * gw + local_x]),
           i32_bits_to_f32(i32_coeff[2][local_y * gw + local_x]));

    float *fbuf[3];
    for (c = 0; c < 3; ++c) {
        size_t i;
        fbuf[c] = malloc((size_t)gw * (size_t)gh * sizeof(float));
        for (i = 0; i < (size_t)gw * (size_t)gh; ++i) {
            fbuf[c][i] = i32_bits_to_f32(i32_coeff[c][i]);
        }
    }

    bx0 = bx * 8;
    by0 = by * 8;
    dump_block_coeffs("pre-dequant hf", fbuf[0], gw, bx0, by0, 8, 8);

    opsin.quant_bias[0] = parsed.opsin_inverse.quant_bias[0];
    opsin.quant_bias[1] = parsed.opsin_inverse.quant_bias[1];
    opsin.quant_bias[2] = parsed.opsin_inverse.quant_bias[2];
    opsin.quant_bias_numerator = parsed.opsin_inverse.quant_bias_numerator;

    hf_dequant.ctx = library_ctx;
    hf_dequant.dequant_matrices = &hf_global.dequant_matrices;
    hf_dequant.quantizer = &lf_global.quantizer;
    hf_dequant.opsin_inverse = &opsin;

    for (c = 0; c < 3; ++c) {
        coeff_sg[c] = jxl_subgrid_f32_from_buf(fbuf[c], gw, gh, gw);
    }
    jxl_dequant_hf_varblock_grouped(library_ctx, coeff_sg, group_idx, &frame.header, &hf_dequant,
                                  &lf_view, NULL);

    printf("post-dequant @local(%u,%u): %.6g %.6g %.6g\n", local_x, local_y,
           fbuf[0][local_y * gw + local_x], fbuf[1][local_y * gw + local_x],
           fbuf[2][local_y * gw + local_x]);
    dump_block_coeffs("post-dequant Y", fbuf[0], gw, bx0, by0, 8, 8);

    if (lf_view.x_from_y.data != NULL && lf_view.b_from_y.data != NULL) {
        uint32_t group_x = group_idx % groups_per_row;
        uint32_t group_y = group_idx / groups_per_row;
        size_t cfl_base_x = (size_t)((group_x % 8u) * group_dim / 64u);
        size_t cfl_base_y = (size_t)((group_y % 8u) * group_dim / 64u);
        size_t cfl_gw = (coeff_sg[0].width + 63) / 64;
        size_t cfl_gh = (coeff_sg[0].height + 63) / 64;
        jxl_const_subgrid_i32 x_cfl;
        jxl_const_subgrid_i32 b_cfl;
        x_cfl.data = lf_view.x_from_y.data + cfl_base_y * lf_view.x_from_y.stride + cfl_base_x;
        x_cfl.width = cfl_gw;
        x_cfl.height = cfl_gh;
        x_cfl.stride = lf_view.x_from_y.stride;

        b_cfl.data = lf_view.b_from_y.data + cfl_base_y * lf_view.b_from_y.stride + cfl_base_x;
        b_cfl.width = cfl_gw;
        b_cfl.height = cfl_gh;
        b_cfl.stride = lf_view.b_from_y.stride;

        jxl_chroma_from_luma_hf_grouped(coeff_sg, x_cfl, b_cfl, &lf_global.lf_chan_corr);
        printf("post-cfl @local(%u,%u): %.6g %.6g %.6g\n", local_x, local_y,
               fbuf[0][local_y * gw + local_x], fbuf[1][local_y * gw + local_x],
               fbuf[2][local_y * gw + local_x]);
    }

    float *fbuf2[3];
    for (c = 0; c < 3; ++c) {
        fbuf2[c] = malloc((size_t)gw * (size_t)gh * sizeof(float));
        memcpy(fbuf2[c], fbuf[c], (size_t)gw * (size_t)gh * sizeof(float));
    }
    for (c = 0; c < 3; ++c) {
        coeff2[c] = jxl_subgrid_f32_from_buf(fbuf2[c], gw, gh, gw);
    }

    for (c = 0; c < 3; ++c) {
        block[c] = jxl_subgrid_f32_sub(coeff2[c], bx0, by0, 8, 8);
    }
    jxl_render_transform_varblock(NULL, &alloc, block[0], info->dct_select);
    jxl_render_transform_varblock(NULL, &alloc, block[1], info->dct_select);
    jxl_render_transform_varblock(NULL, &alloc, block[2], info->dct_select);

    in_bx = local_x - bx0;
    in_by = local_y - by0;
    printf("post-transform (hf only) @block(%zu,%zu): %.6g %.6g %.6g\n", in_bx, in_by,
           jxl_subgrid_f32_get(block[0], in_bx, in_by), jxl_subgrid_f32_get(block[1], in_bx, in_by),
           jxl_subgrid_f32_get(block[2], in_bx, in_by));

    printf("Rust pre-filter ref @440: -0.00413295 0.57746 0.727269\n");

    jxl_free(&alloc, cs);
    for (c = 0; c < 3; ++c) {
        free(i32_coeff[c]);
        free(fbuf[c]);
        free(fbuf2[c]);
    }
    for (i = 0; i < num_lf_groups; ++i) {
        jxl_lf_group_free(&alloc, &lf_groups[i]);
    }
    free(lf_groups);
    jxl_hf_global_free(&alloc, &hf_global);
    jxl_lf_global_free(&alloc, &lf_global);
    jxl_frame_free(&alloc, &frame);
    jxl_context_destroy(library_ctx);
    return 0;
}
