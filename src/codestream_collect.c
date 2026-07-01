// SPDX-License-Identifier: MIT OR Apache-2.0
#include "codestream_collect.h"

#include "allocator.h"
#include "bitstream/container/parser.h"

#include <string.h>

typedef struct {
    uint8_t *data;
    size_t len;
    size_t cap;
    jxl_allocator_state *alloc;
} codestream_buf;

static jxl_bs_status_t grow_codestream(codestream_buf *buf, size_t need) {
    size_t new_cap;
    uint8_t *grown;
    if (need <= buf->cap) {
        return JXL_BS_OK;
    }
    new_cap = buf->cap == 0 ? 4096 : buf->cap;
    while (new_cap < need) {
        if (new_cap > (SIZE_MAX / 2)) {
            return JXL_BS_VALIDATION_FAILED;
        }
        new_cap *= 2;
    }
    grown = jxl_realloc(buf->alloc, buf->data, new_cap);
    if (grown == NULL) {
        return JXL_BS_EOF;
    }
    buf->data = grown;
    buf->cap = new_cap;
    return JXL_BS_OK;
}

static jxl_bs_status_t collect_cb(void *ctx, const jxl_parse_event *event) {
    codestream_buf *buf;
    size_t need;
    jxl_bs_status_t st;
    if (event->type != JXL_PARSE_EVENT_CODESTREAM) {
        return JXL_BS_OK;
    }
    buf = ctx;
    need = buf->len + event->data_len;
    st = grow_codestream(buf, need);
    if (st != JXL_BS_OK) {
        return st;
    }
    memcpy(buf->data + buf->len, event->data, event->data_len);
    buf->len += event->data_len;
    return JXL_BS_OK;
}

jxl_status_t jxl_collect_codestream(jxl_allocator_state *alloc, const uint8_t *file_data,
                                    size_t file_len, uint8_t **out_data, size_t *out_len) {
    size_t remaining;
    jxl_container_parser *parser = jxl_container_parser_create(alloc);
    codestream_buf buf = {0};
    const uint8_t *cursor = file_data;

    *out_data = NULL;
    *out_len = 0;
    if (parser == NULL) {
        return JXL_ERROR_OUT_OF_MEMORY;
    }

    buf.alloc = alloc;

    remaining = file_len;

    while (remaining > 0) {
        size_t consumed = 0;
        jxl_bs_status_t st =
            jxl_container_parser_feed(parser, cursor, remaining, collect_cb, &buf, &consumed);
        if (st != JXL_BS_OK) {
            jxl_free(alloc, buf.data);
            jxl_container_parser_destroy(alloc, parser);
            return JXL_ERROR_INVALID_INPUT;
        }
        if (consumed == 0) {
            break;
        }
        cursor += consumed;
        remaining -= consumed;
    }

    jxl_container_parser_destroy(alloc, parser);
    *out_data = buf.data;
    *out_len = buf.len;
    return JXL_OK;
}
