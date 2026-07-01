// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_OXIDE_TYPES_H_
#define JXL_OXIDE_TYPES_H_

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
typedef struct jxl_cms jxl_cms;

typedef void *(*jxl_alloc_fn)(void *user_data, size_t size);
typedef void (*jxl_free_fn)(void *user_data, void *ptr);
typedef void *(*jxl_calloc_fn)(void *user_data, size_t nmemb, size_t size);
typedef void *(*jxl_realloc_fn)(void *user_data, void *ptr, size_t size);

/*
 * Pluggable heap for a jxl_context. Passed via jxl_context_options::alloc.
 * alloc and free are required; calloc and realloc may be NULL (library defaults).
 * user_data is forwarded unchanged to every callback.
 */
typedef struct {
    jxl_alloc_fn alloc;
    jxl_free_fn free;
    jxl_calloc_fn calloc;   /* NULL → alloc + zero-fill */
    jxl_realloc_fn realloc; /* NULL → alloc/copy/free emulation */
    void *user_data;
} jxl_allocator_t;

typedef struct {
    /* Region in display-oriented coordinates (matches jxl_image_header width/height). */
    uint32_t width;
    uint32_t height;
    uint32_t left;
    uint32_t top;
} jxl_crop;

typedef enum {
    JXL_COLOUR_SPACE_RGB = 0,
    JXL_COLOUR_SPACE_GRAY = 1,
    JXL_COLOUR_SPACE_XYB = 2,
    JXL_COLOUR_SPACE_UNKNOWN = 255,
} jxl_colour_space_t;

typedef enum {
    JXL_WHITE_POINT_D65 = 1,
    JXL_WHITE_POINT_CUSTOM = 2,
    JXL_WHITE_POINT_UNKNOWN = 255,
} jxl_white_point_t;

typedef enum {
    JXL_PRIMARIES_SRGB = 1,
    JXL_PRIMARIES_CUSTOM = 2,
    JXL_PRIMARIES_UNKNOWN = 255,
} jxl_primaries_t;

typedef enum {
    JXL_TRANSFER_SRGB = 1,
    JXL_TRANSFER_LINEAR = 2,
    JXL_TRANSFER_UNKNOWN = 255,
} jxl_transfer_function_t;

typedef enum {
    JXL_RENDERING_PERCEPTUAL = 0,
    JXL_RENDERING_RELATIVE = 1,
    JXL_RENDERING_SATURATION = 2,
    JXL_RENDERING_ABSOLUTE = 3,
    JXL_RENDERING_UNKNOWN = 255,
} jxl_rendering_intent_t;

typedef struct {
    jxl_colour_space_t colour_space;
    jxl_white_point_t white_point;
    jxl_primaries_t primaries;
    jxl_transfer_function_t transfer;
    jxl_rendering_intent_t rendering_intent;
} jxl_color_encoding;

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t bit_depth;
    uint32_t num_extra_channels;
    int have_animation;
} jxl_image_header;

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

#endif /* JXL_OXIDE_TYPES_H_ */
