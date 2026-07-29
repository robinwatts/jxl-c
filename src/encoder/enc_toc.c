// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in LICENSE-BSD.

#include "enc_toc.h"

#include <stddef.h>

#include "base/enc_status.h"
#include "layer_type.h"
#include "fields.h"
#include "toc_fields.h"


static jxl_enc_status jxl_write_toc_permutation_body(void* opaque) {
  jxl_bit_writer* writer = (jxl_bit_writer*)(opaque);
  jxl_bit_writer_write(writer, 1, 0);  // no permutation
  jxl_bit_writer_zero_pad_to_byte(writer);  // before TOC entries
  return jxl_enc_ok_status();
}

typedef struct jxl_write_toc_sizes_ctx {
  const jxl_array_size* group_sizes;
  jxl_bit_writer* writer;
} jxl_write_toc_sizes_ctx;

static jxl_enc_status jxl_write_toc_sizes_body(void* opaque) {
  jxl_write_toc_sizes_ctx* ctx = (jxl_write_toc_sizes_ctx*)(opaque);
  for (size_t group_size_i = 0; group_size_i < jxl_array_len(ctx->group_sizes); ++group_size_i) {
    size_t group_size = *jxl_array_at_const(ctx->group_sizes, group_size_i);
    JXL_RETURN_IF_ERROR(jxl_u32_coder_write(jxl_toc_dist(), group_size, ctx->writer));
  }
  jxl_bit_writer_zero_pad_to_byte(ctx->writer);  // before first group
  return jxl_enc_ok_status();
}

jxl_enc_status jxl_write_toc_permutation(jxl_bit_writer* JXL_RESTRICT writer) {
  return jxl_bit_writer_with_max_bits(writer, jxl_max_bits(0), kLayerToc, jxl_write_toc_permutation_body,
                             writer);
}

jxl_enc_status jxl_write_toc_sizes(const jxl_array_size* group_sizes,
                     jxl_bit_writer* JXL_RESTRICT writer){
  jxl_write_toc_sizes_ctx ctx = {group_sizes, writer};
  return jxl_bit_writer_with_max_bits(writer, jxl_max_bits(jxl_array_len(group_sizes)), kLayerToc,
                             jxl_write_toc_sizes_body, &ctx);
}

