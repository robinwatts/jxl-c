// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_TEST_COLLECT_CODESTREAM_H_
#define JXL_TEST_COLLECT_CODESTREAM_H_

#include "bitstream/error.h"

#include <stddef.h>
#include "jxl_oxide/jxl_types.h"

jxl_bs_status_t jxl_test_extract_codestream(const uint8_t *file_data, size_t file_len,
                                            uint8_t **out_data, size_t *out_len);

#endif /* JXL_TEST_COLLECT_CODESTREAM_H_ */
