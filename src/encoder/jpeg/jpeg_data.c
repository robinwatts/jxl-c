// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "jpeg/jpeg_data.h"

#include <jxl/types.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "base/printf_macros.h"
#include "base/bits.h"
#include "common.h"  // kMaxNumPasses
#include "fields.h"

typedef enum jxl_jpeg_component_type {
  kJpegGray = 0,
  kJpegYCbCr = 1,
  kJpegRGB = 2,
  kJpegCustom = 3,
} jxl_jpeg_component_type;

typedef struct jxl_jpeg_info {
  size_t num_app_markers;
  size_t num_com_markers;
  size_t num_scans;
  size_t num_intermarker;
  bool has_dri;
} jxl_jpeg_info;

static inline void jxl_jpeg_info_init(jxl_jpeg_info* self) {
  self->num_app_markers = 0;
  self->num_com_markers = 0;
  self->num_scans = 0;
  self->num_intermarker = 0;
  self->has_dri = false;
}

static jxl_enc_status jxl_visit_marker(uint8_t* marker, jxl_visitor* visitor, jxl_jpeg_info* info) {
  uint32_t marker32 = *marker - 0xc0;
  JXL_RETURN_IF_ERROR(jxl_visitor_bits(visitor, 6, 0x00, &marker32));
  *marker = marker32 + 0xc0;
  if ((*marker & 0xf0) == 0xe0) {
    info->num_app_markers++;
  }
  if (*marker == 0xfe) {
    info->num_com_markers++;
  }
  if (*marker == 0xda) {
    info->num_scans++;
  }
  // We use a fake 0xff marker to signal intermarker data.
  if (*marker == 0xff) {
    info->num_intermarker++;
  }
  if (*marker == 0xdd) {
    info->has_dri = true;
  }
  return jxl_enc_ok_status();
}

static jxl_enc_status jxl_visit_known_app_marker_type(jxl_visitor* visitor, jxl_app_marker_type* type) {
  JXL_RETURN_IF_ERROR(
      jxl_visitor_u32(visitor, jxl_u32_enc_make(jxl_val(0), jxl_val(1), jxl_bits_offset(1, 2), jxl_bits_offset(2, 4)), 0, (uint32_t*)(type)));
  if (*type != kAppMarkerUnknown && *type != kAppMarkerICC &&
      *type != kAppMarkerExif && *type != kAppMarkerXMP) {
    return JXL_FAILURE("Unknown app marker type %u",
                       (uint32_t)(*type));
  }
  return jxl_enc_ok_status();
}

jxl_enc_status jxl_jpeg_data_visit_fields(jxl_jpeg_data* self, jxl_visitor* visitor) {
  bool is_gray = jxl_array_len(&self->components) == 1;
  JXL_RETURN_IF_ERROR(jxl_visitor_bool(visitor, false, &is_gray));
  jxl_jpeg_info info;
  jxl_jpeg_info_init(&info);
  if (jxl_array_len(&self->marker_order) > 16384) {
    return JXL_FAILURE("Too many markers: %" jxl_pr_iu_s "\n", jxl_array_len(&self->marker_order));
  }
  for (size_t marker_i = 0; marker_i < jxl_array_len(&self->marker_order); ++marker_i) {
    uint8_t* marker = jxl_array_at(&self->marker_order, marker_i);
    JXL_RETURN_IF_ERROR(jxl_visit_marker(marker, visitor, &info));
  }
  if (!jxl_array_empty(&self->marker_order)) {
    // Last marker should always be EOI marker.
    JXL_ENSURE(jxl_array_back(&self->marker_order) == 0xd9);
  }

  if (info.num_scans == 0) {
    return JXL_FAILURE("JPEG: no scans\n");
  }

  JXL_ENSURE(jxl_byte_chunks_size(&self->app_data) == info.num_app_markers);
  JXL_ENSURE(jxl_array_len(&self->app_marker_type) == info.num_app_markers);
  JXL_ENSURE(jxl_byte_chunks_size(&self->com_data) == info.num_com_markers);
  JXL_ENSURE(jxl_array_len(&self->scan_info) == info.num_scans);
  JXL_ENSURE(jxl_u32_chunks_size(&self->scan_reset_points) == jxl_array_len(&self->scan_info));
  JXL_ENSURE(jxl_extra_zero_run_chunks_size(&self->scan_extra_zero_runs) == jxl_array_len(&self->scan_info));
  for (size_t i = 0; i < jxl_byte_chunks_size(&self->app_data); i++) {
    jxl_bytes app = jxl_byte_chunks_at(&self->app_data, i);
    JXL_RETURN_IF_ERROR(
        jxl_visit_known_app_marker_type(visitor, jxl_array_at(&self->app_marker_type, i)));
    uint32_t len = jxl_bytes_size(&app) - 1;
    JXL_RETURN_IF_ERROR(jxl_visitor_bits(visitor, 16, 0, &len));
    if (jxl_bytes_size(&app) < 3) {
      return JXL_FAILURE("Invalid marker size: %" jxl_pr_iu_s "\n", jxl_bytes_size(&app));
    }
  }
  for (size_t i = 0; i < jxl_byte_chunks_size(&self->com_data); i++) {
    jxl_bytes com = jxl_byte_chunks_at(&self->com_data, i);
    uint32_t len = jxl_bytes_size(&com) - 1;
    JXL_RETURN_IF_ERROR(jxl_visitor_bits(visitor, 16, 0, &len));
    if (jxl_bytes_size(&com) < 3) {
      return JXL_FAILURE("Invalid marker size: %" jxl_pr_iu_s "\n", jxl_bytes_size(&com));
    }
  }

  uint32_t num_quant_tables = jxl_array_len(&self->quant);
  JXL_RETURN_IF_ERROR(
      jxl_visitor_u32(visitor, jxl_u32_enc_make(jxl_val(1), jxl_val(2), jxl_val(3), jxl_val(4)), 2, &num_quant_tables));
  if (num_quant_tables == 4) {
    return JXL_FAILURE("Invalid number of quant tables");
  }
  for (size_t i = 0; i < num_quant_tables; i++) {
    if (jxl_array_at(&self->quant, i)->precision > 1) {
      return JXL_FAILURE(
          "Quant tables with more than 16 bits are not supported");
    }
    JXL_RETURN_IF_ERROR(jxl_visitor_bits(visitor, 1, 0, &jxl_array_at(&self->quant, i)->precision));
    JXL_RETURN_IF_ERROR(jxl_visitor_bits(visitor, 2, i, &jxl_array_at(&self->quant, i)->index));
    JXL_RETURN_IF_ERROR(jxl_visitor_bool(visitor, true, &jxl_array_at(&self->quant, i)->is_last));
  }

  jxl_jpeg_component_type component_type =
      jxl_array_len(&self->components) == 1 && jxl_array_at(&self->components, 0)->id == 1 ? kJpegGray
      : jxl_array_len(&self->components) == 3 && jxl_array_at(&self->components, 0)->id == 1 &&
              jxl_array_at(&self->components, 1)->id == 2 && jxl_array_at(&self->components, 2)->id == 3
          ? kJpegYCbCr
      : jxl_array_len(&self->components) == 3 && jxl_array_at(&self->components, 0)->id == 'R' &&
              jxl_array_at(&self->components, 1)->id == 'G' && jxl_array_at(&self->components, 2)->id == 'B'
          ? kJpegRGB
          : kJpegCustom;
  JXL_RETURN_IF_ERROR(
      jxl_visitor_bits(visitor, 2, kJpegYCbCr,
                    (uint32_t*)(&component_type)));
  uint32_t num_components;
  if (component_type == kJpegGray) {
    num_components = 1;
  } else if (component_type != kJpegCustom) {
    num_components = 3;
  } else {
    num_components = jxl_array_len(&self->components);
    JXL_RETURN_IF_ERROR(
        jxl_visitor_u32(visitor, jxl_u32_enc_make(jxl_val(1), jxl_val(2), jxl_val(3), jxl_val(4)), 3, &num_components));
    if (num_components != 1 && num_components != 3) {
      return JXL_FAILURE("Invalid number of components: %u", num_components);
    }
  }
  if (component_type == kJpegCustom) {
    for (size_t component_i = 0; component_i < jxl_array_len(&self->components); ++component_i) {
      jxl_jpeg_component* component = jxl_array_at(&self->components, component_i);
      JXL_RETURN_IF_ERROR(jxl_visitor_bits(visitor, 8, 0, &component->id));
    }
  } else if (component_type == kJpegGray) {
    jxl_array_at(&self->components, 0)->id = 1;
  } else if (component_type == kJpegRGB) {
    jxl_array_at(&self->components, 0)->id = 'R';
    jxl_array_at(&self->components, 1)->id = 'G';
    jxl_array_at(&self->components, 2)->id = 'B';
  } else {
    jxl_array_at(&self->components, 0)->id = 1;
    jxl_array_at(&self->components, 1)->id = 2;
    jxl_array_at(&self->components, 2)->id = 3;
  }
  size_t used_tables = 0;
  for (size_t i = 0; i < jxl_array_len(&self->components); i++) {
    JXL_RETURN_IF_ERROR(jxl_visitor_bits(visitor, 2, 0, &jxl_array_at(&self->components, i)->quant_idx));
    if (jxl_array_at(&self->components, i)->quant_idx >= jxl_array_len(&self->quant)) {
      return JXL_FAILURE("Invalid quant table for component %" jxl_pr_iu_s ": %u\n",
                         i, jxl_array_at(&self->components, i)->quant_idx);
    }
    used_tables |= 1U << jxl_array_at(&self->components, i)->quant_idx;
  }
  for (size_t i = 0; i < jxl_array_len(&self->quant); i++) {
    if (used_tables & (1 << i)) continue;
    if (i == 0) return JXL_FAILURE("First quant table unused.");
    // Unused self->quant table has to be set to copy of previous self->quant table
    for (size_t j = 0; j < 64; j++) {
      if (jxl_array_at(&self->quant, i)->values[j] != jxl_array_at(&self->quant, i - 1)->values[j]) {
        return JXL_FAILURE("Non-trivial unused quant table");
      }
    }
  }

  uint32_t num_huff = jxl_array_len(&self->huffman_code);
  JXL_RETURN_IF_ERROR(jxl_visitor_u32(visitor, jxl_u32_enc_make(jxl_val(4), jxl_bits_offset(3, 2), jxl_bits_offset(4, 10), jxl_bits_offset(6, 26)), 4, &num_huff));
  for (size_t hc_i = 0; hc_i < jxl_array_len(&self->huffman_code); ++hc_i) {
    jxl_jpeg_huffman_code* hc = jxl_array_at(&self->huffman_code, hc_i);
    bool is_ac = ((hc->slot_id >> 4) != 0);
    uint32_t id = hc->slot_id & 0xF;
    JXL_RETURN_IF_ERROR(jxl_visitor_bool(visitor, false, &is_ac));
    JXL_RETURN_IF_ERROR(jxl_visitor_bits(visitor, 2, 0, &id));
    hc->slot_id = ((uint32_t)(is_ac) << 4) | id;
    JXL_RETURN_IF_ERROR(jxl_visitor_bool(visitor, true, &hc->is_last));
    size_t num_symbols = 0;
    for (size_t i = 0; i <= 16; i++) {
      JXL_RETURN_IF_ERROR(jxl_visitor_u32(visitor, jxl_u32_enc_make(jxl_val(0), jxl_val(1), jxl_bits_offset(3, 2), jxl_bits(8)), 0, &hc->counts[i]));
      num_symbols += hc->counts[i];
    }
    if (num_symbols == 0) {
      // Actually, at least 2 symbols are required, since one of them is EOI.
      // This case is used to represent an empty DHT marker.
      continue;
    }
    if (num_symbols > kJpegHuffmanAlphabetSize + 1) {
      return JXL_FAILURE("Huffman code too large (%" jxl_pr_iu_s ")", num_symbols);
    }
    // Presence flags for 4 * 64 + 1 values.
    uint64_t value_slots[5];
    memset(value_slots, 0, sizeof(value_slots));
    for (size_t i = 0; i < num_symbols; i++) {
      // Goes up to 256, included. Might have the same symbol appear twice...
      JXL_RETURN_IF_ERROR(jxl_visitor_u32(visitor, jxl_u32_enc_make(jxl_bits(2), jxl_bits_offset(2, 4), jxl_bits_offset(4, 8), jxl_bits_offset(8, 1)), 0, &hc->values[i]));
      value_slots[hc->values[i] >> 6] |= (uint64_t)(1)
                                        << (hc->values[i] & 0x3F);
    }
    if (hc->values[num_symbols - 1] != kJpegHuffmanAlphabetSize) {
      return JXL_FAILURE("Missing EOI symbol");
    }
    // Last element, denoting EOI, have to be 1 after the loop.
    JXL_ENSURE(value_slots[4] == 1);
    size_t num_values = 1;
    for (size_t i = 0; i < 4; ++i) num_values += jxl_pop_count(value_slots[i]);
    if (num_values != num_symbols) {
      return JXL_FAILURE("Duplicate Huffman symbols");
    }
    if (!is_ac) {
      bool only_dc = ((value_slots[0] >> kJpegDCAlphabetSize) | value_slots[1] |
                      value_slots[2] | value_slots[3]) == 0;
      if (!only_dc) return JXL_FAILURE("Huffman symbols out of DC range");
    }
  }

  for (size_t si = 0; si < jxl_array_len(&self->scan_info); ++si) {
    jxl_jpeg_scan_info* scan = jxl_array_at(&self->scan_info, si);
    JXL_RETURN_IF_ERROR(
        jxl_visitor_u32(visitor, jxl_u32_enc_make(jxl_val(1), jxl_val(2), jxl_val(3), jxl_val(4)), 1, &scan->num_components));
    if (scan->num_components >= 4) {
      return JXL_FAILURE("Invalid number of components in SOS marker");
    }
    JXL_RETURN_IF_ERROR(jxl_visitor_bits(visitor, 6, 0, &scan->jxl_ss));
    JXL_RETURN_IF_ERROR(jxl_visitor_bits(visitor, 6, 63, &scan->Se));
    JXL_RETURN_IF_ERROR(jxl_visitor_bits(visitor, 4, 0, &scan->Al));
    JXL_RETURN_IF_ERROR(jxl_visitor_bits(visitor, 4, 0, &scan->Ah));
    for (size_t i = 0; i < scan->num_components; i++) {
      JXL_RETURN_IF_ERROR(jxl_visitor_bits(visitor, 2, 0, &scan->components[i].comp_idx));
      if (scan->components[i].comp_idx >= jxl_array_len(&self->components)) {
        return JXL_FAILURE("Invalid component idx in SOS marker");
      }
      JXL_RETURN_IF_ERROR(jxl_visitor_bits(visitor, 2, 0, &scan->components[i].ac_tbl_idx));
      JXL_RETURN_IF_ERROR(jxl_visitor_bits(visitor, 2, 0, &scan->components[i].dc_tbl_idx));
    }
    // TODO(veluca): actually set and use self value.
    JXL_RETURN_IF_ERROR(jxl_visitor_u32(visitor, jxl_u32_enc_make(jxl_val(0), jxl_val(1), jxl_val(2), jxl_bits_offset(3, 3)), kMaxNumPasses - 1, &scan->last_needed_pass));
  }

  // From here on, self is data that is not strictly necessary to get a valid
  // JPEG, but necessary for bit-exact JPEG reconstruction.
  if (info.has_dri) {
    JXL_RETURN_IF_ERROR(jxl_visitor_bits(visitor, 16, 0, &self->restart_interval));
  }

  for (size_t si = 0; si < jxl_array_len(&self->scan_info); ++si) {
    jxl_u32_span reset_points = jxl_u32_chunks_mutable(&self->scan_reset_points, si);
    uint32_t num_reset_points = jxl_u32_span_size(&reset_points);
    JXL_RETURN_IF_ERROR(jxl_visitor_u32(visitor, jxl_u32_enc_make(jxl_val(0), jxl_bits_offset(2, 1), jxl_bits_offset(4, 4), jxl_bits_offset(16, 20)), 0, &num_reset_points));
    int last_block_idx = -1;
    for (size_t block_idx_i = 0; block_idx_i < jxl_u32_span_size(&reset_points); ++block_idx_i) {
      uint32_t* block_idx = jxl_u32_span_at(&reset_points, block_idx_i);
      *block_idx -= last_block_idx + 1;
      JXL_RETURN_IF_ERROR(jxl_visitor_u32(visitor, jxl_u32_enc_make(jxl_val(0), jxl_bits_offset(3, 1), jxl_bits_offset(5, 9), jxl_bits_offset(28, 41)), 0, block_idx));
      *block_idx += last_block_idx + 1;
      if (*block_idx >= (3u << 26)) {
        // At most 8K x 8K x num_channels blocks are possible in a JPEG.
        // So valid block indices are below 3 * 2^26.
        return JXL_FAILURE("Invalid block ID: %u", *block_idx);
      }
      last_block_idx = *block_idx;
    }

    jxl_extra_zero_run_span extra_zero_runs = jxl_extra_zero_run_chunks_mutable(&self->scan_extra_zero_runs, si);
    uint32_t num_extra_zero_runs = jxl_extra_zero_run_span_size(&extra_zero_runs);
    JXL_RETURN_IF_ERROR(jxl_visitor_u32(visitor, jxl_u32_enc_make(jxl_val(0), jxl_bits_offset(2, 1), jxl_bits_offset(4, 4), jxl_bits_offset(16, 20)), 0, &num_extra_zero_runs));
    last_block_idx = -1;
    for (size_t extra_zero_run_i = 0; extra_zero_run_i < jxl_extra_zero_run_span_size(&extra_zero_runs); ++extra_zero_run_i) {
      jxl_jpeg_extra_zero_run_info* extra_zero_run = jxl_extra_zero_run_span_at(&extra_zero_runs, extra_zero_run_i);
      uint32_t* block_idx = &extra_zero_run->block_idx;
      uint32_t* zero_runs = &extra_zero_run->num_extra_zero_runs;
      JXL_RETURN_IF_ERROR(jxl_visitor_u32(visitor, jxl_u32_enc_make(jxl_val(1), jxl_bits_offset(2, 2), jxl_bits_offset(4, 5), jxl_bits_offset(8, 20)), 1, zero_runs));
      *block_idx -= last_block_idx + 1;
      JXL_RETURN_IF_ERROR(jxl_visitor_u32(visitor, jxl_u32_enc_make(jxl_val(0), jxl_bits_offset(3, 1), jxl_bits_offset(5, 9), jxl_bits_offset(28, 41)), 0, block_idx));
      *block_idx += last_block_idx + 1;
      if (*zero_runs > 4) {
        return JXL_FAILURE("Invalid number of extra zero runs: %u", *zero_runs);
      }
      if (*block_idx > (3u << 26)) {
        return JXL_FAILURE("Invalid block ID: %u", *block_idx);
      }
      last_block_idx = *block_idx;
    }
  }
  for (size_t i = 0; i < info.num_intermarker; ++i) {
    jxl_bytes inter = jxl_byte_chunks_at(&self->inter_marker_data, i);
    uint32_t len = jxl_bytes_size(&inter);
    JXL_RETURN_IF_ERROR(jxl_visitor_bits(visitor, 16, 0, &len));
  }
  uint32_t tail_data_len = jxl_array_len(&self->tail_data);
  if (tail_data_len > 4260096) {
    return JXL_FAILURE("Tail data too large (max size = 4260096, size = %u)",
                       tail_data_len);
  }
  JXL_RETURN_IF_ERROR(jxl_visitor_u32(visitor, jxl_u32_enc_make(jxl_val(0), jxl_bits_offset(8, 1), jxl_bits_offset(16, 257), jxl_bits_offset(22, 65793)), 0, &tail_data_len));

  JXL_RETURN_IF_ERROR(jxl_visitor_bool(visitor, false, &self->has_zero_padding_bit));
  if (self->has_zero_padding_bit) {
    uint32_t nbit = jxl_array_len(&self->padding_bits);
    JXL_RETURN_IF_ERROR(jxl_visitor_bits(visitor, 24, 0, &nbit));
    for (size_t bit_i = 0; bit_i < jxl_array_len(&self->padding_bits); ++bit_i) {
      uint8_t* bit = jxl_array_at(&self->padding_bits, bit_i);
      bool bbit = FROM_JXL_BOOL(*bit);
      JXL_RETURN_IF_ERROR(jxl_visitor_bool(visitor, false, &bbit));
      *bit = TO_JXL_BOOL(bbit);
    }
  }

  {
    size_t dht_index = 0;
    size_t scan_index = 0;
    bool is_progressive = false;
    bool ac_ok[kMaxHuffmanTables];
    bool dc_ok[kMaxHuffmanTables];
    memset(ac_ok, 0, sizeof(ac_ok));
    memset(dc_ok, 0, sizeof(dc_ok));
    for (size_t marker_i = 0; marker_i < jxl_array_len(&self->marker_order); ++marker_i) {
      uint8_t marker = *jxl_array_at(&self->marker_order, marker_i);
      if (marker == 0xC2) {
        is_progressive = true;
      } else if (marker == 0xC4) {
        for (; dht_index < jxl_array_len(&self->huffman_code);) {
          const jxl_jpeg_huffman_code* huff = jxl_array_at(&self->huffman_code, dht_index++);
          size_t index = huff->slot_id;
          if (index & 0x10) {
            index -= 0x10;
            ac_ok[index] = true;
          } else {
            dc_ok[index] = true;
          }
          if (huff->is_last) break;
        }
      } else if (marker == 0xDA) {
        const jxl_jpeg_scan_info* si = jxl_array_at(&self->scan_info, scan_index++);
        for (size_t i = 0; i < si->num_components; ++i) {
          const jxl_jpeg_component_scan_info* csi = &si->components[i];
          size_t dc_tbl_idx = csi->dc_tbl_idx;
          size_t ac_tbl_idx = csi->ac_tbl_idx;
          bool want_dc = !is_progressive || (si->jxl_ss == 0);
          if (want_dc && !dc_ok[dc_tbl_idx]) {
            return JXL_FAILURE("DC Huffman table used before defined");
          }
          bool want_ac = !is_progressive || (si->jxl_ss != 0) || (si->Se != 0);
          if (want_ac && !ac_ok[ac_tbl_idx]) {
            return JXL_FAILURE("AC Huffman table used before defined");
          }
        }
      }
    }
  }

  return jxl_enc_ok_status();
}

