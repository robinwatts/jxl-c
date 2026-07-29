// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in LICENSE-BSD.

#ifndef JXL_ENC_ICC_CODEC_COMMON_H_
#define JXL_ENC_ICC_CODEC_COMMON_H_

// Compressed representation of ICC profiles.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "base/array.h"
#include "base/enc_status.h"
#include "common/icc_ans_context.h"
#include "common/icc_codec_params.h"
#include "common/icc_header_predict.h"
#include "common/icc_linear_predict.h"
#include "padded_bytes.h"

enum { kICCHeaderSize = JXL_ICC_HEADER_SIZE };

// Four-byte ICC keyword / type code.
enum { kTagSize = JXL_ICC_TAG_SIZE };

typedef struct jxl_tag {
  uint8_t bytes[kTagSize];
} jxl_tag;

static inline uint8_t jxl_tag_at(const jxl_tag* self, size_t i) {
  return self->bytes[i];
}
static inline const uint8_t* jxl_tag_data(const jxl_tag* self) { return self->bytes; }

static inline void jxl_tag_construct_empty(jxl_tag* self) {
  self->bytes[0] = 0;
  self->bytes[1] = 0;
  self->bytes[2] = 0;
  self->bytes[3] = 0;
}
static inline void jxl_tag_set_bytes(jxl_tag* self, uint8_t b0, uint8_t b1, uint8_t b2,
                               uint8_t b3) {
  self->bytes[0] = b0;
  self->bytes[1] = b1;
  self->bytes[2] = b2;
  self->bytes[3] = b3;
}
static inline jxl_tag jxl_tag_make(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3) {
  jxl_tag tag;
  jxl_tag_set_bytes(&tag, b0, b1, b2, b3);
  return tag;
}

static inline bool jxl_tag_equal(const jxl_tag* a, const jxl_tag* b) {
  return memcmp(a->bytes, b->bytes, kTagSize) == 0;
}

JXL_DEFINE_POD_ARRAY(jxl_array_tag, jxl_tag)

static const jxl_tag kBkptTag = {{ 'b', 'k', 'p', 't' }};
static const jxl_tag kBtrcTag = {{ 'b', 'T', 'R', 'C' }};
static const jxl_tag kBxyzTag = {{ 'b', 'X', 'Y', 'Z' }};
static const jxl_tag kChadTag = {{ 'c', 'h', 'a', 'd' }};
static const jxl_tag kChrmTag = {{ 'c', 'h', 'r', 'm' }};
static const jxl_tag kCprtTag = {{ 'c', 'p', 'r', 't' }};
static const jxl_tag kCurvTag = {{ 'c', 'u', 'r', 'v' }};
static const jxl_tag kDescTag = {{ 'd', 'e', 's', 'c' }};
static const jxl_tag kDmddTag = {{ 'd', 'm', 'd', 'd' }};
static const jxl_tag kDmndTag = {{ 'd', 'm', 'n', 'd' }};
static const jxl_tag kGbd_Tag = {{ 'g', 'b', 'd', ' ' }};
static const jxl_tag kGtrcTag = {{ 'g', 'T', 'R', 'C' }};
static const jxl_tag kGxyzTag = {{ 'g', 'X', 'Y', 'Z' }};
static const jxl_tag kKtrcTag = {{ 'k', 'T', 'R', 'C' }};
static const jxl_tag kKxyzTag = {{ 'k', 'X', 'Y', 'Z' }};
static const jxl_tag kLumiTag = {{ 'l', 'u', 'm', 'i' }};
static const jxl_tag kMab_Tag = {{ 'm', 'A', 'B', ' ' }};
static const jxl_tag kMba_Tag = {{ 'm', 'B', 'A', ' ' }};
static const jxl_tag kMlucTag = {{ 'm', 'l', 'u', 'c' }};
static const jxl_tag kParaTag = {{ 'p', 'a', 'r', 'a' }};
static const jxl_tag kRtrcTag = {{ 'r', 'T', 'R', 'C' }};
static const jxl_tag kRxyzTag = {{ 'r', 'X', 'Y', 'Z' }};
static const jxl_tag kSf32Tag = {{ 's', 'f', '3', '2' }};
static const jxl_tag kTextTag = {{ 't', 'e', 'x', 't' }};
static const jxl_tag kVcgtTag = {{ 'v', 'c', 'g', 't' }};
static const jxl_tag kWtptTag = {{ 'w', 't', 'p', 't' }};
static const jxl_tag kXyz_Tag = {{ 'X', 'Y', 'Z', ' ' }};

// jxl_tag names focused on RGB and GRAY monitor profiles
enum { kNumTagStrings = 17 };
static const jxl_tag* const kTagStrings[kNumTagStrings] = {
    &kCprtTag, &kWtptTag, &kBkptTag, &kRxyzTag, &kGxyzTag, &kBxyzTag,
    &kKxyzTag, &kRtrcTag, &kGtrcTag, &kBtrcTag, &kKtrcTag, &kChadTag,
    &kDescTag, &kChrmTag, &kDmndTag, &kDmddTag, &kLumiTag};

enum { kCommandTagUnknown = JXL_ICC_TAG_UNKNOWN };
enum { kCommandTagTRC = JXL_ICC_TAG_TRC };
enum { kCommandTagXYZ = JXL_ICC_TAG_XYZ };
enum { kCommandTagStringFirst = JXL_ICC_TAG_STRING_FIRST };

// jxl_tag types focused on RGB and GRAY monitor profiles
enum { kNumTypeStrings = JXL_ICC_NUM_TYPE_STRINGS };
static const jxl_tag* const kTypeStrings[kNumTypeStrings] = {
    &kXyz_Tag, &kDescTag, &kTextTag, &kMlucTag,
    &kParaTag, &kCurvTag, &kSf32Tag, &kGbd_Tag};

enum { kCommandInsert = JXL_ICC_CMD_INSERT };
enum { kCommandShuffle2 = JXL_ICC_CMD_SHUFFLE2 };
enum { kCommandPredict = JXL_ICC_CMD_PREDICT };
enum { kCommandXYZ = JXL_ICC_CMD_XYZ };
enum { kCommandTypeStartFirst = JXL_ICC_CMD_TYPE_START_FIRST };

enum { kFlagBitOffset = JXL_ICC_FLAG_OFFSET };
enum { kFlagBitSize = JXL_ICC_FLAG_SIZE };

enum { kNumICCContexts = JXL_NUM_ICC_CONTEXTS };

uint32_t jxl_decode_uint32(const uint8_t* data, size_t size, size_t pos);
jxl_tag jxl_decode_keyword(const uint8_t* data, size_t size, size_t pos);
jxl_enc_status jxl_append_keyword(const jxl_tag* keyword, jxl_padded_bytes* data);

// Checks if a + b > size, taking possible integer overflow into account.
jxl_enc_status jxl_check_out_of_bounds(uint64_t a, uint64_t b, uint64_t size);

typedef struct jxl_icc_header_bytes {
  uint8_t bytes[kICCHeaderSize];
} jxl_icc_header_bytes;

static inline uint8_t* jxl_icc_header_bytes_data(jxl_icc_header_bytes* self) { return self->bytes; }
static inline const uint8_t* jxl_icc_header_bytes_data_const(const jxl_icc_header_bytes* self) {
  return self->bytes;
}

static inline void jxl_icc_header_bytes_copy_from(jxl_icc_header_bytes* self, const uint8_t* data) {
  memcpy(self->bytes, data, kICCHeaderSize);
}

jxl_icc_header_bytes jxl_icc_initial_header_prediction(uint32_t size);
void jxl_icc_predict_header(const uint8_t* icc, size_t size, uint8_t* header,
                      size_t pos);
static inline uint8_t jxl_linear_predict_icc_value(const uint8_t* data, size_t start,
                                                   size_t i, size_t stride, size_t width,
                                                   int order) {
  return jxl_icc_linear_predict_value(data, start, i, stride, width, order);
}
size_t jxl_iccans_context(size_t i, size_t b1, size_t b2);


#endif  // JXL_ENC_ICC_CODEC_COMMON_H_
