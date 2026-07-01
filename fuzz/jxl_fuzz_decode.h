// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_FUZZ_DECODE_H_
#define JXL_FUZZ_DECODE_H_

#include <stddef.h>
#include <stdint.h>

/*
 * Exercise incremental jxl_decoder_feed, init, and keyframe render on arbitrary
 * bytes. Invalid input is ignored; dim_limit skips render for oversized headers
 * (matches Rust jxl_oxide_fuzz::fuzz_decode).
 */
void jxl_fuzz_decode(const uint8_t *data, size_t len, uint32_t dim_limit);

#endif /* JXL_FUZZ_DECODE_H_ */
