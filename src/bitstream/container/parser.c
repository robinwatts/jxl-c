// SPDX-License-Identifier: MIT OR Apache-2.0
#include "parser.h"

#include "allocator.h"
#include "../consts.h"

#include <string.h>

typedef enum {
    DETECT_WAITING_SIGNATURE,
    DETECT_WAITING_BOX_HEADER,
    DETECT_WAITING_JXLP_INDEX,
    DETECT_IN_AUX_BOX,
    DETECT_IN_CODESTREAM,
} detect_state;

typedef enum {
    JXLP_INITIAL,
    JXLP_SINGLE_JXLC,
    JXLP_INDEXED,
    JXLP_FINISHED,
} jxlp_index_state;

struct jxl_container_parser {
    detect_state state;
    jxlp_index_state jxlp_index_state;
    uint32_t jxlp_expected_index;
    jxl_box_header pending_header;
    jxl_box_type brotli_inner_type;
    int has_brotli_inner_type;
    size_t bytes_left;
    int has_bytes_left;
    jxl_bitstream_kind codestream_kind;
    int pending_no_more_aux_box;
    size_t previous_consumed_bytes;
};

static int starts_with(const uint8_t *buf, size_t len, const uint8_t *sig, size_t sig_len) {
    if (len < sig_len) {
        return 0;
    }
    return memcmp(buf, sig, sig_len) == 0;
}

static int sig_might_match(const uint8_t *buf, size_t len, const uint8_t *sig, size_t sig_len) {
    size_t n = len < sig_len ? len : sig_len;
    return memcmp(buf, sig, n) == 0;
}

static int is_reserved_brob_type(const jxl_box_type *ty) {
    return (ty->bytes[0] == 'j' && ty->bytes[1] == 'x' && ty->bytes[2] == 'l') ||
           jxl_box_type_eq(*ty, JXL_BOX_BROTLI_COMPRESSED) ||
           jxl_box_type_eq(*ty, JXL_BOX_JPEG_RECONSTRUCTION);
}

jxl_container_parser *jxl_container_parser_create(jxl_allocator_state *alloc) {
    jxl_container_parser *p = jxl_calloc(alloc, 1, sizeof(*p));
    if (p != NULL) {
        p->state = DETECT_WAITING_SIGNATURE;
        p->jxlp_index_state = JXLP_INITIAL;
    }
    return p;
}

void jxl_container_parser_destroy(jxl_allocator_state *alloc, jxl_container_parser *parser) {
    jxl_free(alloc, parser);
}

jxl_bitstream_kind jxl_container_parser_kind(const jxl_container_parser *parser) {
    switch (parser->state) {
    case DETECT_WAITING_SIGNATURE:
        return JXL_BITSTREAM_KIND_UNKNOWN;
    case DETECT_WAITING_BOX_HEADER:
    case DETECT_WAITING_JXLP_INDEX:
    case DETECT_IN_AUX_BOX:
        return JXL_BITSTREAM_KIND_CONTAINER;
    case DETECT_IN_CODESTREAM:
        return parser->codestream_kind;
    }
    return JXL_BITSTREAM_KIND_UNKNOWN;
}

size_t jxl_container_parser_previous_consumed_bytes(const jxl_container_parser *parser) {
    return parser->previous_consumed_bytes;
}

jxl_bs_status_t jxl_container_parser_emit(jxl_container_parser *parser, const uint8_t **buf,
                                          size_t *len, jxl_parse_event *event, int *has_event) {
    *has_event = 0;

    for (;;) {
        if (*len == 0) {
            return JXL_BS_OK;
        }

        switch (parser->state) {
        case DETECT_WAITING_SIGNATURE:
            if (starts_with(*buf, *len, jxl_codestream_sig, JXL_CODESTREAM_SIG_LEN)) {
                parser->state = DETECT_IN_CODESTREAM;
                parser->codestream_kind = JXL_BITSTREAM_KIND_BARE_CODESTREAM;
                parser->has_bytes_left = 0;
                parser->pending_no_more_aux_box = 1;
                event->type = JXL_PARSE_EVENT_BITSTREAM_KIND;
                event->kind = JXL_BITSTREAM_KIND_BARE_CODESTREAM;
                *has_event = 1;
                return JXL_BS_OK;
            }
            if (starts_with(*buf, *len, jxl_container_sig, JXL_CONTAINER_SIG_LEN)) {
                *buf += JXL_CONTAINER_SIG_LEN;
                *len -= JXL_CONTAINER_SIG_LEN;
                parser->state = DETECT_WAITING_BOX_HEADER;
                event->type = JXL_PARSE_EVENT_BITSTREAM_KIND;
                event->kind = JXL_BITSTREAM_KIND_CONTAINER;
                *has_event = 1;
                return JXL_BS_OK;
            }
            if (!sig_might_match(*buf, *len, jxl_codestream_sig, JXL_CODESTREAM_SIG_LEN) &&
                !sig_might_match(*buf, *len, jxl_container_sig, JXL_CONTAINER_SIG_LEN)) {
                parser->state = DETECT_IN_CODESTREAM;
                parser->codestream_kind = JXL_BITSTREAM_KIND_INVALID;
                parser->has_bytes_left = 0;
                parser->pending_no_more_aux_box = 1;
                event->type = JXL_PARSE_EVENT_BITSTREAM_KIND;
                event->kind = JXL_BITSTREAM_KIND_INVALID;
                *has_event = 1;
                return JXL_BS_OK;
            }
            return JXL_BS_OK;

        case DETECT_WAITING_BOX_HEADER: {
            jxl_box_header header;
            size_t header_size = 0;
            jxl_bs_status_t st = jxl_box_header_parse(*buf, *len, &header, &header_size);
            if (header_size == 0) {
                return JXL_BS_OK;
            }
            if (st != JXL_BS_OK) {
                return st;
            }
            *buf += header_size;
            *len -= header_size;

            if (jxl_box_type_eq(header.ty, JXL_BOX_CODESTREAM)) {
                if (parser->jxlp_index_state == JXLP_INITIAL) {
                    parser->jxlp_index_state = JXLP_SINGLE_JXLC;
                } else if (parser->jxlp_index_state == JXLP_SINGLE_JXLC) {
                    return JXL_BS_INVALID_BOX;
                } else {
                    return JXL_BS_INVALID_BOX;
                }
                parser->state = DETECT_IN_CODESTREAM;
                parser->codestream_kind = JXL_BITSTREAM_KIND_CONTAINER;
                parser->has_bytes_left = header.has_box_size;
                if (header.has_box_size) {
                    parser->bytes_left = (size_t)header.box_size;
                }
                parser->pending_no_more_aux_box = !header.has_box_size;
                return JXL_BS_OK;
            }

            if (jxl_box_type_eq(header.ty, JXL_BOX_PARTIAL_CODESTREAM)) {
                if (header.has_box_size && header.box_size < 4) {
                    return JXL_BS_INVALID_BOX;
                }
                if (parser->jxlp_index_state == JXLP_INITIAL) {
                    parser->jxlp_index_state = JXLP_INDEXED;
                    parser->jxlp_expected_index = 0;
                } else if (parser->jxlp_index_state == JXLP_INDEXED) {
                    parser->jxlp_expected_index += 1;
                } else if (parser->jxlp_index_state == JXLP_SINGLE_JXLC) {
                    return JXL_BS_INVALID_BOX;
                } else {
                    return JXL_BS_INVALID_BOX;
                }
                parser->pending_header = header;
                parser->state = DETECT_WAITING_JXLP_INDEX;
                return JXL_BS_OK;
            }

            parser->pending_header = header;
            parser->has_brotli_inner_type = 0;
            parser->has_bytes_left = header.has_box_size;
            if (header.has_box_size) {
                parser->bytes_left = (size_t)header.box_size;
            }

            if (!jxl_box_type_eq(header.ty, JXL_BOX_BROTLI_COMPRESSED)) {
                event->type = JXL_PARSE_EVENT_AUX_BOX_START;
                event->box_type = header.ty;
                event->brotli_compressed = 0;
                event->last_box = !header.has_box_size;
                *has_event = 1;
                parser->state = DETECT_IN_AUX_BOX;
                return JXL_BS_OK;
            }

            if (header.has_box_size && parser->bytes_left <= 3) {
                return JXL_BS_INVALID_BOX;
            }
            parser->state = DETECT_IN_AUX_BOX;
            return JXL_BS_OK;
        }

        case DETECT_WAITING_JXLP_INDEX:
            if (*len < 4) {
                return JXL_BS_OK;
            }
            {
                uint32_t index = ((uint32_t)(*buf)[0] << 24) | ((uint32_t)(*buf)[1] << 16) |
                                 ((uint32_t)(*buf)[2] << 8) | (uint32_t)(*buf)[3];
                *buf += 4;
                *len -= 4;
                int is_last = (index & 0x80000000u) != 0;
                index &= 0x7fffffffu;

                if (parser->jxlp_index_state != JXLP_INDEXED ||
                    parser->jxlp_expected_index != index) {
                    return JXL_BS_INVALID_BOX;
                }
                if (is_last) {
                    parser->jxlp_index_state = JXLP_FINISHED;
                }

                parser->state = DETECT_IN_CODESTREAM;
                parser->codestream_kind = JXL_BITSTREAM_KIND_CONTAINER;
                if (parser->pending_header.has_box_size) {
                    parser->has_bytes_left = 1;
                    parser->bytes_left = (size_t)parser->pending_header.box_size - 4u;
                } else {
                    parser->has_bytes_left = 0;
                }
                parser->pending_no_more_aux_box = !parser->has_bytes_left;
            }
            return JXL_BS_OK;

        case DETECT_IN_CODESTREAM:
            if (parser->pending_no_more_aux_box) {
                parser->pending_no_more_aux_box = 0;
                event->type = JXL_PARSE_EVENT_NO_MORE_AUX_BOX;
                *has_event = 1;
                return JXL_BS_OK;
            }
            if (!parser->has_bytes_left) {
                event->type = JXL_PARSE_EVENT_CODESTREAM;
                event->data = *buf;
                event->data_len = *len;
                *buf += *len;
                *len = 0;
                *has_event = 1;
                return JXL_BS_OK;
            }
            if (*len >= parser->bytes_left) {
                event->type = JXL_PARSE_EVENT_CODESTREAM;
                event->data = *buf;
                event->data_len = parser->bytes_left;
                *buf += parser->bytes_left;
                *len -= parser->bytes_left;
                parser->state = DETECT_WAITING_BOX_HEADER;
                *has_event = 1;
                return JXL_BS_OK;
            }
            event->type = JXL_PARSE_EVENT_CODESTREAM;
            event->data = *buf;
            event->data_len = *len;
            parser->bytes_left -= *len;
            *buf += *len;
            *len = 0;
            *has_event = 1;
            return JXL_BS_OK;

        case DETECT_IN_AUX_BOX:
            if (jxl_box_type_eq(parser->pending_header.ty, JXL_BOX_BROTLI_COMPRESSED) &&
                !parser->has_brotli_inner_type) {
                jxl_box_type ty;
                if (*len < 4) {
                    return JXL_BS_OK;
                }
                ty.bytes[0] = (*buf)[0];
                ty.bytes[1] = (*buf)[1];
                ty.bytes[2] = (*buf)[2];
                ty.bytes[3] = (*buf)[3];
                *buf += 4;
                *len -= 4;
                if (parser->has_bytes_left) {
                    parser->bytes_left -= 4;
                }
                if (is_reserved_brob_type(&ty)) {
                    return JXL_BS_VALIDATION_FAILED;
                }
                parser->brotli_inner_type = ty;
                parser->has_brotli_inner_type = 1;
                event->type = JXL_PARSE_EVENT_AUX_BOX_START;
                event->box_type = ty;
                event->brotli_compressed = 1;
                event->last_box = !parser->pending_header.has_box_size;
                *has_event = 1;
                return JXL_BS_OK;
            }

            {
                size_t payload_len;
                jxl_box_type ty = parser->has_brotli_inner_type ? parser->brotli_inner_type
                                                                : parser->pending_header.ty;
                if (parser->has_bytes_left && parser->bytes_left == 0) {
                    parser->state = DETECT_WAITING_BOX_HEADER;
                    parser->has_brotli_inner_type = 0;
                    event->type = JXL_PARSE_EVENT_AUX_BOX_END;
                    event->box_type = ty;
                    *has_event = 1;
                    return JXL_BS_OK;
                }

                if (parser->has_bytes_left) {
                    payload_len = parser->bytes_left < *len ? parser->bytes_left : *len;
                    parser->bytes_left -= payload_len;
                } else {
                    payload_len = *len;
                    *len = 0;
                }

                event->type = JXL_PARSE_EVENT_AUX_BOX_DATA;
                event->box_type = ty;
                event->data = *buf;
                event->data_len = payload_len;
                *buf += payload_len;
                *len -= payload_len;
                *has_event = 1;
                return JXL_BS_OK;
            }
        }
    }
}

typedef struct {
    jxl_container_event_cb cb;
    void *ctx;
    jxl_bs_status_t error;
} feed_ctx;

static jxl_bs_status_t feed_emit(void *vctx, const jxl_parse_event *event) {
    feed_ctx *f = vctx;
    jxl_bs_status_t st = f->cb(f->ctx, event);
    if (st != JXL_BS_OK) {
        f->error = st;
    }
    return st;
}

jxl_bs_status_t jxl_container_parser_feed(jxl_container_parser *parser, const uint8_t *data,
                                          size_t len, jxl_container_event_cb cb, void *ctx,
                                          size_t *consumed_out) {
    size_t remaining;
    const uint8_t *buf = data;
    feed_ctx fctx = {0};
    remaining = len;
    parser->previous_consumed_bytes = 0;

    fctx.cb = cb;
    fctx.ctx = ctx;
    fctx.error = JXL_BS_OK;


    while (remaining > 0 && fctx.error == JXL_BS_OK) {
        size_t before = remaining;
        for (;;) {
            jxl_parse_event event;
            int has_event = 0;
            jxl_bs_status_t st =
                jxl_container_parser_emit(parser, &buf, &remaining, &event, &has_event);
            if (st != JXL_BS_OK) {
                parser->previous_consumed_bytes = len - remaining;
                if (consumed_out != NULL) {
                    *consumed_out = parser->previous_consumed_bytes;
                }
                return st;
            }
            if (!has_event) {
                break;
            }
            st = feed_emit(&fctx, &event);
            if (st != JXL_BS_OK) {
                parser->previous_consumed_bytes = len - remaining;
                if (consumed_out != NULL) {
                    *consumed_out = parser->previous_consumed_bytes;
                }
                return st;
            }
        }
        if (remaining == before) {
            break;
        }
    }

    parser->previous_consumed_bytes = len - remaining;
    if (consumed_out != NULL) {
        *consumed_out = parser->previous_consumed_bytes;
    }
    return fctx.error;
}
