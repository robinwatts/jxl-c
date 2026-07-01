// SPDX-License-Identifier: MIT OR Apache-2.0
#include "test_paths.h"

#include "allocator.h"
#include "bitstream/bitstream.h"
#include "codestream_collect.h"
#include "frame/frame.h"
#include "frame/frame_header.h"
#include "frame/lf_global_modular.h"
#include "frame/pass_group.h"
#include "frame/toc.h"
#include "image/image_internal.h"
#include "modular/group_decode.h"
#include "modular/group_subimage.h"
#include "modular/modular.h"
#include "modular/prepare_subimage.h"
#include "modular/subimage_decode.h"
#include "render/render_util.h"
#include "vardct/lf.h"
#include "test_helpers.h"
#include "jxl_oxide/jxl_context.h"

static jxl_allocator_state *test_alloc(void) {
    static jxl_allocator_state alloc;
    static int init;
    if (!init) { jxl_allocator_init(&alloc, NULL); init = 1; }
    return &alloc;
}

static jxl_context *test_library_ctx(void) {
    static jxl_context *ctx = NULL;
    if (ctx == NULL) {
        if (jxl_context_create(NULL, &ctx) != JXL_OK) {
            assert(0);
        }
    }
    return ctx;
}

#include <assert.h>
#include "jxl_oxide/jxl_types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>



static const unsigned k_grayalpha_pass_group_bit_offset = 621u;

static int read_fixture(const char *path, uint8_t **out, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }
    long sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        return -1;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }
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
    *out_len = (size_t)sz;
    return 0;
}

static int read_line(FILE *f, char *buf, size_t cap) {
    if (fgets(buf, (int)cap, f) == NULL) {
        return 0;
    }
    size_t n = strlen(buf);
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) {
        buf[--n] = '\0';
    }
    return 1;
}

static int fixture_seen(char seen[][128], size_t seen_len, const char *fixture) {
    size_t i;
    for (i = 0; i < seen_len; ++i) {
        if (strcmp(seen[i], fixture) == 0) {
            return 1;
        }
    }
    return 0;
}

static int load_layout_oracle(const char *fixture, uint32_t *nb_meta_out,
                              jxl_modular_channel_info *channels_out, size_t channels_cap,
                              size_t *channels_len_out) {
    char path[512];
    char line[512];
    size_t line_no;
    snprintf(path, sizeof(path), "%s/modular_transformed_layouts.txt", JXL_OXIDE_FIXTURES_DIR);
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return 0;
    }
    line_no = 0;
    while (read_line(f, line, sizeof(line))) {
        size_t i;
        char name[128];
        size_t n;
        int used;
        int ok;
        line_no += 1;
        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }
        unsigned nb_meta = 0;
        n = 0;
        used = 0;
        if (sscanf(line, "%127s %u %zu %n", name, &nb_meta, &n, &used) < 3) {
            fprintf(stderr, "bad layout oracle row at %s:%zu: %s\n", path, line_no, line);
            fclose(f);
            return 0;
        }
        if (strcmp(name, fixture) != 0 || n > channels_cap) {
            continue;
        }
        if (n == 0) {
            *nb_meta_out = nb_meta;
            *channels_len_out = 0;
            fclose(f);
            return 1;
        }
        char *p = line + used;
        ok = 1;
        for (i = 0; i < n; ++i) {
            int consumed;
            jxl_modular_channel_info compound_tmp;
            unsigned w = 0, h = 0;
            int hs = 0, vs = 0;
            consumed = 0;
            if (sscanf(p, "%u %u %d %d %n", &w, &h, &hs, &vs, &consumed) != 4) {
                fprintf(stderr, "bad layout tuple at %s:%zu fixture=%s\n", path, line_no, fixture);
                ok = 0;
                break;
            }
            compound_tmp.width = w;
            compound_tmp.height = h;
            compound_tmp.hshift = hs;
            compound_tmp.vshift = vs;
            channels_out[i] = compound_tmp;

            p += consumed;
        }
        if (ok) {
            *nb_meta_out = nb_meta;
            *channels_len_out = n;
            fclose(f);
            return 1;
        }
    }
    fclose(f);
    return 0;
}

static void validate_layout_oracle_file(void) {
    char path[512];
    char line[512];
    char seen[64][128];
    size_t seen_len;
    size_t line_no;
    snprintf(path, sizeof(path), "%s/modular_transformed_layouts.txt", JXL_OXIDE_FIXTURES_DIR);
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "cannot open layout oracle file: %s\n", path);
        assert(0);
    }

    seen_len = 0;
    line_no = 0;
    while (read_line(f, line, sizeof(line))) {
        size_t i;
        char name[128];
        size_t n;
        int used;
        line_no += 1;
        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }

        unsigned nb_meta = 0;
        n = 0;
        used = 0;
        if (sscanf(line, "%127s %u %zu %n", name, &nb_meta, &n, &used) < 3) {
            fprintf(stderr, "bad layout oracle row at %s:%zu: %s\n", path, line_no, line);
            assert(0);
        }
        (void)nb_meta;

        if (fixture_seen(seen, seen_len, name)) {
            fprintf(stderr, "duplicate layout oracle fixture: %s\n", name);
            assert(0);
        }
        if (seen_len >= sizeof(seen) / sizeof(seen[0])) {
            fprintf(stderr, "too many fixtures in layout oracle\n");
            assert(0);
        }
        strcpy(seen[seen_len++], name);

        char *p = line + used;
        for (i = 0; i < n; ++i) {
            int consumed;
            unsigned w = 0, h = 0;
            int hs = 0, vs = 0;
            consumed = 0;
            if (sscanf(p, "%u %u %d %d %n", &w, &h, &hs, &vs, &consumed) != 4) {
                fprintf(stderr, "bad layout tuple at %s:%zu fixture=%s\n", path, line_no, name);
                assert(0);
            }
            p += consumed;
        }
    }

    fclose(f);
}

static jxl_frame *load_modular_frame(const char *fixture, jxl_allocator_state *alloc,
                                     jxl_parsed_image_header *image_out, uint8_t **cs_out,
                                     size_t *cs_len_out) {
    char path[512];
    size_t file_len;
    size_t cs_len;
    jxl_bs bs;
    jxl_parsed_image_header image;
    size_t consumed;
    snprintf(path, sizeof(path), "%s/%s/input.jxl", JXL_OXIDE_FIXTURES_DIR, fixture);

    uint8_t *file = NULL;
    file_len = 0;
    if (read_fixture(path, &file, &file_len) != 0) {
        return NULL;
    }

    uint8_t *cs = NULL;
    cs_len = 0;
    if (jxl_collect_codestream(alloc, file, file_len, &cs, &cs_len) != JXL_OK) {
        free(file);
        return NULL;
    }
    free(file);

    jxl_bs_init(&bs, cs, cs_len);
    memset(&image, 0, sizeof(image));
    if (jxl_image_header_parse(&bs, &image) != JXL_BS_OK) {
        jxl_free(alloc, cs);
        return NULL;
    }

    jxl_frame *frame = malloc(sizeof(*frame));
    if (frame == NULL) {
        jxl_free(alloc, cs);
        return NULL;
    }
    jxl_frame_init(frame);
    if (jxl_frame_parse(alloc, &bs, &image, frame) != JXL_FRAME_OK) {
        jxl_frame_free(alloc, frame);
        free(frame);
        jxl_free(alloc, cs);
        return NULL;
    }

    size_t meta_end = bs.num_read_bits / 8;
    consumed = 0;
    jxl_frame_feed_bytes(frame, cs + meta_end, frame->toc.total_size, &consumed);
    if (consumed != frame->toc.total_size || !jxl_frame_is_loading_done(frame)) {
        jxl_frame_free(alloc, frame);
        free(frame);
        jxl_free(alloc, cs);
        return NULL;
    }

    *image_out = image;
    *cs_out = cs;
    *cs_len_out = cs_len;
    return frame;
}

static int64_t grid_sample_sum(const jxl_modular_image_destination *dest, size_t max_px) {
    size_t c;
    int64_t sum = 0;
    for (c = 0; c < dest->image_channels_len; ++c) {
        size_t y;
        const jxl_modular_grid_i32 *g = &dest->image_channels[c];
        size_t w = g->width < max_px ? g->width : max_px;
        size_t h = g->height < max_px ? g->height : max_px;
        for (y = 0; y < h; ++y) {
            size_t x;
            for (x = 0; x < w; ++x) {
                sum += (int64_t)jxl_modular_grid_sample_as_i32(g, x, y);
            }
        }
    }
    return sum;
}

static size_t modular_pass_group_offset_or_zero(jxl_allocator_state *alloc, jxl_frame *frame,
                                                const jxl_parsed_image_header *image) {
    jxl_ma_config global_ma;
    int has_ma;
    jxl_bs pg_bs;
    if (frame->header.encoding != JXL_FRAME_ENCODING_MODULAR) {
        return 0;
    }
    jxl_ma_config_init(&global_ma);
    has_ma = 0;
    jxl_frame_status_t st = jxl_frame_modular_pass_group_bitstream(
        test_library_ctx(), alloc, frame, image, 0, 0, &global_ma, &has_ma, &pg_bs, 1);
    jxl_ma_config_destroy(alloc, &global_ma);
    if (st != JXL_FRAME_OK) {
        return 0;
    }
    return pg_bs.num_read_bits;
}

static jxl_frame_status_t setup_modular_dest_from_lf_global(jxl_allocator_state *alloc,
                                                            jxl_frame *frame,
                                                            const jxl_parsed_image_header *image,
                                                            jxl_ma_config *global_ma,
                                                            int *has_ma_out,
                                                            jxl_modular_image_destination *dest) {
    jxl_bs gbs;
    jxl_lf_channel_dequant dequant;
    int has_ma;
    jxl_modular_params mod_params;
    jxl_modular_parse_ctx ctx = {0};
    const jxl_frame_group_data *src = jxl_toc_is_single_entry(&frame->toc)
                                          ? (frame->data_len > 0 ? &frame->data[0] : NULL)
                                          : jxl_frame_group_by_kind(frame, JXL_TOC_KIND_LF_GLOBAL, 0);
    if (src == NULL) {
        return JXL_FRAME_BITSTREAM_ERROR;
    }

    jxl_ma_config_destroy(alloc, global_ma);
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

        if (jxl_ma_config_parse(alloc, &gbs, &ma_params, global_ma) != JXL_MODULAR_OK) {
            return JXL_FRAME_DECODER_ERROR;
        }
    } else {
        jxl_ma_config_init(global_ma);
    }

    jxl_modular_params_init(&mod_params);
    if (!jxl_modular_params_set_for_modular_frame(test_alloc(), test_library_ctx(), &mod_params,
                                                image, &frame->header)) {
        jxl_modular_params_free(test_alloc(), &mod_params);
        return JXL_FRAME_OUT_OF_MEMORY;
    }

    ctx.ctx = test_library_ctx();
    ctx.params = &mod_params;
    ctx.global_ma = has_ma ? global_ma : NULL;
    ctx.tracker = NULL;
    ctx.retain_pretransform_channels = 1;

    jxl_modular_status_t mst = jxl_modular_dest_apply_local_header(alloc, &gbs, &ctx, dest);
    if (mst != JXL_MODULAR_OK) {
        jxl_modular_params_free(test_alloc(), &mod_params);
        return JXL_FRAME_DECODER_ERROR;
    }
    if (jxl_modular_image_has_squeeze(dest)) {
        ctx.retain_pretransform_channels = 1;
    }
    mst = jxl_modular_prepare_gmodular(alloc, dest);
    if (mst != JXL_MODULAR_OK) {
        jxl_modular_params_free(test_alloc(), &mod_params);
        return JXL_FRAME_DECODER_ERROR;
    }
    int multi_group = !jxl_toc_is_single_entry(&frame->toc);
    mst = jxl_modular_gmodular_decode(test_library_ctx(), test_alloc(), &gbs, dest,
                                      multi_group ? 1 : 0);
    jxl_modular_params_free(test_alloc(), &mod_params);
    if (mst != JXL_MODULAR_OK) {
        return JXL_FRAME_DECODER_ERROR;
    }
    return JXL_FRAME_OK;
}

static void assert_layout_matches_oracle(const char *fixture,
                                         const jxl_modular_image_destination *dest) {
                                             size_t i;
    uint32_t expected_nb_meta = 0;
    jxl_modular_channel_info expected_info[32];
    size_t expected_len = 0;
    JXL_TEST_REQUIRE(load_layout_oracle(fixture, &expected_nb_meta, expected_info,
                                        sizeof(expected_info) / sizeof(expected_info[0]),
                                        &expected_len));
    assert(dest->channels.nb_meta_channels == expected_nb_meta);
    assert(dest->channels.info_len == expected_len);
    for (i = 0; i < expected_len; ++i) {
        const jxl_modular_channel_info *got = &dest->channels.info[i];
        const jxl_modular_channel_info *exp = &expected_info[i];
        assert(got->width == exp->width);
        assert(got->height == exp->height);
        assert(got->hshift == exp->hshift);
        assert(got->vshift == exp->vshift);
    }
}

static void test_grayalpha_gmodular_decode_full(void) {
    jxl_allocator_state alloc;
    jxl_parsed_image_header image;
    size_t cs_len;
    jxl_ma_config global_ma;
    int has_ma;
    jxl_modular_params params;
    jxl_modular_image_destination dest;
    jxl_allocator_init(&alloc, NULL);

    uint8_t *cs = NULL;
    cs_len = 0;
    jxl_frame *frame = load_modular_frame("grayalpha", &alloc, &image, &cs, &cs_len);
    if (frame == NULL) {
        fprintf(stderr, "grayalpha load failed\n");
        assert(0);
    }

    jxl_ma_config_init(&global_ma);
    has_ma = 0;

    jxl_modular_params_init(&params);
    JXL_TEST_REQUIRE(jxl_modular_params_set_for_modular_frame(test_alloc(), test_library_ctx(), &params, &image, &frame->header));
    assert(params.num_channels == 2u);

    jxl_modular_image_destination_init(&dest);
    JXL_TEST_ASSERT_EQ(setup_modular_dest_from_lf_global(&alloc, frame, &image, &global_ma, &has_ma, &dest),
                       JXL_FRAME_OK);
    const jxl_modular_channels *sub = jxl_modular_dest_subimage_channels(&dest);
    assert(dest.channels.info_len == 2u);
    assert(dest.header.transform_len == 2u);
    assert(sub != NULL && sub->info_len == 4u);
    assert(jxl_modular_gmodular_channel_count(&dest) == 4u);

    JXL_TEST_ASSERT_EQ(jxl_modular_gmodular_finish(test_library_ctx(), test_alloc(), &dest,
                                                   frame->header.width, frame->header.height,
                                                  image.bit_depth_bits, &params),
                       JXL_MODULAR_OK);
    jxl_modular_grid_i32 *alpha_grid = jxl_modular_dest_channel_grid(&dest, 1);
    assert(alpha_grid != NULL && alpha_grid->buf != NULL);
    assert(jxl_modular_grid_sample_as_i32(alpha_grid, 1, 0) == 8);

    jxl_modular_image_destination_free(&alloc, &dest);
    jxl_modular_params_free(test_alloc(), &params);
    jxl_ma_config_destroy(&alloc, &global_ma);
    jxl_frame_free(&alloc, frame);
    free(frame);
    jxl_free(&alloc, cs);
}

static void test_issue_311_pass_group0_decode(void) {
    jxl_allocator_state alloc;
    jxl_parsed_image_header image;
    size_t cs_len;
    jxl_ma_config global_ma;
    int has_ma;
    jxl_bs pg_bs;
    jxl_modular_params params;
    jxl_modular_image_destination dest;
    jxl_pass_group_modular_params pg = {0};
    jxl_allocator_init(&alloc, NULL);

    uint8_t *cs = NULL;
    cs_len = 0;
    jxl_frame *frame = load_modular_frame("issue_311", &alloc, &image, &cs, &cs_len);
    if (frame == NULL) {
        fprintf(stderr, "issue_311 load failed\n");
        assert(0);
    }
    assert(frame->header.encoding == JXL_FRAME_ENCODING_MODULAR);
    assert(frame->header.width == 2000 && frame->header.height == 2000);

    jxl_ma_config_init(&global_ma);
    has_ma = 0;
    if (jxl_frame_modular_pass_group_bitstream(
            test_library_ctx(), &alloc, frame, &image, 0, 0, &global_ma, &has_ma, &pg_bs, 1) != JXL_FRAME_OK) {
        fprintf(stderr, "issue_311 pass_group bitstream setup failed\n");
        assert(0);
    }
    assert(has_ma);

    jxl_modular_params_init(&params);
    JXL_TEST_REQUIRE(jxl_modular_params_set_for_modular_frame(test_alloc(), test_library_ctx(), &params, &image, &frame->header));

    jxl_modular_image_destination_init(&dest);
    JXL_TEST_ASSERT_EQ(setup_modular_dest_from_lf_global(&alloc, frame, &image, &global_ma, &has_ma, &dest),
                       JXL_FRAME_OK);

    if (jxl_modular_decode_frame_lf_groups(test_library_ctx(), &alloc, frame, &global_ma, has_ma,
                                           &dest, 0, NULL) !=
        JXL_OK) {
        fprintf(stderr, "issue_311 lf groups failed\n");
        assert(0);
    }

    pg.ctx = test_library_ctx();
    pg.alloc = &alloc;
    pg.frame_header = &frame->header;
    pg.global_ma = &global_ma;
    pg.modular_params = &params;
    pg.modular_dest = &dest;
    pg.pass_idx = 0;
    pg.group_idx = 0;
    pg.allow_partial = 1;


    if (jxl_decode_pass_group_modular_coefficients(&pg_bs, &pg) != JXL_FRAME_OK) {
        fprintf(stderr, "issue_311 pass_group decode failed\n");
        assert(0);
    }

    const jxl_modular_grid_i32 *ch0 = &dest.image_channels[0];
    assert(ch0->buf != NULL);
    assert(jxl_modular_grid_sample_as_i32(ch0, 0, 0) == 255);
    assert(jxl_modular_grid_sample_as_i32(ch0, 1, 0) == 255);

    jxl_modular_image_destination_free(&alloc, &dest);
    jxl_modular_params_free(test_alloc(), &params);
    jxl_ma_config_destroy(&alloc, &global_ma);
    jxl_frame_free(&alloc, frame);
    free(frame);
    jxl_free(&alloc, cs);
}

static void test_grayalpha_lf_global_skip_and_pass_group(void) {
    jxl_allocator_state alloc;
    jxl_parsed_image_header image;
    size_t cs_len;
    jxl_ma_config global_ma;
    int has_ma;
    jxl_bs pg_bs;
    jxl_modular_params params;
    jxl_modular_image_destination dest;
    jxl_pass_group_modular_params pg = {0};
    jxl_allocator_init(&alloc, NULL);

    uint8_t *cs = NULL;
    cs_len = 0;
    jxl_frame *frame = load_modular_frame("grayalpha", &alloc, &image, &cs, &cs_len);
    if (frame == NULL) {
        fprintf(stderr, "grayalpha load failed\n");
        assert(0);
    }
    assert(frame->header.encoding == JXL_FRAME_ENCODING_MODULAR);
    assert(frame->header.width == 32 && frame->header.height == 32);
    assert(image.colour.colour_space == JXL_COLOUR_SPACE_GRAY_I);
    assert(frame->header.encoded_color_channels == 1u);

    jxl_ma_config_init(&global_ma);
    has_ma = 0;
    if (jxl_frame_modular_pass_group_bitstream(
            test_library_ctx(), &alloc, frame, &image, 0, 0, &global_ma, &has_ma, &pg_bs, 1) != JXL_FRAME_OK) {
        fprintf(stderr, "grayalpha pass_group bitstream setup failed at bit %zu\n",
                pg_bs.num_read_bits);
        assert(0);
    }
    assert(pg_bs.num_read_bits == k_grayalpha_pass_group_bit_offset);
    assert(has_ma);

    jxl_modular_params_init(&params);
    JXL_TEST_REQUIRE(jxl_modular_params_set_for_modular_frame(test_alloc(), test_library_ctx(), &params, &image, &frame->header));

    jxl_modular_image_destination_init(&dest);
    JXL_TEST_ASSERT_EQ(setup_modular_dest_from_lf_global(&alloc, frame, &image, &global_ma, &has_ma, &dest),
                       JXL_FRAME_OK);

    pg.ctx = test_library_ctx();
    pg.alloc = &alloc;
    pg.frame_header = &frame->header;
    pg.global_ma = &global_ma;
    pg.modular_params = &params;
    pg.modular_dest = &dest;
    pg.pass_idx = 0;
    pg.group_idx = 0;
    pg.allow_partial = 0;


    assert(dest.channels.info_len > 0);
    assert(dest.image_channels_len > 0);
    jxl_frame_status_t st = jxl_decode_pass_group_modular_coefficients(&pg_bs, &pg);
    assert(st == JXL_FRAME_OK);

    jxl_modular_image_destination_free(&alloc, &dest);
    jxl_modular_params_free(test_alloc(), &params);
    jxl_ma_config_destroy(&alloc, &global_ma);
    jxl_frame_free(&alloc, frame);
    free(frame);
    jxl_free(&alloc, cs);
}

static void test_squeeze_edge_pass_group_modular(void) {
    size_t i;
    jxl_allocator_state alloc;
    jxl_parsed_image_header image;
    size_t cs_len;
    jxl_ma_config global_ma;
    int has_ma;
    jxl_modular_params params;
    jxl_modular_image_destination dest;
    jxl_bs pg_bs;
    jxl_pass_group_modular_params pg = {0};
    jxl_allocator_init(&alloc, NULL);

    uint8_t *cs = NULL;
    cs_len = 0;
    jxl_frame *frame = load_modular_frame("squeeze_edge", &alloc, &image, &cs, &cs_len);
    if (frame == NULL) {
        fprintf(stderr, "squeeze_edge load failed\n");
        assert(0);
    }
    assert(frame->header.encoding == JXL_FRAME_ENCODING_MODULAR);

    const jxl_frame_group_data *best_pg = NULL;
    for (i = 0; i < frame->data_len; ++i) {
        const jxl_frame_group_data *g = &frame->data[i];
        if (g->toc_group.kind == JXL_TOC_KIND_GROUP_PASS) {
            if (best_pg == NULL || g->bytes_len > best_pg->bytes_len) {
                best_pg = g;
            }
        }
    }
    assert(best_pg != NULL);

    jxl_ma_config_init(&global_ma);
    has_ma = 0;

    jxl_modular_params_init(&params);
    JXL_TEST_REQUIRE(jxl_modular_params_set_for_modular_frame(test_alloc(), test_library_ctx(), &params, &image, &frame->header));

    jxl_modular_image_destination_init(&dest);
    if (setup_modular_dest_from_lf_global(&alloc, frame, &image, &global_ma, &has_ma, &dest) !=
        JXL_FRAME_OK) {
        assert(0);
    }
    assert(has_ma);
    assert_layout_matches_oracle("squeeze_edge", &dest);

    jxl_bs_init(&pg_bs, best_pg->bytes, best_pg->bytes_len);

    pg.ctx = test_library_ctx();
    pg.alloc = &alloc;
    pg.frame_header = &frame->header;
    pg.global_ma = &global_ma;
    pg.modular_params = &params;
    pg.modular_dest = &dest;
    pg.pass_idx = best_pg->toc_group.pass_idx;
    pg.group_idx = best_pg->toc_group.group_idx;
    pg.allow_partial = 1;


    if (jxl_decode_pass_group_modular_coefficients(&pg_bs, &pg) != JXL_FRAME_OK) {
        fprintf(stderr, "squeeze_edge pass group decode failed at bit %u\n",
                (unsigned)pg_bs.num_read_bits);
        assert(0);
    }
    assert(dest.image_channels_len > 0);

    int64_t sum = grid_sample_sum(&dest, 8);
    if (sum == 0 && best_pg->bytes_len > 16) {
        fprintf(stderr, "squeeze_edge pass group coefficients all zero\n");
        assert(0);
    }

    jxl_modular_image_destination_free(&alloc, &dest);
    jxl_modular_params_free(test_alloc(), &params);
    jxl_ma_config_destroy(&alloc, &global_ma);
    jxl_frame_free(&alloc, frame);
    free(frame);
    jxl_free(&alloc, cs);
}

static void test_modular_pass_group_offsets_decode_fixtures(void) {
    char oracle_path[512];
    char line[256];
    char seen[64][128];
    size_t seen_len;
    snprintf(oracle_path, sizeof(oracle_path), "%s/modular_pass_group_offsets.txt",
             JXL_OXIDE_FIXTURES_DIR);
    FILE *f = fopen(oracle_path, "rb");
    if (f == NULL) {
        fprintf(stderr, "cannot open oracle file: %s\n", oracle_path);
        assert(0);
    }

    seen_len = 0;
    while (read_line(f, line, sizeof(line))) {
        char fixture[128];
        size_t expected_bits = 0;
        jxl_allocator_state alloc;
        jxl_parsed_image_header image;
        size_t cs_len;
        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }
        if (sscanf(line, "%127s %zu", fixture, &expected_bits) != 2) {
            fprintf(stderr, "bad oracle line: %s\n", line);
            assert(0);
        }
        if (fixture_seen(seen, seen_len, fixture)) {
            fprintf(stderr, "duplicate offset oracle fixture: %s\n", fixture);
            assert(0);
        }
        if (seen_len >= sizeof(seen) / sizeof(seen[0])) {
            fprintf(stderr, "too many fixtures in offset oracle\n");
            assert(0);
        }
        strcpy(seen[seen_len++], fixture);
        jxl_allocator_init(&alloc, NULL);

        uint8_t *cs = NULL;
        cs_len = 0;
        jxl_frame *frame = load_modular_frame(fixture, &alloc, &image, &cs, &cs_len);
        if (frame == NULL) {
            fprintf(stderr, "%s load failed\n", fixture);
            assert(0);
        }

        size_t got_bits = modular_pass_group_offset_or_zero(&alloc, frame, &image);
        if (got_bits != expected_bits) {
            fprintf(stderr, "%s expected bits=%zu got=%zu\n", fixture, expected_bits, got_bits);
            assert(0);
        }

        jxl_frame_free(&alloc, frame);
        free(frame);
        jxl_free(&alloc, cs);
    }
    fclose(f);
}

static jxl_frame *load_conformance_first_frame(const char *case_name, jxl_allocator_state *alloc,
                                               jxl_parsed_image_header *image_out,
                                               uint8_t **cs_out, size_t *cs_len_out) {
    char path[512];
    size_t file_len;
    size_t cs_len;
    jxl_bs bs;
    jxl_parsed_image_header image;
    size_t consumed;
    snprintf(path, sizeof(path), "%s/%s/input.jxl", JXL_OXIDE_CONFORMANCE_DIR, case_name);

    uint8_t *file = NULL;
    file_len = 0;
    if (read_fixture(path, &file, &file_len) != 0) {
        return NULL;
    }

    uint8_t *cs = NULL;
    cs_len = 0;
    if (jxl_collect_codestream(alloc, file, file_len, &cs, &cs_len) != JXL_OK) {
        free(file);
        return NULL;
    }
    free(file);

    jxl_bs_init(&bs, cs, cs_len);
    memset(&image, 0, sizeof(image));
    if (jxl_image_header_parse(&bs, &image) != JXL_BS_OK) {
        jxl_free(alloc, cs);
        return NULL;
    }
    if (jxl_image_skip_post_header(alloc, &bs, &image) != JXL_BS_OK) {
        jxl_free(alloc, cs);
        return NULL;
    }

    jxl_frame *frame = malloc(sizeof(*frame));
    if (frame == NULL) {
        jxl_free(alloc, cs);
        return NULL;
    }
    jxl_frame_init(frame);
    if (jxl_frame_parse(alloc, &bs, &image, frame) != JXL_FRAME_OK) {
        jxl_frame_free(alloc, frame);
        free(frame);
        jxl_free(alloc, cs);
        return NULL;
    }

    size_t meta_end = bs.num_read_bits / 8;
    consumed = 0;
    jxl_frame_feed_bytes(frame, cs + meta_end, frame->toc.total_size, &consumed);
    if (consumed != frame->toc.total_size || !jxl_frame_is_loading_done(frame)) {
        jxl_frame_free(alloc, frame);
        free(frame);
        jxl_free(alloc, cs);
        return NULL;
    }

    *image_out = image;
    *cs_out = cs;
    *cs_len_out = cs_len;
    return frame;
}

static void test_spot_frame0_full_modular_decode(void) {
    jxl_allocator_state alloc;
    jxl_parsed_image_header image;
    size_t cs_len;
    jxl_ma_config global_ma;
    int has_ma;
    jxl_modular_params mod_params;
    jxl_modular_image_destination dest;
    jxl_allocator_init(&alloc, NULL);

    uint8_t *cs = NULL;
    cs_len = 0;
    jxl_frame *frame = load_conformance_first_frame("spot", &alloc, &image, &cs, &cs_len);
    if (frame == NULL) {
        fprintf(stderr, "spot frame0 load failed\n");
        assert(0);
    }
    assert(frame->header.encoding == JXL_FRAME_ENCODING_MODULAR);
    assert(frame->header.width == 600 && frame->header.height == 400);
    assert(!jxl_frame_header_is_keyframe(&frame->header));

    jxl_ma_config_init(&global_ma);
    has_ma = 0;

    jxl_modular_params_init(&mod_params);
    JXL_TEST_REQUIRE(jxl_modular_params_set_for_modular_frame(test_alloc(), test_library_ctx(), &mod_params, &image, &frame->header));

    jxl_modular_image_destination_init(&dest);
    JXL_TEST_ASSERT_EQ(setup_modular_dest_from_lf_global(&alloc, frame, &image, &global_ma, &has_ma, &dest),
                       JXL_FRAME_OK);
    assert(has_ma);

    int multi_group = !jxl_toc_is_single_entry(&frame->toc);
    assert(multi_group);

    jxl_status_t pgst = jxl_modular_decode_frame_group_coefficients(
        test_library_ctx(), &alloc, frame, &image, &global_ma, has_ma, &mod_params, &dest,
        multi_group, 1, NULL);
    if (pgst != JXL_OK) {
        fprintf(stderr, "spot frame0 all pass groups failed status=%d\n", (int)pgst);
        assert(0);
    }

    jxl_modular_status_t fin = jxl_modular_gmodular_finish(test_library_ctx(), test_alloc(),
        &dest, frame->header.width, frame->header.height, image.bit_depth_bits, &mod_params);
    if (fin != JXL_MODULAR_OK) {
        fprintf(stderr, "spot frame0 inverse failed\n");
        assert(0);
    }

    int64_t sum = grid_sample_sum(&dest, 16);
    if (sum == 0) {
        fprintf(stderr, "spot frame0 coefficients all zero\n");
        assert(0);
    }

    jxl_modular_image_destination_free(&alloc, &dest);
    jxl_modular_params_free(test_alloc(), &mod_params);
    jxl_ma_config_destroy(&alloc, &global_ma);
    jxl_frame_free(&alloc, frame);
    free(frame);
    jxl_free(&alloc, cs);
}

static void test_lossless_pfm_modular_decode(void) {
    jxl_allocator_state alloc;
    jxl_parsed_image_header image;
    size_t cs_len;
    jxl_ma_config global_ma;
    int has_ma;
    jxl_modular_params mod_params;
    jxl_modular_image_destination dest;
    jxl_allocator_init(&alloc, NULL);
    uint8_t *cs = NULL;
    cs_len = 0;
    jxl_frame *frame = load_conformance_first_frame("lossless_pfm", &alloc, &image, &cs, &cs_len);
    if (frame == NULL) {
        assert(0);
    }
    jxl_ma_config_init(&global_ma);
    has_ma = 0;
    jxl_modular_params_init(&mod_params);
    JXL_TEST_REQUIRE(jxl_modular_params_set_for_modular_frame(test_alloc(), test_library_ctx(), &mod_params, &image, &frame->header));
    jxl_modular_image_destination_init(&dest);
    JXL_TEST_ASSERT_EQ(setup_modular_dest_from_lf_global(&alloc, frame, &image, &global_ma, &has_ma, &dest),
                       JXL_FRAME_OK);
    int multi_group = !jxl_toc_is_single_entry(&frame->toc);
    jxl_status_t pgst = jxl_modular_decode_frame_group_coefficients(
        test_library_ctx(), &alloc, frame, &image, &global_ma, has_ma, &mod_params, &dest,
        multi_group, 1, NULL);
    if (pgst != JXL_OK) {
        fprintf(stderr, "lossless_pfm pg decode failed %d\n", (int)pgst);
        assert(0);
    }
    jxl_modular_image_destination_free(&alloc, &dest);
    jxl_modular_params_free(test_alloc(), &mod_params);
    jxl_ma_config_destroy(&alloc, &global_ma);
    jxl_frame_free(&alloc, frame);
    free(frame);
    jxl_free(&alloc, cs);
}

int main(void) {
    validate_layout_oracle_file();
    test_grayalpha_gmodular_decode_full();
    test_issue_311_pass_group0_decode();
    test_grayalpha_lf_global_skip_and_pass_group();
    test_squeeze_edge_pass_group_modular();
    test_modular_pass_group_offsets_decode_fixtures();
    test_spot_frame0_full_modular_decode();
    test_lossless_pfm_modular_decode();
    printf("test_modular_group_e2e: ok\n");
    return 0;
}
