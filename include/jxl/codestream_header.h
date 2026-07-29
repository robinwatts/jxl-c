/* Copyright (c) the JPEG XL Project Authors. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style
 * license that can be found in LICENSE-BSD.
 */

/** @addtogroup jxl_metadata
 * @{
 * @file codestream_header.h
 * @brief Minimal metadata enums for the JPEG-to-JXL encoder build.
 */

#ifndef JXL_CODESTREAM_HEADER_H_
#define JXL_CODESTREAM_HEADER_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** jxl_image orientation metadata. Values 1..8 match the EXIF definitions. */
typedef enum {
  JXL_ORIENT_IDENTITY = 1,
  JXL_ORIENT_FLIP_HORIZONTAL = 2,
  JXL_ORIENT_ROTATE_180 = 3,
  JXL_ORIENT_FLIP_VERTICAL = 4,
  JXL_ORIENT_TRANSPOSE = 5,
  JXL_ORIENT_ROTATE_90_CW = 6,
  JXL_ORIENT_ANTI_TRANSPOSE = 7,
  JXL_ORIENT_ROTATE_90_CCW = 8,
} jxl_orientation;

#ifdef __cplusplus
}
#endif

#endif /* JXL_CODESTREAM_HEADER_H_ */

/** @}*/
