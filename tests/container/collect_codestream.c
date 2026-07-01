#include "allocator.h"
// SPDX-License-Identifier: MIT OR Apache-2.0
#include "bitstream/container/parser.h"


static jxl_allocator_state *test_alloc_state(void) {
    static jxl_allocator_state alloc;
    static int init = 0;
    if (!init) { jxl_allocator_init(&alloc, NULL); init = 1; }
    return &alloc;
}
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t *data;
    size_t len;
    size_t cap;
} codestream_buf;

static jxl_bs_status_t collect_cb(void *ctx, const jxl_parse_event *event) {
    if (event->type != JXL_PARSE_EVENT_CODESTREAM) {
        return JXL_BS_OK;
    }
    codestream_buf *buf = ctx;
    size_t need = buf->len + event->data_len;
    if (need > buf->cap) {
        size_t new_cap = buf->cap == 0 ? 4096 : buf->cap;
        while (new_cap < need) {
            new_cap *= 2;
        }
        uint8_t *grown = realloc(buf->data, new_cap);
        if (grown == NULL) {
            return JXL_BS_EOF;
        }
        buf->data = grown;
        buf->cap = new_cap;
    }
    memcpy(buf->data + buf->len, event->data, event->data_len);
    buf->len += event->data_len;
    return JXL_BS_OK;
}

jxl_bs_status_t jxl_test_extract_codestream(const uint8_t *file_data, size_t file_len,
                                            uint8_t **out_data, size_t *out_len) {
    *out_data = NULL;
    *out_len = 0;

    size_t remaining;
    jxl_container_parser *parser = jxl_container_parser_create(test_alloc_state());
    if (parser == NULL) {
        return JXL_BS_EOF;
    }

    codestream_buf buf = {0};
    const uint8_t *cursor = file_data;
    remaining = file_len;

    while (remaining > 0) {
        size_t consumed = 0;
        jxl_bs_status_t st =
            jxl_container_parser_feed(parser, cursor, remaining, collect_cb, &buf, &consumed);
        if (st != JXL_BS_OK) {
            free(buf.data);
            jxl_container_parser_destroy(test_alloc_state(), parser);
            return st;
        }
        if (consumed == 0) {
            break;
        }
        cursor += consumed;
        remaining -= consumed;
    }

    jxl_container_parser_destroy(test_alloc_state(), parser);
    *out_data = buf.data;
    *out_len = buf.len;
    return JXL_BS_OK;
}
