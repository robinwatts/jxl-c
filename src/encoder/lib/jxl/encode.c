// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <brotli/encode.h>
#include <jxl/cms.h>
#include <jxl/encode.h>
#include <jxl/memory_manager.h>
#include <jxl/version.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "lib/jxl/base/exif.h"
#include "lib/jxl/base/printf_macros.h"
#include "lib/jxl/base/sanitizers.h"
#include "lib/jxl/layer_type.h"
#include "lib/jxl/enc_bit_writer.h"
#include "lib/jxl/enc_fields.h"
#include "lib/jxl/enc_frame.h"
#include "lib/jxl/enc_icc_codec.h"
#include "lib/jxl/encode_internal.h"
#include "lib/jxl/context_internal.h"
#include "lib/jxl/image_metadata.h"
#include "lib/jxl/jpeg/enc_jpeg_data.h"
#include "lib/jxl/luminance.h"
#include "lib/jxl/base/common.h"
#include "lib/jxl/brotli_alloc.h"

// Map encoder API status codes onto internal jxl_status.
static jxl_status jxl_status_from_encoder(jxl_encoder_status s) {
  switch (s) {
    case JXL_ENCODER_SUCCESS:
      return jxl_ok_status();
    case JXL_ENCODER_NEED_MORE_OUTPUT:
      return jxl_status_from_code(kNotEnoughBytes);
    default:
      return jxl_status_from_code(kGenericError);
  }
}

#define JXL_API_ERROR_AS_STATUS(enc, error_code, format, ...) \
  jxl_status_from_encoder(JXL_API_ERROR(enc, error_code, format, ##__VA_ARGS__))

// jxl_debug-printing failure macro similar to JXL_FAILURE, but for the status code
// JXL_ENCODER_ERROR
#if (JXL_CRASH_ON_ERROR)
#define JXL_API_ERROR(enc, error_code, format, ...)                          \
  (enc->error = error_code,                                                  \
   jxl_debug(("%s:%d: " format "\n"), __FILE__, __LINE__, ##__VA_ARGS__), \
   jxl_abort(), JXL_ENCODER_ERROR)
#define JXL_API_ERROR_NOSET(format, ...)                                     \
  (jxl_debug(("%s:%d: " format "\n"), __FILE__, __LINE__, ##__VA_ARGS__), \
   jxl_abort(), JXL_ENCODER_ERROR)
#else  // JXL_CRASH_ON_ERROR
#define JXL_API_ERROR(enc, error_code, format, ...)                            \
  (enc->error = error_code,                                                    \
   ((JXL_IS_DEBUG_BUILD) &&                                                    \
    jxl_debug(("%s:%d: " format "\n"), __FILE__, __LINE__, ##__VA_ARGS__)), \
   JXL_ENCODER_ERROR)
#define JXL_API_ERROR_NOSET(format, ...)                                     \
  (jxl_debug(("%s:%d: " format "\n"), __FILE__, __LINE__, ##__VA_ARGS__), \
   JXL_ENCODER_ERROR)
#endif  // JXL_CRASH_ON_ERROR

jxl_status jxl_encoder_output_processor_wrapper_get_buffer(
    jxl_encoder_output_processor_wrapper* self, size_t min_size,
    size_t requested_size, jxl_output_processor_buffer* out) {
  JXL_ENSURE(min_size > 0);
  JXL_ENSURE(!self->has_buffer_);
  requested_size = JXL_MAX(min_size, requested_size);
  JXL_ENSURE(self->output_position_ <= self->position_);
  size_t additional_size = self->position_ - self->output_position_;
  JXL_ENSURE(self->memory_manager_ != NULL);

  if (self->avail_out_ != NULL) {
    if (min_size + additional_size < *self->avail_out_) {
      jxl_encoder_output_processor_wrapper_insert_buffer(self, self->position_);
      self->has_buffer_ = true;
      jxl_output_processor_buffer tmp;
      jxl_output_processor_buffer_construct_empty(&tmp);
      jxl_output_processor_buffer_init(&tmp, *self->next_out_ + additional_size,
                                   *self->avail_out_ - additional_size, 0,
                                   self);
      jxl_output_processor_buffer_swap(out, &tmp);
      jxl_output_processor_buffer_destroy(&tmp);
      return jxl_ok_status();
    }
  }

  size_t idx =
      jxl_encoder_output_processor_wrapper_insert_buffer(self, self->position_);
  jxl_internal_buffer* buffer =
      &jxl_buffered_chunks_at(&self->internal_buffers_, idx)->buffer;
  size_t alloc_size = requested_size;
  if (idx + 1 < jxl_buffered_chunks_size(&self->internal_buffers_)) {
    alloc_size = JXL_MIN(alloc_size,
                         jxl_buffered_chunks_at(&self->internal_buffers_, idx + 1)->start -
                             self->position_);
    JXL_ENSURE(alloc_size >= min_size);
  }
  JXL_RETURN_IF_ERROR(jxl_padded_bytes_resize(&buffer->owned_data, alloc_size));
  self->has_buffer_ = true;
  jxl_output_processor_buffer tmp;
  jxl_output_processor_buffer_construct_empty(&tmp);
  jxl_output_processor_buffer_init(&tmp, jxl_padded_bytes_data(&buffer->owned_data),
                               alloc_size, 0, self);
  jxl_output_processor_buffer_swap(out, &tmp);
  jxl_output_processor_buffer_destroy(&tmp);
  return jxl_ok_status();
}

jxl_status jxl_encoder_output_processor_wrapper_seek(
    jxl_encoder_output_processor_wrapper* self, size_t pos) {
  JXL_ENSURE(!self->has_buffer_);
  JXL_ENSURE(pos >= self->finalized_position_);
  self->position_ = pos;
  return jxl_ok_status();
}

jxl_status jxl_encoder_output_processor_wrapper_set_finalized_position(
    jxl_encoder_output_processor_wrapper* self) {
  JXL_ENSURE(!self->has_buffer_);
  self->finalized_position_ = self->position_;
  JXL_RETURN_IF_ERROR(jxl_encoder_output_processor_wrapper_flush_output(
      self, self->next_out_, self->avail_out_));
  return jxl_ok_status();
}

jxl_status jxl_encoder_output_processor_wrapper_set_avail_out(
    jxl_encoder_output_processor_wrapper* self, uint8_t** next_out,
    size_t* avail_out) {
  self->avail_out_ = avail_out;
  self->next_out_ = next_out;
  JXL_RETURN_IF_ERROR(jxl_encoder_output_processor_wrapper_flush_output(
      self, self->next_out_, self->avail_out_));
  return jxl_ok_status();
}

jxl_status jxl_encoder_output_processor_wrapper_release_buffer(
    jxl_encoder_output_processor_wrapper* self, size_t bytes_used) {
  JXL_ENSURE(self->has_buffer_);
  self->has_buffer_ = false;
  size_t idx =
      jxl_encoder_output_processor_wrapper_find_buffer(self, self->position_);
  JXL_ENSURE(idx < jxl_buffered_chunks_size(&self->internal_buffers_));
  if (bytes_used == 0) {
    jxl_buffered_chunks_erase(&self->internal_buffers_, idx);
    return jxl_ok_status();
  }
  jxl_buffered_chunks_at(&self->internal_buffers_, idx)->buffer.written_bytes =
      bytes_used;
  self->position_ += bytes_used;
  return jxl_ok_status();
}

// Tries to write all the bytes up to the finalized position.
jxl_status jxl_encoder_output_processor_wrapper_flush_output(
    jxl_encoder_output_processor_wrapper* self, uint8_t** next_out,
    size_t* avail_out) {
  JXL_ENSURE(!self->has_buffer_);
  if (!avail_out) {
    return jxl_ok_status();
  }
  while (self->output_position_ < self->finalized_position_ && *avail_out > 0) {
    JXL_ENSURE(!jxl_buffered_chunks_empty(&self->internal_buffers_));
    jxl_buffered_chunk* chunk =
        jxl_buffered_chunks_front(&self->internal_buffers_);
    JXL_ENSURE(self->output_position_ >= chunk->start);
    JXL_ENSURE(chunk->buffer.written_bytes != 0);
    size_t buffer_last_byte = chunk->start + chunk->buffer.written_bytes;
    if (!jxl_padded_bytes_empty(&chunk->buffer.owned_data)) {
      size_t start_in_buffer = self->output_position_ - chunk->start;
      JXL_ENSURE(buffer_last_byte > self->output_position_);
      size_t num_to_write =
          JXL_MIN(buffer_last_byte, self->finalized_position_) -
          self->output_position_;
      size_t n = JXL_MIN(num_to_write, *avail_out);
      memcpy(*next_out,
             jxl_padded_bytes_data(&chunk->buffer.owned_data) + start_in_buffer, n);
      *avail_out -= n;
      *next_out += n;
      self->output_position_ += n;
    } else {
      size_t advance =
          JXL_MIN(buffer_last_byte, self->finalized_position_) -
          self->output_position_;
      self->output_position_ += advance;
      *next_out += advance;
      *avail_out -= advance;
    }
    if (buffer_last_byte == self->output_position_) {
      jxl_buffered_chunks_erase(&self->internal_buffers_, (size_t)(0));
    }
  }
  return jxl_ok_status();
}

size_t jxl_write_box_header(const jxl_enc_box_type* type, size_t size, bool unbounded,
                      bool force_large_box, uint8_t* output) {
  uint64_t box_size = 0;
  bool large_size = false;
  if (!unbounded) {
    if (box_size >= kLargeBoxContentSizeThreshold || force_large_box) {
      large_size = true;
      box_size = size + kLargeBoxHeaderSize;
    } else {
      box_size = size + kSmallBoxHeaderSize;
    }
  }

  size_t idx = 0;
  {
    const uint64_t store = large_size ? 1 : box_size;
    for (size_t i = 0; i < 4; i++) {
      output[idx++] = store >> (8 * (3 - i)) & 0xff;
    }
  }
  for (size_t i = 0; i < 4; i++) {
    output[idx++] = jxl_enc_box_type_at(type, i);
  }

  if (large_size) {
    for (size_t i = 0; i < 8; i++) {
      output[idx++] = box_size >> (8 * (7 - i)) & 0xff;
    }
  }
  return idx;
}

typedef struct jxl_write_box_contents_ctx {
  jxl_encoder* enc;
  const uint8_t* contents;
  size_t size;
} jxl_write_box_contents_ctx;

static jxl_status jxl_write_box_contents(void* opaque) {
  jxl_write_box_contents_ctx* c = (jxl_write_box_contents_ctx*)(opaque);
  return jxl_append_data(&c->enc->output_processor, c->contents, c->size);
}

jxl_status jxl_encoder_append_box(jxl_encoder* self, const jxl_enc_box_type* type,
                                bool unbounded, size_t box_max_size,
                                jxl_status (*write_box)(void*), void* opaque) {
  size_t current_position = jxl_encoder_output_processor_wrapper_current_position(&self->output_processor);
  bool large_box = false;
  size_t box_header_size = 0;
  if (box_max_size >= kLargeBoxContentSizeThreshold && !unbounded) {
    box_header_size = kLargeBoxHeaderSize;
    large_box = true;
  } else {
    box_header_size = kSmallBoxHeaderSize;
  }
  JXL_RETURN_IF_ERROR(
      jxl_encoder_output_processor_wrapper_seek(&self->output_processor, current_position + box_header_size));
  size_t box_contents_start = jxl_encoder_output_processor_wrapper_current_position(&self->output_processor);
  JXL_RETURN_IF_ERROR(write_box(opaque));
  size_t box_contents_end = jxl_encoder_output_processor_wrapper_current_position(&self->output_processor);
  JXL_RETURN_IF_ERROR(jxl_encoder_output_processor_wrapper_seek(&self->output_processor, current_position));
  JXL_ENSURE(box_contents_end >= box_contents_start);
  if (box_contents_end - box_contents_start > box_max_size) {
    return JXL_API_ERROR_AS_STATUS(self, JXL_ENCODER_ERR_GENERIC,
                         "Internal error: upper bound on box size was "
                         "violated, upper bound: %" jxl_pr_iu_s ", actual: %" jxl_pr_iu_s,
                         box_max_size, box_contents_end - box_contents_start);
  }
  // We need to release the buffer before Seek.
  {
    jxl_output_processor_buffer buffer;
    jxl_output_processor_buffer_construct_empty(&buffer);
    jxl_status status = jxl_encoder_output_processor_wrapper_get_buffer(
        &self->output_processor, box_contents_start - current_position, 0,
        &buffer);
    if (!jxl_status_ok(status)) {
      jxl_output_processor_buffer_destroy(&buffer);
      return status;
    }
    const size_t n =
        jxl_write_box_header(type, box_contents_end - box_contents_start,
                            unbounded, large_box, jxl_output_processor_buffer_data(&buffer));
    JXL_ENSURE(n == box_header_size);
    status = jxl_output_processor_buffer_advance(&buffer, n);
    jxl_output_processor_buffer_destroy(&buffer);
    JXL_RETURN_IF_ERROR(status);
  }
  JXL_RETURN_IF_ERROR(jxl_encoder_output_processor_wrapper_seek(&self->output_processor, box_contents_end));
  JXL_RETURN_IF_ERROR(jxl_encoder_output_processor_wrapper_set_finalized_position(&self->output_processor));
  return jxl_ok_status();
}

jxl_status jxl_encoder_append_box_with_contents(jxl_encoder* self,
                                            const jxl_enc_box_type* type,
                                            const uint8_t* contents,
                                            size_t size) {
  jxl_write_box_contents_ctx ctx = {self, contents, size};
  return jxl_encoder_append_box(self, type, /*unbounded=*/false, size,
                             jxl_write_box_contents, &ctx);
}

uint32_t jxl_encoder_version(void) {
  return JPEGXL_MAJOR_VERSION * 1000000 + JPEGXL_MINOR_VERSION * 1000 +
         JPEGXL_PATCH_VERSION;
}

static void jxl_write_jxlp_box_counter_bytes(uint32_t counter, bool last,
                                     uint8_t* buffer) {
  if (last) counter |= 0x80000000;
  for (size_t i = 0; i < 4; i++) {
    buffer[i] = counter >> (8 * (3 - i)) & 0xff;
  }
}

static jxl_status jxl_write_jxlp_box_counter(uint32_t counter, bool last,
                                jxl_output_processor_buffer* buffer) {
  uint8_t buf[4];
  jxl_write_jxlp_box_counter_bytes(counter, last, buf);
  JXL_RETURN_IF_ERROR(jxl_output_processor_buffer_append(buffer, buf, 4));
  return jxl_ok_status();
}

static void jxl_queue_frame(const jxl_encoder_frame_settings* frame_settings,
                jxl_owned_queued_frame* frame) {
  if (!jxl_status_ok(jxl_owned_queued_frames_emplace_back(&frame_settings->enc->input_queue, frame))) {
    JXL_CRASH();
  }
  frame_settings->enc->num_queued_frames++;
}

static bool jxl_queue_jpeg_metadata_box(jxl_encoder* enc, const char* type,
                          const jxl_array_u8* contents, bool compress_box,
                          jxl_encoder_metadata_boxes* out) {
  enc->has_jpeg_metadata_boxes = true;
  jxl_enc_box_type box_type = jxl_make_box_type(type);
  return jxl_status_ok(
      jxl_encoder_metadata_boxes_push_back(out, box_type, contents, compress_box));
}

// TODO(lode): share this code and the Brotli compression code in enc_jpeg_data
static jxl_encoder_status jxl_brotli_compress(int quality, const uint8_t* in, size_t in_size,
                                jxl_padded_bytes* out) {
  jxl_memory_manager* memory_manager = jxl_padded_bytes_memory_manager(out);
  BrotliEncoderState* enc = jxl_brotli_encoder_create(memory_manager);
  if (!enc) return JXL_API_ERROR_NOSET("BrotliEncoderCreateInstance failed");

  BrotliEncoderSetParameter(enc, BROTLI_PARAM_QUALITY, quality);
  BrotliEncoderSetParameter(enc, BROTLI_PARAM_SIZE_HINT, in_size);

  const size_t kBufferSize = 128 * 1024;
#define QUIT(message)                         \
  do {                                        \
    BrotliEncoderDestroyInstance(enc);        \
    return JXL_API_ERROR_NOSET(message);      \
  } while (0)
  jxl_padded_bytes temp_buffer;
  jxl_padded_bytes_make(memory_manager, &temp_buffer);
  if (!jxl_status_ok(jxl_padded_bytes_with_initial_space(memory_manager, kBufferSize,
                                        &temp_buffer))) {
    jxl_padded_bytes_destroy(&temp_buffer);
    QUIT("Initialization of jxl_padded_bytes failed");
  }
#undef QUIT

  size_t avail_in = in_size;
  const uint8_t* next_in = in;

  size_t total_out = 0;

  for (;;) {
    size_t avail_out = kBufferSize;
    uint8_t* next_out = jxl_padded_bytes_data(&temp_buffer);
    jxl_msan_memory_is_initialized(next_in, avail_in);
    if (!BrotliEncoderCompressStream(enc, BROTLI_OPERATION_FINISH, &avail_in,
                                     &next_in, &avail_out, &next_out,
                                     &total_out)) {
      jxl_padded_bytes_destroy(&temp_buffer);
      BrotliEncoderDestroyInstance(enc);
      return JXL_API_ERROR_NOSET("Brotli compression failed");
    }
    size_t out_size = next_out - jxl_padded_bytes_data(&temp_buffer);
    jxl_msan_unpoison_memory(next_out - out_size, out_size);
    if (!jxl_status_ok(jxl_padded_bytes_resize(out, jxl_padded_bytes_size(out) + out_size))) {
      jxl_padded_bytes_destroy(&temp_buffer);
      BrotliEncoderDestroyInstance(enc);
      return JXL_API_ERROR_NOSET("resizing of jxl_padded_bytes failed");
    }

    memcpy(jxl_padded_bytes_data(out) + jxl_padded_bytes_size(out) - out_size, jxl_padded_bytes_data(&temp_buffer), out_size);
    if (BrotliEncoderIsFinished(enc)) break;
  }

  jxl_padded_bytes_destroy(&temp_buffer);
  BrotliEncoderDestroyInstance(enc);
  return JXL_ENCODER_SUCCESS;
}

static jxl_status jxl_write_metadata_box(jxl_encoder* enc, int brotli_effort,
                             const jxl_encoder_metadata_box* box,
                             const jxl_bytes* contents) {
  if (box->compress_box) {
    jxl_padded_bytes compressed;
    jxl_padded_bytes_make(&enc->memory_manager, &compressed);
    jxl_status status = jxl_padded_bytes_append(
        &compressed, jxl_enc_box_type_data(&box->type),
        jxl_enc_box_type_data(&box->type) + jxl_enc_box_type_size());
    if (!jxl_status_ok(status)) {
      jxl_padded_bytes_destroy(&compressed);
      return status;
    }
    if (jxl_brotli_compress((brotli_effort >= 0 ? brotli_effort : 4),
                       jxl_bytes_data(contents), jxl_bytes_size(contents),
                       &compressed) != JXL_ENCODER_SUCCESS) {
      jxl_padded_bytes_destroy(&compressed);
      return JXL_API_ERROR_AS_STATUS(enc, JXL_ENCODER_ERR_GENERIC,
                           "Brotli compression for brob box failed");
    }
    {
      jxl_enc_box_type brob = jxl_make_box_type("brob");
      status = jxl_encoder_append_box_with_contents(
          enc, &brob, jxl_padded_bytes_data(&compressed),
          jxl_padded_bytes_size(&compressed));
      jxl_padded_bytes_destroy(&compressed);
      if (!jxl_status_ok(status)) return status;
    }
    return jxl_ok_status();
  }
  JXL_RETURN_IF_ERROR(jxl_encoder_append_box_with_contents(enc, &box->type, jxl_bytes_data(contents), jxl_bytes_size(contents)));
  return jxl_ok_status();
}

// Returns 5 or 10 on success, or -1 if settings exceed level 10 limits.
static int jxl_required_codestream_level(const jxl_encoder* enc) {
  const jxl_image_metadata* m = &enc->metadata.m;

  const uint64_t xsize = jxl_size_header_x_size(&enc->metadata.size);
  const uint64_t ysize = jxl_size_header_y_size(&enc->metadata.size);
  size_t icc_size = 0;
  if (jxl_enc_color_encoding_want_icc(&m->color_encoding)) {
    icc_size = jxl_array_len(jxl_enc_color_encoding_icc(&m->color_encoding));
  }

  if (xsize > (1ull << 30ull) || ysize > (1ull << 30ull) ||
      xsize * ysize > (1ull << 40ull)) {
    return -1;
  }
  if (icc_size > (1ull << 28)) {
    return -1;
  }

  if (!m->modular_16_bit_buffer_sufficient) {
    return 10;
  }
  if (xsize > (1ull << 18ull) || ysize > (1ull << 18ull) ||
      xsize * ysize > (1ull << 28ull)) {
    return 10;
  }
  if (icc_size > (1ull << 22)) {
    return 10;
  }
  if (m->num_extra_channels > 4) {
    return 10;
  }

  return 5;
}

static jxl_encoder_status jxl_setup_metadata_from_jpeg(jxl_encoder* enc,
                                       const jxl_jpeg_data* jpeg_data) {
  if (enc->jpeg_metadata_set) {
    return JXL_ENCODER_SUCCESS;
  }
  if (!jxl_status_ok(jxl_set_color_encoding_from_jpeg_data(
          enc->ctx, &enc->cms, jpeg_data, &enc->metadata.m.color_encoding))) {
    return JXL_API_ERROR(
        enc, JXL_ENCODER_ERR_BAD_INPUT,
        "Error decoding the ICC profile embedded in the input JPEG");
  }
  if (!jxl_status_ok(jxl_size_header_set(&enc->metadata.size, jpeg_data->width, jpeg_data->height))) {
    return JXL_API_ERROR(enc, JXL_ENCODER_ERR_API_USAGE, "Invalid dimensions");
  }
  enc->metadata.m.bit_depth.bits_per_sample = 8;
  enc->metadata.m.bit_depth.exponent_bits_per_sample = 0;
  enc->metadata.m.bit_depth.floating_point_sample = false;
  enc->metadata.m.modular_16_bit_buffer_sufficient = true;
  enc->metadata.m.num_extra_channels = 0;
  jxl_extra_channel_infos_clear(&enc->metadata.m.extra_channel_info);
  enc->metadata.m.xyb_encoded = false;
  enc->metadata.m.orientation = 1;
  enc->metadata.m.have_preview = false;
  enc->metadata.m.have_animation = false;
  enc->metadata.m.have_intrinsic_size = false;
  jxl_set_intensity_target(&enc->metadata.m);
  // VisitFields nests never entered for JPEG (gates still write bits).
  JXL_DASSERT(!enc->metadata.m.xyb_encoded);
  JXL_DASSERT(!enc->metadata.m.have_preview);
  JXL_DASSERT(!enc->metadata.m.have_animation);
  JXL_DASSERT(!enc->metadata.m.have_intrinsic_size);
  JXL_DASSERT(enc->metadata.m.num_extra_channels == 0);
  JXL_DASSERT(jxl_extra_channel_infos_empty(&enc->metadata.m.extra_channel_info));
  JXL_DASSERT(!enc->metadata.m.bit_depth.floating_point_sample);
  JXL_DASSERT(enc->metadata.m.bit_depth.bits_per_sample == 8);
  enc->jpeg_metadata_set = true;
  return JXL_ENCODER_SUCCESS;
}

static jxl_status jxl_zero_pad_header_body(void* opaque) {
  jxl_bit_writer_zero_pad_to_byte((jxl_bit_writer*)(opaque));
  return jxl_ok_status();
}

typedef struct jxl_jxlp_ctx {
  jxl_encoder* enc;
  jxl_padded_bytes* header_bytes;
  size_t* jxlp_counter;
} jxl_jxlp_ctx;

static jxl_status jxl_write_jxlp_box_contents(void* opaque) {
  jxl_jxlp_ctx* c = (jxl_jxlp_ctx*)(opaque);
  jxl_output_processor_buffer buffer;
  jxl_output_processor_buffer_construct_empty(&buffer);
  jxl_status status = jxl_encoder_output_processor_wrapper_get_buffer(
      &c->enc->output_processor, jxl_padded_bytes_size(c->header_bytes) + 4, 0,
      &buffer);
  if (!jxl_status_ok(status)) {
    jxl_output_processor_buffer_destroy(&buffer);
    return status;
  }
  status = jxl_write_jxlp_box_counter((*c->jxlp_counter)++, /*last=*/false, &buffer);
  if (!jxl_status_ok(status)) {
    jxl_output_processor_buffer_destroy(&buffer);
    return status;
  }
  status = jxl_output_processor_buffer_append(&buffer, jxl_padded_bytes_data(c->header_bytes),
                                          jxl_padded_bytes_size(c->header_bytes));
  jxl_output_processor_buffer_destroy(&buffer);
  return status;
}

jxl_status jxl_encoder_process_one_enqueued_input_body_with_header(
    jxl_encoder* self, jxl_owned_queued_frame* input_frame,
    jxl_padded_bytes* header_bytes) {
  const bool last_frame = self->input_closed && (self->num_queued_frames == 0);

  if (!self->wrote_bytes) {
    const int required_level = jxl_required_codestream_level(self);
    JXL_ENSURE(required_level == -1 || required_level == 5 ||
               required_level == 10);
    if (required_level == -1) {
      return JXL_API_ERROR_AS_STATUS(self, JXL_ENCODER_ERR_API_USAGE,
                           "Codestream level verification failed");
    }
    if (self->codestream_level == -1) self->codestream_level = required_level;
    if (self->codestream_level == 5 && required_level != 5) {
      return JXL_API_ERROR_AS_STATUS(self, JXL_ENCODER_ERR_API_USAGE,
                           "Codestream level verification for level 5 failed");
    }
    jxl_bit_writer writer;
    jxl_bit_writer_make(&self->memory_manager, &writer);
    if (!jxl_status_ok(jxl_write_codestream_headers(&self->metadata, &writer))) {
      jxl_bit_writer_destroy(&writer);
      return JXL_API_ERROR_AS_STATUS(self, JXL_ENCODER_ERR_GENERIC,
                           "Failed to write codestream header");
    }
    if (jxl_enc_color_encoding_want_icc(&self->metadata.m.color_encoding)) {
      jxl_bytes icc_bytes = jxl_bytes_make(
          jxl_array_data_const(jxl_enc_color_encoding_icc(&self->metadata.m.color_encoding)),
          jxl_array_len(jxl_enc_color_encoding_icc(&self->metadata.m.color_encoding)));
      if (!jxl_status_ok(jxl_write_icc(&icc_bytes, &writer, kLayerHeader))) {
        jxl_bit_writer_destroy(&writer);
        return JXL_API_ERROR_AS_STATUS(self, JXL_ENCODER_ERR_GENERIC,
                             "Failed to write ICC profile");
      }
    }
    {
      jxl_status pad_status = jxl_bit_writer_with_max_bits(
          &writer, 8, kLayerHeader, jxl_zero_pad_header_body, &writer);
      if (!jxl_status_ok(pad_status)) {
        jxl_bit_writer_destroy(&writer);
        return pad_status;
      }
    }

    {
      jxl_padded_bytes taken;
      jxl_bit_writer_take_bytes(&writer, &taken);
      jxl_padded_bytes_swap(header_bytes, &taken);
      jxl_padded_bytes_destroy(&taken);
    }
    jxl_bit_writer_destroy(&writer);

    if (jxl_encoder_must_use_container(self)) {
      self->container_ftyp_version = 0;
      jxl_array_u8 container_header;
      jxl_array_construct_empty(&container_header, &self->memory_manager);
      jxl_status box_status = jxl_make_container_header(&self->memory_manager, 0, &container_header);
      if (!jxl_status_ok(box_status)) {
        jxl_array_destroy(&container_header);
        return box_status;
      }
      box_status = jxl_append_data(&self->output_processor,
                              jxl_array_data(&container_header),
                              jxl_array_len(&container_header));
      jxl_array_destroy(&container_header);
      if (!jxl_status_ok(box_status)) return box_status;

      if (self->codestream_level != 5) {
        const uint8_t level = (uint8_t)(self->codestream_level);
        jxl_array_u8 jxll_box;
        jxl_array_construct_empty(&jxll_box, &self->memory_manager);
        {
          jxl_enc_box_type jxll = jxl_make_box_type("jxll");
          box_status = jxl_append_box_header(&jxll, 1,
                                            /*unbounded=*/false, &jxll_box);
          if (!jxl_status_ok(box_status)) {
            jxl_array_destroy(&jxll_box);
            return box_status;
          }
        }
        box_status = jxl_array_u8_push_back(&jxll_box, level);
        if (!jxl_status_ok(box_status)) {
          jxl_array_destroy(&jxll_box);
          return box_status;
        }
        box_status = jxl_append_data(&self->output_processor, jxl_array_data(&jxll_box),
                                jxl_array_len(&jxll_box));
        jxl_array_destroy(&jxll_box);
        if (!jxl_status_ok(box_status)) return box_status;
      }

      bool partial_header =
          self->store_jpeg_metadata || !jxl_encoder_metadata_boxes_empty(&jxl_owned_queued_frame_get(input_frame)->metadata_boxes);

      if (partial_header) {
        jxl_jxlp_ctx jxlp_ctx = {self, header_bytes, &self->jxlp_counter};
        {
          jxl_enc_box_type jxlp = jxl_make_box_type("jxlp");
          JXL_RETURN_IF_ERROR(jxl_encoder_append_box(self, 
              &jxlp, /*unbounded=*/false,
              jxl_padded_bytes_size(header_bytes) + 4, jxl_write_jxlp_box_contents, &jxlp_ctx));
        }
        jxl_padded_bytes_clear(header_bytes);
      }

      if (self->store_jpeg_metadata && !jxl_array_empty(&self->jpeg_metadata)) {
        {
          jxl_enc_box_type jbrd = jxl_make_box_type("jbrd");
          JXL_RETURN_IF_ERROR(
              jxl_encoder_append_box_with_contents(self, &jbrd, jxl_array_data(&self->jpeg_metadata), jxl_array_len(&self->jpeg_metadata)));
        }
      }
    }
    self->wrote_bytes = true;
  }

  JXL_RETURN_IF_ERROR(jxl_encoder_output_processor_wrapper_set_finalized_position(&self->output_processor));

  for (size_t i = 0; i < jxl_encoder_metadata_boxes_size(&jxl_owned_queued_frame_get(input_frame)->metadata_boxes); ++i) {
    jxl_bytes box_contents = jxl_byte_chunks_at(
        &jxl_owned_queued_frame_get(input_frame)->metadata_boxes.contents, i);
    JXL_RETURN_IF_ERROR(jxl_write_metadata_box(
        self, jxl_owned_queued_frame_get(input_frame)->cparams.brotli_effort,
        jxl_array_at(&jxl_owned_queued_frame_get(input_frame)->metadata_boxes.boxes, i),
        &box_contents));
  }

  uint32_t max_bits_per_sample = self->metadata.m.bit_depth.bits_per_sample;
  uint32_t bits_per_channels_estimate =
      JXL_MAX(24u, max_bits_per_sample + 3);
  size_t upper_bound_on_compressed_size_bits =
      jxl_codec_metadata_x_size(&self->metadata) * jxl_codec_metadata_y_size(&self->metadata) *
      jxl_enc_color_encoding_channels(&self->metadata.m.color_encoding) * bits_per_channels_estimate;
  size_t upper_bound_on_compressed_size_bytes =
      0x100000 + (upper_bound_on_compressed_size_bits >> 3);
  bool use_large_box = upper_bound_on_compressed_size_bytes >=
                       kLargeBoxContentSizeThreshold;
  size_t box_header_size =
      use_large_box ? kLargeBoxHeaderSize : kSmallBoxHeaderSize;

  size_t frame_start_pos = jxl_encoder_output_processor_wrapper_current_position(&self->output_processor);

  if (jxl_encoder_must_use_container(self)) {
    if (!last_frame || self->jxlp_counter > 0) {
      box_header_size += 4;
    }
    JXL_RETURN_IF_ERROR(
        jxl_encoder_output_processor_wrapper_seek(&self->output_processor, frame_start_pos + box_header_size));
  }
  const size_t content_start = jxl_encoder_output_processor_wrapper_current_position(&self->output_processor);

  if (!jxl_padded_bytes_empty(header_bytes)) {
    JXL_RETURN_IF_ERROR(jxl_append_data(&self->output_processor, jxl_padded_bytes_data(header_bytes), jxl_padded_bytes_size(header_bytes)));
  }

  jxl_frame_info frame_info;
  frame_info.is_last = last_frame;
  if (!jxl_status_ok(jxl_encode_frame(&self->memory_manager, &jxl_owned_queued_frame_get(input_frame)->cparams, &frame_info,
                        &self->metadata, &jxl_owned_queued_frame_get(input_frame)->frame_data, &self->output_processor))) {
    return JXL_API_ERROR_AS_STATUS(self, JXL_ENCODER_ERR_GENERIC,
                                 "Failed to encode frame");
  }

  const size_t content_size =
      jxl_encoder_output_processor_wrapper_current_position(&self->output_processor) - content_start;

  if (jxl_encoder_must_use_container(self)) {
    JXL_RETURN_IF_ERROR(jxl_encoder_output_processor_wrapper_seek(&self->output_processor, frame_start_pos));
    jxl_array_u8 box_header;
    jxl_array_construct_empty(&box_header, &self->memory_manager);
    jxl_status box_status =
        jxl_array_resize_zero(&box_header, box_header_size);
    if (!jxl_status_ok(box_status)) {
      jxl_array_destroy(&box_header);
      return box_status;
    }
    const size_t frame_content_size = content_size;
    if (!use_large_box &&
        frame_content_size >= kLargeBoxContentSizeThreshold) {
      jxl_array_destroy(&box_header);
      return JXL_API_ERROR_AS_STATUS(
          self, JXL_ENCODER_ERR_GENERIC,
          "Box size was estimated to be small, but turned out to be large. "
          "Please file this error in size estimation as a bug.");
    }
    if (last_frame && self->jxlp_counter == 0) {
      jxl_enc_box_type jxlc = jxl_make_box_type("jxlc");
      size_t n = jxl_write_box_header(
          &jxlc, frame_content_size,
          /*unbounded=*/false, use_large_box, jxl_array_data(&box_header));
      JXL_ENSURE(n == box_header_size);
    } else {
      jxl_enc_box_type jxlp = jxl_make_box_type("jxlp");
      size_t n = jxl_write_box_header(
          &jxlp, frame_content_size + 4,
          /*unbounded=*/false, use_large_box, jxl_array_data(&box_header));
      JXL_ENSURE(n == box_header_size - 4);
      jxl_write_jxlp_box_counter_bytes(self->jxlp_counter++, last_frame,
                               jxl_array_at(&box_header, box_header_size - 4));
    }
    box_status = jxl_append_data(&self->output_processor, jxl_array_data(&box_header),
                            jxl_array_len(&box_header));
    jxl_array_destroy(&box_header);
    if (!jxl_status_ok(box_status)) return box_status;
    JXL_ENSURE(jxl_encoder_output_processor_wrapper_current_position(&self->output_processor) ==
               frame_start_pos + box_header_size);
    JXL_RETURN_IF_ERROR(jxl_encoder_output_processor_wrapper_seek(&self->output_processor, content_start + content_size));
  }
  JXL_RETURN_IF_ERROR(jxl_encoder_output_processor_wrapper_set_finalized_position(&self->output_processor));

  return jxl_ok_status();
}

jxl_status jxl_encoder_process_one_enqueued_input_body(
    jxl_encoder* self, jxl_owned_queued_frame* input_frame) {
  jxl_padded_bytes header_bytes;
  jxl_padded_bytes_make(&self->memory_manager, &header_bytes);
  jxl_status status = jxl_encoder_process_one_enqueued_input_body_with_header(
      self, input_frame, &header_bytes);
  jxl_padded_bytes_destroy(&header_bytes);
  return status;
}

jxl_status jxl_encoder_process_one_enqueued_input(jxl_encoder* self) {
  JXL_ENSURE(!jxl_owned_queued_frames_empty(&self->input_queue));
  jxl_owned_queued_frame input_frame;
  jxl_owned_queued_frame_construct_empty(&input_frame);
  jxl_owned_queued_frame_swap(&input_frame, jxl_owned_queued_frames_front(&self->input_queue));
  jxl_owned_queued_frames_erase(&self->input_queue, (size_t)(0));
  self->num_queued_frames--;

  jxl_status status = jxl_encoder_process_one_enqueued_input_body(self, &input_frame);
  jxl_owned_queued_frame_destroy(&input_frame);
  return status;
}

jxl_encoder_frame_settings* jxl_encoder_frame_settings_create(
    jxl_encoder* enc, const jxl_encoder_frame_settings* source) {
  jxl_owned_frame_settings opts;
  jxl_make_owned_frame_settings(&enc->memory_manager, &opts);
  if (!jxl_owned_frame_settings_ok(&opts)) {
    jxl_owned_frame_settings_destroy(&opts);
    return NULL;
  }
  jxl_owned_frame_settings_get(&opts)->enc = enc;
  if (source != NULL) {
    jxl_owned_frame_settings_get(&opts)->cparams = source->cparams;
  }

  jxl_encoder_frame_settings* ret = jxl_owned_frame_settings_get(&opts);
  if (!jxl_status_ok(jxl_owned_frame_settings_list_emplace_back(&enc->encoder_options, &opts))) {
    JXL_CRASH();
  }
  jxl_owned_frame_settings_destroy(&opts);
  return ret;
}

jxl_encoder_status jxl_encoder_frame_settings_set_option(
    jxl_encoder_frame_settings* frame_settings, jxl_encoder_frame_setting_id option,
    int64_t value) {
  switch (option) {
    case JXL_ENCODER_FRAME_SETTING_JPEG_RECON_CFL:
    case JXL_ENCODER_FRAME_SETTING_JPEG_COMPRESS_BOXES:
    case JXL_ENCODER_FRAME_SETTING_JPEG_KEEP_EXIF:
    case JXL_ENCODER_FRAME_SETTING_JPEG_KEEP_XMP:
      if (value < -1 || value > 1) {
        return JXL_API_ERROR(
            frame_settings->enc, JXL_ENCODER_ERR_API_USAGE,
            "Option value has to be -1 (default), 0 (off) or 1 (on)");
      }
      break;
    default:
      break;
  }

  switch (option) {
    case JXL_ENCODER_FRAME_SETTING_EFFORT:
      if (value < 1 || value > 10) {
        return JXL_API_ERROR(frame_settings->enc, JXL_ENCODER_ERR_NOT_SUPPORTED,
                             "Encode effort has to be in [1..10]");
      }
      frame_settings->cparams.speed_tier =
          (jxl_speed_tier)(10 - value);
      break;
    case JXL_ENCODER_FRAME_SETTING_BROTLI_EFFORT:
      if (value < -1 || value > 11) {
        return JXL_API_ERROR(frame_settings->enc, JXL_ENCODER_ERR_API_USAGE,
                             "Brotli effort has to be in [-1..11]");
      }
      frame_settings->cparams.brotli_effort = value;
      break;
    case JXL_ENCODER_FRAME_SETTING_JPEG_RECON_CFL:
      frame_settings->cparams.force_cfl_jpeg_recompression =
          (value != 0);
      break;
    case JXL_ENCODER_FRAME_SETTING_JPEG_COMPRESS_BOXES:
      frame_settings->cparams.jpeg_compress_boxes =
          (value != 0);
      break;
    case JXL_ENCODER_FRAME_SETTING_JPEG_KEEP_EXIF:
      frame_settings->cparams.jpeg_keep_exif = (value != 0);
      break;
    case JXL_ENCODER_FRAME_SETTING_JPEG_KEEP_XMP:
      frame_settings->cparams.jpeg_keep_xmp = (value != 0);
      break;
    default:
      return JXL_API_ERROR(frame_settings->enc, JXL_ENCODER_ERR_NOT_SUPPORTED,
                           "Unknown option");
  }
  return JXL_ENCODER_SUCCESS;
}

jxl_encoder* jxl_encoder_create(jxl_context* ctx) {
  jxl_memory_manager* mm;
  void* alloc;
  jxl_encoder* enc;
  if (ctx == NULL) {
    return NULL;
  }
  mm = jxl_context_memory_manager(ctx);
  if (mm == NULL) {
    return NULL;
  }
  if (jxl_context_lcms(ctx) == NULL) {
    return NULL;
  }

  alloc = jxl_memory_manager_alloc(mm, sizeof(jxl_encoder));
  if (!alloc) return NULL;
  enc = (jxl_encoder*)(alloc);
  /* Install encoder-owned MM copy first so nested arrays/lists point at it. */
  enc->memory_manager = *mm;
  jxl_encoder_construct_empty(enc, &enc->memory_manager);
  enc->ctx = ctx;
  enc->cms = *jxl_get_default_cms();
  enc->cms.set_fields_data = jxl_context_lcms(ctx);
  jxl_owned_queued_frames_clear(&enc->input_queue);
  enc->num_queued_frames = 0;
  jxl_owned_frame_settings_list_clear(&enc->encoder_options);
  enc->wrote_bytes = false;
  enc->jxlp_counter = 0;
  jxl_codec_metadata_init(&enc->metadata);
  enc->input_closed = false;
  enc->jpeg_metadata_set = false;
  enc->use_container = false;
  enc->has_jpeg_metadata_boxes = false;
  enc->container_ftyp_version = -1;
  enc->store_jpeg_metadata = false;
  enc->codestream_level = -1;
  jxl_encoder_output_processor_wrapper_init(&enc->output_processor,
                                       &enc->memory_manager);
  return enc;
}

void jxl_encoder_destroy(jxl_encoder* enc) {
  if (enc) {
    jxl_memory_manager local_memory_manager = enc->memory_manager;
    // Destroy owning members directly since custom free function is used.
    jxl_encoder_destroy_contents(enc);
    jxl_memory_manager_free(&local_memory_manager, enc);
  }
}

jxl_encoder_error jxl_encoder_get_error(jxl_encoder* enc) { return enc->error; }

jxl_encoder_status jxl_encoder_use_container(jxl_encoder* enc,
                                        JXL_BOOL use_container) {
  if (enc->wrote_bytes) {
    return JXL_API_ERROR(enc, JXL_ENCODER_ERR_API_USAGE,
                         "this setting can only be set at the beginning");
  }
  enc->use_container = FROM_JXL_BOOL(use_container);
  return JXL_ENCODER_SUCCESS;
}

jxl_encoder_status jxl_encoder_store_jpeg_metadata(jxl_encoder* enc,
                                             JXL_BOOL store_jpeg_metadata) {
  if (enc->wrote_bytes) {
    return JXL_API_ERROR(enc, JXL_ENCODER_ERR_API_USAGE,
                         "this setting can only be set at the beginning");
  }
  enc->store_jpeg_metadata = FROM_JXL_BOOL(store_jpeg_metadata);
  return JXL_ENCODER_SUCCESS;
}

jxl_encoder_status jxl_encoder_add_jpeg_frame(
    const jxl_encoder_frame_settings* frame_settings, const uint8_t* buffer,
    size_t size) {
  jxl_memory_manager* memory_manager = &frame_settings->enc->memory_manager;
  if (frame_settings->enc->input_closed) {
    return JXL_API_ERROR(frame_settings->enc, JXL_ENCODER_ERR_API_USAGE,
                         "Frame input is already closed");
  }

  jxl_jpeg_data jpeg_data;
  jxl_jpeg_data_init(&jpeg_data, memory_manager);
  {
    jxl_bytes jpg_bytes = jxl_bytes_make(buffer, size);
    jxl_status status = jxl_parse_jpg(memory_manager, &jpg_bytes, &jpeg_data);
    if (!jxl_status_ok(status)) {
      if (jxl_status_get_code(status) == kUnsupported) {
        return JXL_API_ERROR(
            frame_settings->enc, JXL_ENCODER_ERR_NOT_SUPPORTED,
            "Unsupported JPEG feature (CMYK, arithmetic coding, etc.)");
      } else {
        return JXL_API_ERROR(frame_settings->enc, JXL_ENCODER_ERR_BAD_INPUT,
                             "Error during decode of input JPEG");
      }
    }
  }

  if (jxl_setup_metadata_from_jpeg(frame_settings->enc, &jpeg_data) !=
      JXL_ENCODER_SUCCESS) {
    return JXL_API_ERROR(frame_settings->enc, JXL_ENCODER_ERR_GENERIC,
                         "Error setting JPEG metadata");
  }

  size_t xsize = jxl_codec_metadata_x_size(&frame_settings->enc->metadata);
  size_t ysize = jxl_codec_metadata_y_size(&frame_settings->enc->metadata);
  if (xsize == 0 || ysize == 0) {
    return JXL_API_ERROR(frame_settings->enc, JXL_ENCODER_ERR_API_USAGE,
                         "zero-sized frame is not allowed");
  }
  jxl_jpeg_blobs blobs;
  jxl_jpeg_blobs_construct_empty(&blobs, memory_manager);
  if (!jxl_status_ok(jxl_set_blobs_from_jpeg_data(&jpeg_data, &blobs))) {
    jxl_jpeg_blobs_destroy(&blobs);
    return JXL_API_ERROR(frame_settings->enc, JXL_ENCODER_ERR_BAD_INPUT,
                         "Error during parsing of input JPEG blobs");
  }

  if (!jxl_array_empty(&blobs.exif)) {
    jxl_orientation orientation = (jxl_orientation)(
        frame_settings->enc->metadata.m.orientation);
    jxl_bytes exif_bytes =
        jxl_bytes_make(jxl_array_data(&blobs.exif), jxl_array_len(&blobs.exif));
    jxl_interpret_exif(&exif_bytes, &orientation);
    frame_settings->enc->metadata.m.orientation = orientation;
  }
  jxl_encoder_metadata_boxes metadata_boxes;
  jxl_encoder_metadata_boxes_construct_empty(&metadata_boxes, memory_manager);
  if (!jxl_array_empty(&blobs.exif) && frame_settings->cparams.jpeg_keep_exif) {
    size_t exif_size = jxl_array_len(&blobs.exif);
    // Exif data in JPEG is limited to 64k
    if (exif_size > 0xFFFF) {
      jxl_jpeg_blobs_destroy(&blobs);
      jxl_encoder_metadata_boxes_destroy(&metadata_boxes);
      return JXL_API_ERROR(frame_settings->enc, JXL_ENCODER_ERR_GENERIC,
                           "Exif larger than possible in JPEG?");
    }
    exif_size += 4;  // prefix 4 zero bytes for tiff offset
    jxl_array_u8 exif;
    jxl_array_construct_empty(&exif, memory_manager);
    if (!jxl_status_ok(jxl_array_resize_zero(&exif, exif_size))) {
      jxl_array_destroy(&exif);
      jxl_jpeg_blobs_destroy(&blobs);
      jxl_encoder_metadata_boxes_destroy(&metadata_boxes);
      return JXL_API_ERROR(frame_settings->enc, JXL_ENCODER_ERR_GENERIC,
                           "Failed to allocate Exif box");
    }
    memcpy(jxl_array_data(&exif) + 4, jxl_array_data(&blobs.exif), jxl_array_len(&blobs.exif));
    if (!jxl_queue_jpeg_metadata_box(
            frame_settings->enc, "Exif", &exif,
            frame_settings->cparams.jpeg_compress_boxes, &metadata_boxes)) {
      jxl_array_destroy(&exif);
      jxl_jpeg_blobs_destroy(&blobs);
      jxl_encoder_metadata_boxes_destroy(&metadata_boxes);
      return JXL_API_ERROR(frame_settings->enc, JXL_ENCODER_ERR_OOM,
                           "Failed to queue Exif box");
    }
    jxl_array_destroy(&exif);
  }
  if (!jxl_array_empty(&blobs.xmp) && frame_settings->cparams.jpeg_keep_xmp) {
    if (!jxl_queue_jpeg_metadata_box(
            frame_settings->enc, "xml ", &blobs.xmp,
            frame_settings->cparams.jpeg_compress_boxes, &metadata_boxes)) {
      jxl_jpeg_blobs_destroy(&blobs);
      jxl_encoder_metadata_boxes_destroy(&metadata_boxes);
      return JXL_API_ERROR(frame_settings->enc, JXL_ENCODER_ERR_OOM,
                           "Failed to queue XMP box");
    }
  }
  jxl_jpeg_blobs_destroy(&blobs);
  if (frame_settings->enc->store_jpeg_metadata) {
    if (!frame_settings->cparams.jpeg_keep_exif ||
        !frame_settings->cparams.jpeg_keep_xmp) {
      jxl_encoder_metadata_boxes_destroy(&metadata_boxes);
      return JXL_API_ERROR(frame_settings->enc, JXL_ENCODER_ERR_API_USAGE,
                           "Need to preserve EXIF and XMP to allow JPEG "
                           "bitstream reconstruction");
    }
    jxl_array_u8 jpeg_metadata;
    jxl_array_construct_empty(&jpeg_metadata, memory_manager);
    if (!jxl_status_ok(jxl_encode_jpeg_data(&frame_settings->enc->memory_manager,
                                   &jpeg_data, &jpeg_metadata,
                                   &frame_settings->cparams))) {
      jxl_array_destroy(&jpeg_metadata);
      jxl_encoder_metadata_boxes_destroy(&metadata_boxes);
      return JXL_API_ERROR(
          frame_settings->enc, JXL_ENCODER_ERR_JBRD,
          "JPEG bitstream reconstruction data cannot be encoded");
    }
    jxl_array_swap(&frame_settings->enc->jpeg_metadata, &jpeg_metadata);
    jxl_array_destroy(&jpeg_metadata);
  }

  jxl_compress_params cparams = frame_settings->cparams;
  cparams.color_transform = kColorTransformNone;

  jxl_encoder_queued_frame frame;
  jxl_encoder_queued_frame_construct_empty(&frame, memory_manager);
  jxl_encoder_queued_frame_init(&frame, &cparams, xsize, ysize, memory_manager);
  jxl_encoder_jpeg_frame_adapter_set_jpeg_data(&frame.frame_data, &jpeg_data);
  jxl_encoder_metadata_boxes_swap(&frame.metadata_boxes, &metadata_boxes);
  jxl_encoder_metadata_boxes_destroy(&metadata_boxes);

  jxl_owned_queued_frame queued_frame;
  jxl_make_owned_queued_frame(&frame_settings->enc->memory_manager, &frame,
                            &queued_frame);
  if (!jxl_owned_queued_frame_ok(&queued_frame)) {
    jxl_owned_queued_frame_destroy(&queued_frame);
    return JXL_API_ERROR(frame_settings->enc, JXL_ENCODER_ERR_OOM,
                         "can not allocate queued frame");
  }
  jxl_queue_frame(frame_settings, &queued_frame);
  jxl_owned_queued_frame_destroy(&queued_frame);
  return JXL_ENCODER_SUCCESS;
}

void jxl_encoder_close_input(jxl_encoder* enc) { enc->input_closed = true; }

jxl_encoder_status jxl_encoder_process_output(jxl_encoder* enc, uint8_t** next_out,
                                         size_t* avail_out) {
  if (!jxl_status_ok(jxl_encoder_output_processor_wrapper_set_avail_out(&enc->output_processor, next_out, avail_out))) {
    return JXL_ENCODER_ERROR;
  }
  while (*avail_out != 0 && !jxl_owned_queued_frames_empty(&enc->input_queue)) {
    if (!jxl_status_ok(jxl_encoder_process_one_enqueued_input(enc))) {
      return JXL_ENCODER_ERROR;
    }
  }

  if (!jxl_owned_queued_frames_empty(&enc->input_queue) || jxl_encoder_output_processor_wrapper_has_output_to_write(&enc->output_processor)) {
    return JXL_ENCODER_NEED_MORE_OUTPUT;
  }
  return JXL_ENCODER_SUCCESS;
}
