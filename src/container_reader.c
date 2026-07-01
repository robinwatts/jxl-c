// SPDX-License-Identifier: MIT OR Apache-2.0
#include "container_reader.h"

#include <string.h>

typedef struct {
    uint8_t *data;
    size_t len;
    size_t cap;
    jxl_allocator_state *alloc;
} codestream_buf;

struct jxl_container_reader {
    jxl_allocator_state *alloc;
    jxl_container_parser *parser;
    jxl_aux_box_list *aux_boxes;
    codestream_buf codestream;
};

typedef struct {
    codestream_buf *codestream;
    jxl_aux_box_list *aux_boxes;
    jxl_bs_status_t error;
} reader_feed_ctx;

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

static jxl_bs_status_t append_codestream(codestream_buf *buf, const uint8_t *data, size_t len) {
    size_t need = buf->len + len;
    jxl_bs_status_t st = grow_codestream(buf, need);
    if (st != JXL_BS_OK) {
        return st;
    }
    memcpy(buf->data + buf->len, data, len);
    buf->len = need;
    return JXL_BS_OK;
}

static jxl_bs_status_t reader_event_cb(void *ctx, const jxl_parse_event *event) {
    reader_feed_ctx *f = ctx;
    jxl_bs_status_t st;
    if (event->type == JXL_PARSE_EVENT_CODESTREAM) {
        st = append_codestream(f->codestream, event->data, event->data_len);
        if (st != JXL_BS_OK) {
            f->error = st;
        }
        return st;
    }
    st = jxl_aux_box_list_handle_event(f->aux_boxes, event);
    if (st != JXL_BS_OK) {
        f->error = st;
    }
    return st;
}

jxl_container_reader *jxl_container_reader_create(jxl_allocator_state *alloc) {
    jxl_container_reader *reader = jxl_calloc(alloc, 1, sizeof(*reader));
    if (reader == NULL) {
        return NULL;
    }
    reader->alloc = alloc;
    reader->parser = jxl_container_parser_create(alloc);
    if (reader->parser == NULL) {
        jxl_free(alloc, reader);
        return NULL;
    }
    reader->aux_boxes = jxl_aux_box_list_create(alloc);
    if (reader->aux_boxes == NULL) {
        jxl_container_parser_destroy(alloc, reader->parser);
        jxl_free(alloc, reader);
        return NULL;
    }
    reader->codestream.alloc = alloc;
    return reader;
}

void jxl_container_reader_destroy(jxl_allocator_state *alloc, jxl_container_reader *reader) {
    jxl_allocator_state *a;
    if (reader == NULL) {
        return;
    }
    a = alloc != NULL ? alloc : reader->alloc;
    jxl_container_parser_destroy(a, reader->parser);
    jxl_aux_box_list_destroy(a, reader->aux_boxes);
    jxl_free(a, reader->codestream.data);
    jxl_free(a, reader);
}

jxl_bs_status_t jxl_container_reader_feed(jxl_container_reader *reader, const uint8_t *data,
                                          size_t len, size_t *consumed_out) {
    reader_feed_ctx fctx = {0};
    jxl_bs_status_t st;
    if (reader == NULL) {
        return JXL_BS_VALIDATION_FAILED;
    }
    fctx.codestream = &reader->codestream;
    fctx.aux_boxes = reader->aux_boxes;
    fctx.error = JXL_BS_OK;

    st = jxl_container_parser_feed(reader->parser, data, len, reader_event_cb, &fctx, consumed_out);
    if (st != JXL_BS_OK) {
        return st;
    }
    return fctx.error;
}

const uint8_t *jxl_container_reader_codestream(const jxl_container_reader *reader, size_t *len) {
    if (len != NULL) {
        *len = reader != NULL ? reader->codestream.len : 0;
    }
    return reader != NULL ? reader->codestream.data : NULL;
}

jxl_aux_box_list *jxl_container_reader_aux_boxes(jxl_container_reader *reader) {
    return reader != NULL ? reader->aux_boxes : NULL;
}
