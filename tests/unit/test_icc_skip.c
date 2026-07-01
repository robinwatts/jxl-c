// SPDX-License-Identifier: MIT OR Apache-2.0
#include "test_paths.h"

#include "codestream_collect.h"
#include "image/image_internal.h"

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>


static int load_file(const char *path, uint8_t **out, size_t *out_len) {
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

static void test_fixture(const char *name) {
    char path[512];
    size_t file_len;
    jxl_allocator_state alloc;
    size_t cs_len;
    jxl_bs bs;
    jxl_parsed_image_header parsed;
    snprintf(path, sizeof(path), "%s/%s/input.jxl", JXL_OXIDE_CONFORMANCE_DIR, name);

    uint8_t *file = NULL;
    file_len = 0;
    if (load_file(path, &file, &file_len) != 0) {
        fprintf(stderr, "%s: missing input\n", name);
        assert(0);
    }

    jxl_allocator_init(&alloc, NULL);
    uint8_t *cs = NULL;
    cs_len = 0;
    if (jxl_collect_codestream(&alloc, file, file_len, &cs, &cs_len) != JXL_OK) {
        assert(0);
    }
    free(file);

    jxl_bs_init(&bs, cs, cs_len);
    memset(&parsed, 0, sizeof(parsed));
    if (jxl_image_header_parse(&bs, &parsed) != JXL_BS_OK) {
        assert(0);
    }
    if (!parsed.colour.have_icc_profile) {
        fprintf(stderr, "%s: expected embedded ICC\n", name);
        assert(0);
    }

    jxl_bs_status_t st = jxl_image_skip_post_header(&alloc, &bs, &parsed);
    if (st != JXL_BS_OK) {
        fprintf(stderr, "%s: skip_post_header failed st=%d bits=%zu\n", name, (int)st,
                bs.num_read_bits);
        assert(0);
    }
    if (jxl_bs_zero_pad_to_byte(&bs) != JXL_BS_OK) {
        assert(0);
    }

    printf("%s: icc skip ok bits=%zu\n", name, bs.num_read_bits);
    jxl_free(&alloc, cs);
}

static void test_ec_bit_depth(const char *name) {
    char path[512];
    size_t file_len;
    jxl_allocator_state alloc;
    size_t cs_len;
    jxl_bs bs;
    jxl_parsed_image_header parsed;
    snprintf(path, sizeof(path), "%s/%s/input.jxl", JXL_OXIDE_CONFORMANCE_DIR, name);

    uint8_t *file = NULL;
    file_len = 0;
    if (load_file(path, &file, &file_len) != 0) {
        assert(0);
    }

    jxl_allocator_init(&alloc, NULL);
    uint8_t *cs = NULL;
    cs_len = 0;
    if (jxl_collect_codestream(&alloc, file, file_len, &cs, &cs_len) != JXL_OK) {
        assert(0);
    }
    free(file);

    jxl_bs_init(&bs, cs, cs_len);
    memset(&parsed, 0, sizeof(parsed));
    if (jxl_image_header_parse(&bs, &parsed) != JXL_BS_OK) {
        assert(0);
    }
    if (jxl_image_decode_post_header(&alloc, &bs, &parsed) != JXL_BS_OK) {
        assert(0);
    }
    printf("%s: image_bd=%u ec_count=%u ec0_bd=%u icc=%zu\n", name, parsed.bit_depth_bits,
           parsed.ec_bit_depth_count,
           parsed.ec_bit_depth_count > 0 ? parsed.ec_bit_depth[0] : 0, parsed.embedded_icc_len);
    jxl_parsed_image_header_free_embedded_icc(&alloc, &parsed);
    jxl_free(&alloc, cs);
}

int main(void) {
    test_fixture("cafe_5");
    test_fixture("spot");
    test_ec_bit_depth("alpha_premultiplied");
    test_ec_bit_depth("grayscale_jpeg");
    return 0;
}
