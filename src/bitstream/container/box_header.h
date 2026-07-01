// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_CONTAINER_BOX_HEADER_H_
#define JXL_CONTAINER_BOX_HEADER_H_

#include "../error.h"
#include "box_type.h"

#include <stddef.h>
#include "jxl_oxide/jxl_types.h"

typedef struct {
    jxl_box_type ty;
    uint64_t box_size;
    int has_box_size;
    int is_last;
} jxl_box_header;

typedef enum {
    JXL_BOX_HEADER_DONE = 0,
    JXL_BOX_HEADER_NEED_MORE_DATA,
} jxl_box_header_parse_result;

jxl_bs_status_t jxl_box_header_parse(const uint8_t *buf, size_t len, jxl_box_header *out,
                                     size_t *header_size);

#endif /* JXL_CONTAINER_BOX_HEADER_H_ */
