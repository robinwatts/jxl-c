// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_JBR_OUTPUT_H_
#define JXL_JBR_OUTPUT_H_

#include "allocator.h"
#include "jbr/error.h"

#include <stddef.h>
#include "jxl/types.h"

typedef struct {
    uint8_t *data;
    size_t len;
    size_t cap;
} jxl_jbr_output;

void jxl_jbr_output_init(jxl_jbr_output *out);
void jxl_jbr_output_free(jxl_context *alloc, jxl_jbr_output *out);
jxl_jbr_status jxl_jbr_output_write(jxl_context *alloc, jxl_jbr_output *out,
                                    const uint8_t *data, size_t len);

#endif /* JXL_JBR_OUTPUT_H_ */
