// SPDX-License-Identifier: MIT OR Apache-2.0
#include "test_paths.h"

#include "allocator.h"
#include "bitstream/bitstream.h"
#include "codestream_collect.h"
#include "frame/frame.h"
#include "frame/lf_global.h"
#include "image/image_internal.h"
#include "modular/subimage_decode.h"
#include "test_helpers.h"
#include "jxl_oxide/jxl_context.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static int read_fixture(const char *path, uint8_t **out, size_t *out_len) {
    long sz;
    FILE *f = fopen(path, "rb");
    uint8_t *buf;
    if (f == NULL) {
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }
    sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        return -1;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }
    buf = malloc((size_t)sz);
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

int main(void) {
    char path[1024];
    size_t file_len;
    size_t cs_len;
    jxl_allocator_state alloc;
    jxl_parsed_image_header parsed;
    jxl_bs bs;
    jxl_frame frame;
    jxl_lf_global lf;
    jxl_lf_global_params lp = {0};
    jxl_bs gbs;
    int n;
    uint8_t *file = NULL;
    uint8_t *cs = NULL;
    file_len = 0;
    cs_len = 0;
    const jxl_frame_group_data *src;

    jxl_allocator_init(&alloc, NULL);
    jxl_context *library_ctx = NULL;
    if (jxl_context_create(NULL, &library_ctx) != JXL_OK) {
        fprintf(stderr, "jxl_context_create failed\n");
        return 1;
    }
    memset(&parsed, 0, sizeof(parsed));
    jxl_frame_init(&frame);
    jxl_lf_global_init(&lf);

    n = snprintf(path, sizeof(path), "%s/benchmark-data/srgb.d0-e1.jxl", JXL_OXIDE_FIXTURES_DIR);
    if (n < 0 || (size_t)n >= sizeof(path)) {
        fprintf(stderr, "path too long\n");
        return 1;
    }
    if (read_fixture(path, &file, &file_len) != 0) {
        fprintf(stderr, "read failed\n");
        return 1;
    }
    if (jxl_collect_codestream(&alloc, file, file_len, &cs, &cs_len) != JXL_OK) {
        fprintf(stderr, "collect codestream failed\n");
        free(file);
        return 1;
    }
    free(file);

    jxl_bs_init(&bs, cs, cs_len);
    if (jxl_image_header_parse(&bs, &parsed) != JXL_BS_OK ||
        jxl_image_decode_post_header(&alloc, &bs, &parsed) != JXL_BS_OK) {
        fprintf(stderr, "image header failed\n");
        jxl_free(&alloc, cs);
        return 1;
    }
    if (jxl_frame_parse(&alloc, &bs, &parsed, &frame) != JXL_FRAME_OK) {
        fprintf(stderr, "frame parse failed\n");
        jxl_free(&alloc, cs);
        return 1;
    }
    {
        size_t meta_end = bs.num_read_bits / 8;
        size_t consumed = 0;
        jxl_frame_feed_bytes(&frame, cs + meta_end, frame.toc.total_size, &consumed);
        if (consumed != frame.toc.total_size || !jxl_frame_is_loading_done(&frame)) {
            fprintf(stderr, "frame feed failed\n");
            jxl_free(&alloc, cs);
            return 1;
        }
    }

    printf("frame encoding=%d size=%ux%u toc_groups=%zu single=%d\n",
           frame.header.encoding, frame.header.width, frame.header.height, frame.toc.groups_len,
           jxl_toc_is_single_entry(&frame.toc) ? 1 : 0);

    if (jxl_toc_is_single_entry(&frame.toc)) {
        src = frame.data_len > 0 ? &frame.data[0] : NULL;
    } else {
        src = jxl_frame_group_by_kind(&frame, JXL_TOC_KIND_LF_GLOBAL, 0);
    }
    if (src == NULL) {
        fprintf(stderr, "missing lf-global group\n");
        jxl_free(&alloc, cs);
        return 1;
    }

    jxl_bs_init(&gbs, src->bytes, src->bytes_len);
    lp.ctx = library_ctx;
    lp.image = &parsed;
    lp.frame = &frame.header;
    lp.tracker = NULL;
    lp.allow_partial = 0;

    if (jxl_lf_global_consume(&alloc, &gbs, &lp, &lf) != JXL_FRAME_OK) {
        fprintf(stderr, "lf_global_consume failed at bits=%zu/%zu\n", gbs.num_read_bits,
                src->bytes_len * 8);
        jxl_lf_global_free(&alloc, &lf);
        jxl_frame_free(&alloc, &frame);
        jxl_parsed_image_header_free_embedded_icc(&alloc, &parsed);
        jxl_free(&alloc, cs);
        return 1;
    }

    printf("lf_global_consume ok bits=%zu/%zu gmodular_channels=%zu\n", gbs.num_read_bits,
           src->bytes_len * 8, lf.gmodular.image_channels_len);

    jxl_lf_global_free(&alloc, &lf);
    jxl_frame_free(&alloc, &frame);
    jxl_parsed_image_header_free_embedded_icc(&alloc, &parsed);
    jxl_free(&alloc, cs);
    jxl_context_destroy(library_ctx);
    printf("test_srgb_benchmark: ok\n");
    return 0;
}
