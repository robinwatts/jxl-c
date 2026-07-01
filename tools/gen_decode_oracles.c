// SPDX-License-Identifier: MIT OR Apache-2.0
/*
 * Write decode parity oracle text files used by modular e2e tests.
 *
 * Usage: gen_decode_oracles <decode-fixtures-dir> <oracle-output-dir>
 */
#include "jxl_oxide/jxl_context.h"
#include "jxl_oxide/jxl_types.h"

#include "allocator.h"
#include "bitstream/bitstream.h"
#include "codestream_collect.h"
#include "frame/frame.h"
#include "frame/frame_header.h"
#include "frame/lf_global_modular.h"
#include "frame/toc.h"
#include "image/image_internal.h"
#include "modular/modular.h"
#include "modular/prepare_subimage.h"
#include "vardct/lf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static jxl_allocator_state g_alloc;
static jxl_context *g_ctx;
static int g_init;

static void ensure_init(void) {
    if (g_init) {
        return;
    }
    jxl_allocator_init(&g_alloc, NULL);
    if (jxl_context_create(NULL, &g_ctx) != JXL_OK) {
        fprintf(stderr, "gen_decode_oracles: context create failed\n");
        exit(1);
    }
    g_init = 1;
}

static int read_file(const char *path, uint8_t **out, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    long sz;
    uint8_t *buf;
    if (f == NULL) {
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (sz = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }
    buf = malloc((size_t)sz);
    if (buf == NULL || fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        return -1;
    }
    fclose(f);
    *out = buf;
    *out_len = (size_t)sz;
    return 0;
}

static jxl_frame *load_modular_frame(const char *decode_dir, const char *fixture,
                                   jxl_parsed_image_header *image_out, uint8_t **cs_out,
                                   size_t *cs_len_out) {
    char path[512];
    jxl_bs bs;
    jxl_parsed_image_header image;
    size_t consumed;
    jxl_frame *frame;
    uint8_t *file = NULL;
    size_t file_len = 0;
    uint8_t *cs = NULL;
    size_t cs_len = 0;

    snprintf(path, sizeof(path), "%s/%s/input.jxl", decode_dir, fixture);
    if (read_file(path, &file, &file_len) != 0) {
        return NULL;
    }
    if (jxl_collect_codestream(&g_alloc, file, file_len, &cs, &cs_len) != JXL_OK) {
        free(file);
        return NULL;
    }
    free(file);

    jxl_bs_init(&bs, cs, cs_len);
    memset(&image, 0, sizeof(image));
    if (jxl_image_header_parse(&bs, &image) != JXL_BS_OK) {
        jxl_free(&g_alloc, cs);
        return NULL;
    }

    frame = malloc(sizeof(*frame));
    if (frame == NULL) {
        jxl_free(&g_alloc, cs);
        return NULL;
    }
    jxl_frame_init(frame);
    if (jxl_frame_parse(&g_alloc, &bs, &image, frame) != JXL_FRAME_OK) {
        jxl_frame_free(&g_alloc, frame);
        free(frame);
        jxl_free(&g_alloc, cs);
        return NULL;
    }

    consumed = 0;
    {
        size_t meta_end = bs.num_read_bits / 8;
        jxl_frame_feed_bytes(frame, cs + meta_end, frame->toc.total_size, &consumed);
    }
    if (consumed != frame->toc.total_size || !jxl_frame_is_loading_done(frame)) {
        jxl_frame_free(&g_alloc, frame);
        free(frame);
        jxl_free(&g_alloc, cs);
        return NULL;
    }

    *image_out = image;
    *cs_out = cs;
    *cs_len_out = cs_len;
    return frame;
}

static size_t pass_group_bit_offset(jxl_frame *frame, const jxl_parsed_image_header *image) {
    jxl_ma_config global_ma;
    int has_ma;
    jxl_bs pg_bs;
    jxl_frame_status_t st;

    if (frame->header.encoding != JXL_FRAME_ENCODING_MODULAR) {
        return 0;
    }
    jxl_ma_config_init(&global_ma);
    has_ma = 0;
    st = jxl_frame_modular_pass_group_bitstream(g_ctx, &g_alloc, frame, image, 0, 0, &global_ma,
                                                &has_ma, &pg_bs, 1);
    jxl_ma_config_destroy(&g_alloc, &global_ma);
    if (st != JXL_FRAME_OK) {
        return 0;
    }
    return pg_bs.num_read_bits;
}

static jxl_frame_status_t setup_modular_dest(jxl_frame *frame,
                                             const jxl_parsed_image_header *image,
                                             jxl_ma_config *global_ma, int *has_ma_out,
                                             jxl_modular_image_destination *dest) {
    jxl_bs gbs;
    jxl_lf_channel_dequant dequant;
    int has_ma;
    jxl_modular_params mod_params;
    jxl_modular_parse_ctx ctx = {0};
    const jxl_frame_group_data *src =
        jxl_toc_is_single_entry(&frame->toc)
            ? (frame->data_len > 0 ? &frame->data[0] : NULL)
            : jxl_frame_group_by_kind(frame, JXL_TOC_KIND_LF_GLOBAL, 0);

    if (src == NULL) {
        return JXL_FRAME_BITSTREAM_ERROR;
    }

    jxl_ma_config_destroy(&g_alloc, global_ma);
    jxl_ma_config_init(global_ma);

    jxl_bs_init(&gbs, src->bytes, src->bytes_len);
    if (jxl_lf_channel_dequant_parse(&gbs, &dequant) != JXL_VARDCT_OK) {
        return JXL_FRAME_BITSTREAM_ERROR;
    }

    has_ma = 0;
    if (jxl_bs_read_bool(&gbs, &has_ma) != JXL_BS_OK) {
        return JXL_FRAME_BITSTREAM_ERROR;
    }
    *has_ma_out = has_ma;
    if (has_ma) {
        jxl_ma_config_params ma_params = {0};
        uint64_t num_ch = (uint64_t)frame->header.encoded_color_channels +
                          (uint64_t)image->num_extra_channels;
        uint64_t samples =
            (uint64_t)frame->header.width * (uint64_t)frame->header.height * num_ch / 16u;
        size_t node_limit = (size_t)(1024u + samples);
        if (node_limit > (1u << 22)) {
            node_limit = 1u << 22;
        }
        ma_params.tracker = NULL;
        ma_params.node_limit = node_limit;
        ma_params.depth_limit = 2048;
        if (jxl_ma_config_parse(&g_alloc, &gbs, &ma_params, global_ma) != JXL_MODULAR_OK) {
            return JXL_FRAME_DECODER_ERROR;
        }
    }

    jxl_modular_params_init(&mod_params);
    if (!jxl_modular_params_set_for_modular_frame(&g_alloc, g_ctx, &mod_params, image,
                                                  &frame->header)) {
        jxl_modular_params_free(&g_alloc, &mod_params);
        return JXL_FRAME_OUT_OF_MEMORY;
    }

    ctx.ctx = g_ctx;
    ctx.params = &mod_params;
    ctx.global_ma = has_ma ? global_ma : NULL;
    ctx.tracker = NULL;
    ctx.retain_pretransform_channels = 1;

    if (jxl_modular_dest_apply_local_header(&g_alloc, &gbs, &ctx, dest) != JXL_MODULAR_OK) {
        jxl_modular_params_free(&g_alloc, &mod_params);
        return JXL_FRAME_DECODER_ERROR;
    }
    if (jxl_modular_image_has_squeeze(dest)) {
        ctx.retain_pretransform_channels = 1;
    }
    if (jxl_modular_prepare_gmodular(&g_alloc, dest) != JXL_MODULAR_OK) {
        jxl_modular_params_free(&g_alloc, &mod_params);
        return JXL_FRAME_DECODER_ERROR;
    }
    {
        int multi_group = !jxl_toc_is_single_entry(&frame->toc);
        if (jxl_modular_gmodular_decode(g_ctx, &g_alloc, &gbs, dest, multi_group ? 1 : 0) !=
            JXL_MODULAR_OK) {
            jxl_modular_params_free(&g_alloc, &mod_params);
            return JXL_FRAME_DECODER_ERROR;
        }
    }
    jxl_modular_params_free(&g_alloc, &mod_params);
    return JXL_FRAME_OK;
}

static void write_layout_line(FILE *out, const char *fixture,
                              const jxl_modular_image_destination *dest) {
    const jxl_modular_channels *ch = &dest->channels;
    size_t i;
    fprintf(out, "%s %u %zu", fixture, (unsigned)ch->nb_meta_channels, ch->info_len);
    for (i = 0; i < ch->info_len; ++i) {
        fprintf(out, " %u %u %d %d", ch->info[i].width, ch->info[i].height, ch->info[i].hshift,
                ch->info[i].vshift);
    }
    fputc('\n', out);
}

static int write_pass_group_offsets(const char *fixtures_dir, const char *oracle_dir) {
    static const char *fixtures[] = {"grayalpha", "issue_311", "squeeze_edge"};
    char path[512];
    FILE *out;
    size_t i;

    snprintf(path, sizeof(path), "%s/modular_pass_group_offsets.txt", oracle_dir);
    out = fopen(path, "wb");
    if (out == NULL) {
        perror(path);
        return 1;
    }

    for (i = 0; i < sizeof(fixtures) / sizeof(fixtures[0]); ++i) {
        jxl_parsed_image_header image;
        uint8_t *cs = NULL;
        size_t cs_len = 0;
        jxl_frame *frame = load_modular_frame(fixtures_dir, fixtures[i], &image, &cs, &cs_len);
        size_t bits;
        if (frame == NULL) {
            fprintf(stderr, "gen_decode_oracles: failed to load %s\n", fixtures[i]);
            fclose(out);
            return 1;
        }
        bits = pass_group_bit_offset(frame, &image);
        fprintf(out, "%s %zu\n", fixtures[i], bits);
        jxl_frame_free(&g_alloc, frame);
        free(frame);
        jxl_free(&g_alloc, cs);
    }

    fclose(out);
    printf("wrote %s\n", path);
    return 0;
}

static int write_transformed_layouts(const char *fixtures_dir, const char *oracle_dir) {
    char path[512];
    FILE *out;

    snprintf(path, sizeof(path), "%s/modular_transformed_layouts.txt", oracle_dir);
    out = fopen(path, "wb");
    if (out == NULL) {
        perror(path);
        return 1;
    }

    /* grayalpha: Rust transformed-layout oracle uses empty channel list. */
    fputs("grayalpha 0 0\n", out);

    {
        static const char *fixture = "squeeze_edge";
        jxl_parsed_image_header image;
        uint8_t *cs = NULL;
        size_t cs_len = 0;
        jxl_ma_config global_ma;
        int has_ma;
        jxl_modular_image_destination dest;
        jxl_frame *frame = load_modular_frame(fixtures_dir, fixture, &image, &cs, &cs_len);

        if (frame == NULL) {
            fprintf(stderr, "gen_decode_oracles: failed to load %s\n", fixture);
            fclose(out);
            return 1;
        }
        jxl_ma_config_init(&global_ma);
        has_ma = 0;
        jxl_modular_image_destination_init(&dest);
        if (setup_modular_dest(frame, &image, &global_ma, &has_ma, &dest) != JXL_FRAME_OK) {
            fprintf(stderr, "gen_decode_oracles: setup modular dest failed for %s\n", fixture);
            jxl_modular_image_destination_free(&g_alloc, &dest);
            jxl_ma_config_destroy(&g_alloc, &global_ma);
            jxl_frame_free(&g_alloc, frame);
            free(frame);
            jxl_free(&g_alloc, cs);
            fclose(out);
            return 1;
        }
        write_layout_line(out, fixture, &dest);
        jxl_modular_image_destination_free(&g_alloc, &dest);
        jxl_ma_config_destroy(&g_alloc, &global_ma);
        jxl_frame_free(&g_alloc, frame);
        free(frame);
        jxl_free(&g_alloc, cs);
    }

    fclose(out);
    printf("wrote %s\n", path);
    return 0;
}

int main(int argc, char **argv) {
    const char *fixtures_dir;
    const char *oracle_dir;

    if (argc != 3) {
        fprintf(stderr, "usage: %s <decode-fixtures-dir> <oracle-output-dir>\n", argv[0]);
        return 1;
    }
    fixtures_dir = argv[1];
    oracle_dir = argv[2];
    ensure_init();
    if (write_pass_group_offsets(fixtures_dir, oracle_dir) != 0) {
        return 1;
    }
    if (write_transformed_layouts(fixtures_dir, oracle_dir) != 0) {
        return 1;
    }
    return 0;
}
