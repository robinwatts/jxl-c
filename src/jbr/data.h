// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_JBR_DATA_H_
#define JXL_JBR_DATA_H_

#include "allocator.h"
#include "jbr/error.h"
#include "jbr/header.h"

#include <stddef.h>
#include "jxl_oxide/jxl_types.h"

typedef struct jxl_jbr_data jxl_jbr_data;

jxl_jbr_data *jxl_jbr_data_create(jxl_allocator_state *alloc);
void jxl_jbr_data_destroy(jxl_allocator_state *alloc, jxl_jbr_data *data);

/* Returns JXL_JBR_NEED_MORE_DATA if header not yet complete. */
jxl_jbr_status jxl_jbr_data_try_parse(jxl_allocator_state *alloc, const uint8_t *buf, size_t len,
                                      jxl_jbr_data **out);
jxl_jbr_status jxl_jbr_data_feed(jxl_allocator_state *alloc, jxl_jbr_data *data, const uint8_t *buf,
                                 size_t len);
jxl_jbr_status jxl_jbr_data_finalize(jxl_allocator_state *alloc, jxl_jbr_data *data);

const jxl_jbr_header *jxl_jbr_data_header(const jxl_jbr_data *data);
const uint8_t *jxl_jbr_data_payload(const jxl_jbr_data *data, size_t *len_out);

#endif /* JXL_JBR_DATA_H_ */
