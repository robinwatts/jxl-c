// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_COMMON_ICC_SHUFFLE_H_
#define JXL_COMMON_ICC_SHUFFLE_H_

#include <stddef.h>
#include <stdint.h>

/* Interleave / de-interleave helpers for compressed ICC command streams.
 * `out` must not alias `in` unless the implementation documents otherwise;
 * all helpers here require distinct buffers. */

void jxl_icc_shuffle2(const uint8_t *in, size_t len, uint8_t *out);
void jxl_icc_shuffle4(const uint8_t *in, size_t len, uint8_t *out);

/* Inverse of the width-2/4 interleave used after prediction on the encode
 * path. Writes `size` bytes to `out`. */
void jxl_icc_unshuffle(const uint8_t *in, size_t size, size_t width, uint8_t *out);

#endif /* JXL_COMMON_ICC_SHUFFLE_H_ */
