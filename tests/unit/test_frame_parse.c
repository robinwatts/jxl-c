// SPDX-License-Identifier: MIT OR Apache-2.0
#include "test_paths.h"

#include "allocator.h"
#include "bitstream/bitstream.h"
#include "codestream_collect.h"
#include "frame/frame.h"
#include "frame/frame_header.h"
#include "frame/toc.h"
#include "image/image_internal.h"

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>


#ifndef JXL_FRAME_FIELDS_INC
#define JXL_FRAME_FIELDS_INC "frame_fields.inc"
#endif

#include JXL_FRAME_FIELDS_INC

static int read_fixture(const char *name, uint8_t **file_out, size_t *file_len_out) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s/input.jxl", JXL_OXIDE_FIXTURES_DIR, name);
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
    *file_out = buf;
    *file_len_out = (size_t)sz;
    return 0;
}

static void verify_fixture(const jxl_frame_field_expect *expect) {
    size_t g;
    size_t file_len;
    jxl_allocator_state alloc;
    size_t cs_len;
    jxl_bs bs;
    jxl_parsed_image_header image;
    jxl_frame_header header;
    jxl_toc toc;
    jxl_bs bs_full;
    jxl_frame frame;
    size_t consumed;
    if (!expect->verify_c) {
        return;
    }

    uint8_t *file = NULL;
    file_len = 0;
    if (read_fixture(expect->name, &file, &file_len) != 0) {
        fprintf(stderr, "%s: fixture missing\n", expect->name);
        assert(0);
    }

    jxl_allocator_init(&alloc, NULL);
    uint8_t *cs = NULL;
    cs_len = 0;
    if (jxl_collect_codestream(&alloc, file, file_len, &cs, &cs_len) != JXL_OK) {
        free(file);
        assert(0);
    }
    free(file);

    jxl_bs_init(&bs, cs, cs_len);
    if (jxl_image_header_parse(&bs, &image) != JXL_BS_OK) {
        jxl_free(&alloc, cs);
        assert(0);
    }
    if (jxl_image_skip_post_header(&alloc, &bs, &image) != JXL_BS_OK) {
        jxl_free(&alloc, cs);
        assert(0);
    }

    jxl_frame_header_init(&header);
    if (jxl_frame_header_parse(&alloc, &bs, &image, &header) != JXL_FRAME_OK) {
        fprintf(stderr, "%s: frame header parse failed at bit %u\n", expect->name,
                (unsigned)bs.num_read_bits);
        assert(0);
    }
    if (bs.num_read_bits / 8 != expect->header_end) {
        fprintf(stderr, "%s: header_end %zu expected %u\n", expect->name, bs.num_read_bits / 8,
                expect->header_end);
        assert(0);
    }
    if (header.width != expect->width || header.height != expect->height) {
        fprintf(stderr, "%s: size %ux%u expected %ux%u\n", expect->name, header.width,
                header.height, expect->width, expect->height);
        assert(0);
    }
    if ((uint32_t)header.encoding != expect->encoding) {
        fprintf(stderr, "%s: encoding %d expected %u\n", expect->name, (int)header.encoding,
                (unsigned)expect->encoding);
        assert(0);
    }

    jxl_toc_init(&toc);
    if (jxl_toc_parse(&alloc, &bs, &header, &toc) != JXL_FRAME_OK) {
        fprintf(stderr, "%s: toc parse failed at bit %u\n", expect->name, (unsigned)bs.num_read_bits);
        assert(0);
    }
    if (bs.num_read_bits / 8 != expect->meta_end) {
        fprintf(stderr, "%s: meta_end %zu expected %u\n", expect->name, bs.num_read_bits / 8,
                expect->meta_end);
        assert(0);
    }
    if (toc.total_size != expect->toc_total) {
        fprintf(stderr, "%s: toc_total %zu expected %u\n", expect->name, toc.total_size,
                expect->toc_total);
        assert(0);
    }
    if (toc.groups_len != expect->groups) {
        fprintf(stderr, "%s: groups %zu expected %u\n", expect->name, toc.groups_len,
                expect->groups);
        assert(0);
    }
    if (jxl_toc_is_single_entry(&toc) != expect->single_entry) {
        fprintf(stderr, "%s: single_entry mismatch\n", expect->name);
        assert(0);
    }
    jxl_toc_free(&alloc, &toc);
    jxl_frame_header_free(&alloc, &header);

    jxl_bs_init(&bs_full, cs, cs_len);
    if (jxl_image_header_parse(&bs_full, &image) != JXL_BS_OK) {
        jxl_free(&alloc, cs);
        assert(0);
    }
    if (jxl_image_skip_post_header(&alloc, &bs_full, &image) != JXL_BS_OK) {
        jxl_free(&alloc, cs);
        assert(0);
    }

    jxl_frame_init(&frame);
    if (jxl_frame_parse(&alloc, &bs_full, &image, &frame) != JXL_FRAME_OK) {
        fprintf(stderr, "%s: jxl_frame_parse failed\n", expect->name);
        assert(0);
    }
    if (frame.header.width != expect->width || frame.header.height != expect->height) {
        assert(0);
    }
    if (frame.toc.total_size != expect->toc_total || frame.toc.groups_len != expect->groups) {
        assert(0);
    }

    const uint8_t *group_data = cs + expect->meta_end;
    size_t group_len = expect->toc_total;
    if (expect->meta_end + group_len > cs_len) {
        fprintf(stderr, "%s: meta_end+toc_total overflows codestream\n", expect->name);
        assert(0);
    }

    consumed = 0;
    const uint8_t *left = jxl_frame_feed_bytes(&frame, group_data, group_len, &consumed);
    if (consumed != group_len || left != group_data + group_len) {
        fprintf(stderr, "%s: feed_bytes %zu/%zu\n", expect->name, consumed, group_len);
        assert(0);
    }
    if (!jxl_frame_is_loading_done(&frame)) {
        assert(0);
    }
    for (g = 0; g < frame.data_len; ++g) {
        if (frame.data[g].bytes_len != frame.data[g].toc_group.size) {
            fprintf(stderr, "%s: group %zu size mismatch\n", expect->name, g);
            assert(0);
        }
    }

    jxl_frame_free(&alloc, &frame);
    jxl_free(&alloc, cs);
}

static void test_all_verified_fixtures(void) {
    size_t i;
    size_t verified = 0;
    for (i = 0; i < k_frame_field_count; ++i) {
        if (k_frame_fields[i].verify_c) {
            verified++;
            verify_fixture(&k_frame_fields[i]);
        }
    }
    if (verified == 0) {
        fprintf(stderr, "no verify_c fixtures\n");
        assert(0);
    }
}

int main(void) {
    test_all_verified_fixtures();
    return 0;
}
