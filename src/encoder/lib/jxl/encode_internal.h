/* Copyright (c) the JPEG XL Project Authors. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

#ifndef LIB_JXL_ENCODE_INTERNAL_H_
#define LIB_JXL_ENCODE_INTERNAL_H_

#include <jxl/cms_interface.h>
#include <jxl/encode.h>
#include "lib/jxl/memory_manager.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "lib/jxl/base/array.h"
#include "lib/jxl/base/chunked_array.h"
#include "lib/jxl/base/common.h"
#include "lib/jxl/common.h"
#include "lib/jxl/base/status.h"
#include "lib/jxl/enc_params.h"
#include "lib/jxl/image_metadata.h"
#include "lib/jxl/jpeg/jpeg_data.h"
#include "lib/jxl/memory_manager_internal.h"
#include "lib/jxl/padded_bytes.h"


// Four-byte ISOBMFF / JXL box type.
typedef struct jxl_enc_box_type {
  uint8_t bytes[4];
} jxl_enc_box_type;

static inline uint8_t jxl_enc_box_type_at(const jxl_enc_box_type* self, size_t i) {
  return self->bytes[i];
}
static inline const uint8_t* jxl_enc_box_type_data(const jxl_enc_box_type* self) {
  return self->bytes;
}
static inline void jxl_enc_box_type_construct_empty(jxl_enc_box_type* self) {
  self->bytes[0] = 0;
  self->bytes[1] = 0;
  self->bytes[2] = 0;
  self->bytes[3] = 0;
}
static inline size_t jxl_enc_box_type_size() { return 4; }

static inline bool jxl_enc_box_type_equal(const jxl_enc_box_type* a, const jxl_enc_box_type* b) {
  return memcmp(a->bytes, b->bytes, 4) == 0;
}

static inline void jxl_enc_box_type_make_from_chars(jxl_enc_box_type* self, const char* type) {
  self->bytes[0] = (uint8_t)(type[0]);
  self->bytes[1] = (uint8_t)(type[1]);
  self->bytes[2] = (uint8_t)(type[2]);
  self->bytes[3] = (uint8_t)(type[3]);
}

// Utility function that makes a jxl_enc_box_type from a string literal. The string must
// have 4 characters, a 5th null termination character is optional.
static inline jxl_enc_box_type jxl_make_box_type(const char* type) {
  jxl_enc_box_type box;
  jxl_enc_box_type_make_from_chars(&box, type);
  return box;
}

enum { kSmallBoxHeaderSize = 8 };
enum { kLargeBoxHeaderSize = 16 };
static const size_t kLargeBoxContentSizeThreshold =
    0x100000000ull - kSmallBoxHeaderSize;

size_t jxl_write_box_header(const jxl_enc_box_type* type, size_t size, bool unbounded,
                      bool force_large_box, uint8_t* output);


// Holds parsed JPEG input for jxl_encoder_add_jpeg_frame.

typedef struct jxl_encoder_jpeg_frame_adapter {
  size_t xsize;
  size_t ysize;

  jxl_jpeg_data jpeg_data_;
  bool has_jpeg_data_;

} jxl_encoder_jpeg_frame_adapter;

static inline void jxl_encoder_jpeg_frame_adapter_construct_empty(
    jxl_encoder_jpeg_frame_adapter* self, jxl_memory_manager* mm) {
  self->xsize = 0;
  self->ysize = 0;
  jxl_jpeg_data_construct_empty(&self->jpeg_data_, mm);
  self->has_jpeg_data_ = false;
}
static inline void jxl_encoder_jpeg_frame_adapter_destroy(
    jxl_encoder_jpeg_frame_adapter* self) {
  jxl_jpeg_data_destroy(&self->jpeg_data_);
  self->has_jpeg_data_ = false;
}
static inline void jxl_encoder_jpeg_frame_adapter_init(jxl_encoder_jpeg_frame_adapter* self,
                                           size_t xs, size_t ys,
                                           jxl_memory_manager* mm) {
  self->xsize = xs;
  self->ysize = ys;
  self->has_jpeg_data_ = false;
  jxl_jpeg_data_init(&self->jpeg_data_, mm);
}

static inline void jxl_encoder_jpeg_frame_adapter_set_jpeg_data(
    jxl_encoder_jpeg_frame_adapter* self, jxl_jpeg_data* jpeg_data) {
  jxl_jpeg_data_swap(&self->jpeg_data_, jpeg_data);
  self->has_jpeg_data_ = true;
}

// NB: after TakeJPEGData, IsJPEG returns false.
static inline bool jxl_encoder_jpeg_frame_adapter_is_jpeg(
    const jxl_encoder_jpeg_frame_adapter* self) {
  return self->has_jpeg_data_;
}

static inline void jxl_encoder_jpeg_frame_adapter_take_jpeg_data(
    jxl_encoder_jpeg_frame_adapter* self, jxl_jpeg_data* out,
    jxl_memory_manager* mm) {
  self->has_jpeg_data_ = false;
  jxl_jpeg_data_init(out, mm);
  jxl_jpeg_data_swap(out, &self->jpeg_data_);
}

static inline void jxl_encoder_jpeg_frame_adapter_swap(jxl_encoder_jpeg_frame_adapter* self,
                                           jxl_encoder_jpeg_frame_adapter* other) {
  // xsize/ysize are fixed at construction; only owning payload is swapped.
  jxl_jpeg_data_swap(&self->jpeg_data_, &other->jpeg_data_);
  bool th = self->has_jpeg_data_;
  self->has_jpeg_data_ = other->has_jpeg_data_;
  other->has_jpeg_data_ = th;
}


// Trivially copyable box header; payloads live in MetadataBoxes::contents.
typedef struct jxl_encoder_metadata_box {
  jxl_enc_box_type type;
  bool compress_box;
} jxl_encoder_metadata_box;

static inline void jxl_encoder_metadata_box_construct_empty(jxl_encoder_metadata_box* self) {
  jxl_enc_box_type_construct_empty(&self->type);
  self->compress_box = false;
}

JXL_DEFINE_POD_ARRAY(jxl_array_jxl_encoder_metadata_box, jxl_encoder_metadata_box)


typedef struct jxl_encoder_metadata_boxes {
  jxl_array_jxl_encoder_metadata_box boxes;
  jxl_byte_chunks contents;

} jxl_encoder_metadata_boxes;

static inline void jxl_encoder_metadata_boxes_construct_empty(
    jxl_encoder_metadata_boxes* self, jxl_memory_manager* mm) {
  jxl_array_construct_empty(&self->boxes, mm);
  jxl_byte_chunks_construct_empty(&self->contents, mm);
}
static inline void jxl_encoder_metadata_boxes_destroy(jxl_encoder_metadata_boxes* self) {
  jxl_array_destroy(&self->boxes);
  jxl_byte_chunks_destroy(&self->contents);
}

static inline size_t jxl_encoder_metadata_boxes_size(const jxl_encoder_metadata_boxes* self) {
  return jxl_array_len(&self->boxes);
}
static inline bool jxl_encoder_metadata_boxes_empty(const jxl_encoder_metadata_boxes* self) {
  return jxl_array_empty(&self->boxes);
}

static inline jxl_status jxl_encoder_metadata_boxes_push_back(jxl_encoder_metadata_boxes* self,
                                              jxl_enc_box_type type, const jxl_array_u8* data,
                                              bool compress_box) {
  jxl_encoder_metadata_box box;
  jxl_encoder_metadata_box_construct_empty(&box);
  box.type = type;
  box.compress_box = compress_box;
  JXL_RETURN_IF_ERROR(jxl_array_jxl_encoder_metadata_box_push_back(&self->boxes, box));
  return jxl_byte_chunks_push_back_u8(&self->contents, data);
}

static inline void jxl_encoder_metadata_boxes_swap(jxl_encoder_metadata_boxes* self,
                                        jxl_encoder_metadata_boxes* other) {
  jxl_array_swap(&self->boxes, &other->boxes);
  jxl_byte_chunks_swap(&self->contents, &other->contents);
}



typedef struct jxl_encoder_queued_frame {
  jxl_compress_params cparams;
  jxl_encoder_jpeg_frame_adapter frame_data;
  jxl_encoder_metadata_boxes metadata_boxes;

} jxl_encoder_queued_frame;

static inline void jxl_encoder_queued_frame_construct_empty(
    jxl_encoder_queued_frame* self, jxl_memory_manager* mm) {
  jxl_compress_params_construct_empty(&self->cparams);
  jxl_encoder_jpeg_frame_adapter_construct_empty(&self->frame_data, mm);
  jxl_encoder_metadata_boxes_construct_empty(&self->metadata_boxes, mm);
}
static inline void jxl_encoder_queued_frame_destroy(jxl_encoder_queued_frame* self) {
  jxl_encoder_jpeg_frame_adapter_destroy(&self->frame_data);
  jxl_encoder_metadata_boxes_destroy(&self->metadata_boxes);
}
static inline void jxl_encoder_queued_frame_init(jxl_encoder_queued_frame* self,
                                      const jxl_compress_params* cp, size_t xs,
                                      size_t ys, jxl_memory_manager* mm) {
  self->cparams = *cp;
  jxl_encoder_jpeg_frame_adapter_init(&self->frame_data, xs, ys, mm);
}
static inline void jxl_encoder_queued_frame_swap(jxl_encoder_queued_frame* self, jxl_encoder_queued_frame* other) {
    jxl_compress_params tc = self->cparams;
    self->cparams = other->cparams;
    other->cparams = tc;
    jxl_encoder_jpeg_frame_adapter_swap(&self->frame_data, &other->frame_data);
    jxl_encoder_metadata_boxes_swap(&self->metadata_boxes, &other->metadata_boxes);
  }


// Owning pointer to a jxl_encoder_queued_frame allocated via jxl_memory_manager.

typedef struct jxl_owned_queued_frame {
  jxl_encoder_queued_frame* ptr_;
  const jxl_memory_manager* memory_manager_;

} jxl_owned_queued_frame;

static inline void jxl_owned_queued_frame_construct_empty(jxl_owned_queued_frame* self) {
  self->ptr_ = NULL;
  self->memory_manager_ = NULL;
}
static inline void jxl_owned_queued_frame_destroy(jxl_owned_queued_frame* self) {
  if (self == NULL) return;
  if (self->ptr_ != NULL) {
    jxl_encoder_queued_frame_destroy(self->ptr_);
    self->memory_manager_->free(self->memory_manager_->opaque, self->ptr_);
  }
  self->ptr_ = NULL;
}

static inline void jxl_owned_queued_frame_make(jxl_encoder_queued_frame* ptr,
                                 const jxl_memory_manager* memory_manager,
                                 jxl_owned_queued_frame* out) {
  jxl_owned_queued_frame_destroy(out);
  out->ptr_ = ptr;
  out->memory_manager_ = memory_manager;
}
static inline void jxl_owned_queued_frame_swap(jxl_owned_queued_frame* self, jxl_owned_queued_frame* other) {
    jxl_encoder_queued_frame* tp = self->ptr_;
    self->ptr_ = other->ptr_;
    other->ptr_ = tp;
    const jxl_memory_manager* tm = self->memory_manager_;
    self->memory_manager_ = other->memory_manager_;
    other->memory_manager_ = tm;
  }

static inline jxl_encoder_queued_frame* jxl_owned_queued_frame_get(const jxl_owned_queued_frame* self) {
  return self->ptr_;
}
static inline bool jxl_owned_queued_frame_ok(const jxl_owned_queued_frame* self) {
  return self->ptr_ != NULL;
}



// Move-only list of jxl_owned_queued_frame (was MoveArray<jxl_owned_queued_frame>).


typedef struct jxl_owned_queued_frames {
  jxl_memory_manager* memory_manager;
  jxl_owned_queued_frame* ptr;
  size_t len;
  size_t capacity;

} jxl_owned_queued_frames;
static inline size_t jxl_owned_queued_frames_size(const jxl_owned_queued_frames* self) { return self->len; }
static inline bool jxl_owned_queued_frames_empty(const jxl_owned_queued_frames* self) { return self->len == 0; }
static inline jxl_owned_queued_frame* jxl_owned_queued_frames_data(jxl_owned_queued_frames* self) { return self->ptr; }
static inline const jxl_owned_queued_frame* jxl_owned_queued_frames_data_const(const jxl_owned_queued_frames* self) { return self->ptr; }
static inline jxl_owned_queued_frame* jxl_owned_queued_frames_at(jxl_owned_queued_frames* self, size_t i) { return &self->ptr[i]; }
static inline const jxl_owned_queued_frame* jxl_owned_queued_frames_at_const(const jxl_owned_queued_frames* self, size_t i) { return &self->ptr[i]; }

static inline void jxl_owned_queued_frames_construct_empty(jxl_owned_queued_frames* self) {
  self->memory_manager = NULL;
  self->ptr = NULL;
  self->len = 0;
  self->capacity = 0;
}

static inline void jxl_owned_queued_frames_swap(jxl_owned_queued_frames* self, jxl_owned_queued_frames* other) {
  jxl_memory_manager* tmp_mm = self->memory_manager;
  self->memory_manager = other->memory_manager;
  other->memory_manager = tmp_mm;
  jxl_owned_queued_frame* tmp_ptr = self->ptr;
  self->ptr = other->ptr;
  other->ptr = tmp_ptr;
  jxl_swap(&self->len, &other->len);
  jxl_swap(&self->capacity, &other->capacity);
}

static inline void jxl_owned_queued_frames_clear(jxl_owned_queued_frames* self) {
    for (size_t i = 0; i < self->len; ++i) {
      jxl_owned_queued_frame_destroy(self->ptr + i);
    }
    self->len = 0;
  }

static inline jxl_status jxl_owned_queued_frames_reserve(jxl_owned_queued_frames* self, size_t new_capacity) {
    if (new_capacity <= self->capacity) return jxl_ok_status();

    size_t grown = self->capacity;
    if (grown == 0) grown = 16;
    while (grown < new_capacity) {
      size_t next;
      if (!jxl_safe_add(grown, grown / 2, &next) || next <= grown) {
        grown = new_capacity;
        break;
      }
      grown = next;
    }
    if (grown < new_capacity) grown = new_capacity;

    size_t bytes;
    if (!jxl_safe_mul(grown, sizeof(jxl_owned_queued_frame), &bytes)) {
      return JXL_FAILURE("jxl_owned_queued_frames::reserve: size overflow");
    }
    jxl_owned_queued_frame* neu;
    if (self->memory_manager == NULL) {
      return JXL_FAILURE("jxl_owned_queued_frames::reserve: missing memory manager");
    }
    neu = (jxl_owned_queued_frame*)(
        self->memory_manager->alloc(self->memory_manager->opaque, bytes));
    if (neu == NULL) {
      return JXL_FAILURE("jxl_owned_queued_frames::reserve: allocation failed");
    }
    for (size_t i = 0; i < self->len; ++i) {
      jxl_owned_queued_frame_construct_empty(neu + i);
      jxl_owned_queued_frame_swap(neu + i, &self->ptr[i]);
      jxl_owned_queued_frame_destroy(self->ptr + i);
    }
    if (self->ptr != NULL) {
      self->memory_manager->free(self->memory_manager->opaque, self->ptr);
    }
    self->ptr = neu;
    self->capacity = grown;
    return jxl_ok_status();
  }

static inline void jxl_owned_queued_frames_destroy(jxl_owned_queued_frames* self) {
  jxl_owned_queued_frames_clear(self);
  if (self->ptr != NULL) {
    if (self->memory_manager != NULL) {
      self->memory_manager->free(self->memory_manager->opaque, self->ptr);
    }
  }
  self->ptr = NULL;
  self->capacity = 0;
}
static inline jxl_owned_queued_frame* jxl_owned_queued_frames_front(jxl_owned_queued_frames* self) {
  JXL_DASSERT(self != NULL && self->len > 0);
  return &self->ptr[0];
}
static inline void jxl_owned_queued_frames_erase(jxl_owned_queued_frames* self, size_t index) {
    JXL_DASSERT(index < self->len);
    jxl_owned_queued_frame_destroy(self->ptr + index);
    for (size_t i = index + 1; i < self->len; ++i) {
      jxl_owned_queued_frame_construct_empty(self->ptr + i - 1);
      jxl_owned_queued_frame_swap(self->ptr + i - 1, &self->ptr[i]);
      jxl_owned_queued_frame_destroy(self->ptr + i);
    }
    --self->len;
  }

static inline jxl_status jxl_owned_queued_frames_emplace_back(jxl_owned_queued_frames* self, jxl_owned_queued_frame* value) {
    if (self->len == self->capacity) {
      size_t need;
      if (!jxl_safe_add(self->capacity, (size_t)(1), &need)) {
        return JXL_FAILURE("jxl_owned_queued_frames::emplace_back: overflow");
      }
      JXL_RETURN_IF_ERROR(jxl_owned_queued_frames_reserve(self, need));
    }
    jxl_owned_queued_frame_construct_empty(self->ptr + self->len);
    jxl_owned_queued_frame_swap(self->ptr + self->len, value);
    ++self->len;
    return jxl_ok_status();
  }





static inline void jxl_make_owned_queued_frame(
    const jxl_memory_manager* memory_manager, jxl_encoder_queued_frame* value,
    jxl_owned_queued_frame* out) {
  jxl_owned_queued_frame_construct_empty(out);
  jxl_encoder_queued_frame* mem = (jxl_encoder_queued_frame*)(
      memory_manager->alloc(memory_manager->opaque,
                            sizeof(jxl_encoder_queued_frame)));
  if (!mem) {
    jxl_owned_queued_frame_make(NULL, memory_manager, out);
    return;
  }
  jxl_encoder_queued_frame_construct_empty(mem, (jxl_memory_manager*)memory_manager);
  jxl_encoder_queued_frame_init(mem, &value->cparams, value->frame_data.xsize,
                            value->frame_data.ysize,
                            (jxl_memory_manager*)memory_manager);
  jxl_encoder_jpeg_frame_adapter_swap(&mem->frame_data, &value->frame_data);
  jxl_encoder_metadata_boxes_swap(&mem->metadata_boxes, &value->metadata_boxes);
  jxl_owned_queued_frame_make(mem, memory_manager, out);
}

// Appends a JXL container box header with given type, size, and unbounded
// properties to output.
static inline jxl_status jxl_append_box_header(const jxl_enc_box_type* type, size_t size,
                              bool unbounded, jxl_array_u8* output) {
  size_t current_size = jxl_array_len(output);
  JXL_RETURN_IF_ERROR(jxl_array_resize_zero(output, current_size + kLargeBoxHeaderSize));
  size_t header_size =
      jxl_write_box_header(type, size, unbounded, /*force_large_box=*/false,
                     jxl_array_data(output) + current_size);
  JXL_RETURN_IF_ERROR(jxl_array_resize_zero(output, current_size + header_size));
  return jxl_ok_status();
}


// Returns the JXL container signature box and ftyp box.
// ftyp_version: 0 = standard delivery order, 1 = out-of-order jxlp boxes.
static inline jxl_status jxl_make_container_header(jxl_memory_manager* mm,
                                                   int ftyp_version,
                                                   jxl_array_u8* out) {
  jxl_array_construct_empty(out, mm);
  jxl_array_u8 tmp;
  jxl_array_construct_empty(&tmp, mm);
  JXL_RETURN_IF_ERROR(
      jxl_array_assign(&tmp, kJxlSignatureBox, kJxlSignatureBoxSize));
  // ftyp box: major brand "jxl ", minor version, compatible brand "jxl ".
  const uint8_t ftyp[] = {'j', 'x', 'l', ' ',
                           0,   0,   0,   (uint8_t)(ftyp_version),
                           'j', 'x', 'l', ' '};
  {
    jxl_enc_box_type ftyp_type = jxl_make_box_type("ftyp");
    JXL_RETURN_IF_ERROR(
        jxl_append_box_header(&ftyp_type, sizeof(ftyp), /*unbounded=*/false,
                        &tmp));
  }
  JXL_RETURN_IF_ERROR(jxl_array_append(&tmp, ftyp, sizeof(ftyp)));
  jxl_array_swap(out, &tmp);
  return jxl_ok_status();
}



typedef struct jxl_internal_buffer {
  // jxl_bytes in the range `[output_position_ - start_of_the_buffer,
  // written_bytes)` need to be flushed out.
  size_t written_bytes;
  // If data has been buffered, it is stored in `owned_data`.
  jxl_padded_bytes owned_data;

} jxl_internal_buffer;

static inline void jxl_internal_buffer_construct_empty(jxl_internal_buffer* self) {
  self->written_bytes = 0;
  jxl_padded_bytes_construct_empty(&self->owned_data);
}
static inline void jxl_internal_buffer_destroy(jxl_internal_buffer* self) {
  jxl_padded_bytes_destroy(&self->owned_data);
  self->written_bytes = 0;
}
static inline void jxl_internal_buffer_init(jxl_internal_buffer* self,
                               jxl_memory_manager* memory_manager) {
  JXL_DASSERT(memory_manager != NULL);
  self->written_bytes = 0;
  jxl_padded_bytes_make(memory_manager, &self->owned_data);
}

// Chunk keyed by absolute start position; kept sorted by `start`.
// Invariant: does not contain chunks that are entirely below the output
// position.

typedef struct jxl_buffered_chunk {
  size_t start;
  jxl_internal_buffer buffer;

} jxl_buffered_chunk;

static inline void jxl_buffered_chunk_construct_empty(jxl_buffered_chunk* self) {
  self->start = 0;
  jxl_internal_buffer_construct_empty(&self->buffer);
}
static inline void jxl_buffered_chunk_destroy(jxl_buffered_chunk* self) {
  jxl_internal_buffer_destroy(&self->buffer);
  self->start = 0;
}
static inline void jxl_buffered_chunk_init(jxl_buffered_chunk* self, size_t start_pos,
                              jxl_memory_manager* memory_manager) {
  self->start = start_pos;
  jxl_internal_buffer_init(&self->buffer, memory_manager);
}

// Move-only list of jxl_buffered_chunk (was MoveArray<jxl_buffered_chunk>).

typedef struct jxl_buffered_chunks {
  jxl_memory_manager* memory_manager;
  jxl_buffered_chunk* ptr;
  size_t len;
  size_t capacity;

} jxl_buffered_chunks;
static inline size_t jxl_buffered_chunks_size(const jxl_buffered_chunks* self) { return self->len; }
static inline bool jxl_buffered_chunks_empty(const jxl_buffered_chunks* self) { return self->len == 0; }
static inline jxl_buffered_chunk* jxl_buffered_chunks_data(jxl_buffered_chunks* self) { return self->ptr; }
static inline const jxl_buffered_chunk* jxl_buffered_chunks_data_const(const jxl_buffered_chunks* self) { return self->ptr; }
static inline jxl_buffered_chunk* jxl_buffered_chunks_at(jxl_buffered_chunks* self, size_t i) { return &self->ptr[i]; }
static inline const jxl_buffered_chunk* jxl_buffered_chunks_at_const(const jxl_buffered_chunks* self, size_t i) { return &self->ptr[i]; }

static inline void jxl_buffered_chunks_construct_empty(jxl_buffered_chunks* self) {
  self->memory_manager = NULL;
  self->ptr = NULL;
  self->len = 0;
  self->capacity = 0;
}

typedef struct jxl_encoder_output_processor_wrapper {
  jxl_buffered_chunks internal_buffers_;

  uint8_t** next_out_;
  size_t* avail_out_;
  // Where the next GetBuffer call will write bytes to.
  size_t position_;
  // The position of the last SetFinalizedPosition call.
  size_t finalized_position_;
  // Position `next_out_` points to.
  size_t output_position_;

  bool has_buffer_;

  jxl_memory_manager* memory_manager_;

} jxl_encoder_output_processor_wrapper;

static inline void jxl_encoder_output_processor_wrapper_construct_empty(
    jxl_encoder_output_processor_wrapper* self) {
  jxl_buffered_chunks_construct_empty(&self->internal_buffers_);
  self->next_out_ = NULL;
  self->avail_out_ = NULL;
  self->position_ = 0;
  self->finalized_position_ = 0;
  self->output_position_ = 0;
  self->has_buffer_ = false;
  self->memory_manager_ = NULL;
}
static inline void jxl_buffered_chunks_destroy(
    jxl_buffered_chunks* self) {
  if (self == NULL) return;
  
  for (size_t i = 0; i < self->len; ++i) {
    jxl_buffered_chunk* chunk = self->ptr + i;
    jxl_buffered_chunk_destroy(chunk);
  }
  self->len = 0;
  if (self->ptr != NULL) {
    if (self->memory_manager != NULL) {
      self->memory_manager->free(self->memory_manager->opaque, self->ptr);
    }
  }
  self->ptr = NULL;
  self->capacity = 0;
}

static inline void jxl_encoder_output_processor_wrapper_destroy(
    jxl_encoder_output_processor_wrapper* self) {
  jxl_buffered_chunks_destroy(&self->internal_buffers_);
}
static inline void jxl_encoder_output_processor_wrapper_init(
    jxl_encoder_output_processor_wrapper* self, jxl_memory_manager* memory_manager) {
  self->memory_manager_ = memory_manager;
  self->internal_buffers_.memory_manager = memory_manager;
}

static inline size_t jxl_encoder_output_processor_wrapper_current_position(
    const jxl_encoder_output_processor_wrapper* self) {
  return self->position_;
}

static inline bool jxl_encoder_output_processor_wrapper_has_output_to_write(
    const jxl_encoder_output_processor_wrapper* self) {
  return self->output_position_ < self->finalized_position_;
}

static inline size_t jxl_encoder_output_processor_wrapper_find_buffer(
    const jxl_encoder_output_processor_wrapper* self, size_t pos) {
  for (size_t i = 0; i < jxl_buffered_chunks_size(&self->internal_buffers_); ++i) {
    if (jxl_buffered_chunks_at_const(&self->internal_buffers_, i)->start == pos) return i;
  }
  return jxl_buffered_chunks_size(&self->internal_buffers_);
}

static inline void jxl_buffered_chunk_swap(
    jxl_buffered_chunk* self,
    jxl_buffered_chunk* other);

static inline void jxl_buffered_chunks_swap(
    jxl_buffered_chunks* self,
    jxl_buffered_chunks* other) {
  jxl_memory_manager* tmp_mm = self->memory_manager;
  self->memory_manager = other->memory_manager;
  other->memory_manager = tmp_mm;
  jxl_buffered_chunk* tmp_ptr = self->ptr;
  self->ptr = other->ptr;
  other->ptr = tmp_ptr;
  jxl_swap(&self->len, &other->len);
  jxl_swap(&self->capacity, &other->capacity);
}


static inline jxl_buffered_chunk* jxl_buffered_chunks_front(
    jxl_buffered_chunks* self) {
  JXL_DASSERT(self != NULL && self->len > 0);
  return &self->ptr[0];
}

static inline jxl_status jxl_buffered_chunks_reserve(
    jxl_buffered_chunks* self,
    size_t new_capacity) {
  if (new_capacity <= self->capacity) return jxl_ok_status();

  size_t grown = self->capacity;
  if (grown == 0) grown = 16;
  while (grown < new_capacity) {
    size_t next;
    if (!jxl_safe_add(grown, grown / 2, &next) || next <= grown) {
      grown = new_capacity;
      break;
    }
    grown = next;
  }
  if (grown < new_capacity) grown = new_capacity;

  size_t bytes;
  
  if (!jxl_safe_mul(grown, sizeof(jxl_buffered_chunk), &bytes)) {
    return JXL_FAILURE("jxl_buffered_chunks::reserve: size overflow");
  }
  jxl_buffered_chunk* neu;
  if (self->memory_manager == NULL) {
    return JXL_FAILURE("jxl_buffered_chunks::reserve: missing memory manager");
  }
  neu = (jxl_buffered_chunk*)(
      self->memory_manager->alloc(self->memory_manager->opaque, bytes));
  if (neu == NULL) {
    return JXL_FAILURE("jxl_buffered_chunks::reserve: allocation failed");
  }
  for (size_t i = 0; i < self->len; ++i) {
    jxl_memory_manager* shell_mm =
        jxl_padded_bytes_memory_manager(&self->ptr[i].buffer.owned_data);
    jxl_buffered_chunk_construct_empty(neu + i);
    jxl_buffered_chunk_init(neu + i, 0, shell_mm);
    jxl_buffered_chunk_swap(neu + i, &self->ptr[i]);
    jxl_buffered_chunk* old = self->ptr + i;
    jxl_buffered_chunk_destroy(old);
  }
  if (self->ptr != NULL) {
    self->memory_manager->free(self->memory_manager->opaque, self->ptr);
  }
  self->ptr = neu;
  self->capacity = grown;
  return jxl_ok_status();
}

static inline jxl_status jxl_buffered_chunks_emplace(
    jxl_buffered_chunks* self, size_t index,
    size_t pos, jxl_memory_manager* mm) {
  
  JXL_DASSERT(index <= self->len);
  size_t need;
  if (!jxl_safe_add(self->len, (size_t)(1), &need)) {
    return JXL_FAILURE("jxl_buffered_chunks::emplace: overflow");
  }
  JXL_RETURN_IF_ERROR(jxl_buffered_chunks_reserve(self, need));
  for (size_t i = self->len; i > index; --i) {
    jxl_memory_manager* shell_mm =
        jxl_padded_bytes_memory_manager(&self->ptr[i - 1].buffer.owned_data);
    jxl_buffered_chunk_construct_empty(self->ptr + i);
    jxl_buffered_chunk_init(self->ptr + i, 0, shell_mm);
    jxl_buffered_chunk_swap(self->ptr + i, &self->ptr[i - 1]);
    jxl_buffered_chunk* old = self->ptr + i - 1;
    jxl_buffered_chunk_destroy(old);
  }
  jxl_buffered_chunk_construct_empty(self->ptr + index);
    jxl_buffered_chunk_init(self->ptr + index, pos, mm);
  ++self->len;
  return jxl_ok_status();
}

static inline void jxl_buffered_chunks_erase(
    jxl_buffered_chunks* self, size_t index) {
  
  JXL_DASSERT(index < self->len);
  jxl_buffered_chunk* doomed = self->ptr + index;
  jxl_buffered_chunk_destroy(doomed);
  for (size_t i = index + 1; i < self->len; ++i) {
    jxl_memory_manager* shell_mm =
        jxl_padded_bytes_memory_manager(&self->ptr[i].buffer.owned_data);
    jxl_buffered_chunk_construct_empty(self->ptr + i - 1);
    jxl_buffered_chunk_init(self->ptr + i - 1, 0, shell_mm);
    jxl_buffered_chunk_swap(self->ptr + i - 1, &self->ptr[i]);
    jxl_buffered_chunk* old = self->ptr + i;
    jxl_buffered_chunk_destroy(old);
  }
  --self->len;
}

static inline size_t jxl_encoder_output_processor_wrapper_insert_buffer(
    jxl_encoder_output_processor_wrapper* self, size_t pos) {
  size_t i = 0;
  while (i < jxl_buffered_chunks_size(&self->internal_buffers_) &&
         jxl_buffered_chunks_at(&self->internal_buffers_, i)->start < pos) {
    ++i;
  }
  JXL_DASSERT(i == jxl_buffered_chunks_size(&self->internal_buffers_) ||
              jxl_buffered_chunks_at(&self->internal_buffers_, i)->start != pos);
  if (!jxl_status_ok(jxl_buffered_chunks_emplace(&self->internal_buffers_, i, pos,
                             self->memory_manager_))) {
    JXL_CRASH();
  }
  return i;
}




static inline void jxl_internal_buffer_swap(jxl_internal_buffer* self, jxl_internal_buffer* other) {
      size_t tw = self->written_bytes;
      self->written_bytes = other->written_bytes;
      other->written_bytes = tw;
      jxl_padded_bytes_swap(&self->owned_data, &other->owned_data);
    }


static inline void jxl_buffered_chunk_swap(jxl_buffered_chunk* self, jxl_buffered_chunk* other) {
      size_t ts = self->start;
      self->start = other->start;
      other->start = ts;
      jxl_internal_buffer_swap(&self->buffer, &other->buffer);
    }


typedef struct jxl_output_processor_buffer {

  uint8_t* data_;
  size_t size_;
  size_t bytes_used_;
  jxl_encoder_output_processor_wrapper* wrapper_;
} jxl_output_processor_buffer;

jxl_status jxl_encoder_output_processor_wrapper_get_buffer(
    jxl_encoder_output_processor_wrapper* self, size_t min_size,
    size_t requested_size, jxl_output_processor_buffer* out);
jxl_status jxl_encoder_output_processor_wrapper_seek(
    jxl_encoder_output_processor_wrapper* self, size_t pos);
jxl_status jxl_encoder_output_processor_wrapper_set_finalized_position(
    jxl_encoder_output_processor_wrapper* self);
jxl_status jxl_encoder_output_processor_wrapper_set_avail_out(
    jxl_encoder_output_processor_wrapper* self, uint8_t** next_out,
    size_t* avail_out);
jxl_status jxl_encoder_output_processor_wrapper_release_buffer(
    jxl_encoder_output_processor_wrapper* self, size_t bytes_used);
jxl_status jxl_encoder_output_processor_wrapper_flush_output(
    jxl_encoder_output_processor_wrapper* self, uint8_t** next_out,
    size_t* avail_out);




static inline size_t jxl_output_processor_buffer_size(
    const jxl_output_processor_buffer* self) {
  return self->size_;
}
static inline uint8_t* jxl_output_processor_buffer_data(jxl_output_processor_buffer* self) {
  return self->data_;
}

static inline void jxl_output_processor_buffer_construct_empty(
    jxl_output_processor_buffer* self) {
  self->data_ = NULL;
  self->size_ = 0;
  self->bytes_used_ = 0;
  self->wrapper_ = NULL;
}

static inline void jxl_output_processor_buffer_init(jxl_output_processor_buffer* self,
                                         uint8_t* buffer, size_t size,
                                         size_t bytes_used,
                                         jxl_encoder_output_processor_wrapper* wrapper) {
  self->data_ = buffer;
  self->size_ = size;
  self->bytes_used_ = bytes_used;
  self->wrapper_ = wrapper;
}

static inline jxl_status jxl_output_processor_buffer_advance(
    jxl_output_processor_buffer* self, size_t count) {
  JXL_ENSURE(count <= self->size_);
  self->data_ += count;
  self->size_ -= count;
  self->bytes_used_ += count;
  return jxl_ok_status();
}

static inline jxl_status jxl_output_processor_buffer_release(
    jxl_output_processor_buffer* self) {
  jxl_status result = jxl_ok_status();
  if (self->data_) {
    result = jxl_encoder_output_processor_wrapper_release_buffer(self->wrapper_,
                                                           self->bytes_used_);
  }
  self->data_ = NULL;
  self->size_ = 0;
  return result;
}

static inline jxl_status jxl_output_processor_buffer_append(
    jxl_output_processor_buffer* self, const void* data, size_t count) {
  memcpy(self->data_, data, count);
  JXL_RETURN_IF_ERROR(jxl_output_processor_buffer_advance(self, count));
  return jxl_ok_status();
}

static inline void jxl_output_processor_buffer_swap(jxl_output_processor_buffer* self,
                                         jxl_output_processor_buffer* other) {
  uint8_t* td = self->data_;
  self->data_ = other->data_;
  other->data_ = td;
  size_t ts = self->size_;
  self->size_ = other->size_;
  other->size_ = ts;
  size_t tb = self->bytes_used_;
  self->bytes_used_ = other->bytes_used_;
  other->bytes_used_ = tb;
  jxl_encoder_output_processor_wrapper* tw = self->wrapper_;
  self->wrapper_ = other->wrapper_;
  other->wrapper_ = tw;
}

static inline void jxl_output_processor_buffer_destroy(jxl_output_processor_buffer* self) {
  if (self == NULL) return;
  jxl_status result = jxl_output_processor_buffer_release(self);
  (void)result;
  JXL_DASSERT(jxl_status_ok(result));
}

static inline jxl_status jxl_append_data(jxl_encoder_output_processor_wrapper* output_processor,
                              const uint8_t* data, size_t size) {
  size_t written = 0;
  while (written < size) {
    jxl_output_processor_buffer buffer;
    jxl_output_processor_buffer_construct_empty(&buffer);
    jxl_status status = jxl_encoder_output_processor_wrapper_get_buffer(
        output_processor, 1, size - written, &buffer);
    if (!jxl_status_ok(status)) {
      jxl_output_processor_buffer_destroy(&buffer);
      return status;
    }
    size_t n =
        JXL_MIN(size - written, jxl_output_processor_buffer_size(&buffer));
    status = jxl_output_processor_buffer_append(&buffer, data + written, n);
    jxl_output_processor_buffer_destroy(&buffer);
    if (!jxl_status_ok(status)) return status;
    written += n;
  }
  return jxl_ok_status();
}

// Forward decl so FrameSettings can be completed before jxl_owned_frame_settings.
typedef struct jxl_encoder jxl_encoder;

typedef struct jxl_encoder_frame_settings {
  jxl_encoder* enc;
  jxl_compress_params cparams;
} jxl_encoder_frame_settings;

static inline void jxl_encoder_frame_settings_construct_empty(jxl_encoder_frame_settings* self) {
  self->enc = NULL;
  jxl_compress_params_construct_empty(&self->cparams);
}

static inline void jxl_encoder_frame_settings_destroy(jxl_encoder_frame_settings* self) {
  self->enc = NULL;
}

// Owning pointer to a jxl_encoder_frame_settings allocated via jxl_memory_manager.
// jxl_encoder_frame_settings is completed later in this header; destroy/construct
// helpers are only used from TUs that see the complete type.


typedef struct jxl_owned_frame_settings {
  jxl_encoder_frame_settings* ptr_;
  const jxl_memory_manager* memory_manager_;

} jxl_owned_frame_settings;

static inline void jxl_owned_frame_settings_construct_empty(jxl_owned_frame_settings* self) {
  self->ptr_ = NULL;
  self->memory_manager_ = NULL;
}
static inline void jxl_owned_frame_settings_destroy(jxl_owned_frame_settings* self) {
  if (self == NULL) return;
  if (self->ptr_ != NULL) {
    jxl_encoder_frame_settings_destroy(self->ptr_);
    self->memory_manager_->free(self->memory_manager_->opaque, self->ptr_);
  }
  self->ptr_ = NULL;
}

static inline void jxl_owned_frame_settings_make(jxl_encoder_frame_settings* ptr,
                                   const jxl_memory_manager* memory_manager,
                                   jxl_owned_frame_settings* out) {
  jxl_owned_frame_settings_destroy(out);
  out->ptr_ = ptr;
  out->memory_manager_ = memory_manager;
}
static inline void jxl_owned_frame_settings_swap(jxl_owned_frame_settings* self, jxl_owned_frame_settings* other) {
    jxl_encoder_frame_settings* tp = self->ptr_;
    self->ptr_ = other->ptr_;
    other->ptr_ = tp;
    const jxl_memory_manager* tm = self->memory_manager_;
    self->memory_manager_ = other->memory_manager_;
    other->memory_manager_ = tm;
  }

static inline jxl_encoder_frame_settings* jxl_owned_frame_settings_get(const jxl_owned_frame_settings* self) {
  return self->ptr_;
}
static inline bool jxl_owned_frame_settings_ok(const jxl_owned_frame_settings* self) {
  return self->ptr_ != NULL;
}


// Move-only list of jxl_owned_frame_settings (was MoveArray<jxl_owned_frame_settings>).


typedef struct jxl_owned_frame_settings_list {
  jxl_memory_manager* memory_manager;
  jxl_owned_frame_settings* ptr;
  size_t len;
  size_t capacity;

} jxl_owned_frame_settings_list;
static inline size_t jxl_owned_frame_settings_list_size(const jxl_owned_frame_settings_list* self) { return self->len; }
static inline bool jxl_owned_frame_settings_list_empty(const jxl_owned_frame_settings_list* self) { return self->len == 0; }
static inline jxl_owned_frame_settings* jxl_owned_frame_settings_list_data(jxl_owned_frame_settings_list* self) { return self->ptr; }
static inline const jxl_owned_frame_settings* jxl_owned_frame_settings_list_data_const(const jxl_owned_frame_settings_list* self) { return self->ptr; }
static inline jxl_owned_frame_settings* jxl_owned_frame_settings_list_at(jxl_owned_frame_settings_list* self, size_t i) { return &self->ptr[i]; }
static inline const jxl_owned_frame_settings* jxl_owned_frame_settings_list_at_const(const jxl_owned_frame_settings_list* self, size_t i) { return &self->ptr[i]; }

static inline void jxl_owned_frame_settings_list_construct_empty(jxl_owned_frame_settings_list* self) {
  self->memory_manager = NULL;
  self->ptr = NULL;
  self->len = 0;
  self->capacity = 0;
}

static inline void jxl_owned_frame_settings_list_swap(jxl_owned_frame_settings_list* self, jxl_owned_frame_settings_list* other) {
  jxl_memory_manager* tmp_mm = self->memory_manager;
  self->memory_manager = other->memory_manager;
  other->memory_manager = tmp_mm;
  jxl_owned_frame_settings* tmp_ptr = self->ptr;
  self->ptr = other->ptr;
  other->ptr = tmp_ptr;
  jxl_swap(&self->len, &other->len);
  jxl_swap(&self->capacity, &other->capacity);
}

static inline void jxl_owned_frame_settings_list_clear(jxl_owned_frame_settings_list* self) {
    for (size_t i = 0; i < self->len; ++i) {
      jxl_owned_frame_settings_destroy(self->ptr + i);
    }
    self->len = 0;
  }

static inline jxl_status jxl_owned_frame_settings_list_reserve(jxl_owned_frame_settings_list* self, size_t new_capacity) {
    if (new_capacity <= self->capacity) return jxl_ok_status();

    size_t grown = self->capacity;
    if (grown == 0) grown = 16;
    while (grown < new_capacity) {
      size_t next;
      if (!jxl_safe_add(grown, grown / 2, &next) || next <= grown) {
        grown = new_capacity;
        break;
      }
      grown = next;
    }
    if (grown < new_capacity) grown = new_capacity;

    size_t bytes;
    if (!jxl_safe_mul(grown, sizeof(jxl_owned_frame_settings), &bytes)) {
      return JXL_FAILURE("jxl_owned_frame_settings_list::reserve: size overflow");
    }
    jxl_owned_frame_settings* neu;
    if (self->memory_manager == NULL) {
      return JXL_FAILURE("jxl_owned_frame_settings_list::reserve: missing memory manager");
    }
    neu = (jxl_owned_frame_settings*)(
        self->memory_manager->alloc(self->memory_manager->opaque, bytes));
    if (neu == NULL) {
      return JXL_FAILURE("jxl_owned_frame_settings_list::reserve: allocation failed");
    }
    for (size_t i = 0; i < self->len; ++i) {
      jxl_owned_frame_settings_construct_empty(neu + i);
      jxl_owned_frame_settings_swap(neu + i, &self->ptr[i]);
      jxl_owned_frame_settings_destroy(self->ptr + i);
    }
    if (self->ptr != NULL) {
      self->memory_manager->free(self->memory_manager->opaque, self->ptr);
    }
    self->ptr = neu;
    self->capacity = grown;
    return jxl_ok_status();
  }

static inline void jxl_owned_frame_settings_list_destroy(jxl_owned_frame_settings_list* self) {
  jxl_owned_frame_settings_list_clear(self);
  if (self->ptr != NULL) {
    if (self->memory_manager != NULL) {
      self->memory_manager->free(self->memory_manager->opaque, self->ptr);
    }
  }
  self->ptr = NULL;
  self->capacity = 0;
}
static inline jxl_status jxl_owned_frame_settings_list_emplace_back(jxl_owned_frame_settings_list* self, jxl_owned_frame_settings* value) {
    if (self->len == self->capacity) {
      size_t need;
      if (!jxl_safe_add(self->capacity, (size_t)(1), &need)) {
        return JXL_FAILURE("jxl_owned_frame_settings_list::emplace_back: overflow");
      }
      JXL_RETURN_IF_ERROR(jxl_owned_frame_settings_list_reserve(self, need));
    }
    jxl_owned_frame_settings_construct_empty(self->ptr + self->len);
    jxl_owned_frame_settings_swap(self->ptr + self->len, value);
    ++self->len;
    return jxl_ok_status();
  }






// Internal use only struct, can only be initialized correctly by
// jxl_encoder_create.
typedef struct jxl_encoder {
  jxl_context* ctx;
  jxl_memory_manager memory_manager;
  jxl_owned_frame_settings_list encoder_options;

  size_t num_queued_frames;
  jxl_owned_queued_frames input_queue;
  jxl_encoder_output_processor_wrapper output_processor;

  jxl_cms_interface cms;

  // Force using the container even if not needed
  bool use_container;
  // True when a queued frame carries Exif/XMP metadata boxes from JPEG input.
  bool has_jpeg_metadata_boxes;
  // -1 = no container written yet; 0 = ftyp v0 written; 1 = ftyp v1 written.
  int container_ftyp_version;

  bool store_jpeg_metadata;
  int32_t codestream_level;
  jxl_codec_metadata metadata;
  jxl_array_u8 jpeg_metadata;

  jxl_status_t error;

  // Encoder wrote a jxlp (partial codestream) box, so any next codestream
  // parts must also be written in jxlp boxes, a single jxlc box cannot be
  // used. The counter is used for the 4-byte jxlp box index header.
  size_t jxlp_counter;

  // Wrote any output at all, so wrote the data before the first user added
  // frame or box, such as signature, basic info, ICC profile or jpeg
  // reconstruction box.
  bool wrote_bytes;

  bool input_closed;
  bool jpeg_metadata_set;

  // Takes the first frame in the input_queue, encodes it, and appends
  // the bytes to the output_byte_queue.

  // `write_box` must never seek before the position the output wrapper was at
  // the moment of the call, and must leave the output wrapper such that its
  // position is one byte past the end of the written box.

} jxl_encoder;

static inline void jxl_encoder_construct_empty(jxl_encoder* self,
                                                jxl_memory_manager* mm) {
  jxl_owned_frame_settings_list_construct_empty(&self->encoder_options);
  self->encoder_options.memory_manager = mm;
  jxl_owned_queued_frames_construct_empty(&self->input_queue);
  self->input_queue.memory_manager = mm;
  jxl_encoder_output_processor_wrapper_construct_empty(&self->output_processor);
  jxl_codec_metadata_construct_empty(&self->metadata, mm);
  jxl_array_construct_empty(&self->jpeg_metadata, mm);
  // Match in-class defaults used by jxl_encoder_create.
  self->container_ftyp_version = -1;
  self->error = JXL_OK;
}
static inline void jxl_encoder_destroy_contents(jxl_encoder* self) {
  jxl_owned_frame_settings_list_destroy(&self->encoder_options);
  jxl_owned_queued_frames_destroy(&self->input_queue);
  jxl_encoder_output_processor_wrapper_destroy(&self->output_processor);
  jxl_codec_metadata_destroy(&self->metadata);
  jxl_array_destroy(&self->jpeg_metadata);
}

jxl_status jxl_encoder_process_one_enqueued_input(jxl_encoder* self);
jxl_status jxl_encoder_append_box(jxl_encoder* self, const jxl_enc_box_type* type,
                                bool unbounded, size_t box_max_size,
                                jxl_status (*write_box)(void*), void* opaque);
jxl_status jxl_encoder_append_box_with_contents(jxl_encoder* self,
                                            const jxl_enc_box_type* type,
                                            const uint8_t* contents,
                                            size_t size);


static inline bool jxl_encoder_must_use_container(const jxl_encoder* self) {
  return self->container_ftyp_version >= 0 || self->use_container ||
         (self->codestream_level != 5 && self->codestream_level != -1) ||
         self->store_jpeg_metadata || self->has_jpeg_metadata_boxes;
}



static inline void jxl_make_owned_frame_settings(
    const jxl_memory_manager* memory_manager, jxl_owned_frame_settings* out) {
  jxl_owned_frame_settings_construct_empty(out);
  jxl_encoder_frame_settings* mem = (jxl_encoder_frame_settings*)(
      memory_manager->alloc(memory_manager->opaque,
                            sizeof(jxl_encoder_frame_settings)));
  if (!mem) {
    jxl_owned_frame_settings_make(NULL, memory_manager, out);
    return;
  }
  jxl_encoder_frame_settings_construct_empty(mem);
  jxl_owned_frame_settings_make(mem, memory_manager, out);
}

#endif  // LIB_JXL_ENCODE_INTERNAL_H_
