// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "lib/jxl/enc_bit_writer.h"

#include <stdint.h>
#include <string.h>  // memcpy

#include "lib/jxl/base/common.h"
#include "lib/jxl/base/compiler_specific.h"
#include "lib/jxl/base/span.h"
#include "lib/jxl/base/status.h"
#include "lib/jxl/layer_type.h"


jxl_status jxl_bit_writer_allotment_init(jxl_bit_writer_allotment* self,
                              jxl_bit_writer* JXL_RESTRICT writer) {
  self->prev_bits_written_ = jxl_bit_writer_bits_written(writer);
  const size_t prev_bytes = jxl_padded_bytes_size(&writer->storage_);
  const size_t next_bytes = jxl_div_ceil(self->max_bits_, kBitsPerByte);
  if (!jxl_status_ok(jxl_padded_bytes_resize(&writer->storage_, prev_bytes + next_bytes))) {
    self->called_ = true;
    return jxl_error_status();
  }
  self->parent_ = writer->current_allotment_;
  writer->current_allotment_ = self;
  return jxl_ok_status();
}

void jxl_bit_writer_allotment_destroy(jxl_bit_writer_allotment* self) {
  if (self == NULL) return;
  if (!self->called_) {
    // Not calling is a bug - unused storage will not be reclaimed.
    JXL_DEBUG_ABORT("Did not call jxl_bit_writer_allotment_reclaim");
  }
}

jxl_status jxl_bit_writer_allotment_reclaim(jxl_bit_writer_allotment* self,
                                 jxl_bit_writer* JXL_RESTRICT writer) {
  JXL_DASSERT(!self->called_);  // Do not call twice
  self->called_ = true;
  if (writer == NULL) return jxl_ok_status();

  JXL_DASSERT(jxl_bit_writer_bits_written(writer) >= self->prev_bits_written_);
  const size_t used_bits =
      jxl_bit_writer_bits_written(writer) - self->prev_bits_written_;
  JXL_DASSERT(used_bits <= self->max_bits_);
  const size_t unused_bits = self->max_bits_ - used_bits;

  // Reclaim unused whole bytes from writer's allotment.
  const size_t unused_bytes = unused_bits / kBitsPerByte;  // truncate
  JXL_ENSURE(jxl_padded_bytes_size(&writer->storage_) >= unused_bytes);
  JXL_RETURN_IF_ERROR(jxl_padded_bytes_resize(
      &writer->storage_, jxl_padded_bytes_size(&writer->storage_) - unused_bytes));
  writer->current_allotment_ = self->parent_;
  // Ensure we don't also charge the parent for these bits.
  jxl_bit_writer_allotment* parent = self->parent_;
  while (parent != NULL) {
    parent->prev_bits_written_ += used_bits;
    parent = parent->parent_;
  }
  return jxl_ok_status();
}

jxl_status jxl_bit_writer_append_byte_aligned(jxl_bit_writer* self, const jxl_bit_writer* others,
                                  size_t num) {
  // Total size to add so we can preallocate
  size_t other_bytes = 0;
  for (size_t i = 0; i < num; ++i) {
    JXL_ENSURE(jxl_bit_writer_bits_written(&others[i]) % kBitsPerByte == 0);
    other_bytes += jxl_div_ceil(jxl_bit_writer_bits_written(&others[i]), kBitsPerByte);
  }
  if (other_bytes == 0) {
    // No bytes to append: this happens for example when creating per-group
    // storage for groups, but not writing anything in them for e.g. lossless
    // images with no alpha. Do nothing.
    return jxl_ok_status();
  }
  JXL_RETURN_IF_ERROR(jxl_padded_bytes_resize(
      &self->storage_,
      jxl_padded_bytes_size(&self->storage_) + other_bytes + 1));  // extra zero padding

  // Concatenate by copying bytes because both source and destination are bytes.
  JXL_ENSURE(jxl_bit_writer_bits_written(self) % kBitsPerByte == 0);
  size_t pos = jxl_div_ceil(jxl_bit_writer_bits_written(self), kBitsPerByte);
  for (size_t i = 0; i < num; ++i) {
    const jxl_bytes span = jxl_bit_writer_get_span(&others[i]);
    memcpy(jxl_padded_bytes_data(&self->storage_) + pos, jxl_bytes_data(&span),
           jxl_bytes_size(&span));
    pos += jxl_bytes_size(&span);
  }
  JXL_ENSURE(pos < jxl_padded_bytes_size(&self->storage_));
  *jxl_padded_bytes_at(&self->storage_, pos++) = 0;  // for next Write
  self->bits_written_ += other_bytes * kBitsPerByte;
  return jxl_ok_status();
}

// Example: let's assume that 3 bits (Rs below) have been written already:
// BYTE+0       BYTE+1       BYTE+2
// 0000 0RRR    ???? ????    ???? ????
//
// Now, we could write up to 5 bits by just shifting them left by 3 bits and
// OR'ing to BYTE-0.
//
// For n > 5 bits, we write the lowest 5 bits as above, then write the next
// lowest bits into BYTE+1 starting from its lower bits and so on.
void jxl_bit_writer_write(jxl_bit_writer* self, size_t n_bits, uint64_t bits) {
  JXL_DASSERT((bits >> n_bits) == 0);
  JXL_DASSERT(n_bits <= kBitWriterMaxBitsPerCall);
  size_t bytes_written = self->bits_written_ / kBitsPerByte;
  uint8_t* p = jxl_padded_bytes_at(&self->storage_, bytes_written);
  const size_t bits_in_first_byte = self->bits_written_ % kBitsPerByte;
  bits <<= bits_in_first_byte;
#if JXL_BYTE_ORDER_LITTLE
  uint64_t v = *p;
  // Last (partial) or next byte to write must be zero-initialized!
  // jxl_padded_bytes initializes the first, and Write/Append maintain this.
  JXL_DASSERT(v >> bits_in_first_byte == 0);
  v |= bits;
  memcpy(p, &v, sizeof(v));  // Write bytes: possibly more than n_bits/8
#else
  *p++ |= (uint8_t)(bits & 0xFF);
  for (size_t bits_left_to_write = n_bits + bits_in_first_byte;
       bits_left_to_write >= 9; bits_left_to_write -= 8) {
    bits >>= 8;
    *p++ = (uint8_t)(bits & 0xFF);
  }
  *p = 0;
#endif
  self->bits_written_ += n_bits;
}
