// SPDX-License-Identifier: MIT OR Apache-2.0
#include "allocator.h"
#include "aux_box.h"
#include "bitstream/consts.h"

#include <assert.h>
#include "test_helpers.h"
#include <stdio.h>
#include <string.h>

typedef struct {
    jxl_aux_box_list *aux;
    jxl_bs_status_t error;
} feed_ctx;

static const uint8_t k_jxlc_codestream[] = {0xff, 0x0a};

static jxl_bs_status_t feed_cb(void *ctx, const jxl_parse_event *event) {
    feed_ctx *f = ctx;
    jxl_bs_status_t st = jxl_aux_box_list_handle_event(f->aux, event);
    if (st != JXL_BS_OK) {
        f->error = st;
    }
    return st;
}

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

static void test_brob_exif_decompresses(void) {
    static const uint8_t k_exif_box[] = {0, 0, 0, 0, 'T', 'I', 'F', 'F'};
    static const uint8_t k_brotli_exif[] = {0x8b, 0x03, 0x80, 0x00, 0x00, 0x00, 0x00, 0x54,
                                            0x49, 0x46, 0x46, 0x03};
    uint8_t brob_payload[4 + sizeof(k_brotli_exif)];
    jxl_allocator_state alloc;
    size_t file_len;
    size_t file_cap;
    size_t consumed;
    jxl_bs_status_t st;
    jxl_aux_box_data exif;
    uint8_t *file;
    jxl_aux_box_list *aux;
    jxl_container_parser *parser;
    feed_ctx fctx;

    jxl_allocator_init(&alloc, NULL);

    memcpy(brob_payload, "Exif", 4);
    memcpy(brob_payload + 4, k_brotli_exif, sizeof(k_brotli_exif));

    file = NULL;
    file_len = 0;
    file_cap = 0;
    append_bytes(&file, &file_len, &file_cap, &alloc, jxl_container_sig, JXL_CONTAINER_SIG_LEN);
    append_box(&file, &file_len, &file_cap, &alloc, "brob", brob_payload, sizeof(brob_payload), 0);
    append_box(&file, &file_len, &file_cap, &alloc, "jxlc", k_jxlc_codestream,
               sizeof(k_jxlc_codestream), 1);

    aux = jxl_aux_box_list_create(&alloc);
    assert(aux != NULL);

    parser = jxl_container_parser_create(&alloc);
    assert(parser != NULL);

    fctx.aux = aux;
    fctx.error = JXL_BS_OK;

    consumed = 0;
    st = jxl_container_parser_feed(parser, file, file_len, feed_cb, &fctx, &consumed);
    assert(st == JXL_BS_OK);
    assert(fctx.error == JXL_BS_OK);
    assert(consumed == file_len);

    exif = jxl_aux_box_list_first_exif(aux);
    assert(exif.tag == JXL_AUX_BOX_HAS_DATA);
    assert(exif.data_len == sizeof(k_exif_box));
    assert(memcmp(exif.data, k_exif_box, sizeof(k_exif_box)) == 0);

    jxl_container_parser_destroy(&alloc, parser);
    jxl_aux_box_list_destroy(&alloc, aux);
    jxl_free(&alloc, file);
}

static void test_raw_exif_roundtrip(void) {
    jxl_allocator_state alloc;
    static const uint8_t k_exif_box[] = {0, 0, 0, 0, 'T', 'I', 'F', 'F'};
    size_t file_len;
    size_t file_cap;
    size_t consumed;
    jxl_aux_box_data exif;
    uint8_t *file;
    jxl_aux_box_list *aux;
    jxl_container_parser *parser;
    feed_ctx fctx;

    jxl_allocator_init(&alloc, NULL);

    file = NULL;
    file_len = 0;
    file_cap = 0;
    append_bytes(&file, &file_len, &file_cap, &alloc, jxl_container_sig, JXL_CONTAINER_SIG_LEN);
    append_box(&file, &file_len, &file_cap, &alloc, "Exif", k_exif_box, sizeof(k_exif_box), 0);
    append_box(&file, &file_len, &file_cap, &alloc, "jxlc", k_jxlc_codestream,
               sizeof(k_jxlc_codestream), 1);

    aux = jxl_aux_box_list_create(&alloc);
    parser = jxl_container_parser_create(&alloc);
    fctx.aux = aux;
    fctx.error = JXL_BS_OK;

    consumed = 0;
    JXL_TEST_ASSERT_EQ(jxl_container_parser_feed(parser, file, file_len, feed_cb, &fctx, &consumed),
                       JXL_BS_OK);

    exif = jxl_aux_box_list_first_exif(aux);
    assert(exif.tag == JXL_AUX_BOX_HAS_DATA);
    assert(exif.data_len == sizeof(k_exif_box));
    assert(memcmp(exif.data, k_exif_box, sizeof(k_exif_box)) == 0);

    jxl_container_parser_destroy(&alloc, parser);
    jxl_aux_box_list_destroy(&alloc, aux);
    jxl_free(&alloc, file);
}

int main(void) {
    test_brob_exif_decompresses();
    test_raw_exif_roundtrip();
    printf("test_aux_box: ok\n");
    return 0;
}
