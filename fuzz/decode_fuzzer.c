// SPDX-License-Identifier: MIT OR Apache-2.0
#include "jxl_fuzz_decode.h"

#include <stddef.h>
#include <stdint.h>

/* Match fuzz/fuzz_targets/decode.rs */
enum {
    k_dim_limit = 65536,
};

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    jxl_fuzz_decode(data, size, k_dim_limit);
    return 0;
}
