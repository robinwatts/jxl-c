// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_CONTAINER_PARSER_H_
#define JXL_CONTAINER_PARSER_H_

#include "allocator.h"
#include "../error.h"
#include "box_header.h"
#include "box_type.h"

#include <stddef.h>
#include "jxl_oxide/jxl_types.h"

typedef enum {
    JXL_BITSTREAM_KIND_UNKNOWN = 0,
    JXL_BITSTREAM_KIND_BARE_CODESTREAM,
    JXL_BITSTREAM_KIND_CONTAINER,
    JXL_BITSTREAM_KIND_INVALID,
} jxl_bitstream_kind;

typedef enum {
    JXL_PARSE_EVENT_BITSTREAM_KIND = 0,
    JXL_PARSE_EVENT_CODESTREAM,
    JXL_PARSE_EVENT_NO_MORE_AUX_BOX,
    JXL_PARSE_EVENT_AUX_BOX_START,
    JXL_PARSE_EVENT_AUX_BOX_DATA,
    JXL_PARSE_EVENT_AUX_BOX_END,
} jxl_parse_event_type;

typedef struct {
    jxl_parse_event_type type;
    jxl_bitstream_kind kind;
    const uint8_t *data;
    size_t data_len;
    jxl_box_type box_type;
    int brotli_compressed;
    int last_box;
} jxl_parse_event;

typedef struct jxl_container_parser jxl_container_parser;

jxl_container_parser *jxl_container_parser_create(jxl_allocator_state *alloc);
void jxl_container_parser_destroy(jxl_allocator_state *alloc, jxl_container_parser *parser);

jxl_bitstream_kind jxl_container_parser_kind(const jxl_container_parser *parser);
size_t jxl_container_parser_previous_consumed_bytes(const jxl_container_parser *parser);

jxl_bs_status_t jxl_container_parser_emit(jxl_container_parser *parser, const uint8_t **buf,
                                          size_t *len, jxl_parse_event *event, int *has_event);

typedef jxl_bs_status_t (*jxl_container_event_cb)(void *ctx, const jxl_parse_event *event);

jxl_bs_status_t jxl_container_parser_feed(jxl_container_parser *parser, const uint8_t *data,
                                          size_t len, jxl_container_event_cb cb, void *ctx,
                                          size_t *consumed_out);

#endif /* JXL_CONTAINER_PARSER_H_ */
