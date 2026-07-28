// SPDX-License-Identifier: MIT OR Apache-2.0
#include "jxl/decode.h"
#include "jxl/simple_lossless.h"

#include "allocator.h"
#include "test_helpers.h"

#include <stdio.h>
#include <string.h>

static void fill_gray8(uint8_t *pixels, uint32_t width, uint32_t height) {
    uint32_t y, x;
    for (y = 0; y < height; ++y) {
        for (x = 0; x < width; ++x) {
            pixels[y * width + x] = (uint8_t)((x * 17u + y * 31u) & 0xFFu);
        }
    }
}

static int expect_gray_roundtrip(void) {
    const uint32_t width = 16;
    const uint32_t height = 16;
    uint8_t pixels[16 * 16];
    uint8_t *jxl = NULL;
    size_t jxl_len = 0;
    jxl_context *ctx = NULL;
    jxl_decoder *dec = NULL;
    jxl_render *render = NULL;
    jxl_simple_lossless_image_desc desc;
    jxl_status_t status;
    const jxl_image_header *hdr;
    const int16_t *plane;
    uint32_t pw, ph;
    uint32_t y, x;

    fill_gray8(pixels, width, height);
    JXL_TEST_ASSERT_EQ(jxl_context_create(NULL, &ctx), JXL_OK);

    memset(&desc, 0, sizeof(desc));
    desc.width = width;
    desc.height = height;
    desc.num_channels = 1;
    desc.bits_per_sample = 8;
    desc.big_endian = 0;
    desc.effort = 2;
    status = jxl_simple_lossless_encode(ctx, &desc, pixels, width, &jxl, &jxl_len);
    if (status != JXL_OK) {
        fprintf(stderr, "encode failed: %s\n", jxl_status_string(status));
        jxl_context_destroy(ctx);
        return 1;
    }
    if (jxl_len < 2 || jxl[0] != 0xFF || jxl[1] != 0x0A) {
        fprintf(stderr, "bad JXL signature\n");
        jxl_free(ctx, jxl);
        jxl_context_destroy(ctx);
        return 1;
    }

    JXL_TEST_ASSERT_EQ(jxl_decoder_create(ctx, NULL, &dec), JXL_OK);
    JXL_TEST_ASSERT_EQ(jxl_decoder_feed(dec, jxl, jxl_len), JXL_OK);
    JXL_TEST_ASSERT_EQ(jxl_decoder_try_init(dec), JXL_OK);
    hdr = jxl_decoder_header(dec);
    if (hdr == NULL || hdr->width != width || hdr->height != height) {
        fprintf(stderr, "header size mismatch\n");
        jxl_decoder_destroy(ctx, dec);
        jxl_free(ctx, jxl);
        jxl_context_destroy(ctx);
        return 1;
    }
    JXL_TEST_ASSERT_EQ(jxl_decoder_render(ctx, dec, &render), JXL_OK);
    plane = jxl_render_plane_i16(render, 0, &pw, &ph);
    if (plane == NULL) {
        fprintf(stderr, "expected i16 plane\n");
        jxl_render_destroy(ctx, render);
        jxl_decoder_destroy(ctx, dec);
        jxl_free(ctx, jxl);
        jxl_context_destroy(ctx);
        return 1;
    }
    for (y = 0; y < height; ++y) {
        for (x = 0; x < width; ++x) {
            int expected = (int)pixels[y * width + x];
            int actual = (int)plane[y * pw + x];
            if (actual != expected) {
                fprintf(stderr, "pixel mismatch at %u,%u: got %d expected %d\n", x, y, actual,
                        expected);
                jxl_render_destroy(ctx, render);
                jxl_decoder_destroy(ctx, dec);
                jxl_free(ctx, jxl);
                jxl_context_destroy(ctx);
                return 1;
            }
        }
    }

    jxl_render_destroy(ctx, render);
    jxl_decoder_destroy(ctx, dec);
    jxl_free(ctx, jxl);
    jxl_context_destroy(ctx);
    return 0;
}

int main(void) {
    if (expect_gray_roundtrip() != 0) {
        fprintf(stderr, "test_simple_lossless_roundtrip failed\n");
        return 1;
    }
    printf("test_simple_lossless_roundtrip: ok\n");
    return 0;
}
