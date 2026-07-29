// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in LICENSE-BSD.

#include "enc_icc_codec.h"

#include <jxl/context.h>
#include "enc_allocator.h"

#include <stddef.h>
#include <stdint.h>

#include "base/array.h"
#include "base/common.h"
#include "base/span.h"
#include "base/enc_status.h"
#include "enc_ans.h"
#include "layer_type.h"
#include "fields.h"
#include "icc_codec_common.h"
#include "padded_bytes.h"
#include "base/compiler_specific.h"


// Unshuffles or de-interleaves bytes, for example with width 2, turns
// "AaBbCcDc" into "ABCDabcd", this for example de-interleaves UTF-16 bytes into
// first all the high order bytes, then all the low order bytes.
// Transposes a matrix of width columns and ceil(size / width) rows. There are
// size elements, size may be < width * height, if so the
// last elements of the bottom row are missing, the missing spots are
// transposed along with the filled spots, and the result has the missing
// elements at the bottom of the rightmost column. The input is the input matrix
// in scanline order, the output is the result matrix in scanline order, with
// missing elements skipped over (this may occur at multiple positions).
static jxl_enc_status jxl_unshuffle(jxl_context* ctx, uint8_t* data, size_t size,
                 size_t width) {
  size_t height = (size + width - 1) / width;  // amount of rows of input
  jxl_padded_bytes result;
  jxl_enc_status status =
      jxl_padded_bytes_with_initial_space(ctx, size, &result);
  if (!jxl_enc_status_ok(status)) {
    jxl_padded_bytes_destroy(&result);
    return status;
  }

  // i = input index, j output index
  size_t s = 0;
  size_t j = 0;
  for (size_t i = 0; i < size; i++) {
    *jxl_padded_bytes_at(&result, j) = data[i];
    j += height;
    if (j >= size) j = ++s;
  }

  for (size_t i = 0; i < size; i++) {
    data[i] = *jxl_padded_bytes_at(&result, i);
  }
  jxl_padded_bytes_destroy(&result);
  return jxl_enc_ok_status();
}

// This is performed by the encoder, the encoder must be able to encode any
// random byte stream (not just byte streams that are a valid ICC profile), so
// an error returned by this function is an implementation error.
static jxl_enc_status jxl_predict_and_shuffle(size_t stride, size_t width, int order, size_t num,
                         const uint8_t* data, size_t size, size_t* pos,
                         jxl_padded_bytes* result) {
  JXL_RETURN_IF_ERROR(jxl_check_out_of_bounds(*pos, num, size));
  jxl_context* ctx = jxl_padded_bytes_ctx(result);
  // Required by the specification, see decoder. stride * 4 must be < *pos.
  if (!*pos || ((*pos - 1u) >> 2u) < stride) {
    return JXL_FAILURE("Invalid stride");
  }
  if (*pos < stride * 4) return JXL_FAILURE("Too large stride");
  size_t start = jxl_padded_bytes_size(result);
  for (size_t i = 0; i < num; i++) {
    uint8_t predicted =
        jxl_linear_predict_icc_value(data, *pos, i, stride, width, order);
    JXL_RETURN_IF_ERROR(jxl_padded_bytes_push_back(result, data[*pos + i] - predicted));
  }
  *pos += num;
  if (width > 1) {
    JXL_RETURN_IF_ERROR(
        jxl_unshuffle(ctx, jxl_padded_bytes_data(result) + start, num, width));
  }
  return jxl_enc_ok_status();
}

static inline jxl_enc_status jxl_encode_var_int(uint64_t value, jxl_padded_bytes* data) {
  size_t pos = jxl_padded_bytes_size(data);
  JXL_RETURN_IF_ERROR(jxl_padded_bytes_resize(data, jxl_padded_bytes_size(data) + 9));
  size_t output_size = jxl_padded_bytes_size(data);
  uint8_t* output = jxl_padded_bytes_data(data);

  // While more than 7 bits of data are left,
  // store 7 bits and set the next byte flag
  while (value > 127) {
    // TODO(eustas): should it be `<` ?
    JXL_ENSURE(pos <= output_size);
    // |128: Set the next byte flag
    output[pos++] = ((uint8_t)(value & 127)) | 128;
    // Remove the seven bits we just wrote
    value >>= 7;
  }
  // TODO(eustas): should it be `<` ?
  JXL_ENSURE(pos <= output_size);
  output[pos++] = (uint8_t)(value & 127);

  return jxl_padded_bytes_resize(data, pos);
}

static const size_t kSizeLimit = UINT32_MAX >> 2;

static bool jxl_tag_size_sane(size_t tagsize) {
  return (tagsize > 8) && (tagsize < kSizeLimit);
}

// Outputs a transformed form of the given icc profile. The result itself is
// not particularly smaller than the input data in bytes, but it will be in a
// form that is easier to compress (more zeroes, ...) and will compress better
// with brotli.

jxl_enc_status jxl_predict_icc_main_step(jxl_context* ctx, const uint8_t* icc,
                          size_t size, size_t* pos, jxl_tag* tag, size_t* tagstart,
                          size_t* tagsize, size_t* clutstart, size_t* last0,
                          jxl_padded_bytes* commands, jxl_padded_bytes* data,
                          const jxl_array_size* tagstarts, const jxl_array_size* tagsizes,
                          jxl_padded_bytes* commands_add, jxl_padded_bytes* data_add) {
  size_t last1 = *pos;

  // This means the loop brought the position beyond the tag end.
  // If tagsize is nonsensical, any pos looks "ok-ish".
  if ((*pos > *tagstart + *tagsize) && (*tagsize < kSizeLimit)) {
    jxl_tag_construct_empty(tag);  // nonsensical value
  }

  size_t tag_at_pos = jxl_array_len(tagstarts);
  for (size_t i = 0; i < jxl_array_len(tagstarts); ++i) {
    if (*jxl_array_at_const(tagstarts, i) == *pos) {
      tag_at_pos = i;
      break;
    }
  }
  if (jxl_padded_bytes_empty(commands_add) && jxl_padded_bytes_empty(data_add) &&
      tag_at_pos < jxl_array_len(tagstarts) && *pos + 4 <= size) {
    size_t index = tag_at_pos;
    *tag = jxl_decode_keyword(icc, size, *pos);
    *tagstart = *jxl_array_at_const(tagstarts, index);
    *tagsize = *jxl_array_at_const(tagsizes, index);

    if (jxl_tag_equal(tag, &kMlucTag) && jxl_tag_size_sane(*tagsize) &&
        *pos + *tagsize <= size && icc[*pos + 4] == 0 && icc[*pos + 5] == 0 &&
        icc[*pos + 6] == 0 && icc[*pos + 7] == 0) {
      size_t num = *tagsize - 8;
      JXL_RETURN_IF_ERROR(
          jxl_padded_bytes_push_back(commands_add, kCommandTypeStartFirst + 3));
      *pos += 8;
      JXL_RETURN_IF_ERROR(jxl_padded_bytes_push_back(commands_add, kCommandShuffle2));
      JXL_RETURN_IF_ERROR(jxl_encode_var_int(num, commands_add));
      size_t start = jxl_padded_bytes_size(data_add);
      for (size_t i = 0; i < num; i++) {
        JXL_RETURN_IF_ERROR(jxl_padded_bytes_push_back(data_add, icc[*pos]));
        (*pos)++;
      }
      JXL_RETURN_IF_ERROR(
          jxl_unshuffle(ctx, jxl_padded_bytes_data(data_add) + start, num, 2));
    }

    if (jxl_tag_equal(tag, &kCurvTag) && jxl_tag_size_sane(*tagsize) &&
        *pos + *tagsize <= size && icc[*pos + 4] == 0 && icc[*pos + 5] == 0 &&
        icc[*pos + 6] == 0 && icc[*pos + 7] == 0) {
      size_t num = *tagsize - 8;
      if (num > 16 && num < (1 << 28) && *pos + num <= size && *pos > 0) {
        JXL_RETURN_IF_ERROR(
            jxl_padded_bytes_push_back(commands_add, kCommandTypeStartFirst + 5));
        *pos += 8;
        JXL_RETURN_IF_ERROR(jxl_padded_bytes_push_back(commands_add, kCommandPredict));
        int order = 1;
        int width = 2;
        int stride = width;
        JXL_RETURN_IF_ERROR(
            jxl_padded_bytes_push_back(commands_add, (order << 2) | (width - 1)));
        JXL_RETURN_IF_ERROR(jxl_encode_var_int(num, commands_add));
        JXL_RETURN_IF_ERROR(jxl_predict_and_shuffle(stride, width, order, num, icc,
                                              size, pos, data_add));
      }
    }
  }

  if (jxl_tag_equal(tag, &kMab_Tag) || jxl_tag_equal(tag, &kMba_Tag)) {
    jxl_tag subTag = jxl_decode_keyword(icc, size, *pos);
    if (*pos + 12 < size &&
        (jxl_tag_equal(&subTag, &kCurvTag) || jxl_tag_equal(&subTag, &kVcgtTag)) &&
        jxl_decode_uint32(icc, size, *pos + 4) == 0) {
      uint32_t num = jxl_decode_uint32(icc, size, *pos + 8) * 2;
      if (num > 16 && num < (1 << 28) && *pos + 12 + num <= size) {
        *pos += 12;
        last1 = *pos;
        JXL_RETURN_IF_ERROR(jxl_padded_bytes_push_back(commands_add, kCommandPredict));
        int order = 1;
        int width = 2;
        int stride = width;
        JXL_RETURN_IF_ERROR(
            jxl_padded_bytes_push_back(commands_add, (order << 2) | (width - 1)));
        JXL_RETURN_IF_ERROR(jxl_encode_var_int(num, commands_add));
        JXL_RETURN_IF_ERROR(jxl_predict_and_shuffle(stride, width, order, num, icc,
                                              size, pos, data_add));
      }
    }

    if (*pos == *tagstart + 24 && *pos + 4 < size) {
      // Note that this value can be remembered for next iterations of the
      // loop, so the "pos == clutstart" if below can trigger during a later
      // iteration.
      *clutstart = *tagstart + jxl_decode_uint32(icc, size, *pos);
    }

    if (*pos == *clutstart && *clutstart + 16 < size) {
      size_t numi = icc[*tagstart + 8];
      size_t numo = icc[*tagstart + 9];
      size_t width = icc[*clutstart + 16];
      size_t stride = width * numo;
      size_t num = width * numo;
      for (size_t i = 0; i < numi && *clutstart + i < size; i++) {
        num *= icc[*clutstart + i];
      }
      if ((width == 1 || width == 2) && num > 64 && num < (1 << 28) &&
          *pos + num <= size && *pos > stride * 4) {
        JXL_RETURN_IF_ERROR(jxl_padded_bytes_push_back(commands_add, kCommandPredict));
        int order = 1;
        uint8_t flags =
            (order << 2) | (width - 1) | (stride == width ? 0 : 16);
        JXL_RETURN_IF_ERROR(jxl_padded_bytes_push_back(commands_add, flags));
        if (flags & 16) {
          JXL_RETURN_IF_ERROR(jxl_encode_var_int(stride, commands_add));
        }
        JXL_RETURN_IF_ERROR(jxl_encode_var_int(num, commands_add));
        JXL_RETURN_IF_ERROR(jxl_predict_and_shuffle(stride, width, order, num, icc,
                                              size, pos, data_add));
      }
    }
  }

  if (jxl_padded_bytes_empty(commands_add) && jxl_padded_bytes_empty(data_add) &&
      jxl_tag_equal(tag, &kGbd_Tag) && jxl_tag_size_sane(*tagsize) &&
      *pos == *tagstart + 8 && *pos + *tagsize - 8 <= size && *pos > 16) {
    size_t width = 4;
    size_t order = 0;
    size_t stride = width;
    size_t num = *tagsize - 8;
    uint8_t flags = (order << 2) | (width - 1) | (stride == width ? 0 : 16);
    JXL_RETURN_IF_ERROR(jxl_padded_bytes_push_back(commands_add, kCommandPredict));
    JXL_RETURN_IF_ERROR(jxl_padded_bytes_push_back(commands_add, flags));
    if (flags & 16) {
      JXL_RETURN_IF_ERROR(jxl_encode_var_int(stride, commands_add));
    }
    JXL_RETURN_IF_ERROR(jxl_encode_var_int(num, commands_add));
    JXL_RETURN_IF_ERROR(jxl_predict_and_shuffle(stride, width, order, num, icc, size,
                                          pos, data_add));
  }

  if (jxl_padded_bytes_empty(commands_add) && jxl_padded_bytes_empty(data_add) &&
      *pos + 20 <= size) {
    jxl_tag subTag = jxl_decode_keyword(icc, size, *pos);
    if (jxl_tag_equal(&subTag, &kXyz_Tag) &&
        jxl_decode_uint32(icc, size, *pos + 4) == 0) {
      JXL_RETURN_IF_ERROR(jxl_padded_bytes_push_back(commands_add, kCommandXYZ));
      *pos += 8;
      for (size_t j = 0; j < 12; j++) {
        JXL_RETURN_IF_ERROR(jxl_padded_bytes_push_back(data_add, icc[(*pos)++]));
      }
    }
  }

  if (jxl_padded_bytes_empty(commands_add) && jxl_padded_bytes_empty(data_add) &&
      *pos + 8 <= size) {
    if (jxl_decode_uint32(icc, size, *pos + 4) == 0) {
      jxl_tag subTag = jxl_decode_keyword(icc, size, *pos);
      for (size_t i = 0; i < kNumTypeStrings; i++) {
        if (jxl_tag_equal(&subTag, kTypeStrings[i])) {
          JXL_RETURN_IF_ERROR(
              jxl_padded_bytes_push_back(commands_add, kCommandTypeStartFirst + i));
          *pos += 8;
          break;
        }
      }
    }
  }

  if (!(jxl_padded_bytes_empty(commands_add) && jxl_padded_bytes_empty(data_add)) ||
      *pos == size) {
    if (*last0 < last1) {
      JXL_RETURN_IF_ERROR(jxl_padded_bytes_push_back(commands, kCommandInsert));
      JXL_RETURN_IF_ERROR(jxl_encode_var_int(last1 - *last0, commands));
      while (*last0 < last1) {
        JXL_RETURN_IF_ERROR(jxl_padded_bytes_push_back(data, icc[(*last0)++]));
      }
    }
    for (size_t b_i = 0; b_i < jxl_padded_bytes_size(commands_add); ++b_i) {
      uint8_t b = *jxl_padded_bytes_at(commands_add, b_i);
      JXL_RETURN_IF_ERROR(jxl_padded_bytes_push_back(commands, b));
    }
    for (size_t b_i = 0; b_i < jxl_padded_bytes_size(data_add); ++b_i) {
      uint8_t b = *jxl_padded_bytes_at(data_add, b_i);
      JXL_RETURN_IF_ERROR(jxl_padded_bytes_push_back(data, b));
    }
    *last0 = *pos;
  }
  if (jxl_padded_bytes_empty(commands_add) && jxl_padded_bytes_empty(data_add)) {
    (*pos)++;
  }
  return jxl_enc_ok_status();
}

jxl_enc_status jxl_predict_icc_body(const uint8_t* icc, size_t size, jxl_padded_bytes* result,
                      jxl_padded_bytes* commands, jxl_padded_bytes* data,
                      jxl_padded_bytes* header, jxl_array_tag* tags,
                      jxl_array_size* tagstarts, jxl_array_size* tagsizes) {
  jxl_context* ctx = jxl_padded_bytes_ctx(result);

  JXL_STATIC_ASSERT(sizeof(size_t) >= 4, "size_t is too short");
  // Fuzzer expects that jxl_predict_icc can accept any input,
  // but 1GB should be enough for any purpose.
  if (size > kSizeLimit) {
    return JXL_FAILURE("ICC profile is too large");
  }

  JXL_RETURN_IF_ERROR(jxl_encode_var_int(size, result));

  // Header
  {
    jxl_icc_header_bytes predicted = jxl_icc_initial_header_prediction(size);
    JXL_RETURN_IF_ERROR(jxl_padded_bytes_append(
        header, jxl_icc_header_bytes_data(&predicted),
        jxl_icc_header_bytes_data(&predicted) + kICCHeaderSize));
  }
  for (size_t i = 0; i < kICCHeaderSize && i < size; i++) {
    jxl_icc_predict_header(icc, size, jxl_padded_bytes_data(header), i);
    JXL_RETURN_IF_ERROR(
        jxl_padded_bytes_push_back(data, icc[i] - *jxl_padded_bytes_at(header, i)));
  }
  if (size <= kICCHeaderSize) {
    JXL_RETURN_IF_ERROR(jxl_encode_var_int(0, result));  // 0 commands
    for (size_t b_i = 0; b_i < jxl_padded_bytes_size(data); ++b_i) {
      uint8_t b = *jxl_padded_bytes_at(data, b_i);
      JXL_RETURN_IF_ERROR(jxl_padded_bytes_push_back(result, b));
    }
    return jxl_enc_ok_status();
  }

  // jxl_tag list
  size_t pos = kICCHeaderSize;
  if (pos + 4 <= size) {
    uint64_t numtags = jxl_decode_uint32(icc, size, pos);
    pos += 4;
    JXL_RETURN_IF_ERROR(jxl_encode_var_int(numtags + 1, commands));
    uint64_t prevtagstart = kICCHeaderSize + numtags * 12;
    uint32_t prevtagsize = 0;
    for (size_t i = 0; i < numtags; i++) {
      if (pos + 12 > size) break;

      jxl_tag tag = jxl_decode_keyword(icc, size, pos + 0);
      uint32_t tagstart = jxl_decode_uint32(icc, size, pos + 4);
      uint32_t tagsize = jxl_decode_uint32(icc, size, pos + 8);
      pos += 12;

      if (!jxl_enc_status_ok(jxl_array_tag_push_back(tags, tag))) JXL_CRASH();
      JXL_RETURN_IF_ERROR(jxl_array_size_push_back(tagstarts, (size_t)(tagstart)));
      JXL_RETURN_IF_ERROR(jxl_array_size_push_back(tagsizes, (size_t)(tagsize)));

      uint8_t tagcode = kCommandTagUnknown;
      for (size_t j = 0; j < kNumTagStrings; j++) {
        if (jxl_tag_equal(&tag, kTagStrings[j])) {
          tagcode = j + kCommandTagStringFirst;
          break;
        }
      }

      if (jxl_tag_equal(&tag, &kRtrcTag) && pos + 24 < size) {
        bool ok = true;
        {
          jxl_tag t0 = jxl_decode_keyword(icc, size, pos + 0);
          jxl_tag t1 = jxl_decode_keyword(icc, size, pos + 12);
          ok &= jxl_tag_equal(&t0, &kGtrcTag);
          ok &= jxl_tag_equal(&t1, &kBtrcTag);
        }
        if (ok) {
          for (size_t kk = 0; kk < 8; kk++) {
            if (icc[pos - 8 + kk] != icc[pos + 4 + kk]) ok = false;
            if (icc[pos - 8 + kk] != icc[pos + 16 + kk]) ok = false;
          }
        }
        if (ok) {
          tagcode = kCommandTagTRC;
          pos += 24;
          i += 2;
        }
      }

      if (jxl_tag_equal(&tag, &kRxyzTag) && pos + 24 < size) {
        bool ok = true;
        {
          jxl_tag t0 = jxl_decode_keyword(icc, size, pos + 0);
          jxl_tag t1 = jxl_decode_keyword(icc, size, pos + 12);
          ok &= jxl_tag_equal(&t0, &kGxyzTag);
          ok &= jxl_tag_equal(&t1, &kBxyzTag);
        }
        uint32_t offsetr = tagstart;
        uint32_t offsetg = jxl_decode_uint32(icc, size, pos + 4);
        uint32_t offsetb = jxl_decode_uint32(icc, size, pos + 16);
        uint32_t sizer = tagsize;
        uint32_t sizeg = jxl_decode_uint32(icc, size, pos + 8);
        uint32_t sizeb = jxl_decode_uint32(icc, size, pos + 20);
        ok &= sizer == 20;
        ok &= sizeg == 20;
        ok &= sizeb == 20;
        ok &= (offsetg == offsetr + 20);
        ok &= (offsetb == offsetr + 40);
        if (ok) {
          tagcode = kCommandTagXYZ;
          pos += 24;
          i += 2;
        }
      }

      uint8_t command = tagcode;
      uint64_t predicted_tagstart = prevtagstart + prevtagsize;
      if (predicted_tagstart != tagstart) command |= kFlagBitOffset;
      size_t predicted_tagsize = prevtagsize;
      if (jxl_tag_equal(&tag, &kRxyzTag) || jxl_tag_equal(&tag, &kGxyzTag) ||
          jxl_tag_equal(&tag, &kBxyzTag) || jxl_tag_equal(&tag, &kKxyzTag) ||
          jxl_tag_equal(&tag, &kWtptTag) || jxl_tag_equal(&tag, &kBkptTag) ||
          jxl_tag_equal(&tag, &kLumiTag)) {
        predicted_tagsize = 20;
      }
      if (predicted_tagsize != tagsize) command |= kFlagBitSize;
      JXL_RETURN_IF_ERROR(jxl_padded_bytes_push_back(commands, command));
      if (tagcode == 1) {
        JXL_RETURN_IF_ERROR(jxl_append_keyword(&tag, data));
      }
      if (command & kFlagBitOffset)
        JXL_RETURN_IF_ERROR(jxl_encode_var_int(tagstart, commands));
      if (command & kFlagBitSize)
        JXL_RETURN_IF_ERROR(jxl_encode_var_int(tagsize, commands));

      prevtagstart = tagstart;
      prevtagsize = tagsize;
    }
  }
  // Indicate end of tag list or varint indicating there's none
  JXL_RETURN_IF_ERROR(jxl_padded_bytes_push_back(commands, 0));

  // Main content
  jxl_tag tag;
  jxl_tag_construct_empty(&tag);
  size_t tagstart = 0;
  size_t tagsize = 0;
  size_t clutstart = 0;

  size_t last0 = pos;
  while (pos <= size) {
    jxl_padded_bytes commands_add;
    jxl_padded_bytes data_add;
    jxl_padded_bytes_make(ctx, &commands_add);
    jxl_padded_bytes_make(ctx, &data_add);
    jxl_enc_status step_status = jxl_predict_icc_main_step(
        ctx, icc, size, &pos, &tag, &tagstart, &tagsize, &clutstart,
        &last0, commands, data, tagstarts, tagsizes, &commands_add, &data_add);
    jxl_padded_bytes_destroy(&commands_add);
    jxl_padded_bytes_destroy(&data_add);
    if (!jxl_enc_status_ok(step_status)) return step_status;
  }

  JXL_RETURN_IF_ERROR(jxl_encode_var_int(jxl_padded_bytes_size(commands), result));
  for (size_t b_i = 0; b_i < jxl_padded_bytes_size(commands); ++b_i) {
    uint8_t b = *jxl_padded_bytes_at(commands, b_i);
    JXL_RETURN_IF_ERROR(jxl_padded_bytes_push_back(result, b));
  }
  for (size_t b_i = 0; b_i < jxl_padded_bytes_size(data); ++b_i) {
    uint8_t b = *jxl_padded_bytes_at(data, b_i);
    JXL_RETURN_IF_ERROR(jxl_padded_bytes_push_back(result, b));
  }

  return jxl_enc_ok_status();
}

jxl_enc_status jxl_predict_icc(const uint8_t* icc, size_t size, jxl_padded_bytes* result) {
  jxl_context* ctx = jxl_padded_bytes_ctx(result);
  jxl_padded_bytes commands;
  jxl_padded_bytes data;
  jxl_padded_bytes header;
  jxl_padded_bytes_make(ctx, &commands);
  jxl_padded_bytes_make(ctx, &data);
  jxl_padded_bytes_make(ctx, &header);
  jxl_array_tag tags;
  jxl_array_construct_empty(&tags, ctx);
  jxl_array_size tagstarts;
  jxl_array_construct_empty(&tagstarts, ctx);
  jxl_array_size tagsizes;
  jxl_array_construct_empty(&tagsizes, ctx);
  jxl_enc_status status = jxl_predict_icc_body(icc, size, result, &commands, &data, &header,
                                 &tags, &tagstarts, &tagsizes);
  jxl_padded_bytes_destroy(&commands);
  jxl_padded_bytes_destroy(&data);
  jxl_padded_bytes_destroy(&header);
  jxl_array_destroy(&tags);
  jxl_array_destroy(&tagstarts);
  jxl_array_destroy(&tagsizes);
  return status;
}

typedef struct jxl_write_size_ctx {
  size_t size;
  jxl_bit_writer* writer;
} jxl_write_size_ctx;

static jxl_enc_status jxl_write_icc_size_body(void* opaque) {
  jxl_write_size_ctx* c = (jxl_write_size_ctx*)(opaque);
  return jxl_u64_coder_write(c->size, c->writer);
}

jxl_enc_status jxl_write_icc_body(const jxl_bytes* icc, jxl_bit_writer* JXL_RESTRICT writer,
                    jxl_layer_type layer, jxl_token_streams* tokens) {
  jxl_context* ctx = jxl_bit_writer_ctx(writer);
  jxl_padded_bytes enc;
  jxl_padded_bytes_make(ctx, &enc);
  jxl_enc_status status = jxl_predict_icc(jxl_bytes_data(icc), jxl_bytes_size(icc), &enc);
  if (!jxl_enc_status_ok(status)) {
    jxl_padded_bytes_destroy(&enc);
    return status;
  }
  jxl_write_size_ctx size_ctx = {jxl_padded_bytes_size(&enc), writer};
  status =
      jxl_bit_writer_with_max_bits(writer, 128, layer, jxl_write_icc_size_body, &size_ctx);
  if (!jxl_enc_status_ok(status)) {
    jxl_padded_bytes_destroy(&enc);
    return status;
  }

  for (size_t i = 0; i < jxl_padded_bytes_size(&enc); i++) {
    uint32_t ctx = (uint32_t)(jxl_iccans_context(
        i, i > 0 ? *jxl_padded_bytes_at(&enc, i - 1) : 0,
        i > 1 ? *jxl_padded_bytes_at(&enc, i - 2) : 0));
    if (!jxl_enc_status_ok(jxl_array_token_push_back(jxl_token_streams_at(tokens, 0),
                                jxl_token_make(ctx, *jxl_padded_bytes_at(&enc, i))))) {
      JXL_CRASH();
    }
  }
  jxl_histogram_params params;
  jxl_histogram_params_construct_empty(&params);
  params.lz77_method =
      jxl_padded_bytes_size(&enc) < 16384 ? kLZ77Optimal : kLZ77;
  jxl_entropy_encoding_data code;
  jxl_entropy_encoding_data_init(&code, ctx);
  params.force_huffman = true;
  jxl_array_size empty_widths;
  jxl_array_construct_empty(&empty_widths, ctx);
  size_t cost;
  status = jxl_build_and_encode_histograms(
      ctx, &params, kNumICCContexts, tokens, &code, writer, layer,
      &empty_widths, &cost);
  if (!jxl_enc_status_ok(status)) {
    jxl_array_destroy(&empty_widths);
    jxl_entropy_encoding_data_destroy(&code);
    jxl_padded_bytes_destroy(&enc);
    return status;
  }
  (void)cost;
  status = jxl_write_tokens(jxl_token_streams_at(tokens, 0), &code, 0, writer, layer);
  jxl_array_destroy(&empty_widths);
  jxl_entropy_encoding_data_destroy(&code);
  jxl_padded_bytes_destroy(&enc);
  return status;
}


jxl_enc_status jxl_write_icc(const jxl_bytes* icc, jxl_bit_writer* JXL_RESTRICT writer,
                jxl_layer_type layer) {
  if (jxl_bytes_is_empty(icc)) return JXL_FAILURE("ICC must be non-empty");
  jxl_context* ctx = jxl_bit_writer_ctx(writer);
  jxl_token_streams tokens;
  jxl_token_streams_create(&tokens, 1, ctx);
  jxl_enc_status status = jxl_write_icc_body(icc, writer, layer, &tokens);
  jxl_token_streams_destroy(&tokens);
  return status;
}
