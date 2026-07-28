// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_SIMPLE_LOSSLESS_H_
#define JXL_SIMPLE_LOSSLESS_H_

#include <jxl/context.h>
#include <jxl/status.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t num_channels;
    uint32_t bits_per_sample;
    int big_endian;
    int effort;
    uint32_t reserved; /* must be 0 for v1 */
} jxl_simple_lossless_image_desc;

/*
 * Lossless modular encode (libjxl effort-1 class). Input is interleaved samples:
 * G, GA, RGB, or RGBA; 8-bit uses one byte per channel, >8-bit uses 16-bit samples
 * (little-endian in the buffer unless big_endian is set).
 *
 * On success, *jxl_out receives an allocated buffer (free with jxl_free(ctx, *jxl_out)).
 */
jxl_status_t jxl_simple_lossless_encode(jxl_context *ctx,
                                        const jxl_simple_lossless_image_desc *desc,
                                        const uint8_t *pixels, size_t row_stride,
                                        uint8_t **jxl_out, size_t *jxl_len);

#ifdef __cplusplus
}
#endif

#endif /* JXL_SIMPLE_LOSSLESS_H_ */
