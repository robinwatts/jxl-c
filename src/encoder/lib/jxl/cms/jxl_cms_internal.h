// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef LIB_JXL_CMS_JXL_CMS_INTERNAL_H_
#define LIB_JXL_CMS_JXL_CMS_INTERNAL_H_

// ICC profiles and color space conversions.

#include <jxl/color_encoding.h>

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "lib/jxl/base/common.h"
#include "lib/jxl/base/compiler_specific.h"
#include "lib/jxl/base/matrix_ops.h"
#include "lib/jxl/base/array.h"
#include "lib/jxl/base/status.h"
#include "lib/jxl/cms/transfer_functions.h"


typedef enum jxl_extra_tf {
  kExtraTFNone,
  kExtraTFPQ,
  kExtraTFHLG,
  kExtraTFSRGB,
} jxl_extra_tf;

static jxl_status jxl_primaries_to_xyz(float rx, float ry, float gx, float gy, float bx,
                             float by, float wx, float wy, jxl_matrix3x3* matrix) {
  bool ok = (wx >= 0) && (wx <= 1) && (wy > 0) && (wy <= 1);
  if (!ok) {
    return JXL_FAILURE("Invalid white point");
  }
  // TODO(lode): also require rx, ry, gx, gy, bx, to be in range 0-1? ICC
  // profiles in theory forbid negative XYZ values, but in practice the ACES P0
  // color space uses a negative y for the blue primary.
  jxl_matrix3x3 primaries;
  jxl_matrix3x3_set_row(&primaries, 0, rx, gx, bx);
  jxl_matrix3x3_set_row(&primaries, 1, ry, gy, by);
  jxl_matrix3x3_set_row(&primaries, 2, 1.0f - rx - ry, 1.0f - gx - gy,
                  1.0f - bx - by);
  jxl_matrix3x3 primaries_inv;
  primaries_inv = primaries;
  JXL_RETURN_IF_ERROR(jxl_inv3x3_matrix(&primaries_inv));

  jxl_vector3 w = jxl_vector3_make(wx / wy, 1.0f, (1.0f - wx - wy) / wy);
  // 1 / tiny float can still overflow
  JXL_RETURN_IF_ERROR(jxl_status_from_bool(isfinite(*jxl_vector3_at(&w, 0)) && isfinite(*jxl_vector3_at(&w, 2))));
  jxl_vector3 xyz;
  jxl_mul3x3_vector(&primaries_inv, &w, &xyz);

  jxl_matrix3x3 a;
  jxl_matrix3x3_set_diagonal(&a, *jxl_vector3_at(&xyz, 0), *jxl_vector3_at(&xyz, 1),
                       *jxl_vector3_at(&xyz, 2));

  jxl_mul3x3_matrix(&primaries, &a, matrix);
  return jxl_ok_status();
}

/* Chromatic adaptation matrices */
static const jxl_matrix3x3 kBradford = {{
    {0.8951f, 0.2664f, -0.1614f},
    {-0.7502f, 1.7135f, 0.0367f},
    {0.0389f, -0.0685f, 1.0296f},
}};
static const jxl_matrix3x3 kBradfordInv = {{
    {0.9869929f, -0.1470543f, 0.1599627f},
    {0.4323053f, 0.5183603f, 0.0492912f},
    {-0.0085287f, 0.0400428f, 0.9684867f},
}};

// Adapts white point x, y to D50
static jxl_status jxl_adapt_to_xyzd50(float wx, float wy, jxl_matrix3x3* matrix) {
  bool ok = (wx >= 0) && (wx <= 1) && (wy > 0) && (wy <= 1);
  if (!ok) {
    // Out of range values can cause division through zero
    // further down with the bradford adaptation too.
    return JXL_FAILURE("Invalid white point");
  }
  jxl_vector3 w = jxl_vector3_make(wx / wy, 1.0f, (1.0f - wx - wy) / wy);
  // 1 / tiny float can still overflow
  JXL_RETURN_IF_ERROR(jxl_status_from_bool(isfinite(*jxl_vector3_at(&w, 0)) && isfinite(*jxl_vector3_at(&w, 2))));
  jxl_vector3 w50 = jxl_vector3_make(0.96422f, 1.0f, 0.82521f);

  jxl_vector3 lms;
  jxl_vector3 lms50;

  jxl_mul3x3_vector(&kBradford, &w, &lms);
  jxl_mul3x3_vector(&kBradford, &w50, &lms50);

  if (*jxl_vector3_at(&lms, 0) == 0 || *jxl_vector3_at(&lms, 1) == 0 || *jxl_vector3_at(&lms, 2) == 0) {
    return JXL_FAILURE("Invalid white point");
  }
  jxl_matrix3x3 a;
  jxl_matrix3x3_set_diagonal(&a, *jxl_vector3_at(&lms50, 0) / *jxl_vector3_at(&lms, 0),
                       *jxl_vector3_at(&lms50, 1) / *jxl_vector3_at(&lms, 1),
                       *jxl_vector3_at(&lms50, 2) / *jxl_vector3_at(&lms, 2));
  if (!isfinite(jxl_matrix3x3_at(&a, 0)[0]) || !isfinite(jxl_matrix3x3_at(&a, 1)[1]) ||
      !isfinite(jxl_matrix3x3_at(&a, 2)[2])) {
    return JXL_FAILURE("Invalid white point");
  }

  jxl_matrix3x3 b;
  jxl_mul3x3_matrix(&a, &kBradford, &b);
  jxl_mul3x3_matrix(&kBradfordInv, &b, matrix);

  return jxl_ok_status();
}

static jxl_status jxl_primaries_to_xyzd50(float rx, float ry, float gx, float gy,
                                float bx, float by, float wx, float wy,
                                jxl_matrix3x3* matrix) {
  jxl_matrix3x3 toXYZ;
  JXL_RETURN_IF_ERROR(jxl_primaries_to_xyz(rx, ry, gx, gy, bx, by, wx, wy, &toXYZ));
  jxl_matrix3x3 d50;
  JXL_RETURN_IF_ERROR(jxl_adapt_to_xyzd50(wx, wy, &d50));

  jxl_mul3x3_matrix(&d50, &toXYZ, matrix);
  return jxl_ok_status();
}

static void jxl_create_table_curve(size_t n, jxl_extra_tf tf, jxl_array_u16* table) {
  static const float kPQIntensityTarget = 10000;

  JXL_DASSERT(n <= 4096);  // ICC MFT2 only allows 4K entries
  JXL_DASSERT(tf == kExtraTFPQ || tf == kExtraTFHLG);

  // No point using float - LCMS converts to 16-bit for A2B/MFT.
  if (!jxl_status_ok(jxl_array_resize_zero(table, n))) JXL_CRASH();
  for (uint32_t i = 0; i < n; ++i) {
    const float x = (float)(i) / (n - 1);  // 1.0 at index n - 1.
    const double dx = (double)(x);
    // LCMS requires EOTF (e.g. 2.4 exponent).
    double y = (tf == kExtraTFHLG)
                   ? TF_HLG_BaseDisplayFromEncoded(dx)
                   : TF_PQ_BaseDisplayFromEncoded(kPQIntensityTarget, dx);
    JXL_DASSERT(y >= 0.0);
    // Clamp to table range - necessary for HLG.
    y = jxl_clamp1_d(y, 0.0, 1.0);
    // 1.0 corresponds to table value 0xFFFF.
    *jxl_array_at(table, i) = (uint16_t)(roundf(y * 65535.0));
  }
}

static jxl_status jxl_ciexyz_from_white_ci_exy(double wx, double wy, jxl_color* XYZ) {
  // Target Y = 1.
  if (fabs(wy) < 1e-12) return JXL_FAILURE("Y value is too small");
  const float factor = 1 / wy;
  *jxl_color_at(XYZ, 0) = wx * factor;
  *jxl_color_at(XYZ, 1) = 1;
  *jxl_color_at(XYZ, 2) = (1 - wx - wy) * factor;
  return jxl_ok_status();
}

static void jxl_icc_compute_md5(const jxl_array_u8* data, uint8_t sum[16])
    JXL_NO_SANITIZE("unsigned-integer-overflow") {
  jxl_array_u8 data64;
  jxl_array_construct_empty(&data64, data->memory_manager);
  if (!jxl_status_ok(jxl_array_copy_from(&data64, data))) JXL_CRASH();
  if (!jxl_status_ok(jxl_array_u8_push_back(&data64, (uint8_t)(128)))) {
    JXL_CRASH();
  }
  // Add bytes such that ((size + 8) & 63) == 0.
  size_t extra = ((64 - ((jxl_array_len(&data64) + 8) & 63)) & 63);
  if (!jxl_status_ok(jxl_array_resize_zero(&data64, jxl_array_len(&data64) + extra))) {
    JXL_CRASH();
  }
  for (uint64_t i = 0; i < 64; i += 8) {
    if (!jxl_status_ok(jxl_array_u8_push_back(
            &data64,
            (uint8_t)((uint64_t)(jxl_array_len(data) << 3u) >>
                                 i)))) {
      JXL_CRASH();
    }
  }

  static const uint32_t sineparts[64] = {
      0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a,
      0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
      0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340,
      0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
      0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8,
      0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
      0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa,
      0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
      0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92,
      0xffeff47d, 0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
      0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
  };
  static const uint32_t shift[64] = {
      7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
      5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
      4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
      6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
  };

  uint32_t a0 = 0x67452301;
  uint32_t b0 = 0xefcdab89;
  uint32_t c0 = 0x98badcfe;
  uint32_t d0 = 0x10325476;

  for (size_t i = 0; i < jxl_array_len(&data64); i += 64) {
    uint32_t a = a0;
    uint32_t b = b0;
    uint32_t c = c0;
    uint32_t d = d0;
    uint32_t f;
    uint32_t g;
    for (size_t j = 0; j < 64; j++) {
      if (j < 16) {
        f = (b & c) | ((~b) & d);
        g = j;
      } else if (j < 32) {
        f = (d & b) | ((~d) & c);
        g = (5 * j + 1) & 0xf;
      } else if (j < 48) {
        f = b ^ c ^ d;
        g = (3 * j + 5) & 0xf;
      } else {
        f = c ^ (b | (~d));
        g = (7 * j) & 0xf;
      }
      uint32_t dg0 = *jxl_array_at(&data64, i + g * 4 + 0);
      uint32_t dg1 = *jxl_array_at(&data64, i + g * 4 + 1);
      uint32_t dg2 = *jxl_array_at(&data64, i + g * 4 + 2);
      uint32_t dg3 = *jxl_array_at(&data64, i + g * 4 + 3);
      uint32_t u = dg0 | (dg1 << 8u) | (dg2 << 16u) | (dg3 << 24u);
      f += a + sineparts[j] + u;
      a = d;
      d = c;
      c = b;
      b += (f << shift[j]) | (f >> (32u - shift[j]));
    }
    a0 += a;
    b0 += b;
    c0 += c;
    d0 += d;
  }
  sum[0] = a0;
  sum[1] = a0 >> 8u;
  sum[2] = a0 >> 16u;
  sum[3] = a0 >> 24u;
  sum[4] = b0;
  sum[5] = b0 >> 8u;
  sum[6] = b0 >> 16u;
  sum[7] = b0 >> 24u;
  sum[8] = c0;
  sum[9] = c0 >> 8u;
  sum[10] = c0 >> 16u;
  sum[11] = c0 >> 24u;
  sum[12] = d0;
  sum[13] = d0 >> 8u;
  sum[14] = d0 >> 16u;
  sum[15] = d0 >> 24u;
  jxl_array_destroy(&data64);
}

static jxl_status jxl_create_icc_chad_matrix(double wx, double wy, jxl_matrix3x3* result) {
  jxl_matrix3x3 m;
  if (wy == 0) {  // jxl_white_point can not be pitch-black.
    return JXL_FAILURE("Invalid jxl_white_point");
  }
  JXL_RETURN_IF_ERROR(jxl_adapt_to_xyzd50(wx, wy, &m));
  *result = m;
  return jxl_ok_status();
}

// Creates RGB to XYZ matrix given RGB primaries and white point in xy.
static jxl_status jxl_create_iccrgb_matrix(double rx, double ry, double gx, double gy,
                                 double bx, double by, double wx, double wy,
                                 jxl_matrix3x3* result) {
  jxl_matrix3x3 m;
  JXL_RETURN_IF_ERROR(jxl_primaries_to_xyzd50(rx, ry, gx, gy, bx, by, wx, wy, &m));
  *result = m;
  return jxl_ok_status();
}

static void jxl_array_ensure_size(jxl_array_u8* a, size_t n) {
  if (jxl_array_len(a) < n && !jxl_status_ok(jxl_array_resize_zero(a, n))) {
    JXL_CRASH();
  }
}

static void jxl_write_icc_uint32(uint32_t value, size_t pos,
                           jxl_array_u8* icc) {
  jxl_array_ensure_size(icc, pos + 4);
  *jxl_array_at(icc, pos + 0) = (value >> 24u) & 255;
  *jxl_array_at(icc, pos + 1) = (value >> 16u) & 255;
  *jxl_array_at(icc, pos + 2) = (value >> 8u) & 255;
  *jxl_array_at(icc, pos + 3) = value & 255;
}

static void jxl_write_icc_uint16(uint16_t value, size_t pos,
                           jxl_array_u8* icc) {
  jxl_array_ensure_size(icc, pos + 2);
  *jxl_array_at(icc, pos + 0) = (value >> 8u) & 255;
  *jxl_array_at(icc, pos + 1) = value & 255;
}

static void jxl_write_icc_uint8(uint8_t value, size_t pos,
                          jxl_array_u8* icc) {
  jxl_array_ensure_size(icc, pos + 1);
  *jxl_array_at(icc, pos) = value;
}

// Writes a 4-character tag
static void jxl_write_icc_tag(const char* value, size_t pos,
                        jxl_array_u8* icc) {
  jxl_array_ensure_size(icc, pos + 4);
  memcpy(jxl_array_data(icc) + pos, value, 4);
}

static jxl_status jxl_write_iccs15_fixed16(float value, size_t pos,
                                 jxl_array_u8* icc) {
  // "nextafterf" for 32768.0f towards zero are:
  // 32767.998046875, 32767.99609375, 32767.994140625
  // Even the first value works well,...
  bool ok = (-32767.995f <= value) && (value <= 32767.995f);
  if (!ok) return JXL_FAILURE("ICC value is out of range / NaN");
  int32_t i = (int32_t)(lround(value * 65536.0f));
  // Use two's complement
  uint32_t u = (uint32_t)(i);
  jxl_write_icc_uint32(u, pos, icc);
  return jxl_ok_status();
}

static jxl_status jxl_create_icc_header(const jxl_color_encoding* c,
                              jxl_array_u8* header) {
  // TODO(lode): choose color management engine name, e.g. "skia" if
  // integrated in skia.
  static const char* kCmm = "jxl ";

  if (!jxl_status_ok(jxl_array_resize_zero(header, 128))) {
    return JXL_FAILURE("Failed to allocate ICC header");
  }

  jxl_write_icc_uint32(0, 0, header);  // size, correct value filled in at end
  jxl_write_icc_tag(kCmm, 4, header);
  jxl_write_icc_uint32(0x04400000u, 8, header);
  jxl_write_icc_tag("mntr", 12, header);
  jxl_write_icc_tag(c->color_space == JXL_COLOR_SPACE_GRAY ? "GRAY" : "RGB ", 16,
              header);
  jxl_write_icc_tag("XYZ ", 20, header);

  // Three uint32_t's date/time encoding.
  // TODO(lode): encode actual date and time, this is a placeholder
  uint32_t year = 2019;
  uint32_t month = 12;
  uint32_t day = 1;
  uint32_t hour = 0;
  uint32_t minute = 0;
  uint32_t second = 0;
  jxl_write_icc_uint16(year, 24, header);
  jxl_write_icc_uint16(month, 26, header);
  jxl_write_icc_uint16(day, 28, header);
  jxl_write_icc_uint16(hour, 30, header);
  jxl_write_icc_uint16(minute, 32, header);
  jxl_write_icc_uint16(second, 34, header);

  jxl_write_icc_tag("acsp", 36, header);
  jxl_write_icc_tag("APPL", 40, header);
  jxl_write_icc_uint32(0, 44, header);  // flags
  jxl_write_icc_uint32(0, 48, header);  // device manufacturer
  jxl_write_icc_uint32(0, 52, header);  // device model
  jxl_write_icc_uint32(0, 56, header);  // device attributes
  jxl_write_icc_uint32(0, 60, header);  // device attributes
  jxl_write_icc_uint32((uint32_t)(c->rendering_intent), 64, header);

  // Mandatory D50 white point of profile connection space
  jxl_write_icc_uint32(0x0000f6d6, 68, header);
  jxl_write_icc_uint32(0x00010000, 72, header);
  jxl_write_icc_uint32(0x0000d32d, 76, header);

  jxl_write_icc_tag(kCmm, 80, header);

  return jxl_ok_status();
}

static void jxl_add_to_icc_tag_table(const char* tag, size_t offset, size_t size,
                             jxl_array_u8* tagtable,
                             jxl_array_size* offsets) {
  jxl_write_icc_tag(tag, jxl_array_len(tagtable), tagtable);
  // writing true offset deferred to later
  jxl_write_icc_uint32(0, jxl_array_len(tagtable), tagtable);
  if (!jxl_status_ok(jxl_array_size_push_back(offsets, offset))) JXL_CRASH();
  jxl_write_icc_uint32(size, jxl_array_len(tagtable), tagtable);
}

static void jxl_finalize_icc_tag(jxl_array_u8* tags, size_t* offset,
                           size_t* size) {
  while ((jxl_array_len(tags) & 3) != 0) {
    if (!jxl_status_ok(jxl_array_u8_push_back(tags, (uint8_t)(0)))) {
      JXL_CRASH();
    }
  }
  *offset += *size;
  *size = jxl_array_len(tags) - *offset;
}

// The input text must be ASCII, writing other characters to UTF-16 is not
// implemented.
static void jxl_create_icc_mluc_tag(const char* text, size_t text_size,
                             jxl_array_u8* tags) {
  jxl_write_icc_tag("mluc", jxl_array_len(tags), tags);
  jxl_write_icc_uint32(0, jxl_array_len(tags), tags);
  jxl_write_icc_uint32(1, jxl_array_len(tags), tags);
  jxl_write_icc_uint32(12, jxl_array_len(tags), tags);
  jxl_write_icc_tag("enUS", jxl_array_len(tags), tags);
  jxl_write_icc_uint32(text_size * 2, jxl_array_len(tags), tags);
  jxl_write_icc_uint32(28, jxl_array_len(tags), tags);
  for (size_t i = 0; i < text_size; ++i) {
    if (!jxl_status_ok(jxl_array_u8_push_back(tags, (uint8_t)(0)))) {
      JXL_CRASH();
    }
    if (!jxl_status_ok(jxl_array_u8_push_back(tags, (uint8_t)(text[i])))) {
      JXL_CRASH();
    }
  }
}

static jxl_status jxl_create_iccxyz_tag(const jxl_color* xyz, jxl_array_u8* tags) {
  jxl_write_icc_tag("XYZ ", jxl_array_len(tags), tags);
  jxl_write_icc_uint32(0, jxl_array_len(tags), tags);
  for (size_t i = 0; i < 3; ++i) {
    JXL_RETURN_IF_ERROR(jxl_write_iccs15_fixed16(*jxl_color_at_const(xyz, i), jxl_array_len(tags), tags));
  }
  return jxl_ok_status();
}

static jxl_status jxl_create_icc_chad_tag(const jxl_matrix3x3* chad,
                               jxl_array_u8* tags) {
  jxl_write_icc_tag("sf32", jxl_array_len(tags), tags);
  jxl_write_icc_uint32(0, jxl_array_len(tags), tags);
  for (size_t j = 0; j < 3; j++) {
    for (size_t i = 0; i < 3; i++) {
      JXL_RETURN_IF_ERROR(jxl_write_iccs15_fixed16(jxl_matrix3x3_at_const(chad, j)[i], jxl_array_len(tags), tags));
    }
  }
  return jxl_ok_status();
}

static void jxl_maybe_create_icccicp_tag(const jxl_color_encoding* c,
                                  jxl_array_u8* tags, size_t* offset,
                                  size_t* size, jxl_array_u8* tagtable,
                                  jxl_array_size* offsets) {
  if (c->color_space != JXL_COLOR_SPACE_RGB) {
    return;
  }
  uint8_t primaries = 0;
  if (c->primaries == JXL_PRIMARIES_P3) {
    if (c->white_point == JXL_WHITE_POINT_D65) {
      primaries = 12;
    } else if (c->white_point == JXL_WHITE_POINT_DCI) {
      primaries = 11;
    } else {
      return;
    }
  } else if (c->primaries != JXL_PRIMARIES_CUSTOM &&
             c->white_point == JXL_WHITE_POINT_D65) {
    primaries = (uint8_t)(c->primaries);
  } else {
    return;
  }
  jxl_transfer_function tf = c->transfer_function;
  if (tf == JXL_TRANSFER_FUNCTION_UNKNOWN ||
      tf == JXL_TRANSFER_FUNCTION_GAMMA) {
    return;
  }
  jxl_write_icc_tag("cicp", jxl_array_len(tags), tags);
  jxl_write_icc_uint32(0, jxl_array_len(tags), tags);
  jxl_write_icc_uint8(primaries, jxl_array_len(tags), tags);
  jxl_write_icc_uint8((uint8_t)(tf), jxl_array_len(tags), tags);
  // Matrix
  jxl_write_icc_uint8(0, jxl_array_len(tags), tags);
  // Full range
  jxl_write_icc_uint8(1, jxl_array_len(tags), tags);
  jxl_finalize_icc_tag(tags, offset, size);
  jxl_add_to_icc_tag_table("cicp", *offset, *size, tagtable, offsets);
}

static void jxl_create_icc_curv_curv_tag(const jxl_array_u16* curve,
                                 jxl_array_u8* tags) {
  size_t pos = jxl_array_len(tags);
  if (!jxl_status_ok(jxl_array_resize_zero(tags, jxl_array_len(tags) + 12 + jxl_array_len(curve) * 2))) {
    JXL_CRASH();
  }
  jxl_write_icc_tag("curv", pos, tags);
  jxl_write_icc_uint32(0, pos + 4, tags);
  jxl_write_icc_uint32(jxl_array_len(curve), pos + 8, tags);
  for (size_t i = 0; i < jxl_array_len(curve); i++) {
    jxl_write_icc_uint16(*jxl_array_at_const(curve, i), pos + 12 + i * 2, tags);
  }
}

// Writes 12 + 4*num_params bytes
static jxl_status jxl_create_icc_curv_para_tag(const float* params, size_t num_params,
                                   size_t curve_type,
                                   jxl_array_u8* tags) {
  jxl_write_icc_tag("para", jxl_array_len(tags), tags);
  jxl_write_icc_uint32(0, jxl_array_len(tags), tags);
  jxl_write_icc_uint16(curve_type, jxl_array_len(tags), tags);
  jxl_write_icc_uint16(0, jxl_array_len(tags), tags);
  for (size_t i = 0; i < num_params; ++i) {
    float param = params[i];
    JXL_RETURN_IF_ERROR(jxl_write_iccs15_fixed16(param, jxl_array_len(tags), tags));
  }
  return jxl_ok_status();
}

// These strings are baked into Description - do not change.

static const char* jxl_to_string_color_space(jxl_color_space color_space) {
  switch (color_space) {
    case JXL_COLOR_SPACE_RGB:
      return "RGB";
    case JXL_COLOR_SPACE_GRAY:
      return "Gra";
    case JXL_COLOR_SPACE_XYB:
      return "XYB";
    case JXL_COLOR_SPACE_UNKNOWN:
      return "CS?";
    default:
      // Should not happen - visitor fails if enum is invalid.
      JXL_DEBUG_ABORT("Invalid jxl_color_space %u",
                      (uint32_t)(color_space));
      return "Invalid";
  }
}

static const char* jxl_to_string_white_point(jxl_white_point white_point) {
  switch (white_point) {
    case JXL_WHITE_POINT_D65:
      return "D65";
    case JXL_WHITE_POINT_CUSTOM:
      return "Cst";
    case JXL_WHITE_POINT_E:
      return "EER";
    case JXL_WHITE_POINT_DCI:
      return "DCI";
    default:
      // Should not happen - visitor fails if enum is invalid.
      JXL_DEBUG_ABORT("Invalid jxl_white_point %u",
                      (uint32_t)(white_point));
      return "Invalid";
  }
}

static const char* jxl_to_string_primaries(jxl_primaries primaries) {
  switch (primaries) {
    case JXL_PRIMARIES_SRGB:
      return "SRG";
    case JXL_PRIMARIES_2100:
      return "202";
    case JXL_PRIMARIES_P3:
      return "DCI";
    case JXL_PRIMARIES_CUSTOM:
      return "Cst";
    default:
      // Should not happen - visitor fails if enum is invalid.
      JXL_DEBUG_ABORT("Invalid jxl_primaries %u", (uint32_t)(primaries));
      return "Invalid";
  }
}

static const char* jxl_to_string_transfer_function(jxl_transfer_function transfer_function) {
  switch (transfer_function) {
    case JXL_TRANSFER_FUNCTION_SRGB:
      return "SRG";
    case JXL_TRANSFER_FUNCTION_LINEAR:
      return "Lin";
    case JXL_TRANSFER_FUNCTION_709:
      return "709";
    case JXL_TRANSFER_FUNCTION_PQ:
      return "PeQ";
    case JXL_TRANSFER_FUNCTION_HLG:
      return "HLG";
    case JXL_TRANSFER_FUNCTION_DCI:
      return "DCI";
    case JXL_TRANSFER_FUNCTION_UNKNOWN:
      return "TF?";
    case JXL_TRANSFER_FUNCTION_GAMMA:
      JXL_DEBUG_ABORT("Invalid jxl_transfer_function: gamma");
      return "Invalid";
    default:
      // Should not happen - visitor fails if enum is invalid.
      JXL_DEBUG_ABORT("Invalid jxl_transfer_function %u",
                      (uint32_t)(transfer_function));
      return "Invalid";
  }
}

static const char* jxl_to_string_rendering_intent(jxl_rendering_intent rendering_intent) {
  switch (rendering_intent) {
    case JXL_RENDERING_INTENT_PERCEPTUAL:
      return "Per";
    case JXL_RENDERING_INTENT_RELATIVE:
      return "Rel";
    case JXL_RENDERING_INTENT_SATURATION:
      return "Sat";
    case JXL_RENDERING_INTENT_ABSOLUTE:
      return "Abs";
  }
  // Should not happen - visitor fails if enum is invalid.
  JXL_DEBUG_ABORT("Invalid jxl_rendering_intent %u",
                  (uint32_t)(rendering_intent));
  return "Invalid";
}

static inline bool jxl_close_enough(double a, double b) {
  return fabs(a - b) < 3e-5;
}

static void jxl_append_c_str(jxl_array_char* out, const char* s) {
  if (!jxl_status_ok(jxl_array_append(out, s, strlen(s)))) JXL_CRASH();
}

static void jxl_append_char(jxl_array_char* out, char c) {
  if (!jxl_status_ok(jxl_array_char_push_back(out, c))) JXL_CRASH();
}

static void jxl_append_number(jxl_array_char* out, double n) {
  char data[32];
  jxl_format_number_d(data, sizeof(data), n);
  jxl_append_c_str(out, data);
}

static void jxl_color_encoding_description_impl(const jxl_color_encoding* c,
                                         bool uniquename, jxl_array_char* out) {
jxl_array_clear(out);
  // Return short names for the most common color spaces.
  // These names are returned regardless of rendering intent, and also there is
  // some tolerance regarding primaries and transfer function, so different
  // ColorEncodings can return the same short name.
  if (c->color_space == JXL_COLOR_SPACE_RGB && !uniquename) {
    if (c->white_point == JXL_WHITE_POINT_D65) {
      if (c->transfer_function == JXL_TRANSFER_FUNCTION_SRGB) {
        if (c->primaries == JXL_PRIMARIES_SRGB) {
          jxl_append_c_str(out, "sRGB");
          return;
        }
        if (c->primaries == JXL_PRIMARIES_P3) {
          jxl_append_c_str(out, "DisplayP3");
          return;
        }
      }
      if (c->primaries == JXL_PRIMARIES_2100) {
        if (c->transfer_function == JXL_TRANSFER_FUNCTION_PQ) {
          jxl_append_c_str(out, "Rec2100PQ");
          return;
        }
        if (c->transfer_function == JXL_TRANSFER_FUNCTION_HLG) {
          jxl_append_c_str(out, "Rec2100HLG");
          return;
        }
      }

      if (c->primaries == JXL_PRIMARIES_CUSTOM &&
          jxl_close_enough(c->primaries_red_xy[0], 0.6400) &&
          jxl_close_enough(c->primaries_red_xy[1], 0.3300) &&
          jxl_close_enough(c->primaries_green_xy[0], 0.2100) &&
          jxl_close_enough(c->primaries_green_xy[1], 0.7100) &&
          jxl_close_enough(c->primaries_blue_xy[0], 0.1500) &&
          jxl_close_enough(c->primaries_blue_xy[1], 0.0600) &&
          c->transfer_function == JXL_TRANSFER_FUNCTION_GAMMA &&
          jxl_close_enough(c->gamma, 256.0 / 563.0)) {
        jxl_append_c_str(out, "Adobe98");
        return;
      }
    }

    // Apple's ROMM profile:
    // White point:       Red:               Green:             Blue:
    // 0.345669;0.358502  0.734698;0.265298  0.159585;0.840432 0.036650;0.000126
    // Adobe's ProPhoto profile:
    // 0.345705;0.358540  0.734699;0.265302  0.159600;0.840399 0.036597;0.000106
    // CSS definition of prophoto-rgb:
    // (https://drafts.csswg.org/css-color-4/#predefined-prophoto-rgb)
    // 0.345700;0.358500  0.734699;0.265301  0.159597;0.840403 0.036598;0.000105
    if (c->white_point == JXL_WHITE_POINT_CUSTOM &&
        jxl_close_enough(c->white_point_xy[0], 0.345669) &&
        jxl_close_enough(c->white_point_xy[1], 0.358496) &&
        c->primaries == JXL_PRIMARIES_CUSTOM &&
        jxl_close_enough(c->primaries_red_xy[0], 0.734699) &&
        jxl_close_enough(c->primaries_red_xy[1], 0.265301) &&
        jxl_close_enough(c->primaries_green_xy[0], 0.159597) &&
        jxl_close_enough(c->primaries_green_xy[1], 0.840403) &&
        jxl_close_enough(c->primaries_blue_xy[0], 0.036598) &&
        jxl_close_enough(c->primaries_blue_xy[1], 0.000105) &&
        // Adobe's ProPhoto profile uses a simple gamma curve (g0.555315)
        // Others have a small linear segment near black
        c->transfer_function == JXL_TRANSFER_FUNCTION_GAMMA &&
        jxl_close_enough(c->gamma, 1.0 / 1.8)) {
      jxl_append_c_str(out, "ProPhoto");
      return;
    }
  }

  jxl_append_c_str(out, jxl_to_string_color_space(c->color_space));

  bool explicit_wp_tf = (c->color_space != JXL_COLOR_SPACE_XYB);
  if (explicit_wp_tf) {
    jxl_append_char(out, '_');
    if (c->white_point == JXL_WHITE_POINT_CUSTOM) {
      jxl_append_number(out, c->white_point_xy[0]);
      jxl_append_char(out, ';');
      jxl_append_number(out, c->white_point_xy[1]);
    } else {
      jxl_append_c_str(out, jxl_to_string_white_point(c->white_point));
    }
  }

  if ((c->color_space != JXL_COLOR_SPACE_GRAY) &&
      (c->color_space != JXL_COLOR_SPACE_XYB)) {
    jxl_append_char(out, '_');
    if (c->primaries == JXL_PRIMARIES_CUSTOM) {
      jxl_append_number(out, c->primaries_red_xy[0]);
      jxl_append_char(out, ';');
      jxl_append_number(out, c->primaries_red_xy[1]);
      jxl_append_char(out, ';');
      jxl_append_number(out, c->primaries_green_xy[0]);
      jxl_append_char(out, ';');
      jxl_append_number(out, c->primaries_green_xy[1]);
      jxl_append_char(out, ';');
      jxl_append_number(out, c->primaries_blue_xy[0]);
      jxl_append_char(out, ';');
      jxl_append_number(out, c->primaries_blue_xy[1]);
    } else {
      jxl_append_c_str(out, jxl_to_string_primaries(c->primaries));
    }
  }

  jxl_append_char(out, '_');
  jxl_append_c_str(out, jxl_to_string_rendering_intent(c->rendering_intent));

  if (explicit_wp_tf) {
    jxl_transfer_function tf = c->transfer_function;
    jxl_append_char(out, '_');
    if (tf == JXL_TRANSFER_FUNCTION_GAMMA) {
      jxl_append_char(out, 'g');
      jxl_append_number(out, c->gamma);
    } else {
      jxl_append_c_str(out, jxl_to_string_transfer_function(tf));
    }
  }
}

static jxl_status jxl_maybe_create_profile_impl(const jxl_color_encoding* c,
                                     jxl_array_u8* icc) {
  jxl_status status = jxl_ok_status();
  jxl_memory_manager* mm = icc->memory_manager;
  jxl_array_u8 header;
  jxl_array_u8 tagtable;
  jxl_array_u8 tags;
  jxl_array_size offsets;
  jxl_array_char description;
  jxl_array_u8 icc_sum;
  jxl_transfer_function tf;
  size_t tag_offset = 0;
  size_t tag_size = 0;
  uint8_t checksum[16];
  size_t i;

  if (mm == NULL) {
    return JXL_FAILURE("maybe_create_profile: missing memory manager");
  }
  jxl_array_construct_empty(&header, mm);
  jxl_array_construct_empty(&tagtable, mm);
  jxl_array_construct_empty(&tags, mm);
  jxl_array_construct_empty(&offsets, mm);
  jxl_array_construct_empty(&description, mm);
  jxl_array_construct_empty(&icc_sum, mm);

  tf = c->transfer_function;
  if (c->color_space == JXL_COLOR_SPACE_UNKNOWN ||
      tf == JXL_TRANSFER_FUNCTION_UNKNOWN) {
    status = jxl_error_status();  // Not an error
    goto done;
  }

  switch (c->color_space) {
    case JXL_COLOR_SPACE_RGB:
    case JXL_COLOR_SPACE_GRAY:
      break;  // OK
    default:
      status = JXL_FAILURE("Invalid CS %u",
                           (unsigned int)(c->color_space));
      goto done;
  }

  status = jxl_create_icc_header(c, &header);
  if (!jxl_status_ok(status)) {
    goto done;
  }

  // tag count, deferred to later
  jxl_write_icc_uint32(0, jxl_array_len(&tagtable), &tagtable);

  jxl_color_encoding_description_impl(c, false, &description);
  jxl_create_icc_mluc_tag(jxl_array_data(&description), jxl_array_len(&description), &tags);
  jxl_finalize_icc_tag(&tags, &tag_offset, &tag_size);
  jxl_add_to_icc_tag_table("desc", tag_offset, tag_size, &tagtable, &offsets);

  jxl_create_icc_mluc_tag("CC0", 3, &tags);
  jxl_finalize_icc_tag(&tags, &tag_offset, &tag_size);
  jxl_add_to_icc_tag_table("cprt", tag_offset, tag_size, &tagtable, &offsets);

  // TODO(eustas): isn't it the other way round: gray image has d50 jxl_white_point?
  if (c->color_space == JXL_COLOR_SPACE_GRAY) {
    jxl_color wtpt;
    status = jxl_ciexyz_from_white_ci_exy(c->white_point_xy[0], c->white_point_xy[1], &wtpt);
    if (!jxl_status_ok(status)) goto done;
    status = jxl_create_iccxyz_tag(&wtpt, &tags);
    if (!jxl_status_ok(status)) goto done;
  } else {
    jxl_color d50 = jxl_color_make(0.964203f, 1.0f, 0.824905f);
    status = jxl_create_iccxyz_tag(&d50, &tags);
    if (!jxl_status_ok(status)) goto done;
  }
  jxl_finalize_icc_tag(&tags, &tag_offset, &tag_size);
  jxl_add_to_icc_tag_table("wtpt", tag_offset, tag_size, &tagtable, &offsets);

  if (c->color_space != JXL_COLOR_SPACE_GRAY) {
    // Chromatic adaptation matrix
    jxl_matrix3x3 chad;
    status = jxl_create_icc_chad_matrix(c->white_point_xy[0], c->white_point_xy[1], &chad);
    if (!jxl_status_ok(status)) goto done;

    status = jxl_create_icc_chad_tag(&chad, &tags);
    if (!jxl_status_ok(status)) goto done;
    jxl_finalize_icc_tag(&tags, &tag_offset, &tag_size);
    jxl_add_to_icc_tag_table("chad", tag_offset, tag_size, &tagtable, &offsets);
  }

  if (c->color_space == JXL_COLOR_SPACE_RGB) {
    jxl_matrix3x3 m;
    jxl_color r;
    jxl_color g;
    jxl_color b;
    jxl_maybe_create_icccicp_tag(c, &tags, &tag_offset, &tag_size, &tagtable,
                          &offsets);

    status = jxl_create_iccrgb_matrix(
        c->primaries_red_xy[0], c->primaries_red_xy[1], c->primaries_green_xy[0],
        c->primaries_green_xy[1], c->primaries_blue_xy[0], c->primaries_blue_xy[1],
        c->white_point_xy[0], c->white_point_xy[1], &m);
    if (!jxl_status_ok(status)) goto done;
    r = jxl_color_make(jxl_matrix3x3_at(&m, 0)[0], jxl_matrix3x3_at(&m, 1)[0],
                        jxl_matrix3x3_at(&m, 2)[0]);
    g = jxl_color_make(jxl_matrix3x3_at(&m, 0)[1], jxl_matrix3x3_at(&m, 1)[1],
                        jxl_matrix3x3_at(&m, 2)[1]);
    b = jxl_color_make(jxl_matrix3x3_at(&m, 0)[2], jxl_matrix3x3_at(&m, 1)[2],
                        jxl_matrix3x3_at(&m, 2)[2]);

    status = jxl_create_iccxyz_tag(&r, &tags);
    if (!jxl_status_ok(status)) goto done;
    jxl_finalize_icc_tag(&tags, &tag_offset, &tag_size);
    jxl_add_to_icc_tag_table("rXYZ", tag_offset, tag_size, &tagtable, &offsets);

    status = jxl_create_iccxyz_tag(&g, &tags);
    if (!jxl_status_ok(status)) goto done;
    jxl_finalize_icc_tag(&tags, &tag_offset, &tag_size);
    jxl_add_to_icc_tag_table("gXYZ", tag_offset, tag_size, &tagtable, &offsets);

    status = jxl_create_iccxyz_tag(&b, &tags);
    if (!jxl_status_ok(status)) goto done;
    jxl_finalize_icc_tag(&tags, &tag_offset, &tag_size);
    jxl_add_to_icc_tag_table("bXYZ", tag_offset, tag_size, &tagtable, &offsets);
  }

  if (tf == JXL_TRANSFER_FUNCTION_GAMMA) {
    float gamma = 1.0 / c->gamma;
    const float gamma_params[] = {gamma};
    status = jxl_create_icc_curv_para_tag(gamma_params, 1, 0, &tags);
    if (!jxl_status_ok(status)) goto done;
  } else {
    switch (tf) {
      case JXL_TRANSFER_FUNCTION_HLG:
        {
          jxl_array_u16 curv;
          jxl_array_construct_empty(&curv, mm);
          jxl_create_table_curve(64, kExtraTFHLG, &curv);
          jxl_create_icc_curv_curv_tag(&curv, &tags);
          jxl_array_destroy(&curv);
        }
        break;
      case JXL_TRANSFER_FUNCTION_PQ:
        {
          jxl_array_u16 curv;
          jxl_array_construct_empty(&curv, mm);
          jxl_create_table_curve(64, kExtraTFPQ, &curv);
          jxl_create_icc_curv_curv_tag(&curv, &tags);
          jxl_array_destroy(&curv);
        }
        break;
      case JXL_TRANSFER_FUNCTION_SRGB:
        {
          const float params[] = {2.4f, 1.0f / 1.055f, 0.055f / 1.055f,
                                  1.0f / 12.92f, 0.04045f};
          status = jxl_create_icc_curv_para_tag(params, 5, 3, &tags);
          if (!jxl_status_ok(status)) goto done;
        }
        break;
      case JXL_TRANSFER_FUNCTION_709:
        {
          const float params[] = {1.0f / 0.45f, 1.0f / 1.099f, 0.099f / 1.099f,
                                  1.0f / 4.5f, 0.081f};
          status = jxl_create_icc_curv_para_tag(params, 5, 3, &tags);
          if (!jxl_status_ok(status)) goto done;
        }
        break;
      case JXL_TRANSFER_FUNCTION_LINEAR:
        {
          const float params[] = {1.0f, 1.0f, 0.0f, 1.0f, 0.0f};
          status = jxl_create_icc_curv_para_tag(params, 5, 3, &tags);
          if (!jxl_status_ok(status)) goto done;
        }
        break;
      case JXL_TRANSFER_FUNCTION_DCI:
        {
          const float params[] = {2.6f, 1.0f, 0.0f, 1.0f, 0.0f};
          status = jxl_create_icc_curv_para_tag(params, 5, 3, &tags);
          if (!jxl_status_ok(status)) goto done;
        }
        break;
      default:
        status = JXL_UNREACHABLE("unknown TF %u", (unsigned int)(tf));
        goto done;
    }
  }
  jxl_finalize_icc_tag(&tags, &tag_offset, &tag_size);
  if (c->color_space == JXL_COLOR_SPACE_GRAY) {
    jxl_add_to_icc_tag_table("kTRC", tag_offset, tag_size, &tagtable, &offsets);
  } else {
    jxl_add_to_icc_tag_table("rTRC", tag_offset, tag_size, &tagtable, &offsets);
    jxl_add_to_icc_tag_table("gTRC", tag_offset, tag_size, &tagtable, &offsets);
    jxl_add_to_icc_tag_table("bTRC", tag_offset, tag_size, &tagtable, &offsets);
  }

  // jxl_tag count
  jxl_write_icc_uint32(jxl_array_len(&offsets), 0, &tagtable);
  for (i = 0; i < jxl_array_len(&offsets); i++) {
    jxl_write_icc_uint32(*jxl_array_at(&offsets, i) + jxl_array_len(&header) + jxl_array_len(&tagtable), 4 + 12 * i + 4,
                   &tagtable);
  }

  // ICC profile size
  jxl_write_icc_uint32(jxl_array_len(&header) + jxl_array_len(&tagtable) + jxl_array_len(&tags), 0, &header);

  jxl_array_swap(icc, &header);
  if (!jxl_status_ok(jxl_array_append(icc, jxl_array_data(&tagtable), jxl_array_len(&tagtable))) ||
      !jxl_status_ok(jxl_array_append(icc, jxl_array_data(&tags), jxl_array_len(&tags)))) {
    JXL_CRASH();
  }

  // The MD5 checksum must be computed on the profile with profile flags,
  // rendering intent, and region of the checksum itself, set to 0.
  // TODO(lode): manually verify with a reliable tool that this creates correct
  // signature (profile id) for ICC profiles.
  if (!jxl_status_ok(jxl_array_copy_from(&icc_sum, icc))) JXL_CRASH();
  if (jxl_array_len(&icc_sum) >= 64 + 4) {
    memset(jxl_array_data(&icc_sum) + 44, 0, 4);
    memset(jxl_array_data(&icc_sum) + 64, 0, 4);
  }
  jxl_icc_compute_md5(&icc_sum, checksum);

  memcpy(jxl_array_data(icc) + 84, checksum, sizeof(checksum));
  status = jxl_ok_status();

done:
  jxl_array_destroy(&header);
  jxl_array_destroy(&tagtable);
  jxl_array_destroy(&tags);
  jxl_array_destroy(&offsets);
  jxl_array_destroy(&description);
  jxl_array_destroy(&icc_sum);
  return status;
}


// NOTE: for XYB colorspace, the created profile can be used to transform a
// *scaled* XYB image (created by ScaleXYB()) to another colorspace.
static JXL_MAYBE_UNUSED jxl_status jxl_maybe_create_profile(const jxl_color_encoding* c,
                                                  jxl_array_u8* icc) {
  return jxl_maybe_create_profile_impl(c, icc);
}


#endif  // LIB_JXL_CMS_JXL_CMS_INTERNAL_H_
