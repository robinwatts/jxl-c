// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_CODESTREAM_COLLECT_H_
#define JXL_CODESTREAM_COLLECT_H_

#include "allocator.h"
#include "bitstream/container/parser.h"
#include "jxl_oxide/jxl_status.h"

#include <stddef.h>
#include "jxl_oxide/jxl_types.h"

jxl_status_t jxl_collect_codestream(jxl_allocator_state *alloc, const uint8_t *file_data,
                                    size_t file_len, uint8_t **out_data, size_t *out_len);

#endif /* JXL_CODESTREAM_COLLECT_H_ */
