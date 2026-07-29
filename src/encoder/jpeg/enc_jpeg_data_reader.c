// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "jpeg/enc_jpeg_data_reader.h"

#include <stdint.h>
#include <string.h>

#include "base/array.h"
#include "base/common.h"
#include "base/printf_macros.h"
#include "base/enc_status.h"
#include "frame_dimensions.h"
#include "jpeg/enc_jpeg_huffman_decode.h"
#include "jpeg/jpeg_data.h"


static const int kBrunsliMaxSampling = 15;

// Macros for commonly used error conditions.

#define JXL_JPEG_VERIFY_LEN(n)                                \
  if (*pos + (n) > len) {                                     \
    return JXL_FAILURE("Unexpected end of input: pos=%" jxl_pr_iu_s \
                       " need=%d len=%" jxl_pr_iu_s,                \
                       *pos, (int)(n), len);       \
  }

#define JXL_JPEG_VERIFY_INPUT(var, low, high)                          \
  if ((var) < (low) || (var) > (high)) {                               \
    return JXL_FAILURE("Invalid " #var ": %d", (int)(var)); \
  }

#define JXL_JPEG_VERIFY_MARKER_END()                             \
  if (start_pos + marker_len != *pos) {                          \
    return JXL_FAILURE("Invalid marker length: declared=%" jxl_pr_iu_s \
                       " actual=%" jxl_pr_iu_s,                        \
                       marker_len, (*pos - start_pos));          \
  }

#define JXL_JPEG_EXPECT_MARKER()                                 \
  if (pos + 2 > len || data[pos] != 0xff) {                      \
    return JXL_FAILURE(                                          \
        "Marker byte (0xff) expected, found: 0x%.2x pos=%" jxl_pr_iu_s \
        " len=%" jxl_pr_iu_s,                                          \
        (pos < len ? data[pos] : 0), pos, len);                  \
  }

static inline int jxl_read_uint8(const uint8_t* data, size_t* pos) {
  return data[(*pos)++];
}

static inline int jxl_read_uint16(const uint8_t* data, size_t* pos) {
  int v = (data[*pos] << 8) + data[*pos + 1];
  *pos += 2;
  return v;
}

static jxl_enc_status jxl_process_sof_components(const uint8_t* data, const size_t len, size_t* pos,
                            jxl_jpeg_data* jpg, size_t start_pos, size_t marker_len,
                            jxl_array_u8* ids_seen);

// Reads the Start of Frame (SOF) marker segment and fills in *jpg with the
// parsed data.
static jxl_enc_status jxl_process_sof(const uint8_t* data, const size_t len, size_t* pos,
                  jxl_jpeg_data* jpg) {
  if (jpg->width != 0) {
    return JXL_FAILURE("Duplicate SOF marker.");
  }
  const size_t start_pos = *pos;
  JXL_JPEG_VERIFY_LEN(8);
  size_t marker_len = jxl_read_uint16(data, pos);
  int precision = jxl_read_uint8(data, pos);
  int height = jxl_read_uint16(data, pos);
  int width = jxl_read_uint16(data, pos);
  int num_components = jxl_read_uint8(data, pos);
  // 'jbrd' is hardcoded for 8bits:
  JXL_JPEG_VERIFY_INPUT(precision, 8, 8);
  JXL_JPEG_VERIFY_INPUT(height, 1, kMaxDimPixels);
  JXL_JPEG_VERIFY_INPUT(width, 1, kMaxDimPixels);
  JXL_JPEG_VERIFY_INPUT(num_components, 1, kJpegMaxComponents);
  JXL_JPEG_VERIFY_LEN(3 * num_components);
  jpg->height = height;
  jpg->width = width;
  {
    jxl_jpeg_component init;
    jxl_jpeg_component_construct_empty(&init);
    JXL_RETURN_IF_ERROR(
        jxl_array_jpeg_component_resize_fill(&jpg->components, num_components, init));
  }
  for (size_t i = 0; i < kJpegMaxComponents; ++i) {
    jxl_array_destroy(&jpg->component_coeffs[i]);
    jxl_array_construct_empty(&jpg->component_coeffs[i], jpg->huffman_code.ctx);
  }

  // Read sampling factors and quant table index for each component.
  jxl_array_u8 ids_seen;
  jxl_array_construct_empty(&ids_seen, jpg->huffman_code.ctx);
  jxl_enc_status status = jxl_process_sof_components(data, len, pos, jpg, start_pos,
                                       marker_len, &ids_seen);
  jxl_array_destroy(&ids_seen);
  return status;
}

static jxl_enc_status jxl_process_sof_components(const uint8_t* data, const size_t len, size_t* pos,
                            jxl_jpeg_data* jpg, size_t start_pos, size_t marker_len,
                            jxl_array_u8* ids_seen) {
  if (!jxl_enc_status_ok(jxl_array_u8_resize_fill(ids_seen, 256, (uint8_t)(0)))) {
    return JXL_FAILURE("OOM");
  }
  int max_h_samp_factor = 1;
  int max_v_samp_factor = 1;
  for (size_t component_i = 0; component_i < jxl_array_len(&jpg->components); ++component_i) {
    jxl_jpeg_component* component = jxl_array_at(&jpg->components, component_i);
    const int id = jxl_read_uint8(data, pos);
    if (*jxl_array_at(ids_seen, id)) {  // (cf. section B.2.2, syntax of Ci)
      return JXL_FAILURE("Duplicate ID %d in SOF.", id);
    }
    *jxl_array_at(ids_seen, id) = true;
    component->id = id;
    int factor = jxl_read_uint8(data, pos);
    int h_samp_factor = factor >> 4;
    int v_samp_factor = factor & 0xf;
    JXL_JPEG_VERIFY_INPUT(h_samp_factor, 1, kBrunsliMaxSampling);
    JXL_JPEG_VERIFY_INPUT(v_samp_factor, 1, kBrunsliMaxSampling);
    component->h_samp_factor = h_samp_factor;
    component->v_samp_factor = v_samp_factor;
    component->quant_idx = jxl_read_uint8(data, pos);
    max_h_samp_factor = JXL_MAX(max_h_samp_factor, h_samp_factor);
    max_v_samp_factor = JXL_MAX(max_v_samp_factor, v_samp_factor);
  }

  // We have checked above that none of the sampling factors are 0, so the max
  // sampling factors can not be 0.
  int MCU_rows = jxl_div_ceil(jpg->height, max_v_samp_factor * 8);
  int MCU_cols = jxl_div_ceil(jpg->width, max_h_samp_factor * 8);
  // Compute the block dimensions for each component.
  for (size_t c_i = 0; c_i < jxl_array_len(&jpg->components); ++c_i) {
    jxl_jpeg_component* c = jxl_array_at(&jpg->components, c_i);
    if (max_h_samp_factor % c->h_samp_factor != 0 ||
        max_v_samp_factor % c->v_samp_factor != 0) {
      return JXL_FAILURE("Non-integral subsampling ratios.");
    }
    c->width_in_blocks = MCU_cols * c->h_samp_factor;
    c->height_in_blocks = MCU_rows * c->v_samp_factor;
  }
  JXL_JPEG_VERIFY_MARKER_END();
  return jxl_enc_ok_status();
}

static jxl_enc_status jxl_process_sos_with_ids(const uint8_t* data, const size_t len, size_t* pos,
                         jxl_jpeg_data* jpg, size_t start_pos, size_t marker_len,
                         size_t comps_in_scan, jxl_jpeg_scan_info* scan_info,
                         jxl_array_u8* ids_seen);

// Reads the Start of Scan (SOS) marker segment and fills in *scan_info with the
// parsed data.
static jxl_enc_status jxl_process_sos(const uint8_t* data, const size_t len, size_t* pos,
                  jxl_jpeg_data* jpg) {
  const size_t start_pos = *pos;
  JXL_JPEG_VERIFY_LEN(3);
  size_t marker_len = jxl_read_uint16(data, pos);
  size_t comps_in_scan = jxl_read_uint8(data, pos);
  JXL_JPEG_VERIFY_INPUT(comps_in_scan, 1, jxl_array_len(&jpg->components));

  jxl_jpeg_scan_info scan_info;
  jxl_jpeg_scan_info_construct_empty(&scan_info);
  scan_info.num_components = comps_in_scan;
  JXL_JPEG_VERIFY_LEN(2 * comps_in_scan);
  jxl_array_u8 ids_seen;
  jxl_array_construct_empty(&ids_seen, jpg->huffman_code.ctx);
  jxl_enc_status status = jxl_process_sos_with_ids(data, len, pos, jpg, start_pos, marker_len,
                                    comps_in_scan, &scan_info, &ids_seen);
  jxl_array_destroy(&ids_seen);
  return status;
}

static jxl_enc_status jxl_process_sos_with_ids(const uint8_t* data, const size_t len, size_t* pos,
                         jxl_jpeg_data* jpg, size_t start_pos, size_t marker_len,
                         size_t comps_in_scan, jxl_jpeg_scan_info* scan_info,
                         jxl_array_u8* ids_seen) {
  if (!jxl_enc_status_ok(jxl_array_u8_resize_fill(ids_seen, 256, (uint8_t)(0)))) {
    return JXL_FAILURE("OOM");
  }
  for (size_t i = 0; i < comps_in_scan; ++i) {
    uint32_t id = jxl_read_uint8(data, pos);
    if (*jxl_array_at(ids_seen, id)) {  // (cf. section B.2.3, regarding CSj)
      return JXL_FAILURE("Duplicate ID %d in SOS.", id);
    }
    *jxl_array_at(ids_seen, id) = true;
    bool found_index = false;
    for (size_t j = 0; j < jxl_array_len(&jpg->components); ++j) {
      if (jxl_array_at(&jpg->components, j)->id == id) {
        scan_info->components[i].comp_idx = j;
        found_index = true;
      }
    }
    if (!found_index) {
      return JXL_FAILURE("SOS marker: Could not find component with id %d", id);
    }
    int c = jxl_read_uint8(data, pos);
    int dc_tbl_idx = c >> 4;
    int ac_tbl_idx = c & 0xf;
    JXL_JPEG_VERIFY_INPUT(dc_tbl_idx, 0, 3);
    JXL_JPEG_VERIFY_INPUT(ac_tbl_idx, 0, 3);
    scan_info->components[i].dc_tbl_idx = dc_tbl_idx;
    scan_info->components[i].ac_tbl_idx = ac_tbl_idx;
  }
  JXL_JPEG_VERIFY_LEN(3);
  scan_info->jxl_ss = jxl_read_uint8(data, pos);
  scan_info->Se = jxl_read_uint8(data, pos);
  JXL_JPEG_VERIFY_INPUT((int)(scan_info->jxl_ss), 0, 63);
  JXL_JPEG_VERIFY_INPUT(scan_info->Se, scan_info->jxl_ss, 63);
  int c = jxl_read_uint8(data, pos);
  scan_info->Ah = c >> 4;
  scan_info->Al = c & 0xf;
  if (scan_info->Ah != 0 && scan_info->Al != scan_info->Ah - 1) {
    // section G.1.1.1.2 : Successive approximation control only improves
    // by one bit at a time. But it's not always respected, so we just issue
    // a warning.
    JXL_WARNING("Invalid progressive parameters: Al=%d Ah=%d", scan_info->Al,
                scan_info->Ah);
  }
  // Check that all the Huffman tables needed for this scan are defined.
  for (size_t i = 0; i < comps_in_scan; ++i) {
    bool found_dc_table = false;
    bool found_ac_table = false;
    for (size_t code_i = 0; code_i < jxl_array_len(&jpg->huffman_code); ++code_i) {
      const jxl_jpeg_huffman_code* code = jxl_array_at(&jpg->huffman_code, code_i);
      uint32_t slot_id = code->slot_id;
      if (slot_id == scan_info->components[i].dc_tbl_idx) {
        found_dc_table = true;
      } else if (slot_id == scan_info->components[i].ac_tbl_idx + 16) {
        found_ac_table = true;
      }
    }
    if (scan_info->jxl_ss == 0 && !found_dc_table) {
      return JXL_FAILURE(
          "SOS marker: Could not find DC Huffman table with index %d",
          scan_info->components[i].dc_tbl_idx);
    }
    if (scan_info->Se > 0 && !found_ac_table) {
      return JXL_FAILURE(
          "SOS marker: Could not find AC Huffman table with index %d",
          scan_info->components[i].ac_tbl_idx);
    }
  }
  JXL_RETURN_IF_ERROR(jxl_array_jpeg_scan_info_push_back(&jpg->scan_info, *scan_info));
  JXL_RETURN_IF_ERROR(jxl_u32_chunks_push_empty(&jpg->scan_reset_points));
  JXL_RETURN_IF_ERROR(jxl_extra_zero_run_chunks_push_empty(&jpg->scan_extra_zero_runs));
  JXL_JPEG_VERIFY_MARKER_END();
  return jxl_enc_ok_status();
}

static jxl_enc_status jxl_process_dht_values(const uint8_t* data, const size_t len, size_t* pos,
                        jxl_jpeg_data* jpg, size_t start_pos, size_t marker_len,
                        int total_count, int max_depth, int space,
                        bool is_ac_table, jxl_huffman_table_entry* huff_lut,
                        jxl_jpeg_huffman_code* huff, jxl_array_u8* values_seen);

// Reads the Define Huffman Table (DHT) marker segment and fills in *jpg with
// the parsed data. Builds the Huffman decoding table in either dc_huff_lut or
// ac_huff_lut, depending on the type and slot_id of Huffman code being read.
static jxl_enc_status jxl_process_dht(const uint8_t* data, const size_t len,
                  jxl_array_huffman_table_entry* dc_huff_lut,
                  jxl_array_huffman_table_entry* ac_huff_lut, size_t* pos,
                  jxl_jpeg_data* jpg) {
  const size_t start_pos = *pos;
  JXL_JPEG_VERIFY_LEN(2);
  size_t marker_len = jxl_read_uint16(data, pos);
  if (marker_len == 2) {
    // Empty DHT marker. Useless but does seem to occur in the wild.
    // We represent this situation with a dummy all-zeroes Huffman table.
    jxl_jpeg_huffman_code huff;
    jxl_jpeg_huffman_code_construct_empty(&huff);
    huff.is_last = true;
if (!jxl_enc_status_ok(jxl_array_jpeg_huffman_code_push_back(&jpg->huffman_code, huff))) JXL_CRASH();
return jxl_enc_ok_status();
  }
  while (*pos < start_pos + marker_len) {
    JXL_JPEG_VERIFY_LEN(1 + kJpegHuffmanMaxBitLength);
    jxl_jpeg_huffman_code huff;
    jxl_jpeg_huffman_code_construct_empty(&huff);
    huff.slot_id = jxl_read_uint8(data, pos);
    int huffman_index = huff.slot_id;
    bool is_ac_table = ((huff.slot_id & 0x10) != 0);
    jxl_huffman_table_entry* huff_lut;
    if (is_ac_table) {
      huffman_index -= 0x10;
      JXL_JPEG_VERIFY_INPUT(huffman_index, 0, 3);
      huff_lut = jxl_array_at(ac_huff_lut, huffman_index * kJpegHuffmanLutSize);
    } else {
      JXL_JPEG_VERIFY_INPUT(huffman_index, 0, 3);
      huff_lut = jxl_array_at(dc_huff_lut, huffman_index * kJpegHuffmanLutSize);
    }
    huff.counts[0] = 0;
    int total_count = 0;
    int space = 1 << kJpegHuffmanMaxBitLength;
    int max_depth = 1;
    for (size_t i = 1; i <= kJpegHuffmanMaxBitLength; ++i) {
      int count = jxl_read_uint8(data, pos);
      if (count != 0) {
        max_depth = i;
      }
      huff.counts[i] = count;
      total_count += count;
      space -= count * (1 << (kJpegHuffmanMaxBitLength - i));
    }
    if (is_ac_table) {
      JXL_JPEG_VERIFY_INPUT(total_count, 0, kJpegHuffmanAlphabetSize);
    } else {
      JXL_JPEG_VERIFY_INPUT(total_count, 0, kJpegDCAlphabetSize);
    }
    JXL_JPEG_VERIFY_LEN(total_count);
    jxl_array_u8 values_seen;
    jxl_array_construct_empty(&values_seen, jpg->huffman_code.ctx);
    jxl_enc_status values_status = jxl_process_dht_values(
        data, len, pos, jpg, start_pos, marker_len, total_count, max_depth,
        space, is_ac_table, huff_lut, &huff, &values_seen);
    jxl_array_destroy(&values_seen);
    if (!jxl_enc_status_ok(values_status)) return values_status;
  }
  JXL_JPEG_VERIFY_MARKER_END();
  return jxl_enc_ok_status();
}

static jxl_enc_status jxl_process_dht_values(const uint8_t* data, const size_t len, size_t* pos,
                        jxl_jpeg_data* jpg, size_t start_pos, size_t marker_len,
                        int total_count, int max_depth, int space,
                        bool is_ac_table, jxl_huffman_table_entry* huff_lut,
                        jxl_jpeg_huffman_code* huff, jxl_array_u8* values_seen) {
  if (!jxl_enc_status_ok(jxl_array_u8_resize_fill(values_seen, 256, (uint8_t)(0)))) {
    return JXL_FAILURE("OOM");
  }
  for (int i = 0; i < total_count; ++i) {
    int value = jxl_read_uint8(data, pos);
    if (!is_ac_table) {
      JXL_JPEG_VERIFY_INPUT(value, 0, kJpegDCAlphabetSize - 1);
    }
    if (*jxl_array_at(values_seen, value)) {
      return JXL_FAILURE("Duplicate Huffman code value %d", value);
    }
    *jxl_array_at(values_seen, value) = true;
    huff->values[i] = value;
  }
  // Add an invalid symbol that will have the all 1 code.
  ++huff->counts[max_depth];
  huff->values[total_count] = kJpegHuffmanAlphabetSize;
  space -= (1 << (kJpegHuffmanMaxBitLength - max_depth));
  if (space < 0) {
    return JXL_FAILURE("Invalid Huffman code lengths.");
  } else if (space > 0 && huff_lut[0].value != 0xffff) {
    // Re-initialize the values to an invalid symbol so that we can recognize
    // it when reading the bit stream using a Huffman code with space > 0.
    for (int i = 0; i < kJpegHuffmanLutSize; ++i) {
      huff_lut[i].bits = 0;
      huff_lut[i].value = 0xffff;
    }
  }
  huff->is_last = (*pos == start_pos + marker_len);
  jxl_build_jpeg_huffman_table(huff->counts, huff->values, huff_lut);
  if (!jxl_enc_status_ok(jxl_array_jpeg_huffman_code_push_back(&jpg->huffman_code, *huff))) JXL_CRASH();
  return jxl_enc_ok_status();
}

// Reads the Define Quantization Table (DQT) marker segment and fills in *jpg
// with the parsed data.
static jxl_enc_status jxl_process_dqt(const uint8_t* data, const size_t len, size_t* pos,
                  jxl_jpeg_data* jpg) {
  const size_t start_pos = *pos;
  JXL_JPEG_VERIFY_LEN(2);
  size_t marker_len = jxl_read_uint16(data, pos);
  if (marker_len == 2) {
    return JXL_FAILURE("DQT marker: no quantization table found");
  }
  while (*pos < start_pos + marker_len && jxl_array_len(&jpg->quant) < kMaxQuantTables) {
    JXL_JPEG_VERIFY_LEN(1);
    int quant_table_index = jxl_read_uint8(data, pos);
    int quant_table_precision = quant_table_index >> 4;
    JXL_JPEG_VERIFY_INPUT(quant_table_precision, 0, 1);
    quant_table_index &= 0xf;
    JXL_JPEG_VERIFY_INPUT(quant_table_index, 0, 3);
    JXL_JPEG_VERIFY_LEN((quant_table_precision + 1) * kDCTBlockSize);
    jxl_jpeg_quant_table table;
    jxl_jpeg_quant_table_construct_empty(&table);
    table.index = quant_table_index;
    table.precision = quant_table_precision;
    for (size_t i = 0; i < kDCTBlockSize; ++i) {
      int quant_val =
          quant_table_precision ? jxl_read_uint16(data, pos) : jxl_read_uint8(data, pos);
      JXL_JPEG_VERIFY_INPUT(quant_val, 1, 65535);
      table.values[kJPEGNaturalOrder[i]] = quant_val;
    }
    table.is_last = (*pos == start_pos + marker_len);
if (!jxl_enc_status_ok(jxl_array_jpeg_quant_table_push_back(&jpg->quant, table))) JXL_CRASH();
}
  JXL_JPEG_VERIFY_MARKER_END();
  return jxl_enc_ok_status();
}

// Reads the DRI marker and saves the restart interval into *jpg.
static jxl_enc_status jxl_process_dri(const uint8_t* data, const size_t len, size_t* pos,
                  bool* found_dri, jxl_jpeg_data* jpg) {
  if (*found_dri) {
    return JXL_FAILURE("Duplicate DRI marker.");
  }
  *found_dri = true;
  const size_t start_pos = *pos;
  JXL_JPEG_VERIFY_LEN(4);
  size_t marker_len = jxl_read_uint16(data, pos);
  int restart_interval = jxl_read_uint16(data, pos);
  jpg->restart_interval = restart_interval;
  JXL_JPEG_VERIFY_MARKER_END();
  return jxl_enc_ok_status();
}

// Saves the APP marker segment as a string to *jpg.
static jxl_enc_status jxl_process_app(const uint8_t* data, const size_t len, size_t* pos,
                  jxl_jpeg_data* jpg) {
  JXL_JPEG_VERIFY_LEN(2);
  size_t marker_len = jxl_read_uint16(data, pos);
  JXL_JPEG_VERIFY_INPUT(marker_len, 2, 65535);
  JXL_JPEG_VERIFY_LEN(marker_len - 2);
  JXL_ENSURE(*pos >= 3);
  // Save the marker type together with the app data.
  const uint8_t* app_str_start = data + *pos - 3;
  jxl_array_u8 app_str;
  jxl_array_construct_empty(&app_str, jpg->huffman_code.ctx);
  jxl_enc_status status = jxl_array_assign(&app_str, app_str_start, marker_len + 1);
  if (!jxl_enc_status_ok(status)) {
    jxl_array_destroy(&app_str);
    return status;
  }
  *pos += marker_len - 2;
  status = jxl_byte_chunks_push_back_u8(&jpg->app_data, &app_str);
  jxl_array_destroy(&app_str);
  return status;
}

// Saves the COM marker segment as a string to *jpg.
static jxl_enc_status jxl_process_com(const uint8_t* data, const size_t len, size_t* pos,
                  jxl_jpeg_data* jpg) {
  JXL_JPEG_VERIFY_LEN(2);
  size_t marker_len = jxl_read_uint16(data, pos);
  JXL_JPEG_VERIFY_INPUT(marker_len, 2, 65535);
  JXL_JPEG_VERIFY_LEN(marker_len - 2);
  const uint8_t* com_str_start = data + *pos - 3;
  jxl_array_u8 com_str;
  jxl_array_construct_empty(&com_str, jpg->huffman_code.ctx);
  jxl_enc_status status = jxl_array_assign(&com_str, com_str_start, marker_len + 1);
  if (!jxl_enc_status_ok(status)) {
    jxl_array_destroy(&com_str);
    return status;
  }
  *pos += marker_len - 2;
  status = jxl_byte_chunks_push_back_u8(&jpg->com_data, &com_str);
  jxl_array_destroy(&com_str);
  return status;
}

// Helper structure to read bits from the entropy coded data segment.
typedef struct jxl_bit_reader_state jxl_bit_reader_state;
static void jxl_bit_reader_state_init(jxl_bit_reader_state* self, const uint8_t* data, size_t len,
                        size_t pos);
static void jxl_bit_reader_state_reset(jxl_bit_reader_state* self, size_t pos);
static uint8_t jxl_bit_reader_state_get_next_byte(jxl_bit_reader_state* self);
static void jxl_bit_reader_state_fill_bit_window(jxl_bit_reader_state* self);
static int jxl_bit_reader_state_read_bits(jxl_bit_reader_state* self, int nbits);
static jxl_enc_status jxl_bit_reader_state_finish_stream(jxl_bit_reader_state* self, jxl_jpeg_data* jpg,
                                  size_t* pos);

struct jxl_bit_reader_state {
  const uint8_t* data_;
  size_t len_;
  size_t pos_;
  uint64_t val_;
  int bits_left_;
  size_t next_marker_pos_;
};

static void jxl_bit_reader_state_init(jxl_bit_reader_state* self, const uint8_t* data, size_t len,
                        size_t pos) {
  self->data_ = data;
  self->len_ = len;
  jxl_bit_reader_state_reset(self, pos);
}

static void jxl_bit_reader_state_reset(jxl_bit_reader_state* self, size_t pos) {
  self->pos_ = pos;
  self->val_ = 0;
  self->bits_left_ = 0;
  self->next_marker_pos_ = self->len_ - 2;
  jxl_bit_reader_state_fill_bit_window(self);
}

// Returns the next byte and skips the 0xff/0x00 escape sequences.
static uint8_t jxl_bit_reader_state_get_next_byte(jxl_bit_reader_state* self) {
  if (self->pos_ >= self->next_marker_pos_) {
    ++self->pos_;
    return 0;
  }
  uint8_t c = self->data_[self->pos_++];
  if (c == 0xff) {
    uint8_t escape = self->data_[self->pos_];
    if (escape == 0) {
      ++self->pos_;
    } else {
      // 0xff was followed by a non-zero byte, which means that we found the
      // start of the next marker segment.
      self->next_marker_pos_ = self->pos_ - 1;
    }
  }
  return c;
}

static void jxl_bit_reader_state_fill_bit_window(jxl_bit_reader_state* self) {
  if (self->bits_left_ <= 16) {
    while (self->bits_left_ <= 56) {
      self->val_ <<= 8;
      self->val_ |= (uint64_t)(jxl_bit_reader_state_get_next_byte(self));
      self->bits_left_ += 8;
    }
  }
}

static int jxl_bit_reader_state_read_bits(jxl_bit_reader_state* self, int nbits) {
  jxl_bit_reader_state_fill_bit_window(self);
  uint64_t val =
      (self->val_ >> (self->bits_left_ - nbits)) & ((1ULL << nbits) - 1);
  self->bits_left_ -= nbits;
  return val;
}

// Sets *pos to the next stream position where parsing should continue.
// Enqueue the padding bits seen (0 or 1).
// Returns error if there is inconsistent or invalid padding or the stream
// ended too early.
static jxl_enc_status jxl_bit_reader_state_finish_stream(jxl_bit_reader_state* self, jxl_jpeg_data* jpg,
                                  size_t* pos) {
  int npadbits = self->bits_left_ & 7;
  if (npadbits > 0) {
    uint64_t padmask = (1ULL << npadbits) - 1;
    uint64_t padbits =
        (self->val_ >> (self->bits_left_ - npadbits)) & padmask;
    if (padbits != padmask) {
      jpg->has_zero_padding_bit = true;
    }
    for (int i = npadbits - 1; i >= 0; --i) {
      JXL_RETURN_IF_ERROR(jxl_array_u8_push_back(
          &jpg->padding_bits, (uint8_t)((padbits >> i) & 1)));
    }
  }
  // Give back some bytes that we did not use.
  int unused_bytes_left = self->bits_left_ >> 3;
  while (unused_bytes_left-- > 0) {
    --self->pos_;
    // If we give back a 0 byte, we need to check if it was a 0xff/0x00 escape
    // sequence, and if yes, we need to give back one more byte.
    if (self->pos_ < self->next_marker_pos_ && self->data_[self->pos_] == 0 &&
        self->data_[self->pos_ - 1] == 0xff) {
      --self->pos_;
    }
  }
  if (self->pos_ > self->next_marker_pos_) {
    // Data ran out before the scan was complete.
    return JXL_FAILURE("Unexpected end of scan.");
  }
  *pos = self->pos_;
  return jxl_enc_ok_status();
}

// Returns the next Huffman-coded symbol.
static int jxl_read_symbol(const jxl_huffman_table_entry* table, jxl_bit_reader_state* br) {
  int nbits;
  jxl_bit_reader_state_fill_bit_window(br);
  int val = (br->val_ >> (br->bits_left_ - 8)) & 0xff;
  table += val;
  nbits = table->bits - 8;
  if (nbits > 0) {
    br->bits_left_ -= 8;
    table += table->value;
    val = (br->val_ >> (br->bits_left_ - nbits)) & ((1 << nbits) - 1);
    table += val;
  }
  br->bits_left_ -= table->bits;
  return table->value;
}

/**
 * Returns the DC diff or AC value for extra bits value x and prefix code s.
 *
 * CCITT Rec. T.81 (1992 E)
 * Table F.1 – Difference magnitude categories for DC coding
 *  SSSS | DIFF values
 * ------+--------------------------
 *     0 | 0
 *     1 | –1, 1
 *     2 | –3, –2, 2, 3
 *     3 | –7..–4, 4..7
 * ......|..........................
 *    11 | –2047..–1024, 1024..2047
 *
 * CCITT Rec. T.81 (1992 E)
 * Table F.2 – Categories assigned to coefficient values
 * [ Same as Table F.1, but does not include SSSS equal to 0 and 11]
 *
 *
 * CCITT Rec. T.81 (1992 E)
 * F.1.2.1.1 Structure of DC code table
 * For each category,... additional bits... appended... to uniquely identify
 * which difference... occurred... When DIFF is positive... SSSS... bits of DIFF
 * are appended. When DIFF is negative... SSSS... bits of (DIFF – 1) are
 * appended... Most significant bit... is 0 for negative differences and 1 for
 * positive differences.
 *
 * In other words the upper half of extra bits range represents DIFF as is.
 * The lower half represents the negative DIFFs with an offset.
 */
static int jxl_huff_extend(int x, int s) {
  JXL_DASSERT(s >= 1);
  int half = 1 << (s - 1);
  if (x >= half) {
    JXL_DASSERT(x < (1 << s));
    return x;
  } else {
    return x - (1 << s) + 1;
  }
}

// Decodes one 8x8 block of DCT coefficients from the bit stream.
static jxl_enc_status jxl_decode_dct_block(const jxl_huffman_table_entry* dc_huff,
                      const jxl_huffman_table_entry* ac_huff, int jxl_ss, int Se, int Al,
                      int* eobrun, bool* reset_state, int* num_zero_runs,
                      jxl_bit_reader_state* br, jxl_jpeg_data* jpg, jxl_jpeg_coeff* last_dc_coeff,
                      jxl_jpeg_coeff* coeffs) {
  // Nowadays multiplication is even faster than variable shift.
  int Am = 1 << Al;
  bool eobrun_allowed = jxl_ss > 0;
  if (jxl_ss == 0) {
    int s = jxl_read_symbol(dc_huff, br);
    if (s >= kJpegDCAlphabetSize) {
      return JXL_FAILURE("Invalid Huffman symbol %d  for DC coefficient.", s);
    }
    int diff = 0;
    if (s > 0) {
      int bits = jxl_bit_reader_state_read_bits(br, s);
      diff = jxl_huff_extend(bits, s);
    }
    int coeff = diff + *last_dc_coeff;
    const int dc_coeff = coeff * Am;
    coeffs[0] = dc_coeff;
    // TODO(eustas): is there a more elegant / explicit way to check this?
    if (dc_coeff != coeffs[0]) {
      return JXL_FAILURE("Invalid DC coefficient %d", dc_coeff);
    }
    *last_dc_coeff = coeff;
    ++jxl_ss;
  }
  if (jxl_ss > Se) {
    return jxl_enc_ok_status();
  }
  if (*eobrun > 0) {
    --(*eobrun);
    return jxl_enc_ok_status();
  }
  *num_zero_runs = 0;
  for (int k = jxl_ss; k <= Se; k++) {
    int sr = jxl_read_symbol(ac_huff, br);
    if (sr >= kJpegHuffmanAlphabetSize) {
      return JXL_FAILURE("Invalid Huffman symbol %d for AC coefficient %d", sr,
                         k);
    }
    int r = sr >> 4;
    int s = sr & 15;
    if (s > 0) {
      k += r;
      if (k > Se) {
        return JXL_FAILURE("Out-of-band coefficient %d band was %d-%d", k, jxl_ss,
                           Se);
      }
      if (s + Al >= kJpegDCAlphabetSize) {
        return JXL_FAILURE(
            "Out of range AC coefficient value: s = %d Al = %d k = %d", s, Al,
            k);
      }
      int bits = jxl_bit_reader_state_read_bits(br, s);
      int coeff = jxl_huff_extend(bits, s);
      coeffs[kJPEGNaturalOrder[k]] = coeff * Am;
      *num_zero_runs = 0;
    } else if (r == 15) {
      k += 15;
      ++(*num_zero_runs);
    } else {
      if (eobrun_allowed && k == jxl_ss && *eobrun == 0) {
        // We have two end-of-block runs right after each other, so we signal
        // the jpeg encoder to force a state reset at this point.
        *reset_state = true;
      }
      *eobrun = 1 << r;
      if (r > 0) {
        if (!eobrun_allowed) {
          return JXL_FAILURE("End-of-block run crossing DC coeff.");
        }
        *eobrun += jxl_bit_reader_state_read_bits(br, r);
      }
      break;
    }
  }
  --(*eobrun);
  return jxl_enc_ok_status();
}

static jxl_enc_status jxl_refine_dct_block(const jxl_huffman_table_entry* ac_huff, int jxl_ss, int Se, int Al,
                      int* eobrun, bool* reset_state, jxl_bit_reader_state* br,
                      jxl_jpeg_data* jpg, jxl_jpeg_coeff* coeffs) {
  // Nowadays multiplication is even faster than variable shift.
  int Am = 1 << Al;
  bool eobrun_allowed = jxl_ss > 0;
  if (jxl_ss == 0) {
    int s = jxl_bit_reader_state_read_bits(br, 1);
    jxl_jpeg_coeff dc_coeff = coeffs[0];
    dc_coeff |= s * Am;
    coeffs[0] = dc_coeff;
    ++jxl_ss;
  }
  if (jxl_ss > Se) {
    return jxl_enc_ok_status();
  }
  int p1 = Am;
  int m1 = -Am;
  int k = jxl_ss;
  int r;
  int s;
  bool in_zero_run = false;
  if (*eobrun <= 0) {
    for (; k <= Se; k++) {
      s = jxl_read_symbol(ac_huff, br);
      if (s >= kJpegHuffmanAlphabetSize) {
        return JXL_FAILURE("Invalid Huffman symbol %d for AC coefficient %d", s,
                           k);
      }
      r = s >> 4;
      s &= 15;
      if (s) {
        if (s != 1) {
          return JXL_FAILURE("Invalid Huffman symbol %d for AC coefficient %d",
                             s, k);
        }
        s = jxl_bit_reader_state_read_bits(br, 1) ? p1 : m1;
        in_zero_run = false;
      } else {
        if (r != 15) {
          if (eobrun_allowed && k == jxl_ss && *eobrun == 0) {
            // We have two end-of-block runs right after each other, so we
            // signal the jpeg encoder to force a state reset at this point.
            *reset_state = true;
          }
          *eobrun = 1 << r;
          if (r > 0) {
            if (!eobrun_allowed) {
              return JXL_FAILURE("End-of-block run crossing DC coeff.");
            }
            *eobrun += jxl_bit_reader_state_read_bits(br, r);
          }
          break;
        }
        in_zero_run = true;
      }
      do {
        jxl_jpeg_coeff thiscoef = coeffs[kJPEGNaturalOrder[k]];
        if (thiscoef != 0) {
          if (jxl_bit_reader_state_read_bits(br, 1)) {
            if ((thiscoef & p1) == 0) {
              if (thiscoef >= 0) {
                thiscoef += p1;
              } else {
                thiscoef += m1;
              }
            }
          }
          coeffs[kJPEGNaturalOrder[k]] = thiscoef;
        } else {
          if (--r < 0) {
            break;
          }
        }
        k++;
      } while (k <= Se);
      if (s) {
        if (k > Se) {
          return JXL_FAILURE("Out-of-band coefficient %d band was %d-%d", k, jxl_ss,
                             Se);
        }
        coeffs[kJPEGNaturalOrder[k]] = s;
      }
    }
  }
  if (in_zero_run) {
    return JXL_FAILURE("Extra zero run before end-of-block.");
  }
  if (*eobrun > 0) {
    for (; k <= Se; k++) {
      jxl_jpeg_coeff thiscoef = coeffs[kJPEGNaturalOrder[k]];
      if (thiscoef != 0) {
        if (jxl_bit_reader_state_read_bits(br, 1)) {
          if ((thiscoef & p1) == 0) {
            if (thiscoef >= 0) {
              thiscoef += p1;
            } else {
              thiscoef += m1;
            }
          }
        }
        coeffs[kJPEGNaturalOrder[k]] = thiscoef;
      }
    }
  }
  --(*eobrun);
  return jxl_enc_ok_status();
}

static jxl_enc_status jxl_process_restart(const uint8_t* data, const size_t len,
                      int* next_restart_marker, jxl_bit_reader_state* br,
                      jxl_jpeg_data* jpg) {
  size_t pos = 0;
  JXL_RETURN_IF_ERROR(jxl_bit_reader_state_finish_stream(br, jpg, &pos));
  int expected_marker = 0xd0 + *next_restart_marker;
  JXL_JPEG_EXPECT_MARKER();
  int marker = data[pos + 1];
  if (marker != expected_marker) {
    return JXL_FAILURE("Did not find expected restart marker %d actual %d",
                       expected_marker, marker);
  }
  jxl_bit_reader_state_reset(br, pos + 2);
  *next_restart_marker += 1;
  *next_restart_marker &= 0x7;
  return jxl_enc_ok_status();
}

static jxl_enc_status jxl_process_scan(const uint8_t* data, const size_t len,
                   const jxl_array_huffman_table_entry* dc_huff_lut,
                   const jxl_array_huffman_table_entry* ac_huff_lut,
                   uint16_t scan_progression[kJpegMaxComponents][kDCTBlockSize],
                   bool is_progressive, size_t* pos, jxl_jpeg_data* jpg) {
  JXL_RETURN_IF_ERROR(jxl_process_sos(data, len, pos, jpg));
  jxl_jpeg_scan_info* scan_info = jxl_array_back_ptr(&jpg->scan_info);
  bool is_interleaved = (scan_info->num_components > 1);
  int max_h_samp_factor = 1;
  int max_v_samp_factor = 1;
  for (size_t component_i = 0; component_i < jxl_array_len(&jpg->components); ++component_i) {
    const jxl_jpeg_component* component = jxl_array_at(&jpg->components, component_i);
    max_h_samp_factor = JXL_MAX(max_h_samp_factor, component->h_samp_factor);
    max_v_samp_factor = JXL_MAX(max_v_samp_factor, component->v_samp_factor);
  }

  int MCU_rows = jxl_div_ceil(jpg->height, max_v_samp_factor * 8);
  int MCUs_per_row = jxl_div_ceil(jpg->width, max_h_samp_factor * 8);
  if (!is_interleaved) {
    const jxl_jpeg_component* c = jxl_array_at(&jpg->components, scan_info->components[0].comp_idx);
    MCUs_per_row = jxl_div_ceil(jpg->width * c->h_samp_factor, 8 * max_h_samp_factor);
    MCU_rows = jxl_div_ceil(jpg->height * c->v_samp_factor, 8 * max_v_samp_factor);
  }
  jxl_jpeg_coeff last_dc_coeff[kJpegMaxComponents];
  memset(last_dc_coeff, 0, sizeof(last_dc_coeff));
  jxl_bit_reader_state br;
  jxl_bit_reader_state_init(&br, data, len, *pos);
  int restarts_to_go = jpg->restart_interval;
  int next_restart_marker = 0;
  int eobrun = -1;
  int block_scan_index = 0;
  const int Al = is_progressive ? scan_info->Al : 0;
  const int Ah = is_progressive ? scan_info->Ah : 0;
  const int jxl_ss = is_progressive ? scan_info->jxl_ss : 0;
  const int Se = is_progressive ? scan_info->Se : 63;
  const uint16_t scan_bitmask = Ah == 0 ? (0xffff << Al) : (1u << Al);
  const uint16_t refinement_bitmask = (1 << Al) - 1;
  for (size_t i = 0; i < scan_info->num_components; ++i) {
    int comp_idx = scan_info->components[i].comp_idx;
    for (int k = jxl_ss; k <= Se; ++k) {
      if (scan_progression[comp_idx][k] & scan_bitmask) {
        return JXL_FAILURE(
            "Overlapping scans: component=%d k=%d prev_mask: %u cur_mask %u",
            comp_idx, k, scan_progression[i][k], scan_bitmask);
      }
      if (scan_progression[comp_idx][k] & refinement_bitmask) {
        return JXL_FAILURE(
            "Invalid scan order, a more refined scan was already done: "
            "component=%d k=%d prev_mask=%u cur_mask=%u",
            comp_idx, k, scan_progression[i][k], scan_bitmask);
      }
      scan_progression[comp_idx][k] |= scan_bitmask;
    }
  }
  if (Al > 10) {
    return JXL_FAILURE("Scan parameter Al=%d is not supported.", Al);
  }

  for (size_t ci = 0; ci < jxl_array_len(&jpg->components); ++ci) {
    jxl_jpeg_component* c = jxl_array_at(&jpg->components, ci);
    jxl_array_i16* coeffs = &jpg->component_coeffs[ci];
    if (jxl_array_empty(coeffs)) {
      const uint64_t num_blocks =
          (uint64_t)(c->width_in_blocks) * c->height_in_blocks;
      JXL_RETURN_IF_ERROR(jxl_array_resize_zero(coeffs, num_blocks * kDCTBlockSize));
    }
  }

  for (int mcu_y = 0; mcu_y < MCU_rows; ++mcu_y) {
    for (int mcu_x = 0; mcu_x < MCUs_per_row; ++mcu_x) {
      // Handle the restart intervals.
      if (jpg->restart_interval > 0) {
        if (restarts_to_go == 0) {
          if (jxl_enc_status_ok(jxl_process_restart(data, len, &next_restart_marker, &br, jpg))) {
            restarts_to_go = jpg->restart_interval;
            memset((void*)(last_dc_coeff), 0, sizeof(last_dc_coeff));
            if (eobrun > 0) {
              return JXL_FAILURE("End-of-block run too long.");
            }
            eobrun = -1;  // fresh start
          } else {
            return JXL_FAILURE("Could not process restart.");
          }
        }
        --restarts_to_go;
      }
      // Decode one MCU.
      for (size_t i = 0; i < scan_info->num_components; ++i) {
        jxl_jpeg_component_scan_info* si = &scan_info->components[i];
        jxl_jpeg_component* c = jxl_array_at(&jpg->components, si->comp_idx);
        jxl_array_i16* component_coeffs =
            &jpg->component_coeffs[si->comp_idx];
        const jxl_huffman_table_entry* dc_lut =
            jxl_array_at_const(dc_huff_lut, si->dc_tbl_idx * kJpegHuffmanLutSize);
        const jxl_huffman_table_entry* ac_lut =
            jxl_array_at_const(ac_huff_lut, si->ac_tbl_idx * kJpegHuffmanLutSize);
        int nblocks_y = is_interleaved ? c->v_samp_factor : 1;
        int nblocks_x = is_interleaved ? c->h_samp_factor : 1;
        for (int iy = 0; iy < nblocks_y; ++iy) {
          for (int ix = 0; ix < nblocks_x; ++ix) {
            int block_y = mcu_y * nblocks_y + iy;
            int block_x = mcu_x * nblocks_x + ix;
            size_t block_idx =
                (size_t)(block_y) * c->width_in_blocks +
                (size_t)(block_x);
            bool reset_state = false;
            int num_zero_runs = 0;
            jxl_jpeg_coeff* coeffs = jxl_array_at(component_coeffs, block_idx * kDCTBlockSize);
            if (Ah == 0) {
              JXL_RETURN_IF_ERROR(
                  jxl_decode_dct_block(dc_lut, ac_lut, jxl_ss, Se, Al, &eobrun,
                                 &reset_state, &num_zero_runs, &br, jpg,
                                 &last_dc_coeff[si->comp_idx], coeffs));
            } else {
              JXL_RETURN_IF_ERROR(jxl_refine_dct_block(
                  ac_lut, jxl_ss, Se, Al, &eobrun, &reset_state, &br, jpg, coeffs));
            }
            if (reset_state) {
              JXL_RETURN_IF_ERROR(jxl_u32_chunks_push_to_last(
                  &jpg->scan_reset_points, (uint32_t)(block_scan_index)));
            }
            if (num_zero_runs > 0) {
              jxl_jpeg_extra_zero_run_info info;
              info.block_idx = block_scan_index;
              info.num_extra_zero_runs = num_zero_runs;
              JXL_RETURN_IF_ERROR(
                  jxl_extra_zero_run_chunks_push_to_last(&jpg->scan_extra_zero_runs, info));
            }
            ++block_scan_index;
          }
        }
      }
    }
  }
  if (eobrun > 0) {
    return JXL_FAILURE("End-of-block run too long.");
  }
  if (!jxl_enc_status_ok(jxl_bit_reader_state_finish_stream(&br, jpg, pos))) {
    return JXL_FAILURE("Invalid scan.");
  }
  if (*pos > len) {
    return JXL_FAILURE("Unexpected end of file during scan. pos=%" jxl_pr_iu_s
                       " len=%" jxl_pr_iu_s,
                       *pos, len);
  }
  return jxl_enc_ok_status();
}

// Changes the quant_idx field of the components to refer to the index of the
// quant table in the jpg->quant array.
static jxl_enc_status jxl_fixup_indexes(jxl_jpeg_data* jpg) {
  for (size_t i = 0; i < jxl_array_len(&jpg->components); ++i) {
    jxl_jpeg_component* c = jxl_array_at(&jpg->components, i);
    bool found_index = false;
    for (size_t j = 0; j < jxl_array_len(&jpg->quant); ++j) {
      if (jxl_array_at(&jpg->quant, j)->index == c->quant_idx) {
        c->quant_idx = j;
        found_index = true;
        break;
      }
    }
    if (!found_index) {
      return JXL_FAILURE("Quantization table with index %u not found",
                         c->quant_idx);
    }
  }
  return jxl_enc_ok_status();
}

static size_t jxl_find_next_marker(const uint8_t* data, const size_t len, size_t pos) {
  // kIsValidMarker[i] == 1 means (0xc0 + i) is a valid marker.
  static const uint8_t kIsValidMarker[] = {
      1, 1, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1,
      1, 1, 0, 1, 1, 1, 0, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
      1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0,
  };
  size_t num_skipped = 0;
  while (pos + 1 < len && (data[pos] != 0xff || data[pos + 1] < 0xc0 ||
                           !kIsValidMarker[data[pos + 1] - 0xc0])) {
    ++pos;
    ++num_skipped;
  }
  return num_skipped;
}

static jxl_enc_status jxl_read_jpeg_after_soi(const uint8_t* data, const size_t len, jxl_jpeg_data* jpg,
                        size_t pos, jxl_array_huffman_table_entry* dc_huff_lut,
                        jxl_array_huffman_table_entry* ac_huff_lut) {
  int lut_size = kMaxHuffmanTables * kJpegHuffmanLutSize;
  JXL_RETURN_IF_ERROR(
      jxl_array_huffman_table_entry_resize_fill(dc_huff_lut, lut_size, jxl_huffman_table_entry_make()));
  JXL_RETURN_IF_ERROR(
      jxl_array_huffman_table_entry_resize_fill(ac_huff_lut, lut_size, jxl_huffman_table_entry_make()));
  bool found_sof = false;
  bool found_sos = false;
  bool found_dri = false;
  uint16_t scan_progression[kJpegMaxComponents][kDCTBlockSize];
  memset(scan_progression, 0, sizeof(scan_progression));

  jxl_array_clear(&jpg->padding_bits);
  bool is_progressive = false;  // default
  int marker = 0;
  do {
    // Read next marker.
    size_t num_skipped = jxl_find_next_marker(data, len, pos);
    if (num_skipped > 0) {
      // Add a fake marker to indicate arbitrary in-between-markers data.
      JXL_RETURN_IF_ERROR(
          jxl_array_u8_push_back(&jpg->marker_order, (uint8_t)(0xff)));
      jxl_array_u8 skipped;
      jxl_array_construct_empty(&skipped, jpg->huffman_code.ctx);
      jxl_enc_status skip_status = jxl_array_assign(&skipped, data + pos, num_skipped);
      if (!jxl_enc_status_ok(skip_status)) {
        jxl_array_destroy(&skipped);
        return skip_status;
      }
      skip_status = jxl_byte_chunks_push_back_u8(&jpg->inter_marker_data, &skipped);
      jxl_array_destroy(&skipped);
      if (!jxl_enc_status_ok(skip_status)) return skip_status;
      pos += num_skipped;
    }
    JXL_JPEG_EXPECT_MARKER();
    marker = data[pos + 1];
    pos += 2;
    switch (marker) {
      case 0xc0:
      case 0xc1:
      case 0xc2:
        is_progressive = (marker == 0xc2);
        JXL_RETURN_IF_ERROR(jxl_process_sof(data, len, &pos, jpg));
        found_sof = true;
        break;
      case 0xc4:
        JXL_RETURN_IF_ERROR(
            jxl_process_dht(data, len, dc_huff_lut, ac_huff_lut, &pos, jpg));
        break;
      case 0xd0:
      case 0xd1:
      case 0xd2:
      case 0xd3:
      case 0xd4:
      case 0xd5:
      case 0xd6:
      case 0xd7:
        // RST markers do not have any data.
        break;
      case 0xd9:
        // Found end marker.
        break;
      case 0xda:
        JXL_RETURN_IF_ERROR(jxl_process_scan(data, len, dc_huff_lut, ac_huff_lut,
                                        scan_progression, is_progressive,
                                        &pos, jpg));
        found_sos = true;
        break;
      case 0xdb:
        JXL_RETURN_IF_ERROR(jxl_process_dqt(data, len, &pos, jpg));
        break;
      case 0xdd:
        JXL_RETURN_IF_ERROR(jxl_process_dri(data, len, &pos, &found_dri, jpg));
        break;
      case 0xe0:
      case 0xe1:
      case 0xe2:
      case 0xe3:
      case 0xe4:
      case 0xe5:
      case 0xe6:
      case 0xe7:
      case 0xe8:
      case 0xe9:
      case 0xea:
      case 0xeb:
      case 0xec:
      case 0xed:
      case 0xee:
      case 0xef:
        JXL_RETURN_IF_ERROR(jxl_process_app(data, len, &pos, jpg));
        break;
      case 0xfe:
        JXL_RETURN_IF_ERROR(jxl_process_com(data, len, &pos, jpg));
        break;
      default:
        return JXL_FAILURE("Unsupported marker: %d pos=%" jxl_pr_iu_s " len=%" jxl_pr_iu_s,
                           marker, pos, len);
    }
    JXL_RETURN_IF_ERROR(
        jxl_array_u8_push_back(&jpg->marker_order, (uint8_t)(marker)));
  } while (marker != 0xd9);

  if (!found_sof) {
    return JXL_FAILURE("Missing SOF marker.");
  }
  if (!found_sos) {
    return JXL_FAILURE("Missing SOS marker.");
  }

  // Supplemental checks.
  if (pos < len) {
    JXL_RETURN_IF_ERROR(jxl_array_assign(&jpg->tail_data, data + pos, len - pos));
  }
  JXL_RETURN_IF_ERROR(jxl_fixup_indexes(jpg));
  if (jxl_array_empty(&jpg->huffman_code)) {
    // Section B.2.4.2: "If a table has never been defined for a particular
    // destination, then when this destination is specified in a scan header,
    // the results are unpredictable."
    return JXL_FAILURE("Need at least one Huffman code table.");
  }
  if (jxl_array_len(&jpg->huffman_code) >= kMaxDHTMarkers) {
    return JXL_FAILURE("Too many Huffman tables.");
  }
  return jxl_enc_ok_status();
}

jxl_enc_status jxl_read_jpeg(const uint8_t* data, const size_t len, jxl_jpeg_data* jpg) {
  size_t pos = 0;
  // Check SOI marker.
  JXL_JPEG_EXPECT_MARKER();
  int marker = data[pos + 1];
  pos += 2;
  if (marker != 0xd8) {
    return JXL_FAILURE("Did not find expected SOI marker, actual=%d", marker);
  }
  jxl_context* mm = jpg->huffman_code.ctx;
  jxl_array_huffman_table_entry dc_huff_lut;
  jxl_array_construct_empty(&dc_huff_lut, mm);
  jxl_array_huffman_table_entry ac_huff_lut;
  jxl_array_construct_empty(&ac_huff_lut, mm);
  jxl_enc_status status =
      jxl_read_jpeg_after_soi(data, len, jpg, pos, &dc_huff_lut, &ac_huff_lut);
  jxl_array_destroy(&dc_huff_lut);
  jxl_array_destroy(&ac_huff_lut);
  return status;
}
