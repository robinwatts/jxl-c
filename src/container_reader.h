// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_CONTAINER_READER_H_
#define JXL_CONTAINER_READER_H_

#include "allocator.h"
#include "aux_box.h"
#include "bitstream/container/parser.h"

#include <stddef.h>
#include "jxl_oxide/jxl_types.h"

typedef struct jxl_container_reader jxl_container_reader;

jxl_container_reader *jxl_container_reader_create(jxl_allocator_state *alloc);
void jxl_container_reader_destroy(jxl_allocator_state *alloc, jxl_container_reader *reader);

jxl_bs_status_t jxl_container_reader_feed(jxl_container_reader *reader, const uint8_t *data,
                                          size_t len, size_t *consumed_out);

const uint8_t *jxl_container_reader_codestream(const jxl_container_reader *reader, size_t *len);
jxl_aux_box_list *jxl_container_reader_aux_boxes(jxl_container_reader *reader);

#endif /* JXL_CONTAINER_READER_H_ */
