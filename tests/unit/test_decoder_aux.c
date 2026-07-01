// SPDX-License-Identifier: MIT OR Apache-2.0
#include "allocator.h"
#include "bitstream/consts.h"
#include "jxl_oxide/jxl_oxide.h"

#include <assert.h>
#include "test_helpers.h"
#include <stdio.h>
#include <string.h>

static const uint8_t k_jxlc_codestream[] = {0xff, 0x0a};

static void append_bytes(uint8_t **buf, size_t *len, size_t *cap, jxl_allocator_state *alloc,
                         const uint8_t *data, size_t data_len) {
    size_t need = *len + data_len;
    if (need > *cap) {
        size_t new_cap = *cap == 0 ? 256 : *cap;
        while (new_cap < need) {
            new_cap *= 2;
        }
        uint8_t *grown = jxl_realloc(alloc, *buf, new_cap);
        assert(grown != NULL);
        *buf = grown;
        *cap = new_cap;
    }
    memcpy(*buf + *len, data, data_len);
    *len = need;
}

static void append_u32_be(uint8_t **buf, size_t *len, size_t *cap, jxl_allocator_state *alloc,
                          uint32_t value) {
    uint8_t bytes[4];
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;

    append_bytes(buf, len, cap, alloc, bytes, sizeof(bytes));
}

static void append_box(uint8_t **buf, size_t *len, size_t *cap, jxl_allocator_state *alloc,
                       const char *type, const uint8_t *payload, size_t payload_len, int last_box) {
    if (last_box) {
        append_u32_be(buf, len, cap, alloc, 0);
    } else {
        append_u32_be(buf, len, cap, alloc, (uint32_t)(8 + payload_len));
    }
    append_bytes(buf, len, cap, alloc, (const uint8_t *)type, 4);
    append_bytes(buf, len, cap, alloc, payload, payload_len);
}

static void test_decoder_first_exif_brob(void) {
    static const uint8_t k_exif_box[] = {0, 0, 0, 0, 'T', 'I', 'F', 'F'};
    static const uint8_t k_brotli_exif[] = {0x8b, 0x03, 0x80, 0x00, 0x00, 0x00, 0x00, 0x54,
                                            0x49, 0x46, 0x46, 0x03};
    uint8_t brob_payload[4 + sizeof(k_brotli_exif)];
    jxl_allocator_state alloc;
    size_t file_len;
    size_t file_cap;
    jxl_exif_metadata exif;
    jxl_context *ctx;
    jxl_decoder *dec;
    uint8_t *file;

    ctx = NULL;
    JXL_TEST_ASSERT_EQ(jxl_context_create(NULL, &ctx), JXL_OK);

    dec = NULL;
    JXL_TEST_ASSERT_EQ(jxl_decoder_create(ctx, NULL, &dec), JXL_OK);

    memcpy(brob_payload, "Exif", 4);
    memcpy(brob_payload + 4, k_brotli_exif, sizeof(k_brotli_exif));

    jxl_allocator_init(&alloc, NULL);

    file = NULL;
    file_len = 0;
    file_cap = 0;
    append_bytes(&file, &file_len, &file_cap, &alloc, jxl_container_sig, JXL_CONTAINER_SIG_LEN);
    append_box(&file, &file_len, &file_cap, &alloc, "brob", brob_payload, sizeof(brob_payload), 0);
    append_box(&file, &file_len, &file_cap, &alloc, "jxlc", k_jxlc_codestream,
               sizeof(k_jxlc_codestream), 1);

    JXL_TEST_ASSERT_EQ(jxl_decoder_feed(dec, file, file_len), JXL_OK);

    JXL_TEST_ASSERT_EQ(jxl_decoder_first_exif(dec, &exif), JXL_OK);
    assert(exif.status == JXL_EXIF_AVAILABLE);
    assert(exif.tiff_header_offset == 0);
    assert(exif.payload_len == 4);
    assert(memcmp(exif.payload, "TIFF", 4) == 0);

    jxl_decoder_destroy(ctx, dec);
    jxl_context_destroy(ctx);
    jxl_free(&alloc, file);
}

static void test_decoder_first_exif_raw_box(void) {
    static const uint8_t k_exif_box[] = {0, 0, 0, 0, 'T', 'I', 'F', 'F'};
    jxl_allocator_state alloc;
    size_t file_len;
    size_t file_cap;
    jxl_exif_metadata exif;
    jxl_context *ctx;
    jxl_decoder *dec;
    uint8_t *file;

    ctx = NULL;
    JXL_TEST_ASSERT_EQ(jxl_context_create(NULL, &ctx), JXL_OK);

    dec = NULL;
    JXL_TEST_ASSERT_EQ(jxl_decoder_create(ctx, NULL, &dec), JXL_OK);

    jxl_allocator_init(&alloc, NULL);

    file = NULL;
    file_len = 0;
    file_cap = 0;
    append_bytes(&file, &file_len, &file_cap, &alloc, jxl_container_sig, JXL_CONTAINER_SIG_LEN);
    append_box(&file, &file_len, &file_cap, &alloc, "Exif", k_exif_box, sizeof(k_exif_box), 0);
    append_box(&file, &file_len, &file_cap, &alloc, "jxlc", k_jxlc_codestream,
               sizeof(k_jxlc_codestream), 1);

    JXL_TEST_ASSERT_EQ(jxl_decoder_feed(dec, file, file_len), JXL_OK);

    JXL_TEST_ASSERT_EQ(jxl_decoder_first_exif(dec, &exif), JXL_OK);
    assert(exif.status == JXL_EXIF_AVAILABLE);
    assert(exif.tiff_header_offset == 0);
    assert(exif.payload_len == 4);
    assert(memcmp(exif.payload, "TIFF", 4) == 0);

    jxl_decoder_destroy(ctx, dec);
    jxl_context_destroy(ctx);
    jxl_free(&alloc, file);
}

static void test_decoder_first_exif_not_found(void) {
    jxl_exif_metadata exif;
    jxl_context *ctx = NULL;
    JXL_TEST_ASSERT_EQ(jxl_context_create(NULL, &ctx), JXL_OK);

    jxl_decoder *dec = NULL;
    JXL_TEST_ASSERT_EQ(jxl_decoder_create(ctx, NULL, &dec), JXL_OK);

    static const uint8_t bare[] = {0xff, 0x0a};

    JXL_TEST_ASSERT_EQ(jxl_decoder_feed(dec, bare, sizeof(bare)), JXL_OK);

    JXL_TEST_ASSERT_EQ(jxl_decoder_first_exif(dec, &exif), JXL_OK);
    assert(exif.status == JXL_EXIF_NOT_FOUND);

    jxl_decoder_destroy(ctx, dec);
    jxl_context_destroy(ctx);
}

int main(void) {
    test_decoder_first_exif_brob();
    test_decoder_first_exif_raw_box();
    test_decoder_first_exif_not_found();
    printf("test_decoder_aux: ok\n");
    return 0;
}
