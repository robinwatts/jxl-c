/* Shared public types for decode, encode, and CMS. */
#ifndef JXL_TYPES_H_
#define JXL_TYPES_H_

#include <jxl/allocator.h>
#include <jxl/color_encoding.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- Encoder / CMS pixel I/O --- */

#define JXL_BOOL int
#define JXL_TRUE 1
#define JXL_FALSE 0
#define TO_JXL_BOOL(C) (!!(C) ? JXL_TRUE : JXL_FALSE)
#ifdef __cplusplus
#define FROM_JXL_BOOL(C) (static_cast<bool>(C))
#else
#define FROM_JXL_BOOL(C) (!!(C))
#endif

typedef enum {
  JXL_TYPE_FLOAT = 0,
  JXL_TYPE_UINT8 = 2,
  JXL_TYPE_UINT16 = 3,
  JXL_TYPE_FLOAT16 = 5,
} jxl_data_type;

typedef enum {
  JXL_NATIVE_ENDIAN = 0,
  JXL_LITTLE_ENDIAN = 1,
  JXL_BIG_ENDIAN = 2,
} jxl_endianness;

typedef struct {
  uint32_t num_channels;
  jxl_data_type data_type;
  jxl_endianness endianness;
  size_t align;
} jxl_pixel_format;

typedef enum {
  JXL_BIT_DEPTH_FROM_PIXEL_FORMAT = 0,
  JXL_BIT_DEPTH_FROM_CODESTREAM = 1,
  JXL_BIT_DEPTH_CUSTOM = 2,
} jxl_bit_depth_type;

typedef struct {
  jxl_bit_depth_type type;
  uint32_t bits_per_sample;
  uint32_t exponent_bits_per_sample;
} jxl_bit_depth;

typedef char jxl_box_type[4];

/* --- Decoder metadata --- */

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

#endif /* JXL_TYPES_H_ */
