// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// Data structures that represent the non-pixel contents of a jpeg file.

#ifndef LIB_JXL_JPEG_JPEG_DATA_H_
#define LIB_JXL_JPEG_JPEG_DATA_H_

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "lib/jxl/base/array.h"
#include "lib/jxl/base/chunked_array.h"
#include "lib/jxl/base/enc_status.h"
#include "lib/jxl/fields.h"
#include "lib/jxl/frame_dimensions.h"

enum {
  kJpegMaxComponents = 4,
  kMaxQuantTables = 4,
  kMaxHuffmanTables = 4,
  kJpegHuffmanMaxBitLength = 16,
  kJpegHuffmanAlphabetSize = 256,
  kJpegDCAlphabetSize = 12,
  kMaxDHTMarkers = 512,
  kMaxDimPixels = 65535,
  kApp1 = 0xE1,
  kApp2 = 0xE2
};
static const uint8_t kIccProfileTag[12] = "ICC_PROFILE";
static const uint8_t kExifTag[6] = "Exif\0";
static const uint8_t kXMPTag[29] = "http://ns.adobe.com/xap/1.0/";

/* clang-format off */
static const uint32_t kJPEGNaturalOrder[80] = {
  0,   1,  8, 16,  9,  2,  3, 10,
  17, 24, 32, 25, 18, 11,  4,  5,
  12, 19, 26, 33, 40, 48, 41, 34,
  27, 20, 13,  6,  7, 14, 21, 28,
  35, 42, 49, 56, 57, 50, 43, 36,
  29, 22, 15, 23, 30, 37, 44, 51,
  58, 59, 52, 45, 38, 31, 39, 46,
  53, 60, 61, 54, 47, 55, 62, 63,
  // extra entries for bounds safety
  63, 63, 63, 63, 63, 63, 63, 63,
  63, 63, 63, 63, 63, 63, 63, 63
};
/* clang-format on */

// Quantization values for an 8x8 pixel block.
typedef struct jxl_jpeg_quant_table {
  int32_t values[kDCTBlockSize];
  uint32_t precision;
  // The index of this quantization table as it was parsed from the input JPEG.
  // Each DQT marker segment contains an 'index' field, and we save this index
  // here. Valid values are 0 to 3.
  uint32_t index;
  // Set to true if this table is the last one within its marker segment.
  bool is_last;
} jxl_jpeg_quant_table;

static inline void jxl_jpeg_quant_table_construct_empty(jxl_jpeg_quant_table* self) {
  memset(self->values, 0, sizeof(self->values));
  self->precision = 0;
  self->index = 0;
  self->is_last = true;
}

// Huffman code and decoding lookup table used for DC and AC coefficients.
typedef struct jxl_jpeg_huffman_code {
  // Bit length histogram.
  uint32_t counts[kJpegHuffmanMaxBitLength + 1];
  // Symbol values sorted by increasing bit lengths.
  uint32_t values[kJpegHuffmanAlphabetSize + 1];
  // The index of the Huffman code in the current set of Huffman codes. For AC
  // component Huffman codes, 0x10 is added to the index.
  int slot_id;
  // Set to true if this Huffman code is the last one within its marker segment.
  bool is_last;
} jxl_jpeg_huffman_code;

static inline void jxl_jpeg_huffman_code_construct_empty(jxl_jpeg_huffman_code* self) {
  memset(self->counts, 0, sizeof(self->counts));
  memset(self->values, 0, sizeof(self->values));
  self->slot_id = 0;
  self->is_last = true;
}

// Huffman table indexes used for one component of one scan.
typedef struct jxl_jpeg_component_scan_info {
  uint32_t comp_idx;
  uint32_t dc_tbl_idx;
  uint32_t ac_tbl_idx;
} jxl_jpeg_component_scan_info;

// Contains information that is used in one scan.
// Trivially copyable: reset_points / extra_zero_runs live on jxl_jpeg_data.
typedef struct jxl_jpeg_extra_zero_run_info {
  uint32_t block_idx;
  uint32_t num_extra_zero_runs;
} jxl_jpeg_extra_zero_run_info;

typedef struct jxl_jpeg_scan_info {
  // Parameters used for progressive scans (named the same way as in the spec):
  //   jxl_ss : Start of spectral band in zig-zag sequence.
  //   Se : End of spectral band in zig-zag sequence.
  //   Ah : Successive approximation bit position, high.
  //   Al : Successive approximation bit position, low.
  uint32_t jxl_ss;
  uint32_t Se;
  uint32_t Ah;
  uint32_t Al;
  uint32_t num_components;
  jxl_jpeg_component_scan_info components[4];
  // Last codestream pass that is needed to write this scan.
  uint32_t last_needed_pass;

  // The number of extra zero runs (Huffman symbol 0xf0) before the end of
  // block (if nonzero), indexed by block index.
  // All of these symbols can be omitted without changing the pixel values, but
  // some jpeg encoders put these at the end of blocks.
} jxl_jpeg_scan_info;

static inline void jxl_jpeg_scan_info_construct_empty(jxl_jpeg_scan_info* self) {
  self->jxl_ss = 0;
  self->Se = 0;
  self->Ah = 0;
  self->Al = 0;
  self->num_components = 0;
  memset(self->components, 0, sizeof(self->components));
  self->last_needed_pass = 0;
}

JXL_DEFINE_POD_ARRAY(jxl_array_jpeg_quant_table, jxl_jpeg_quant_table)
JXL_DEFINE_POD_ARRAY(jxl_array_jpeg_huffman_code, jxl_jpeg_huffman_code)
JXL_DEFINE_POD_ARRAY(jxl_array_jpeg_scan_info, jxl_jpeg_scan_info)
JXL_DEFINE_POD_ARRAY(jxl_array_jpeg_extra_zero_run_info, jxl_jpeg_extra_zero_run_info)

// JPEG scan extra-zero-run sidecars (parallel to scan_info).
typedef struct jxl_extra_zero_run_span {
  jxl_jpeg_extra_zero_run_info* ptr_;
  size_t len_;
} jxl_extra_zero_run_span;

static inline size_t jxl_extra_zero_run_span_size(const jxl_extra_zero_run_span* self) {
  return self->len_;
}
static inline bool jxl_extra_zero_run_span_is_empty(const jxl_extra_zero_run_span* self) {
  return self->len_ == 0;
}
static inline jxl_jpeg_extra_zero_run_info* jxl_extra_zero_run_span_data(const jxl_extra_zero_run_span* self) {
  return self->ptr_;
}
static inline jxl_jpeg_extra_zero_run_info* jxl_extra_zero_run_span_at(const jxl_extra_zero_run_span* self,
                                                size_t i) {
  return jxl_extra_zero_run_span_data(self) + i;
}

static inline jxl_extra_zero_run_span jxl_extra_zero_run_span_make(jxl_jpeg_extra_zero_run_info* array,
                                             size_t length) {
  jxl_extra_zero_run_span span;
  span.ptr_ = array;
  span.len_ = length;
  return span;
}

static inline jxl_extra_zero_run_span jxl_extra_zero_run_span_empty(void) {
  return jxl_extra_zero_run_span_make(NULL, 0);
}

typedef struct jxl_extra_zero_run_chunks {
  jxl_array_jpeg_extra_zero_run_info data;
  jxl_array_u32 starts;

} jxl_extra_zero_run_chunks;

static inline size_t jxl_extra_zero_run_chunks_size(const jxl_extra_zero_run_chunks* self) {
  return jxl_array_empty(&self->starts) ? 0 : jxl_array_len(&self->starts) - 1;
}
static inline bool jxl_extra_zero_run_chunks_empty(const jxl_extra_zero_run_chunks* self) {
  return jxl_extra_zero_run_chunks_size(self) == 0;
}

static inline jxl_extra_zero_run_span jxl_extra_zero_run_chunks_mutable(jxl_extra_zero_run_chunks* self,
                                                  size_t i) {
  return jxl_extra_zero_run_span_make(
      jxl_array_data(&self->data) + *jxl_array_at(&self->starts, i),
      *jxl_array_at(&self->starts, i + 1) - *jxl_array_at(&self->starts, i));
}

static inline void jxl_extra_zero_run_chunks_clear(jxl_extra_zero_run_chunks* self) {
  jxl_array_clear(&self->data);
  jxl_array_clear(&self->starts);
}

static inline void jxl_extra_zero_run_chunks_construct_empty(jxl_extra_zero_run_chunks* self,
                                                          jxl_context* mm) {
  jxl_array_construct_empty(&self->data, mm);
  jxl_array_construct_empty(&self->starts, mm);
}

static inline void jxl_extra_zero_run_chunks_destroy(jxl_extra_zero_run_chunks* self) {
  jxl_array_destroy(&self->data);
  jxl_array_destroy(&self->starts);
}

static inline void jxl_extra_zero_run_chunks_swap(jxl_extra_zero_run_chunks* self,
                                   jxl_extra_zero_run_chunks* other) {
  jxl_array_swap(&self->data, &other->data);
  jxl_array_swap(&self->starts, &other->starts);
}

static inline jxl_enc_status jxl_extra_zero_run_chunks_push_empty(jxl_extra_zero_run_chunks* self) {
  if (jxl_array_empty(&self->starts)) {
    JXL_RETURN_IF_ERROR(jxl_array_u32_push_back(&self->starts, (uint32_t)(0)));
  }
  return jxl_array_u32_push_back(&self->starts, (uint32_t)(jxl_array_len(&self->data)));
}

static inline jxl_enc_status jxl_extra_zero_run_chunks_push_to_last(jxl_extra_zero_run_chunks* self,
                                           jxl_jpeg_extra_zero_run_info value) {
  JXL_DASSERT(jxl_extra_zero_run_chunks_size(self) > 0);
  JXL_RETURN_IF_ERROR(jxl_array_jpeg_extra_zero_run_info_push_back(&self->data, value));
  *jxl_array_at(&self->starts, jxl_array_len(&self->starts) - 1) =
      (uint32_t)(jxl_array_len(&self->data));
  return jxl_enc_ok_status();
}


typedef int16_t jxl_jpeg_coeff;

// Represents one component of a jpeg file.
// Trivially copyable: coefficient storage lives on jxl_jpeg_data.
typedef struct jxl_jpeg_component {
  // One-byte id of the component.
  uint32_t id;
  // Horizontal and vertical sampling factors.
  // In interleaved mode, each minimal coded unit (MCU) has
  // h_samp_factor x v_samp_factor DCT blocks from this component.
  int h_samp_factor;
  int v_samp_factor;
  // The index of the quantization table used for this component.
  uint32_t quant_idx;
  // The dimensions of the component measured in 8x8 blocks.
  uint32_t width_in_blocks;
  uint32_t height_in_blocks;
} jxl_jpeg_component;

static inline void jxl_jpeg_component_construct_empty(jxl_jpeg_component* self) {
  self->id = 0;
  self->h_samp_factor = 1;
  self->v_samp_factor = 1;
  self->quant_idx = 0;
  self->width_in_blocks = 0;
  self->height_in_blocks = 0;
}

typedef enum jxl_app_marker_type {
  kAppMarkerUnknown = 0,
  kAppMarkerICC = 1,
  kAppMarkerExif = 2,
  kAppMarkerXMP = 3,
} jxl_app_marker_type;

JXL_DEFINE_POD_ARRAY(jxl_array_jpeg_component, jxl_jpeg_component)
JXL_DEFINE_POD_ARRAY(jxl_array_app_marker_type, jxl_app_marker_type)

// Represents a parsed jpeg file.
typedef struct jxl_jpeg_data {
  jxl_fields fields;

  // Write-only: skips brotli-encoded data and what is already in the
  // codestream.


  int width;
  int height;
  uint32_t restart_interval;
  jxl_byte_chunks app_data;
  jxl_array_app_marker_type app_marker_type;
  jxl_byte_chunks com_data;
  jxl_array_jpeg_quant_table quant;
  jxl_array_jpeg_huffman_code huffman_code;
  jxl_array_jpeg_component components;
  // Parallel to components: DCT coeffs (block-by-block, dequantized).
  // Only the first components.size() entries are used.
  jxl_array_i16 component_coeffs[kJpegMaxComponents];
  jxl_array_jpeg_scan_info scan_info;
  // Parallel to scan_info: bit-exact reconstruction sidecars.
  jxl_u32_chunks scan_reset_points;
  jxl_extra_zero_run_chunks scan_extra_zero_runs;
  jxl_array_u8 marker_order;
  jxl_byte_chunks inter_marker_data;
  jxl_array_u8 tail_data;

  // Extra information required for bit-precise JPEG file reconstruction.

  bool has_zero_padding_bit;
  jxl_array_u8 padding_bits;

} jxl_jpeg_data;

jxl_enc_status jxl_jpeg_data_visit_fields(jxl_jpeg_data* self, jxl_visitor* visitor);
JXL_FIELDS_NAME(jxl_jpeg_data)

static inline void jxl_jpeg_data_construct_empty(jxl_jpeg_data* self,
                                                 jxl_context* mm) {
  self->fields.visit_fields_fn = NULL;
#if (JXL_IS_DEBUG_BUILD)
  self->fields.name_fn = NULL;
#endif
  self->width = 0;
  self->height = 0;
  self->restart_interval = 0;
  jxl_byte_chunks_construct_empty(&self->app_data, mm);
  jxl_array_construct_empty(&self->app_marker_type, mm);
  jxl_byte_chunks_construct_empty(&self->com_data, mm);
  jxl_array_construct_empty(&self->quant, mm);
  jxl_array_construct_empty(&self->huffman_code, mm);
  jxl_array_construct_empty(&self->components, mm);
  for (size_t i = 0; i < kJpegMaxComponents; ++i) {
    jxl_array_construct_empty(&self->component_coeffs[i], mm);
  }
  jxl_array_construct_empty(&self->scan_info, mm);
  jxl_u32_chunks_construct_empty(&self->scan_reset_points, mm);
  jxl_extra_zero_run_chunks_construct_empty(&self->scan_extra_zero_runs, mm);
  jxl_array_construct_empty(&self->marker_order, mm);
  jxl_byte_chunks_construct_empty(&self->inter_marker_data, mm);
  jxl_array_construct_empty(&self->tail_data, mm);
  self->has_zero_padding_bit = false;
  jxl_array_construct_empty(&self->padding_bits, mm);
}
static inline void jxl_jpeg_data_init(jxl_jpeg_data* self, jxl_context* mm) {
  jxl_jpeg_data_construct_empty(self, mm);
  JXL_FIELDS_REGISTER_PTR(jxl_jpeg_data, &self->fields);
}
static inline void jxl_jpeg_data_destroy(jxl_jpeg_data* self) {
  jxl_byte_chunks_destroy(&self->app_data);
  jxl_array_destroy(&self->app_marker_type);
  jxl_byte_chunks_destroy(&self->com_data);
  jxl_array_destroy(&self->quant);
  jxl_array_destroy(&self->huffman_code);
  jxl_array_destroy(&self->components);
  for (size_t i = 0; i < kJpegMaxComponents; ++i) {
    jxl_array_destroy(&self->component_coeffs[i]);
  }
  jxl_array_destroy(&self->scan_info);
  jxl_u32_chunks_destroy(&self->scan_reset_points);
  jxl_extra_zero_run_chunks_destroy(&self->scan_extra_zero_runs);
  jxl_array_destroy(&self->marker_order);
  jxl_byte_chunks_destroy(&self->inter_marker_data);
  jxl_array_destroy(&self->tail_data);
  jxl_array_destroy(&self->padding_bits);
}
static inline void jxl_jpeg_data_swap(jxl_jpeg_data* self, jxl_jpeg_data* other) {
    jxl_fields tf = self->fields;
    self->fields = other->fields;
    other->fields = tf;
    int tw = self->width;
    self->width = other->width;
    other->width = tw;
    int th = self->height;
    self->height = other->height;
    other->height = th;
    uint32_t tri = self->restart_interval;
    self->restart_interval = other->restart_interval;
    other->restart_interval = tri;
    jxl_byte_chunks_swap(&self->app_data, &other->app_data);
    jxl_array_swap(&self->app_marker_type, &other->app_marker_type);
    jxl_byte_chunks_swap(&self->com_data, &other->com_data);
    jxl_array_swap(&self->quant, &other->quant);
    jxl_array_swap(&self->huffman_code, &other->huffman_code);
    jxl_array_swap(&self->components, &other->components);
    for (size_t i = 0; i < kJpegMaxComponents; ++i) {
      jxl_array_swap(&self->component_coeffs[i], &other->component_coeffs[i]);
    }
    jxl_array_swap(&self->scan_info, &other->scan_info);
    jxl_u32_chunks_swap(&self->scan_reset_points, &other->scan_reset_points);
    jxl_extra_zero_run_chunks_swap(&self->scan_extra_zero_runs,
                           &other->scan_extra_zero_runs);
    jxl_array_swap(&self->marker_order, &other->marker_order);
    jxl_byte_chunks_swap(&self->inter_marker_data, &other->inter_marker_data);
    jxl_array_swap(&self->tail_data, &other->tail_data);
    bool tz = self->has_zero_padding_bit;
    self->has_zero_padding_bit = other->has_zero_padding_bit;
    other->has_zero_padding_bit = tz;
    jxl_array_swap(&self->padding_bits, &other->padding_bits);
  }



#endif  // LIB_JXL_JPEG_JPEG_DATA_H_
