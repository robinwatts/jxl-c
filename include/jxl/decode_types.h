// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_DECODE_TYPES_H_
#define JXL_DECODE_TYPES_H_

#include <jxl/allocator.h>
#include <jxl/color_encoding.h>

#include <stddef.h>
#include <stdint.h>

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#define jxl_inline static inline
#elif defined(__GNUC__) || defined(__clang__) || defined(_MSC_VER)
#define jxl_inline static __inline
#else
#define jxl_inline static
#endif

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L && !defined(__cplusplus)
#define jxl_restrict restrict
#elif defined(__GNUC__) || defined(__clang__) || defined(_MSC_VER)
#define jxl_restrict __restrict
#else
#define jxl_restrict
#endif

#if defined(__GNUC__) || defined(__clang__)
#define JXL_ATTRIBUTE_HOT __attribute__((hot))
#define JXL_ALWAYS_INLINE static inline __attribute__((always_inline))
#else
#define JXL_ATTRIBUTE_HOT
#define JXL_ALWAYS_INLINE jxl_inline
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct jxl_decoder jxl_decoder;
typedef struct jxl_render jxl_render;

typedef struct {
    /* Region in display-oriented coordinates (matches jxl_image_header width/height). */
    uint32_t width;
    uint32_t height;
    uint32_t left;
    uint32_t top;
} jxl_crop;

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t bit_depth;
    uint32_t num_extra_channels;
    int have_animation;
} jxl_image_header;

/*
 * Animation timing from the codestream AnimationHeader bundle.
 * TPS (ticks per second) = tps_numerator / tps_denominator.
 * num_loops == 0 means loop forever.
 */
typedef struct {
    uint32_t tps_numerator;
    uint32_t tps_denominator;
    uint32_t num_loops;
    int have_timecodes;
} jxl_animation_header;

typedef enum {
    JXL_EXIF_DECODING = 0,
    JXL_EXIF_NOT_FOUND,
    JXL_EXIF_AVAILABLE,
} jxl_exif_status;

typedef struct {
    jxl_exif_status status;
    uint32_t tiff_header_offset;
    const uint8_t *payload;
    size_t payload_len;
} jxl_exif_metadata;

#ifdef __cplusplus
}
#endif

#endif /* JXL_DECODE_TYPES_H_ */
