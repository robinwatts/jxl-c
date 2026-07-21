// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "lib/jxl/enc_frame.h"

#include <jxl/memory_manager.h>

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "lib/jxl/ac_strategy.h"
#include "lib/jxl/base/bits.h"
#include "lib/jxl/base/status.h"
#include "lib/jxl/coeff_order.h"
#include "lib/jxl/dct_util.h"
#include "lib/jxl/enc_ans.h"
#include "lib/jxl/layer_type.h"
#include "lib/jxl/enc_bit_writer.h"
#include "lib/jxl/enc_chroma_from_luma.h"
#include "lib/jxl/enc_coeff_order.h"
#include "lib/jxl/enc_context_map.h"
#include "lib/jxl/enc_entropy_coder.h"
#include "lib/jxl/enc_fields.h"
#include "lib/jxl/enc_modular.h"
#include "lib/jxl/enc_quant_weights.h"
#include "lib/jxl/enc_toc.h"
#include "lib/jxl/fields.h"
#include "lib/jxl/jpeg/enc_jpeg_data.h"
#include "lib/jxl/jpeg/jpeg_data.h"
#include "lib/jxl/toc.h"
#include "lib/jxl/base/common.h"


jxl_status jxl_make_frame_header(const jxl_frame_info* frame_info,
                       const jxl_jpeg_data* jpeg_data,
                       jxl_frame_header* JXL_RESTRICT frame_header){
  frame_header->is_last = frame_info->is_last;
  frame_header->passes.num_passes = 1;

  frame_header->encoding = kVarDCT;
  frame_header->x_qm_scale = 2;
  frame_header->b_qm_scale = 2;
  JXL_RETURN_IF_ERROR(jxl_set_chroma_subsampling_from_jpeg_data(
      jpeg_data, &frame_header->chroma_subsampling));
  JXL_RETURN_IF_ERROR(jxl_set_color_transform_from_jpeg_data(
      jpeg_data, &frame_header->color_transform));

  if (frame_header->color_transform != kColorTransformYCbCr &&
      (jxl_y_cb_cr_chroma_subsampling_max_h_shift(&frame_header->chroma_subsampling) != 0 ||
       jxl_y_cb_cr_chroma_subsampling_max_v_shift(&frame_header->chroma_subsampling) != 0)) {
    return JXL_FAILURE(
        "Chroma subsampling is not supported when color transform is not "
        "YCbCr");
  }

  frame_header->flags = kSkipAdaptiveDCSmoothing;
  frame_header->loop_filter.gab = false;
  frame_header->loop_filter.epf_iters = 0;

  // VisitFields nests never entered for JPEG (gates still write bits).
  JXL_DASSERT(frame_header->frame_type == kRegularFrame);
  JXL_DASSERT(frame_header->encoding == kVarDCT);
  JXL_DASSERT(frame_header->passes.num_passes == 1);
  JXL_DASSERT(frame_header->flags == kSkipAdaptiveDCSmoothing);
  JXL_DASSERT(!frame_header->loop_filter.gab);
  JXL_DASSERT(frame_header->loop_filter.epf_iters == 0);
  JXL_DASSERT(!frame_header->custom_size_or_origin);
  JXL_DASSERT(frame_header->upsampling == 1);
  JXL_DASSERT(frame_header->blending_info.mode == kReplace);
  JXL_DASSERT(jxl_array_empty(&frame_header->extra_channel_upsampling));
  JXL_DASSERT(jxl_blending_infos_empty(&frame_header->extra_channel_blending_info));
  JXL_DASSERT(frame_header->save_as_reference == 0);
  return jxl_ok_status();
}

static void jxl_find_avg_index_of_sum_maximum256(const int32_t* array, int* idx,
                                 int32_t* sum) {
  int32_t maxval = 0;
  int32_t val = 0;
  int maxidx_begin = 0;
  int maxidx_end = 0;
  for (size_t i = 0; i < 256; ++i) {
    val += array[i];
    if (val > maxval) {
      maxval = val;
      maxidx_begin = (int)(i);
    }
    if (val == maxval) {
      maxidx_end = (int)(i);
    }
  }
  *idx = (maxidx_begin + maxidx_end + 1) >> 1;
  *sum = maxval;
}

static const int16_t* jxl_jpeg_coeff_row(const jxl_jpeg_data* jpeg_data,
                            const int* jpeg_c_map, size_t c, size_t y) {
  return jxl_array_data_const(&jpeg_data->component_coeffs[jpeg_c_map[c]]) +
         jxl_array_at_const(&jpeg_data->components, jpeg_c_map[c])->width_in_blocks * kDCTBlockSize * y;
}

static jxl_bit_writer* jxl_group_code_writer(jxl_bit_writers* group_codes, bool is_small_image,
                           size_t index) {
  return jxl_bit_writers_at(group_codes, is_small_image ? 0 : index);
}

static void jxl_compute_jpeg_transcoding_data_cleanup(jxl_array_quant_encoding* qe,
                                       jxl_array_int* raw_qtables, jxl_array_int* qt,
                                       jxl_array_i32* scaled_qtable,
                                       jxl_array_size* dc_counts, jxl_image3_f* dc) {
  if (qe != NULL) jxl_array_destroy(qe);
  if (raw_qtables != NULL) {
    for (size_t i = 0; i < kNumQuantTables; ++i) {
      jxl_array_destroy(&raw_qtables[i]);
    }
  }
  if (qt != NULL) jxl_array_destroy(qt);
  if (scaled_qtable != NULL) jxl_array_destroy(scaled_qtable);
  if (dc_counts != NULL) jxl_array_destroy(dc_counts);
  if (dc != NULL) jxl_image3_f_destroy(dc);
}

static jxl_status jxl_compute_jpeg_transcoding_data(const jxl_jpeg_data* jpeg_data,
                                  const jxl_frame_header* frame_header,
                                  jxl_modular_frame_encoder* enc_modular,
                                  jxl_passes_encoder_state* enc_state){
  jxl_passes_shared_state* shared = &enc_state->shared;
  jxl_memory_manager* memory_manager = jxl_passes_encoder_state_memory_manager(enc_state);
  const jxl_frame_dimensions* frame_dim = &shared->frame_dim;

  const size_t xsize = frame_dim->xsize_padded;
  const size_t ysize = frame_dim->ysize_padded;
  const size_t xsize_blocks = frame_dim->xsize_blocks;
  const size_t ysize_blocks = frame_dim->ysize_blocks;

  // no-op chroma from luma
  JXL_RETURN_IF_ERROR(jxl_color_correlation_map_create(memory_manager, xsize, ysize,
                                                    &shared->cmap));
  jxl_ac_strategy_image_fill_dct8(&shared->ac_strategy);
  jxl_fill_image_b((uint8_t)(0), &shared->epf_sharpness);

  JXL_RETURN_IF_ERROR(jxl_ac_image_make(memory_manager, kGroupDim * kGroupDim,
                                      frame_dim->num_groups,
                                      &enc_state->coeffs));

  // convert JPEG quantization table to a jxl_quantizer object
  float dcquantization[3];
  jxl_array_quant_encoding qe;
  jxl_array_construct_empty(&qe, memory_manager);
  {
    jxl_quant_encoding init = jxl_quant_encoding_library(0);
    jxl_status status = jxl_array_quant_encoding_resize_fill(&qe, kNumQuantTables, init);
    if (!jxl_status_ok(status)) {
      jxl_compute_jpeg_transcoding_data_cleanup(&qe, NULL, NULL, NULL, NULL, NULL);
      return status;
    }
  }
  jxl_array_int raw_qtables[kNumQuantTables];
  for (size_t i = 0; i < kNumQuantTables; ++i) {
    jxl_array_construct_empty(&raw_qtables[i], memory_manager);
  }

  int jpeg_c_map[3];
  jxl_jpeg_order(frame_header->color_transform, jxl_array_len(&jpeg_data->components) == 1,
            jpeg_c_map);

  jxl_array_int qt;
  jxl_array_construct_empty(&qt, memory_manager);
  {
    jxl_status status = jxl_array_resize_zero(&qt, kDCTBlockSize * 3);
    if (!jxl_status_ok(status)) {
      jxl_compute_jpeg_transcoding_data_cleanup(&qe, raw_qtables, &qt, NULL, NULL, NULL);
      return status;
    }
  }
  int32_t qt_dc[3];
  for (size_t c = 0; c < 3; c++) {
    size_t jpeg_c = jpeg_c_map[c];
    const int32_t* quant =
        jxl_array_at_const(&jpeg_data->quant, jxl_array_at_const(&jpeg_data->components, jpeg_c)->quant_idx)->values;

    dcquantization[c] = 255 * 8.0f / quant[0];
    for (size_t y = 0; y < 8; y++) {
      for (size_t x = 0; x < 8; x++) {
        // JPEG XL transposes the DCT, JPEG doesn't.
        *jxl_array_at(&qt, kDCTBlockSize * c + 8 * x + y) = quant[8 * y + x];
      }
    }
    qt_dc[c] = *jxl_array_at(&qt, kDCTBlockSize * c);
  }
  {
    jxl_status status = jxl_dequant_matrices_set_custom_dc(&shared->matrices,
                                                 dcquantization);
    if (!jxl_status_ok(status)) {
      jxl_compute_jpeg_transcoding_data_cleanup(&qe, raw_qtables, &qt, NULL, NULL, NULL);
      return status;
    }
  }
  float dcquantization_r[3] = {1.0f / dcquantization[0],
                               1.0f / dcquantization[1],
                               1.0f / dcquantization[2]};

  // not transposed
  jxl_array_i32 scaled_qtable;
  jxl_array_construct_empty(&scaled_qtable, memory_manager);
  {
    jxl_status status = jxl_array_resize_zero(&scaled_qtable, kDCTBlockSize * 3);
    if (!jxl_status_ok(status)) {
      jxl_compute_jpeg_transcoding_data_cleanup(&qe, raw_qtables, &qt, &scaled_qtable,
                                        NULL, NULL);
      return status;
    }
  }
  for (size_t c = 0; c < 3; c++) {
    for (size_t y = 0; y < 8; y++) {
      for (size_t x = 0; x < 8; x++) {
        int coeffpos = y * 8 + x;
        int32_t ratio = (1 << kCFLFixedPointPrecision) *
                        *jxl_array_at(&qt, kDCTBlockSize + coeffpos) /
                        *jxl_array_at(&qt, kDCTBlockSize * c + coeffpos);
        if (ratio > kCFLFixedPointRatioMax) {
          jxl_compute_jpeg_transcoding_data_cleanup(&qe, raw_qtables, &qt, &scaled_qtable,
                                            NULL, NULL);
          return JXL_FAILURE(
              "Ratio of two entries in a JPEG quantization table is too large");
        }
        *jxl_array_at(&scaled_qtable, kDCTBlockSize * c + 8 * x + y) = ratio;
      }
    }
  }

  const size_t dct_idx = (size_t)(kAcStrategyDCT);
  *jxl_array_at(&qe, dct_idx) = jxl_quant_encoding_raw(0);
  if (!jxl_status_ok(jxl_array_copy_from(&raw_qtables[dct_idx], &qt))) {
    jxl_compute_jpeg_transcoding_data_cleanup(&qe, raw_qtables, &qt, &scaled_qtable,
                                      NULL, NULL);
    return JXL_FAILURE("raw_qtables copy failed");
  }
  {
    jxl_status status = jxl_dequant_matrices_set_custom(
      &shared->matrices, &qe, raw_qtables, enc_modular);
    if (!jxl_status_ok(status)) {
      jxl_compute_jpeg_transcoding_data_cleanup(&qe, raw_qtables, &qt, &scaled_qtable,
                                        NULL, NULL);
      return status;
    }
  }

  // Ensure that InvGlobalScale() is 1.
  jxl_quantizer_init_with(&shared->quantizer, &shared->matrices, 1, kGlobalScaleDenom);

  // Per-block dequant scaling should be 1.
  jxl_fill_image_i((int32_t)(jxl_quantizer_inv_global_scale(&shared->quantizer)),
             &shared->raw_quant_field);

  bool DCzero = (frame_header->color_transform == kColorTransformYCbCr);
  // Compute chroma-from-luma for AC (doesn't seem to be useful for DC)
  if (jxl_y_cb_cr_chroma_subsampling_is444(&frame_header->chroma_subsampling) &&
      enc_state->cparams.force_cfl_jpeg_recompression &&
      jxl_array_len(&jpeg_data->components) == 3) {
    static const size_t kChroma[] = {0, 2};
    for (size_t c_i = 0; c_i < 2; ++c_i) {
      size_t c = kChroma[c_i];
      jxl_image_sb* map = (c == 0 ? &shared->cmap.ytox_map : &shared->cmap.ytob_map);
      const float kScale = kDefaultColorFactor;
      const int kOffset = 127;
      const float kBase = c == 0 ? jxl_color_correlation_yto_x_ratio(jxl_color_correlation_map_base(&shared->cmap), 0)
                                 : jxl_color_correlation_yto_b_ratio(jxl_color_correlation_map_base(&shared->cmap), 0);
      const float kZeroThresh =
          kScale * kZeroBiasDefault[c] *
          0.9999f;  // just epsilon less for better rounding

      for (uint32_t ty = 0; ty < jxl_image_sb_y_size(map); ++ty) {
        int8_t* JXL_RESTRICT row_out = jxl_image_sb_row(map, ty);
        for (size_t tx = 0; tx < jxl_image_sb_x_size(map); ++tx) {
          const size_t y0 = ty * kColorTileDimInBlocks;
          const size_t x0 = tx * kColorTileDimInBlocks;
          const size_t y1 = JXL_MIN(frame_dim->ysize_blocks,
                                     (ty + 1) * kColorTileDimInBlocks);
          const size_t x1 = JXL_MIN(frame_dim->xsize_blocks,
                                     (tx + 1) * kColorTileDimInBlocks);
          int32_t d_num_zeros[257];
          memset(d_num_zeros, 0, sizeof(d_num_zeros));
          // TODO(veluca): this needs SIMD + fixed point adaptation, and/or
          // conversion to the new CfL algorithm.
          for (size_t y = y0; y < y1; ++y) {
            const int16_t* JXL_RESTRICT row_m = jxl_jpeg_coeff_row(jpeg_data, jpeg_c_map, 1, y);
            const int16_t* JXL_RESTRICT row_s = jxl_jpeg_coeff_row(jpeg_data, jpeg_c_map, c, y);
            for (size_t x = x0; x < x1; ++x) {
              for (size_t coeffpos = 1; coeffpos < kDCTBlockSize; coeffpos++) {
                const float scaled_m = row_m[x * kDCTBlockSize + coeffpos] *
                                       (1.0f / (1 << kCFLFixedPointPrecision)) *
                                       *jxl_array_at(&scaled_qtable, 64 * c + coeffpos);
                const float scaled_s =
                    kScale * row_s[x * kDCTBlockSize + coeffpos] +
                    (kOffset - kBase * kScale) * scaled_m;
                if (fabsf(scaled_m) > 1e-8f) {
                  float from;
                  float to;
                  if (scaled_m > 0) {
                    from = (scaled_s - kZeroThresh) / scaled_m;
                    to = (scaled_s + kZeroThresh) / scaled_m;
                  } else {
                    from = (scaled_s + kZeroThresh) / scaled_m;
                    to = (scaled_s - kZeroThresh) / scaled_m;
                  }
                  if (from < 0.0f) {
                    from = 0.0f;
                  }
                  if (to > 255.0f) {
                    to = 255.0f;
                  }
                  // Instead of clamping the both values
                  // we just check that range is sane.
                  if (from <= to) {
                    d_num_zeros[(int)(ceilf(from))]++;
                    d_num_zeros[(int)(floorf(to + 1))]--;
                  }
                }
              }
            }
          }
          int best = 0;
          int32_t best_sum = 0;
          jxl_find_avg_index_of_sum_maximum256(d_num_zeros, &best, &best_sum);
          int32_t offset_sum = 0;
          for (int i = 0; i <= kOffset; ++i) {
            offset_sum += d_num_zeros[i];
          }
          row_out[tx] = 0;
          if (best_sum > offset_sum + 1) {
            row_out[tx] = best - kOffset;
          }
        }
      }
    }
  }

  jxl_image3_f dc;
  jxl_image3_f_construct_empty(&dc);
  jxl_status status =
      jxl_image3_f_create(memory_manager, xsize_blocks, ysize_blocks, &dc);
  if (!jxl_status_ok(status)) {
    jxl_compute_jpeg_transcoding_data_cleanup(&qe, raw_qtables, &qt, &scaled_qtable,
                                      NULL, &dc);
    return status;
  }
  if (!jxl_y_cb_cr_chroma_subsampling_is444(&frame_header->chroma_subsampling)) {
    jxl_zero_fill_image3_f(&dc);
    jxl_ac_image_zero_fill(&enc_state->coeffs);
  }
  // JPEG DC is from -1024 to 1023.
  jxl_array_size dc_counts;
  jxl_array_construct_empty(&dc_counts, memory_manager);
  status = jxl_array_resize_zero(&dc_counts, 2048);
  if (!jxl_status_ok(status)) {
    jxl_compute_jpeg_transcoding_data_cleanup(&qe, raw_qtables, &qt, &scaled_qtable,
                                      &dc_counts, &dc);
    return status;
  }
  size_t total_dc[3];
  memset(total_dc, 0, sizeof(total_dc));
  static const size_t kChans[] = {1, 0, 2};
  for (size_t c_i = 0; c_i < 3; ++c_i) {
    size_t c = kChans[c_i];
    if (jxl_array_len(&jpeg_data->components) == 1 && c != 1) {
      jxl_ac_image_zero_fill_plane(&enc_state->coeffs, c);
      jxl_zero_fill_image_f(jxl_image3_f_plane(&dc, c));
      // Ensure no division by 0.
      total_dc[c] = 1;
      continue;
    }
    size_t hshift = jxl_y_cb_cr_chroma_subsampling_h_shift(&frame_header->chroma_subsampling, c);
    size_t vshift = jxl_y_cb_cr_chroma_subsampling_v_shift(&frame_header->chroma_subsampling, c);
    jxl_image_sb* map = (c == 0 ? &shared->cmap.ytox_map : &shared->cmap.ytob_map);
    for (size_t group_index = 0; group_index < frame_dim->num_groups;
         group_index++) {
      const size_t gx = group_index % frame_dim->xsize_groups;
      const size_t gy = group_index / frame_dim->xsize_groups;
      int32_t* coeff_row =
          jxl_ac_image_plane_row(&enc_state->coeffs, c, group_index, 0).ptr32;
      int32_t block[64];
      for (size_t by = gy * kGroupDimInBlocks;
           by < ysize_blocks && by < (gy + 1) * kGroupDimInBlocks; ++by) {
        if ((by >> vshift) << vshift != by) continue;
        const int16_t* JXL_RESTRICT inputjpeg = jxl_jpeg_coeff_row(jpeg_data, jpeg_c_map, c, by >> vshift);
        const int16_t* JXL_RESTRICT inputjpegY = jxl_jpeg_coeff_row(jpeg_data, jpeg_c_map, 1, by);
        float* JXL_RESTRICT fdc = jxl_image3_f_plane_row(&dc, c, by >> vshift);
        const int8_t* JXL_RESTRICT cm =
            jxl_image_sb_const_row(map, by / kColorTileDimInBlocks);
        for (size_t bx = gx * kGroupDimInBlocks;
             bx < xsize_blocks && bx < (gx + 1) * kGroupDimInBlocks; ++bx) {
          if ((bx >> hshift) << hshift != bx) continue;
          size_t base = (bx >> hshift) * kDCTBlockSize;
          int idc;
          if (DCzero) {
            idc = inputjpeg[base];
          } else {
            idc = inputjpeg[base] + 1024 / qt_dc[c];
          }
          if (c == 1) {
            (*jxl_array_at(&dc_counts, JXL_MIN(idc + 1024, 2047)))++;
          }
          total_dc[c]++;
          fdc[bx >> hshift] = idc * dcquantization_r[c];
          if (c == 1 || !enc_state->cparams.force_cfl_jpeg_recompression ||
              !jxl_y_cb_cr_chroma_subsampling_is444(&frame_header->chroma_subsampling)) {
            for (size_t y = 0; y < 8; y++) {
              for (size_t x = 0; x < 8; x++) {
                block[x * 8 + y] = inputjpeg[base + y * 8 + x];
              }
            }
          } else {
            const int32_t scale =
                jxl_color_correlation_ratio_jpeg(cm[bx / kColorTileDimInBlocks]);

            for (size_t y = 0; y < 8; y++) {
              for (size_t x = 0; x < 8; x++) {
                int coeffpos = y * 8 + x;
                int Y = inputjpegY[kDCTBlockSize * bx + coeffpos];
                int QChroma = inputjpeg[kDCTBlockSize * bx + coeffpos];
                // Fixed-point multiply of CfL scale with quant table ratio
                // first, and Y value second.
                int coeff_scale =
                    (scale * *jxl_array_at(&scaled_qtable, kDCTBlockSize * c + coeffpos) +
                     (1 << (kCFLFixedPointPrecision - 1))) >>
                    kCFLFixedPointPrecision;
                int cfl_factor =
                    (Y * coeff_scale + (1 << (kCFLFixedPointPrecision - 1))) >>
                    kCFLFixedPointPrecision;
                int QCR = QChroma - cfl_factor;
                block[x * 8 + y] = QCR;
              }
            }
          }
          memcpy(coeff_row, block, sizeof(block));
          coeff_row += kDCTBlockSize;
        }
      }
    }
  }

  jxl_array_int* dct = enc_state->shared.block_ctx_map.dc_thresholds;
  size_t* num_dc_ctxs = &enc_state->shared.block_ctx_map.num_dc_ctxs;

  for (size_t i = 0; i < 3; i++) {
jxl_array_clear(&dct[i]);
  }
  // use more contexts for larger and higher quality images
  int num_thresholds = jxl_ceil_log2_nonzero64(total_dc[1]) -
                       jxl_ceil_log2_nonzero32((unsigned)(
                           *jxl_array_at(&qt, 1) + *jxl_array_at(&qt, 2) + *jxl_array_at(&qt, 3) + *jxl_array_at(&qt, 4) + *jxl_array_at(&qt, 5))) -
                       7;
  // up to 8 buckets, based on luma only
  num_thresholds = jxl_clamp1_i(num_thresholds, 1, 7);
  size_t cumsum = 0;
  size_t cut = total_dc[1] / (num_thresholds + 1);
  for (int j = 0; j < 2048; j++) {
    cumsum += *jxl_array_at(&dc_counts, j);
    if (cumsum > cut) {
      status = jxl_array_int_push_back(&dct[1], j - 1025);
      if (!jxl_status_ok(status)) {
        jxl_compute_jpeg_transcoding_data_cleanup(&qe, raw_qtables, &qt, &scaled_qtable,
                                          &dc_counts, &dc);
        return status;
      }
      cut = total_dc[1] * (jxl_array_len(&dct[1]) + 1) / (num_thresholds + 1);
    }
  }
  *num_dc_ctxs = jxl_array_len(&dct[1]) + 1;

  jxl_array_u8* ctx_map = &enc_state->shared.block_ctx_map.ctx_map;
jxl_array_clear(ctx_map);
  status = jxl_array_resize_zero(ctx_map, 3 * kNumOrders * *num_dc_ctxs);
  if (!jxl_status_ok(status)) {
    jxl_compute_jpeg_transcoding_data_cleanup(&qe, raw_qtables, &qt, &scaled_qtable,
                                      &dc_counts, &dc);
    return status;
  }

  for (size_t i = 0; i < *num_dc_ctxs; i++) {
    // luma: one context per luma DC bucket
    *jxl_array_at(ctx_map, i) = i;
    if (jxl_array_len(&jpeg_data->components) == 1) {
      // grayscale -> one context for all chroma
      *jxl_array_at(ctx_map, kNumOrders * *num_dc_ctxs + i) =
          *jxl_array_at(ctx_map, 2 * kNumOrders * *num_dc_ctxs + i) = *num_dc_ctxs;
    } else {
      // color -> multiple contexts per chroma component
      *jxl_array_at(ctx_map, kNumOrders * *num_dc_ctxs + i) = *num_dc_ctxs + i / 2;
      *jxl_array_at(ctx_map, 2 * kNumOrders * *num_dc_ctxs + i) =
          *num_dc_ctxs + (*num_dc_ctxs - 1) / 2 + 1 + i / 2;
    }
  }
  enc_state->shared.block_ctx_map.num_ctxs =
      jxl_u8_max_element(ctx_map) + 1;

  JXL_ENSURE(enc_state->shared.block_ctx_map.num_ctxs <= 16);

  // disable DC frame for now
  for (uint32_t group_index = 0; group_index < shared->frame_dim.num_dc_groups;
       ++group_index) {
    const jxl_rect r = jxl_frame_dimensions_dc_group_rect(&enc_state->shared.frame_dim, group_index);
    status = jxl_modular_frame_encoder_add_var_dctdc(enc_modular, frame_header, &dc, &r,
                                            group_index, enc_state);
    if (!jxl_status_ok(status)) {
      jxl_compute_jpeg_transcoding_data_cleanup(&qe, raw_qtables, &qt, &scaled_qtable,
                                        &dc_counts, &dc);
      return status;
    }
    status =
        jxl_modular_frame_encoder_add_ac_metadata(enc_modular, &r, group_index, enc_state);
    if (!jxl_status_ok(status)) {
      jxl_compute_jpeg_transcoding_data_cleanup(&qe, raw_qtables, &qt, &scaled_qtable,
                                        &dc_counts, &dc);
      return status;
    }
  }

  jxl_compute_jpeg_transcoding_data_cleanup(&qe, raw_qtables, &qt, &scaled_qtable,
                                    &dc_counts, &dc);
  return jxl_ok_status();
}

static jxl_status jxl_compute_all_coeff_orders(jxl_passes_encoder_state* enc_state,
                             const jxl_frame_dimensions* frame_dim){
  jxl_rect used_orders_rect = jxl_rect_from_size(
      jxl_image_i_x_size(&enc_state->shared.raw_quant_field),
      jxl_image_i_y_size(&enc_state->shared.raw_quant_field));
  jxl_used_orders used_orders_info = jxl_compute_used_orders(
      enc_state->cparams.speed_tier, &enc_state->shared.ac_strategy,
      &used_orders_rect);
  JXL_RETURN_IF_ERROR(jxl_compute_coeff_order(
      jxl_passes_encoder_state_memory_manager(enc_state),
      enc_state->cparams.speed_tier, &enc_state->coeffs,
      &enc_state->shared.ac_strategy, frame_dim, &enc_state->used_orders,
      enc_state->used_acs, used_orders_info.used, used_orders_info.customize,
      jxl_array_at(&enc_state->shared.coeff_orders, 0)));
  enc_state->used_acs |= used_orders_info.used;
  return jxl_ok_status();
}

// Working area for jxl_tokenize_coefficients (per-group!)
typedef struct jxl_enc_cache {
  // jxl_tokenize_coefficients
  jxl_image3_i num_nzeroes;
} jxl_enc_cache;

static inline void jxl_enc_cache_construct_empty(jxl_enc_cache* self) {
  jxl_image3_i_construct_empty(&self->num_nzeroes);
}
static inline void jxl_enc_cache_destroy(jxl_enc_cache* self) {
  jxl_image3_i_destroy(&self->num_nzeroes);
}

// Allocates memory when first called.
static jxl_status jxl_enc_cache_init_once(jxl_enc_cache* self, jxl_memory_manager* memory_manager) {
  if (jxl_image3_i_x_size(&self->num_nzeroes) == 0) {
    JXL_RETURN_IF_ERROR(jxl_image3_i_create(memory_manager, kGroupDimInBlocks,
                                          kGroupDimInBlocks, &self->num_nzeroes));
  }
  return jxl_ok_status();
}

static jxl_status jxl_tokenize_all_coefficients(const jxl_frame_header* frame_header,
                               jxl_passes_encoder_state* enc_state){
  jxl_passes_shared_state* shared = &enc_state->shared;
  jxl_enc_cache group_cache;
  jxl_enc_cache_construct_empty(&group_cache);
  jxl_memory_manager* memory_manager = jxl_passes_encoder_state_memory_manager(enc_state);
  for (uint32_t group_index = 0; group_index < shared->frame_dim.num_groups;
       ++group_index) {
    // Tokenize coefficients.
    const jxl_rect rect = jxl_frame_dimensions_block_group_rect(&shared->frame_dim, group_index);
    const int32_t* JXL_RESTRICT ac_rows[3] = {
        jxl_ac_image_plane_row(&enc_state->coeffs, 0, group_index, 0).ptr32,
        jxl_ac_image_plane_row(&enc_state->coeffs, 1, group_index, 0).ptr32,
        jxl_ac_image_plane_row(&enc_state->coeffs, 2, group_index, 0).ptr32,
    };
    jxl_status status = jxl_enc_cache_init_once(&group_cache, memory_manager);
    if (!jxl_status_ok(status)) {
      jxl_enc_cache_destroy(&group_cache);
      return status;
    }
    status = jxl_tokenize_coefficients(
        jxl_array_at(&shared->coeff_orders, 0), &rect, ac_rows, &shared->ac_strategy,
        &frame_header->chroma_subsampling, &group_cache.num_nzeroes,
        jxl_token_streams_at(&enc_state->ac_tokens, group_index), &shared->quant_dc,
        &shared->raw_quant_field, &shared->block_ctx_map);
    if (!jxl_status_ok(status)) {
      jxl_enc_cache_destroy(&group_cache);
      return status;
    }
  }
  jxl_enc_cache_destroy(&group_cache);
  return jxl_ok_status();
}

static jxl_status jxl_encode_global_dc_info(const jxl_passes_shared_state* shared, jxl_bit_writer* writer){
  // Encode quantizer DC and global scale.
  jxl_quantizer_params params = jxl_quantizer_get_params(&shared->quantizer);
  JXL_RETURN_IF_ERROR(
      jxl_write_quantizer_params(&params, writer, kLayerQuant));
  JXL_RETURN_IF_ERROR(jxl_encode_block_ctx_map(&shared->block_ctx_map, writer));
  JXL_RETURN_IF_ERROR(jxl_color_correlation_encode_dc(jxl_color_correlation_map_base(&shared->cmap), writer,
                                               kLayerDc));
  return jxl_ok_status();
}

typedef struct jxl_histo_ctx {
  jxl_bit_writer* writer;
  size_t num_histo_bits;
} jxl_histo_ctx;

static jxl_status jxl_write_num_histograms_body(void* opaque) {
  jxl_histo_ctx* c = (jxl_histo_ctx*)(opaque);
  jxl_bit_writer_write(c->writer, c->num_histo_bits, 0);
  return jxl_ok_status();
}

typedef struct jxl_order_ctx {
  jxl_bit_writer* writer;
  uint32_t used_orders;
} jxl_order_ctx;

static jxl_status jxl_write_used_orders_body(void* opaque) {
  jxl_order_ctx* c = (jxl_order_ctx*)(opaque);
  return jxl_u32_coder_write(jxl_order_enc(), c->used_orders, c->writer);
}

typedef struct jxl_dc_prec_ctx {
  jxl_bit_writer* output;
  uint8_t precision;
} jxl_dc_prec_ctx;

static jxl_status jxl_write_dc_precision_body(void* opaque) {
  jxl_dc_prec_ctx* c = (jxl_dc_prec_ctx*)(opaque);
  jxl_bit_writer_write(c->output, 2, c->precision);
  return jxl_ok_status();
}

typedef struct jxl_ac_meta_ctx {
  jxl_bit_writer* output;
  size_t nb_bits;
  size_t ac_metadata_size_m1;
} jxl_ac_meta_ctx;

static jxl_status jxl_write_ac_metadata_size_body(void* opaque) {
  jxl_ac_meta_ctx* c = (jxl_ac_meta_ctx*)(opaque);
  jxl_bit_writer_write(c->output, c->nb_bits, c->ac_metadata_size_m1);
  return jxl_ok_status();
}

static jxl_status jxl_zero_pad_group_body(void* opaque) {
  jxl_bit_writer_zero_pad_to_byte((jxl_bit_writer*)(opaque));  // end of group.
  return jxl_ok_status();
}

static jxl_status jxl_encode_global_ac_info(jxl_passes_encoder_state* enc_state, jxl_bit_writer* writer,
                          jxl_modular_frame_encoder* enc_modular) {
  jxl_passes_shared_state* shared = &enc_state->shared;
  jxl_memory_manager* memory_manager = jxl_passes_encoder_state_memory_manager(enc_state);
  JXL_RETURN_IF_ERROR(jxl_dequant_matrices_encode(memory_manager, &shared->matrices,
                                            writer, kLayerQuant,
                                            enc_modular));
  size_t num_histo_bits = jxl_ceil_log2_nonzero64(shared->frame_dim.num_groups);
  if (num_histo_bits != 0) {
    jxl_histo_ctx histo_ctx = {writer, num_histo_bits};
    JXL_RETURN_IF_ERROR(jxl_bit_writer_with_max_bits(writer, 
        num_histo_bits, kLayerAc, jxl_write_num_histograms_body, &histo_ctx));
  }

  {
    size_t order_bits = 0;
    JXL_RETURN_IF_ERROR(jxl_u32_coder_can_encode(
        jxl_order_enc(), enc_state->used_orders, &order_bits));
    jxl_order_ctx order_ctx = {writer, enc_state->used_orders};
    JXL_RETURN_IF_ERROR(jxl_bit_writer_with_max_bits(writer, 
        order_bits, kLayerOrder, jxl_write_used_orders_body, &order_ctx));
    JXL_RETURN_IF_ERROR(
        jxl_encode_coeff_orders(enc_state->used_orders, jxl_array_at(&shared->coeff_orders, 0),
                          writer, kLayerOrder));

    jxl_histogram_params hist_params;
    jxl_histogram_params_construct_empty(&hist_params);
    jxl_array_size empty_ac_widths;
    jxl_array_construct_empty(&empty_ac_widths, memory_manager);
    const jxl_speed_tier speed = enc_state->cparams.speed_tier;
    if (speed > kFalcon) {
      hist_params.clustering = kClusteringFastest;
    } else if (speed > kTortoise) {
      hist_params.clustering = kClusteringFast;
    }
    if (speed > kTortoise) {
      hist_params.uint_method = kHybridUintNone;
      hist_params.lz77_method = kLZ77None;
    }
    if (speed >= kSquirrel) {
      hist_params.ans_histogram_strategy =
          kANSHistApproximate;
    }
    size_t cost;
    jxl_status hist_status = jxl_build_and_encode_histograms(
        memory_manager, &hist_params, jxl_block_ctx_map_num_ac_contexts(&shared->block_ctx_map),
        &enc_state->ac_tokens, &enc_state->ac_codes, writer, kLayerAc,
        &empty_ac_widths, &cost);
    jxl_array_destroy(&empty_ac_widths);
    (void)cost;
    JXL_RETURN_IF_ERROR(hist_status);
  }

  return jxl_ok_status();
}
static jxl_status jxl_encode_groups(const jxl_frame_header* frame_header,
                    jxl_passes_encoder_state* enc_state,
                    jxl_modular_frame_encoder* enc_modular,
                    jxl_bit_writers* group_codes){
  const jxl_passes_shared_state* shared = &enc_state->shared;
  jxl_memory_manager* memory_manager = shared->memory_manager;
  const jxl_frame_dimensions* frame_dim = &shared->frame_dim;
  const size_t num_groups = frame_dim->num_groups;
  const size_t global_ac_index = frame_dim->num_dc_groups + 1;
  const bool is_small_image = num_groups == 1;
  const size_t num_toc_entries =
      is_small_image ? 1
                     : jxl_ac_group_index(0, 0, num_groups, frame_dim->num_dc_groups) +
                           num_groups;
  JXL_ENSURE(jxl_bit_writers_empty(group_codes));
  JXL_RETURN_IF_ERROR(jxl_bit_writers_reserve(group_codes, num_toc_entries));
  for (size_t i = 0; i < num_toc_entries; ++i) {
    JXL_RETURN_IF_ERROR(jxl_bit_writers_emplace_back(group_codes, memory_manager));
  }

  jxl_bit_writer* global_writer =
      jxl_group_code_writer(group_codes, is_small_image, 0);
  JXL_RETURN_IF_ERROR(jxl_dequant_matrices_encode_dc(&shared->matrices, global_writer,
                                              kLayerQuant));
  JXL_RETURN_IF_ERROR(jxl_encode_global_dc_info(shared, global_writer));
  JXL_RETURN_IF_ERROR(jxl_modular_frame_encoder_encode_global_info(enc_modular, global_writer));
  {
    jxl_modular_stream_id stream = jxl_modular_stream_id_global();
    JXL_RETURN_IF_ERROR(jxl_modular_frame_encoder_encode_stream(enc_modular, 
        global_writer, kLayerModularGlobal, &stream));
  }

  for (uint32_t group_index = 0; group_index < frame_dim->num_dc_groups;
       ++group_index) {
    jxl_bit_writer* output =
        jxl_group_code_writer(group_codes, is_small_image, group_index + 1);
    jxl_dc_prec_ctx dc_prec_ctx = {output, *jxl_array_at(&enc_modular->extra_dc_precision, group_index)};
    JXL_RETURN_IF_ERROR(jxl_bit_writer_with_max_bits(output, 
        2, kLayerDc, jxl_write_dc_precision_body, &dc_prec_ctx));
    {
      jxl_modular_stream_id stream = jxl_modular_stream_id_var_dctdc(group_index);
      JXL_RETURN_IF_ERROR(
          jxl_modular_frame_encoder_encode_stream(enc_modular, output, kLayerDc, &stream));
    }
    const jxl_rect rect = jxl_frame_dimensions_dc_group_rect(&enc_state->shared.frame_dim, group_index);
    size_t nb_bits = jxl_ceil_log2_nonzero64(jxl_rect_x_size(&rect) * jxl_rect_y_size(&rect));
    if (nb_bits != 0) {
      jxl_ac_meta_ctx ac_meta_ctx = {output, nb_bits,
                               *jxl_array_at(&enc_modular->ac_metadata_size, group_index) - 1};
      JXL_RETURN_IF_ERROR(jxl_bit_writer_with_max_bits(output, 
          nb_bits, kLayerControlFields, jxl_write_ac_metadata_size_body,
          &ac_meta_ctx));
    }
    {
      jxl_modular_stream_id stream = jxl_modular_stream_id_ac_metadata(group_index);
      JXL_RETURN_IF_ERROR(jxl_modular_frame_encoder_encode_stream(enc_modular, 
          output, kLayerControlFields, &stream));
    }
  }
  JXL_RETURN_IF_ERROR(jxl_encode_global_ac_info(
      enc_state,
      jxl_group_code_writer(group_codes, is_small_image, global_ac_index),
      enc_modular));

  for (uint32_t group_index = 0; group_index < num_groups; ++group_index) {
    jxl_bit_writer* ac_writer = jxl_group_code_writer(
        group_codes, is_small_image,
        jxl_ac_group_index(0, group_index, frame_dim->num_groups,
                     frame_dim->num_dc_groups));
    JXL_RETURN_IF_ERROR(jxl_write_tokens(
        jxl_token_streams_at(&enc_state->ac_tokens, group_index), &enc_state->ac_codes,
        0, ac_writer, kLayerAcTokens));
  }

  for (size_t bw_i = 0; bw_i < jxl_bit_writers_size(group_codes); ++bw_i) {
    jxl_bit_writer* bw = jxl_bit_writers_at(group_codes, bw_i);
    JXL_RETURN_IF_ERROR(
        jxl_bit_writer_with_max_bits(bw, 8, kLayerAc, jxl_zero_pad_group_body, bw));
  }
  return jxl_ok_status();
}

static jxl_status jxl_compute_encoding_data(
    const jxl_compress_params* cparams, const jxl_codec_metadata* metadata,
    const jxl_jpeg_data* jpeg_data, jxl_frame_header* mutable_frame_header,
    jxl_modular_frame_encoder* enc_modular, jxl_passes_encoder_state* enc_state,
    jxl_bit_writers* group_codes){
  jxl_memory_manager* memory_manager = jxl_passes_encoder_state_memory_manager(enc_state);
  const jxl_frame_header* frame_header = mutable_frame_header;
  jxl_passes_shared_state* shared = &enc_state->shared;
  shared->metadata = metadata;
  shared->frame_dim = jxl_frame_header_to_frame_dimensions(frame_header);

  const jxl_frame_dimensions* frame_dim = &shared->frame_dim;
  JXL_RETURN_IF_ERROR(jxl_ac_strategy_image_create(
      memory_manager, frame_dim->xsize_blocks, frame_dim->ysize_blocks,
      &shared->ac_strategy));
  JXL_RETURN_IF_ERROR(jxl_image_i_create(memory_manager, frame_dim->xsize_blocks,
                                       frame_dim->ysize_blocks, 0,
                                       &shared->raw_quant_field));
  JXL_RETURN_IF_ERROR(jxl_image_b_create(memory_manager, frame_dim->xsize_blocks,
                                       frame_dim->ysize_blocks, 0,
                                       &shared->epf_sharpness));
  JXL_ENSURE(frame_header->passes.num_passes == 1);
  shared->coeff_order_size = kCoeffOrderMaxSize;
  JXL_RETURN_IF_ERROR(jxl_array_resize_zero(&shared->coeff_orders, kCoeffOrderMaxSize));

  JXL_RETURN_IF_ERROR(jxl_image_b_create(memory_manager, frame_dim->xsize_blocks,
                                       frame_dim->ysize_blocks, 0,
                                       &shared->quant_dc));

  enc_state->cparams = *cparams;
  JXL_RETURN_IF_ERROR(
      jxl_token_streams_resize(&enc_state->ac_tokens, shared->frame_dim.num_groups));
  JXL_RETURN_IF_ERROR(jxl_compute_jpeg_transcoding_data(
      jpeg_data, mutable_frame_header, enc_modular, enc_state));
  JXL_RETURN_IF_ERROR(jxl_compute_all_coeff_orders(enc_state, frame_dim));
  JXL_RETURN_IF_ERROR(jxl_tokenize_all_coefficients(mutable_frame_header, enc_state));

  JXL_RETURN_IF_ERROR(jxl_modular_frame_encoder_compute_tree(enc_modular));
  JXL_RETURN_IF_ERROR(jxl_modular_frame_encoder_compute_tokens(enc_modular));

  JXL_RETURN_IF_ERROR(jxl_encode_groups(mutable_frame_header, enc_state, enc_modular,
                                   group_codes));
  return jxl_ok_status();
}

static jxl_status jxl_encode_frame_one_shot_write_codestream(
    jxl_memory_manager* memory_manager, jxl_frame_header* frame_header,
    jxl_bit_writers* group_codes,
    jxl_encoder_output_processor_wrapper* output_processor) {
  jxl_bit_writer writer;
  jxl_bit_writer_make(memory_manager, &writer);
  jxl_status status = jxl_write_frame_header(frame_header, &writer);
  if (!jxl_status_ok(status)) {
    jxl_bit_writer_destroy(&writer);
    return status;
  }

  jxl_array_size group_sizes;
  jxl_array_construct_empty(&group_sizes, memory_manager);
  status = jxl_array_reserve(&group_sizes, jxl_bit_writers_size(group_codes));
  if (!jxl_status_ok(status)) {
    jxl_array_destroy(&group_sizes);
    jxl_bit_writer_destroy(&writer);
    return status;
  }
  for (size_t bw_i = 0; bw_i < jxl_bit_writers_size(group_codes); ++bw_i) {
    const jxl_bit_writer* bw = jxl_bit_writers_at(group_codes, bw_i);
    JXL_ENSURE(jxl_bit_writer_bits_written(bw) % kBitsPerByte == 0);
    status =
        jxl_array_size_push_back(&group_sizes, jxl_bit_writer_bits_written(bw) / kBitsPerByte);
    if (!jxl_status_ok(status)) {
      jxl_array_destroy(&group_sizes);
      jxl_bit_writer_destroy(&writer);
      return status;
    }
  }
  status = jxl_write_toc_permutation(&writer);
  if (!jxl_status_ok(status)) {
    jxl_array_destroy(&group_sizes);
    jxl_bit_writer_destroy(&writer);
    return status;
  }
  status = jxl_write_toc_sizes(&group_sizes, &writer);
  if (!jxl_status_ok(status)) {
    jxl_array_destroy(&group_sizes);
    jxl_bit_writer_destroy(&writer);
    return status;
  }
  jxl_array_destroy(&group_sizes);

  status = jxl_bit_writer_append_byte_aligned(
      &writer, jxl_bit_writers_data(group_codes), jxl_bit_writers_size(group_codes));
  if (!jxl_status_ok(status)) {
    jxl_bit_writer_destroy(&writer);
    return status;
  }
  jxl_padded_bytes frame_bytes;
  jxl_bit_writer_take_bytes(&writer, &frame_bytes);
  status = jxl_append_data(output_processor, jxl_padded_bytes_data(&frame_bytes),
                      jxl_padded_bytes_size(&frame_bytes));
  jxl_padded_bytes_destroy(&frame_bytes);
  jxl_bit_writer_destroy(&writer);
  return status;
}

static jxl_status jxl_encode_frame_one_shot_with_header(
    jxl_memory_manager* memory_manager, const jxl_compress_params* cparams,
    const jxl_frame_info* frame_info, const jxl_codec_metadata* metadata,
    jxl_encoder_jpeg_frame_adapter* frame_data,
    jxl_encoder_output_processor_wrapper* output_processor,
    jxl_bit_writers* group_codes, jxl_passes_encoder_state* enc_state,
    jxl_modular_frame_encoder* enc_modular, jxl_frame_header* frame_header) {
  jxl_jpeg_data jpeg_data;
  jxl_encoder_jpeg_frame_adapter_take_jpeg_data(frame_data, &jpeg_data,
                                                memory_manager);
  jxl_status status = jxl_make_frame_header(frame_info, &jpeg_data, frame_header);
  if (!jxl_status_ok(status)) {
    jxl_jpeg_data_destroy(&jpeg_data);
    return status;
  }
  status = jxl_modular_frame_encoder_create(memory_manager, frame_header, cparams,
                                     enc_modular);
  if (!jxl_status_ok(status)) {
    jxl_jpeg_data_destroy(&jpeg_data);
    return status;
  }
  status = jxl_compute_encoding_data(cparams, metadata, &jpeg_data, frame_header,
                               enc_modular, enc_state, group_codes);
  if (!jxl_status_ok(status)) {
    jxl_jpeg_data_destroy(&jpeg_data);
    return status;
  }
  jxl_jpeg_data_destroy(&jpeg_data);

  return jxl_encode_frame_one_shot_write_codestream(memory_manager, frame_header,
                                           group_codes, output_processor);
}

static jxl_status jxl_encode_frame_one_shot_with_modular(
    jxl_memory_manager* memory_manager, const jxl_compress_params* cparams,
    const jxl_frame_info* frame_info, const jxl_codec_metadata* metadata,
    jxl_encoder_jpeg_frame_adapter* frame_data,
    jxl_encoder_output_processor_wrapper* output_processor,
    jxl_bit_writers* group_codes, jxl_passes_encoder_state* enc_state,
    jxl_modular_frame_encoder* enc_modular) {
  jxl_frame_header frame_header;
  jxl_frame_header_init(&frame_header, metadata);
  jxl_status status = jxl_encode_frame_one_shot_with_header(
      memory_manager, cparams, frame_info, metadata, frame_data,
      output_processor, group_codes, enc_state, enc_modular, &frame_header);
  jxl_frame_header_destroy(&frame_header);
  return status;
}

static jxl_status jxl_encode_frame_one_shot_body_inner(
    jxl_memory_manager* memory_manager, const jxl_compress_params* cparams,
    const jxl_frame_info* frame_info, const jxl_codec_metadata* metadata,
    jxl_encoder_jpeg_frame_adapter* frame_data,
    jxl_encoder_output_processor_wrapper* output_processor,
    jxl_bit_writers* group_codes, jxl_passes_encoder_state* enc_state) {
  jxl_modular_frame_encoder enc_modular;
  jxl_modular_frame_encoder_init_mm(&enc_modular, memory_manager);
  jxl_status status = jxl_encode_frame_one_shot_with_modular(
      memory_manager, cparams, frame_info, metadata, frame_data,
      output_processor, group_codes, enc_state, &enc_modular);
  jxl_modular_frame_encoder_destroy(&enc_modular);
  return status;
}

static jxl_status jxl_encode_frame_one_shot_body(
    jxl_memory_manager* memory_manager, const jxl_compress_params* cparams,
    const jxl_frame_info* frame_info, const jxl_codec_metadata* metadata,
    jxl_encoder_jpeg_frame_adapter* frame_data,
    jxl_encoder_output_processor_wrapper* output_processor,
    jxl_bit_writers* group_codes) {
  jxl_passes_encoder_state enc_state;
  jxl_passes_encoder_state_init(&enc_state, memory_manager);
  jxl_status status = jxl_encode_frame_one_shot_body_inner(
      memory_manager, cparams, frame_info, metadata, frame_data,
      output_processor, group_codes, &enc_state);
  jxl_passes_encoder_state_destroy(&enc_state);
  return status;
}

static jxl_status jxl_encode_frame_one_shot(jxl_memory_manager* memory_manager,
                          const jxl_compress_params* cparams,
                          const jxl_frame_info* frame_info,
                          const jxl_codec_metadata* metadata,
                          jxl_encoder_jpeg_frame_adapter* frame_data,
                          jxl_encoder_output_processor_wrapper* output_processor) {
  jxl_bit_writers group_codes;
  jxl_bit_writers_construct_empty(&group_codes);
  group_codes.memory_manager = memory_manager;
  jxl_status status =
      jxl_encode_frame_one_shot_body(memory_manager, cparams, frame_info, metadata,
                             frame_data, output_processor, &group_codes);
  jxl_bit_writers_destroy(&group_codes);
  return status;
}


jxl_status jxl_encode_frame(jxl_memory_manager* memory_manager,
                   const jxl_compress_params* cparams_orig,
                   const jxl_frame_info* frame_info, const jxl_codec_metadata* metadata,
                   jxl_encoder_jpeg_frame_adapter* frame_data,
                   jxl_encoder_output_processor_wrapper* output_processor) {
  jxl_compress_params cparams = *cparams_orig;
  if (cparams.speed_tier == kLightning) {
    cparams.speed_tier = kThunder;
  }

  JXL_ENSURE(jxl_encoder_jpeg_frame_adapter_is_jpeg(frame_data));

  if (frame_data->xsize == 0 || frame_data->ysize == 0) {
    return JXL_FAILURE("Empty image");
  }

  if (cparams.color_transform == kColorTransformXYB) {
    return JXL_FAILURE("Can't add JPEG frame to XYB codestream");
  }

  return jxl_encode_frame_one_shot(memory_manager, &cparams, frame_info, metadata,
                                 frame_data, output_processor);
}
