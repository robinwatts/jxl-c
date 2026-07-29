// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in LICENSE-BSD.

#include "icc_codec_common.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "base/byte_order.h"
#include "base/compiler_specific.h"
#include "base/enc_status.h"
#include "padded_bytes.h"

static uint8_t jxl_predict_value_u8(uint8_t p1, uint8_t p2, uint8_t p3, int order) {
  if (order == 0) return p1;
  if (order == 1) return (uint8_t)(2 * p1 - p2);
  if (order == 2) return (uint8_t)(3 * p1 - 3 * p2 + p3);
  return 0;
}

static uint16_t jxl_predict_value_u16(uint16_t p1, uint16_t p2, uint16_t p3, int order) {
  if (order == 0) return p1;
  if (order == 1) return (uint16_t)(2 * p1 - p2);
  if (order == 2) return (uint16_t)(3 * p1 - 3 * p2 + p3);
  return 0;
}

static uint32_t jxl_predict_value_u32(uint32_t p1, uint32_t p2, uint32_t p3, int order) {
  if (order == 0) return p1;
  if (order == 1) return 2 * p1 - p2;
  if (order == 2) return 3 * p1 - 3 * p2 + p3;
  return 0;
}

uint32_t jxl_decode_uint32(const uint8_t* data, size_t size, size_t pos) {
  return pos + 4 > size ? 0 : jxl_load_be32(data + pos);
}

jxl_tag jxl_decode_keyword(const uint8_t* data, size_t size, size_t pos) {
  jxl_tag tag;
  if (pos + 4 > size) {
    jxl_tag_set_bytes(&tag, ' ', ' ', ' ', ' ');
    return tag;
  }
  jxl_tag_set_bytes(&tag, data[pos], data[pos + 1], data[pos + 2], data[pos + 3]);
  return tag;
}

jxl_enc_status jxl_append_keyword(const jxl_tag* keyword, jxl_padded_bytes* data) {
  JXL_STATIC_ASSERT(sizeof(jxl_tag) == kTagSize, "jxl_tag should be 4-bytes");
  return jxl_padded_bytes_append(data, jxl_tag_data(keyword),
                                  jxl_tag_data(keyword) + kTagSize);
}

jxl_enc_status jxl_check_out_of_bounds(uint64_t a, uint64_t b, uint64_t size) {
  uint64_t pos = a + b;
  if (pos > size) return JXL_FAILURE("Out of bounds");
  if (pos < a) return JXL_FAILURE("Out of bounds");
  return jxl_enc_ok_status();
}

jxl_icc_header_bytes jxl_icc_initial_header_prediction(uint32_t size) {
  jxl_icc_header_bytes copy;
  jxl_icc_header_bytes_copy_from(&copy, jxl_icc_initial_header_prediction_bytes);
  jxl_store_be32(size, jxl_icc_header_bytes_data(&copy));
  return copy;
}

void jxl_icc_predict_header(const uint8_t* icc, size_t size, uint8_t* header,
                            size_t pos) {
  if (pos == 8 && size >= 8) {
    header[80] = icc[4];
    header[81] = icc[5];
    header[82] = icc[6];
    header[83] = icc[7];
  }
  if (pos == 41 && size >= 41) {
    if (icc[40] == 'A') {
      header[41] = 'P';
      header[42] = 'P';
      header[43] = 'L';
    }
    if (icc[40] == 'M') {
      header[41] = 'S';
      header[42] = 'F';
      header[43] = 'T';
    }
  }
  if (pos == 42 && size >= 42) {
    if (icc[40] == 'S' && icc[41] == 'G') {
      header[42] = 'I';
      header[43] = ' ';
    }
    if (icc[40] == 'S' && icc[41] == 'U') {
      header[42] = 'N';
      header[43] = 'W';
    }
  }
}

uint8_t jxl_linear_predict_icc_value(const uint8_t* data, size_t start, size_t i,
                                     size_t stride, size_t width, int order) {
  size_t pos = start + i;
  if (width == 1) {
    uint8_t p1 = data[pos - stride];
    uint8_t p2 = data[pos - stride * 2];
    uint8_t p3 = data[pos - stride * 3];
    return jxl_predict_value_u8(p1, p2, p3, order);
  } else if (width == 2) {
    size_t p = start + (i & ~1);
    uint16_t p1 = (data[p - stride * 1] << 8) + data[p - stride * 1 + 1];
    uint16_t p2 = (data[p - stride * 2] << 8) + data[p - stride * 2 + 1];
    uint16_t p3 = (data[p - stride * 3] << 8) + data[p - stride * 3 + 1];
    uint16_t pred = jxl_predict_value_u16(p1, p2, p3, order);
    return (i & 1) ? (pred & 255) : ((pred >> 8) & 255);
  } else {
    size_t p = start + (i & ~3);
    uint32_t p1 = jxl_decode_uint32(data, pos, p - stride);
    uint32_t p2 = jxl_decode_uint32(data, pos, p - stride * 2);
    uint32_t p3 = jxl_decode_uint32(data, pos, p - stride * 3);
    uint32_t pred = jxl_predict_value_u32(p1, p2, p3, order);
    unsigned shiftbytes = 3 - (i & 3);
    return (pred >> (shiftbytes * 8)) & 255;
  }
}

size_t jxl_iccans_context(size_t i, size_t b1, size_t b2) {
  return jxl_icc_ans_context(i, (uint8_t)b1, (uint8_t)b2);
}
