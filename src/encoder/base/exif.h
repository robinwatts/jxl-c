// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef LIB_JXL_BASE_EXIF_H_
#define LIB_JXL_BASE_EXIF_H_

// Basic parsing of Exif (just enough for the render-impacting things
// like orientation)

#include <jxl/codestream_header.h>

#include <stddef.h>
#include <stdint.h>

#include "base/byte_order.h"
#include "base/compiler_specific.h"
#include "base/span.h"


static const uint16_t kExifOrientationTag = 274;

// Checks if a blob looks like Exif, and if so, sets bigendian
// according to the tiff endianness
static JXL_INLINE bool jxl_is_exif(const jxl_bytes* exif, bool* bigendian) {
  if (jxl_bytes_size(exif) < 12) return false;  // not enough bytes for a valid exif blob
  const uint8_t* t = jxl_bytes_data(exif);
  if (jxl_load_le32(t) == 0x2A004D4D) {
    *bigendian = true;
    return true;
  } else if (jxl_load_le32(t) == 0x002A4949) {
    *bigendian = false;
    return true;
  }
  return false;  // not a valid tiff header
}

// Finds the position of an Exif tag, or 0 if it is not found
static JXL_INLINE size_t jxl_find_exif_tag_position(const jxl_bytes* exif,
                                             uint16_t tagname) {
  bool bigendian;
  if (!jxl_is_exif(exif, &bigendian)) return 0;
  const uint8_t* t = jxl_bytes_data(exif) + 4;
  uint64_t offset = (bigendian ? jxl_load_be32(t) : jxl_load_le32(t));
  if (jxl_bytes_size(exif) < 12 + offset + 2 || offset < 8) return 0;
  t += offset - 4;
  if (offset + 2 >= jxl_bytes_size(exif)) return 0;
  uint16_t nb_tags = (bigendian ? jxl_load_be16(t) : jxl_load_le16(t));
  t += 2;
  while (nb_tags > 0) {
    if (t + 12 >= jxl_bytes_data(exif) + jxl_bytes_size(exif)) return 0;
    uint16_t tag = (bigendian ? jxl_load_be16(t) : jxl_load_le16(t));
    t += 2;
    if (tag == tagname) return (size_t)(t - jxl_bytes_data(exif));
    t += 10;
    nb_tags--;
  }
  return 0;
}

// TODO(jon): tag 1 can be used to represent Adobe RGB 1998 if it has value
// "R03"
// TODO(jon): set intrinsic dimensions according to
// https://discourse.wicg.io/t/proposal-exif-image-resolution-auto-and-from-image/4326/24
// Parses the Exif data just enough to extract any render-impacting info.
// If the Exif data is invalid or could not be parsed, then it is treated
// as a no-op.
static JXL_INLINE void jxl_interpret_exif(const jxl_bytes* exif,
                                      jxl_orientation* orientation) {
  bool bigendian;
  if (!jxl_is_exif(exif, &bigendian)) return;
  size_t o_pos = jxl_find_exif_tag_position(exif, kExifOrientationTag);
  if (o_pos) {
    const uint8_t* t = jxl_bytes_data(exif) + o_pos;
    uint16_t type = (bigendian ? jxl_load_be16(t) : jxl_load_le16(t));
    t += 2;
    uint32_t count = (bigendian ? jxl_load_be32(t) : jxl_load_le32(t));
    t += 4;
    uint16_t value = (bigendian ? jxl_load_be16(t) : jxl_load_le16(t));
    if (type == 3 && count == 1 && value >= 1 && value <= 8) {
      *orientation = (jxl_orientation)(value);
    }
  }
}


#endif  // LIB_JXL_BASE_EXIF_H_
