// SPDX-License-Identifier: MIT OR Apache-2.0
#include "test_paths.h"

/* Isolate progressive_5 keyframe pass 0 / group 0 VarDCT decode. */
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
#include "frame/filter.h"
#include "frame/toc.h"
#include "image/image_internal.h"
#include "vardct/hf_metadata.h"

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
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        return -1;
    }
    fclose(f);
    *out = buf;
    *len = (size_t)sz;
    return 0;
}

static size_t count_block_kind(const jxl_lf_group_view *lf, jxl_block_info_kind kind) {
    size_t y;
    size_t n;
    if (lf->block_info_data == NULL) {
        return 0;
    }
    n = 0;
    for (y = 0; y < lf->block_info_height; ++y) {
        size_t x;
        for (x = 0; x < lf->block_info_width; ++x) {
            if (lf->block_info_data[y * lf->block_info_stride + x].kind == kind) {
                ++n;
            }
        }
    }
    return n;
}

int main(void) {
    size_t i;
    uint32_t pass;
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
    int rc;
    uint32_t fail_count;
    uint32_t ok_count;
    uint32_t skip_count;
    jxl_lf_global_params lp = {0};
    jxl_hf_global_params hp = {0};
    snprintf(path, sizeof(path), "%s/progressive_5/input.jxl", JXL_OXIDE_CONFORMANCE_DIR);

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
        fprintf(stderr, "image header failed\n");
        return 1;
    }
    if (jxl_image_skip_post_header(&alloc, &bs, &parsed) != JXL_BS_OK) {
        fprintf(stderr, "skip post header failed\n");
        return 1;
    }

    jxl_frame_init(&frame);
    if (jxl_frame_parse_keyframe(&alloc, &bs, &parsed, cs, cs_len, &frame) != JXL_FRAME_OK) {
        fprintf(stderr, "keyframe parse failed\n");
        return 1;
    }

    if (frame.toc.original_to_bitstream_len > 170) {
        size_t pg0_bs = jxl_toc_group_index_bitstream_order(
            &frame.toc, JXL_TOC_KIND_GROUP_PASS, 0);
        size_t pg0_orig = 1 + (size_t)frame.toc.num_lf_groups + 1;
        size_t bs6 = frame.toc.bitstream_to_original_len > 6
                         ? frame.toc.bitstream_to_original[6]
                         : 6;
        printf("toc num_lf=%u pg0_orig=%zu pg0_bs=%zu orig6->bs=%zu g6_size=%u g170_size=%u "
               "orig_to_bs[170]=%zu\n",
               frame.toc.num_lf_groups, pg0_orig, pg0_bs, bs6,
               frame.toc.groups_len > bs6 ? frame.toc.groups[bs6].size : 0,
               frame.toc.groups_len > 170 ? frame.toc.groups[170].size : 0,
               frame.toc.original_to_bitstream[170]);
        printf("  data6_len=%zu data170_len=%zu\n",
               frame.data_len > 6 ? frame.data[6].bytes_len : 0,
               frame.data_len > 170 ? frame.data[170].bytes_len : 0);
    }
    printf("keyframe %ux%u passes=%u groups=%u flags=0x%llx use_lf=%d patches=%d noise=%d "
           "do_ycbcr=%d upsampling=%u gab=%d epf=%d\n",
           frame.header.width, frame.header.height, frame.header.passes.num_passes,
           jxl_frame_header_num_groups(&frame.header), (unsigned long long)frame.header.flags.flags,
           jxl_frame_flags_use_lf_frame(&frame.header.flags),
           jxl_frame_flags_patches(&frame.header.flags), jxl_frame_flags_noise(&frame.header.flags),
           frame.header.do_ycbcr, frame.header.upsampling,
           jxl_gabor_enabled(&frame.header.restoration),
           jxl_epf_enabled(&frame.header.restoration));
    for (i = 0; i < frame.header.passes.shift_len; ++i) {
        printf("  pass_shift[%zu]=%u\n", i, frame.header.passes.shift[i]);
    }

    size_t meta_end = bs.num_read_bits / 8;
    consumed = 0;
    printf("loaded keyframe meta_end=%zu cs_remain=%zu toc_total=%zu\n", meta_end,
           cs_len > meta_end ? cs_len - meta_end : 0, frame.toc.total_size);
    jxl_frame_feed_bytes(&frame, cs + meta_end, cs_len - meta_end, &consumed);
    printf("fed consumed=%zu\n", consumed);

    jxl_lf_global_init(&lf_global);
    jxl_hf_global_init(&hf_global);

    const jxl_frame_group_data *lf_src = group_by_kind(&frame, JXL_TOC_KIND_LF_GLOBAL, 0);
    jxl_bs_init(&lf_bs, lf_src->bytes, lf_src->bytes_len);
    lp.ctx = library_ctx;
    lp.image = &parsed;
    lp.frame = &frame.header;
    lp.allow_partial = 0;

    if (jxl_lf_global_consume(&alloc, &lf_bs, &lp, &lf_global) != JXL_FRAME_OK) {
        fprintf(stderr, "lf global failed\n");
        return 1;
    }
    printf("lf_global patches=%d global_ma=%d gmodular_used=%d\n", lf_global.has_patches,
           lf_global.has_global_ma, lf_global.gmodular_used);

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
    printf("hf_global passes=%u presets=%u\n", hf_global.hf_pass_count, hf_global.num_hf_presets);

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
        lgp.gmodular = lf_global.gmodular_used ? &lf_global.gmodular : NULL;
        lgp.lf_group_idx = i;
        lgp.allow_partial = 0;

        if (jxl_lf_group_parse(&alloc, &lg_bs, &lgp, &lf_groups[i]) != JXL_FRAME_OK) {
            fprintf(stderr, "lf group %u failed\n", i);
            return 1;
        }
    }

    uint32_t num_groups = jxl_frame_header_num_groups(&frame.header);
    uint32_t num_passes = frame.header.passes.num_passes;
    rc = 0;
    fail_count = 0;
    ok_count = 0;
    skip_count = 0;
    for (pass = 0; pass < num_passes; ++pass) {
        uint32_t group;
        for (group = 0; group < num_groups; ++group) {
            int c;
            uint32_t lf_idx = jxl_frame_header_lf_group_idx_from_group_idx(&frame.header, group);
            jxl_lf_group_view lf_view;
            uint32_t gw;
            uint32_t gh;
            jxl_bs pgbs;
            jxl_hf_global_view hf_view = {0};
            jxl_subgrid_i32 sg_out[3];
            jxl_pass_group_vardct_params vparams = {0};
            if (lf_idx >= num_lf_groups || !lf_groups[lf_idx].has_hf_meta) {
                ++skip_count;
                continue;
            }
            const jxl_frame_group_data *pgd =
                group_by_kind(&frame, JXL_TOC_KIND_GROUP_PASS, pass * num_groups + group);
            if (pgd == NULL || pgd->bytes_len == 0) {
                fprintf(stderr, "pg%u/%u: missing payload\n", pass, group);
                ++fail_count;
                rc = 1;
                continue;
            }
            jxl_lf_group_fill_view(&lf_groups[lf_idx], &lf_view);
            gw = 0;
            gh = 0;
            jxl_frame_header_group_size_for(&frame.header, group, &gw, &gh);
            int32_t *coeff[3];
            for (c = 0; c < 3; ++c) {
                coeff[c] = calloc((size_t)gw * (size_t)gh, sizeof(int32_t));
            }
            jxl_bs_init(&pgbs, pgd->bytes, pgd->bytes_len);
            hf_view.num_hf_presets = hf_global.num_hf_presets;
            hf_view.hf_block_ctx = &lf_global.hf_block_ctx;
            hf_view.hf_passes = hf_global.hf_passes;
            hf_view.hf_pass_count = hf_global.hf_pass_count;


            sg_out[0].data = coeff[0];

            sg_out[0].width = gw;

            sg_out[0].height = gh;

            sg_out[0].stride = gw;

            sg_out[1].data = coeff[1];

            sg_out[1].width = gw;

            sg_out[1].height = gh;

            sg_out[1].stride = gw;

            sg_out[2].data = coeff[2];

            sg_out[2].width = gw;

            sg_out[2].height = gh;

            sg_out[2].stride = gw;

            vparams.ctx = library_ctx;
            vparams.frame_header = &frame.header;
            vparams.lf_group = &lf_view;
            vparams.hf_global = &hf_view;
            vparams.group_idx = group;
            vparams.pass_idx = pass;
            vparams.hf_coeff_out[0] = sg_out[0];
    vparams.hf_coeff_out[1] = sg_out[1];
    vparams.hf_coeff_out[2] = sg_out[2];
            vparams.allow_partial = 0;

            jxl_frame_status_t fst = jxl_decode_pass_group_vardct(&pgbs, &vparams);
            if (fst != JXL_FRAME_OK) {
                fprintf(stderr, "pg%u/%u decode failed st=%d len=%zu\n", pass, group, (int)fst,
                        pgd->bytes_len);
                ++fail_count;
                rc = 1;
            } else {
                ++ok_count;
            }
            for (c = 0; c < 3; ++c) {
                free(coeff[c]);
            }
        }
    }
    printf("pg scan: ok=%u fail=%u skip=%u (passes=%u groups=%u)\n", ok_count, fail_count,
           skip_count, num_passes, num_groups);

    jxl_frame_free(&alloc, &frame);
    jxl_lf_global_free(&alloc, &lf_global);
    jxl_hf_global_free(&alloc, &hf_global);
    for (i = 0; i < num_lf_groups; ++i) {
        jxl_lf_group_free(&alloc, &lf_groups[i]);
    }
    free(lf_groups);
    jxl_free(&alloc, cs);
    jxl_context_destroy(library_ctx);
    return rc;
}
