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

size_t jxl_iccans_context(size_t i, size_t b1, size_t b2) {
  return jxl_icc_ans_context(i, (uint8_t)b1, (uint8_t)b2);
}
