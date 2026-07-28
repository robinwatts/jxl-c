/* Encoder-facing pixel / bit-depth types (shared with CMS interface). */
#ifndef JXL_TYPES_H_
#define JXL_TYPES_H_

#include <jxl/allocator.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

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

#ifdef __cplusplus
}
#endif

#endif /* JXL_TYPES_H_ */
