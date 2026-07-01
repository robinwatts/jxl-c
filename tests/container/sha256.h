// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_TEST_SHA256_H_
#define JXL_TEST_SHA256_H_

#include <stddef.h>
#include "jxl_oxide/jxl_types.h"

void jxl_test_sha256(const uint8_t *data, size_t len, uint8_t out[32]);
void jxl_test_sha256_hex(const uint8_t *data, size_t len, char out_hex[65]);

#endif /* JXL_TEST_SHA256_H_ */
