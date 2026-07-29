// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in LICENSE-BSD.

#include "compressed_dc.h"

#include <stdint.h>
#include <string.h>

#include "ac_context.h"
#include "base/rect.h"
#include "enc_frame_header.h"
#include "enc_image.h"
#include "modular/modular_image.h"


void jxl_fill_quant_dc(const jxl_rect* r, jxl_image_b* quant_dc, const jxl_image* in,
                 const jxl_y_cb_cr_chroma_subsampling* chroma_subsampling,
                 const jxl_block_ctx_map* bctx){
  if (bctx->num_dc_ctxs <= 1) {
    for (size_t y = 0; y < jxl_rect_y_size(r); y++) {
      uint8_t* qdc_row = jxl_rect_row_b(r, quant_dc, y);
      memset(qdc_row, 0, sizeof(*qdc_row) * jxl_rect_x_size(r));
    }
    return;
  }

  JXL_DASSERT(jxl_rect_y_size(r) == 0 ||
              (jxl_rect_y_size(r) - 1) >> jxl_y_cb_cr_chroma_subsampling_v_shift(chroma_subsampling, 0) <
                  jxl_image_i_y_size(&jxl_channels_at_const(&in->channel, 1)->plane));
  JXL_DASSERT(jxl_rect_y_size(r) == 0 ||
              (jxl_rect_y_size(r) - 1) >> jxl_y_cb_cr_chroma_subsampling_v_shift(chroma_subsampling, 1) <
                  jxl_image_i_y_size(&jxl_channels_at_const(&in->channel, 0)->plane));
  JXL_DASSERT(jxl_rect_y_size(r) == 0 ||
              (jxl_rect_y_size(r) - 1) >> jxl_y_cb_cr_chroma_subsampling_v_shift(chroma_subsampling, 2) <
                  jxl_image_i_y_size(&jxl_channels_at_const(&in->channel, 2)->plane));
  for (size_t y = 0; y < jxl_rect_y_size(r); y++) {
    uint8_t* qdc_row_val = jxl_rect_row_b(r, quant_dc, y);
    const int32_t* quant_row_x =
        jxl_image_i_const_row(&jxl_channels_at_const(&in->channel, 1)->plane, y >> jxl_y_cb_cr_chroma_subsampling_v_shift(chroma_subsampling, 0));
    const int32_t* quant_row_y =
        jxl_image_i_const_row(&jxl_channels_at_const(&in->channel, 0)->plane, y >> jxl_y_cb_cr_chroma_subsampling_v_shift(chroma_subsampling, 1));
    const int32_t* quant_row_b =
        jxl_image_i_const_row(&jxl_channels_at_const(&in->channel, 2)->plane, y >> jxl_y_cb_cr_chroma_subsampling_v_shift(chroma_subsampling, 2));
    for (size_t x = 0; x < jxl_rect_x_size(r); x++) {
      int bucket_x = 0;
      int bucket_y = 0;
      int bucket_b = 0;
      for (size_t t_i = 0; t_i < jxl_array_len(&bctx->dc_thresholds[0]); ++t_i) {
        int t = *jxl_array_at_const(&bctx->dc_thresholds[0], t_i);
        if (quant_row_x[x >> jxl_y_cb_cr_chroma_subsampling_h_shift(chroma_subsampling, 0)] > t) bucket_x++;
      }
      for (size_t t_i = 0; t_i < jxl_array_len(&bctx->dc_thresholds[1]); ++t_i) {
        int t = *jxl_array_at_const(&bctx->dc_thresholds[1], t_i);
        if (quant_row_y[x >> jxl_y_cb_cr_chroma_subsampling_h_shift(chroma_subsampling, 1)] > t) bucket_y++;
      }
      for (size_t t_i = 0; t_i < jxl_array_len(&bctx->dc_thresholds[2]); ++t_i) {
        int t = *jxl_array_at_const(&bctx->dc_thresholds[2], t_i);
        if (quant_row_b[x >> jxl_y_cb_cr_chroma_subsampling_h_shift(chroma_subsampling, 2)] > t) bucket_b++;
      }
      int bucket = bucket_x;
      bucket *= jxl_array_len(&bctx->dc_thresholds[2]) + 1;
      bucket += bucket_b;
      bucket *= jxl_array_len(&bctx->dc_thresholds[1]) + 1;
      bucket += bucket_y;
      qdc_row_val[x] = bucket;
    }
  }
}
