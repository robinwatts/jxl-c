// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in LICENSE-BSD.

#include "jpeg/enc_jpeg_data.h"

#include "context_internal.h"

#include <brotli/encode.h>
#include <jxl/cms.h>
#include <jxl/context.h>
#include "enc_allocator.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "base/array.h"
#include "base/sanitizers.h"
#include "base/span.h"
#include "base/enc_status.h"
#include "brotli_alloc.h"
#include "color_encoding_internal.h"
#include "layer_type.h"
#include "enc_bit_writer.h"
#include "enc_params.h"
#include "fields.h"
#include "enc_frame_header.h"
#include "jpeg/enc_jpeg_data_reader.h"
#include "jpeg/jpeg_data.h"
#include "padded_bytes.h"


// See if there is a canonically chunked ICC profile and mark corresponding
// app-tags with kAppMarkerICC.
static jxl_enc_status jxl_detect_icc_profile(jxl_jpeg_data* jpeg_data) {
  JXL_ENSURE(jxl_byte_chunks_size(&jpeg_data->app_data) == jxl_array_len(&jpeg_data->app_marker_type));
  size_t num_icc = 0;
  size_t num_icc_jpeg = 0;
  for (size_t i = 0; i < jxl_byte_chunks_size(&jpeg_data->app_data); i++) {
    jxl_bytes app = jxl_byte_chunks_at(&jpeg_data->app_data, i);
    // At least APPn + size; otherwise it should be intermarker-data.
    JXL_ENSURE(jxl_bytes_size(&app) >= 3);
    size_t pos = 0;
    if (jxl_bytes_at(&app, pos++) != 0xE2) continue;
    size_t tag_length = (jxl_bytes_at(&app, pos) << 8) + jxl_bytes_at(&app, pos + 1);
    pos += 2;
    JXL_ENSURE(jxl_bytes_size(&app) == tag_length + 1);
    // Minimum is 2 bytes for tag length itself + signature + 2 bytes for
    // chunk_id and num_chunks (read below).
    if (tag_length < 2 + sizeof kIccProfileTag + 2) continue;

    if (memcmp(jxl_bytes_data(&app) + pos, kIccProfileTag, sizeof kIccProfileTag) != 0) continue;
    pos += sizeof kIccProfileTag;
    uint8_t chunk_id = jxl_bytes_at(&app, pos++);
    uint8_t num_chunks = jxl_bytes_at(&app, pos++);
    if (chunk_id != num_icc + 1) continue;
    if (num_icc_jpeg == 0) num_icc_jpeg = num_chunks;
    if (num_icc_jpeg != num_chunks) continue;
    num_icc++;
    *jxl_array_at(&jpeg_data->app_marker_type, i) = kAppMarkerICC;
  }
  if (num_icc != num_icc_jpeg) {
    return JXL_FAILURE("Invalid ICC chunks");
  }
  return jxl_enc_ok_status();
}

static bool jxl_get_marker_payload(const uint8_t* data, size_t size, jxl_bytes* payload) {
  if (size < 3) {
    return false;
  }
  size_t hi = data[1];
  size_t lo = data[2];
  size_t internal_size = (hi << 8u) | lo;
  // Second byte of marker is not counted towards size.
  if (internal_size != size - 1) {
    return false;
  }
  // cut second marker byte and "length" from payload.
  *payload = jxl_bytes_make(data, size);
  if (!jxl_enc_status_ok(jxl_bytes_remove_prefix(payload, 3))) return false;
  return true;
}

static jxl_enc_status jxl_detect_blobs(jxl_jpeg_data* jpeg_data) {
  JXL_ENSURE(jxl_byte_chunks_size(&jpeg_data->app_data) == jxl_array_len(&jpeg_data->app_marker_type));
  bool have_exif = false;
  bool have_xmp = false;
  for (size_t i = 0; i < jxl_byte_chunks_size(&jpeg_data->app_data); i++) {
    jxl_bytes marker = jxl_byte_chunks_at(&jpeg_data->app_data, i);
    if (jxl_bytes_is_empty(&marker) || jxl_bytes_at(&marker, 0) != kApp1) {
      continue;
    }
    jxl_bytes payload;
    if (!jxl_get_marker_payload(jxl_bytes_data(&marker), jxl_bytes_size(&marker), &payload)) {
      // Something is wrong with this marker; does not care.
      continue;
    }
    if (!have_exif && jxl_bytes_size(&payload) > sizeof kExifTag &&
        !memcmp(jxl_bytes_data(&payload), kExifTag, sizeof kExifTag)) {
      *jxl_array_at(&jpeg_data->app_marker_type, i) = kAppMarkerExif;
      have_exif = true;
    }
    if (!have_xmp && jxl_bytes_size(&payload) >= sizeof kXMPTag &&
        !memcmp(jxl_bytes_data(&payload), kXMPTag, sizeof kXMPTag)) {
      *jxl_array_at(&jpeg_data->app_marker_type, i) = kAppMarkerXMP;
      have_xmp = true;
    }
  }
  return jxl_enc_ok_status();
}

static jxl_enc_status jxl_parse_chunked_marker_body(const jxl_jpeg_data* src, uint8_t marker_type,
                              const jxl_bytes* tag, jxl_icc_bytes* output,
                              jxl_array_bytes* chunks, jxl_array_u8* presence) {
  size_t expected_number_of_parts = 0;
  bool is_first_chunk = true;
  size_t ordinal = 0;
  for (size_t mi = 0; mi < jxl_byte_chunks_size(&src->app_data); ++mi) {
    jxl_bytes marker = jxl_byte_chunks_at(&src->app_data, mi);
    if (jxl_bytes_is_empty(&marker) || jxl_bytes_at(&marker, 0) != marker_type) {
      continue;
    }
    jxl_bytes payload;
    if (!jxl_get_marker_payload(jxl_bytes_data(&marker), jxl_bytes_size(&marker), &payload)) {
      // Something is wrong with this marker; does not care.
      continue;
    }
    if ((jxl_bytes_size(&payload) < jxl_bytes_size(tag)) ||
        memcmp(jxl_bytes_data(&payload), jxl_bytes_data(tag), jxl_bytes_size(tag)) != 0) {
      continue;
    }
    JXL_RETURN_IF_ERROR(jxl_bytes_remove_prefix(&payload, jxl_bytes_size(tag)));
    if (jxl_bytes_size(&payload) < 2) {
      return JXL_FAILURE("Chunk is too small.");
    }
    uint8_t index = jxl_bytes_at(&payload, 0);
    uint8_t total = jxl_bytes_at(&payload, 1);
    ordinal++;
    if (index != ordinal) return JXL_FAILURE("Invalid chunk order.");

    JXL_RETURN_IF_ERROR(jxl_bytes_remove_prefix(&payload, 2));

    JXL_RETURN_IF_ERROR(jxl_enc_status_from_bool(total != 0));
    if (is_first_chunk) {
      is_first_chunk = false;
      expected_number_of_parts = total;
      // 1-based indices; 0-th element is added for convenience.
      // jxl_bytes is TC but not memset-safe (user ctors); fill with empty spans.
      JXL_RETURN_IF_ERROR(jxl_array_bytes_resize_fill(chunks, total + 1, jxl_bytes_empty()));
      JXL_RETURN_IF_ERROR(jxl_array_resize_zero(presence, total + 1));
    } else {
      JXL_RETURN_IF_ERROR(jxl_enc_status_from_bool(expected_number_of_parts == total));
    }

    if (index == 0 || index > total) {
      return JXL_FAILURE("Invalid chunk index.");
    }

    if (*jxl_array_at(presence, index)) {
      return JXL_FAILURE("Duplicate chunk.");
    }
    *jxl_array_at(presence, index) = true;
    *jxl_array_at(chunks, index) = payload;
  }

  for (size_t i = 0; i < expected_number_of_parts; ++i) {
    // 0-th element is not used.
    size_t index = i + 1;
    if (!*jxl_array_at(presence, index)) {
      return JXL_FAILURE("Missing chunk.");
    }
    if (!jxl_enc_status_ok(jxl_array_append(output, jxl_bytes_data(jxl_array_at(chunks, index)),
                              jxl_bytes_size(jxl_array_at(chunks, index))))) {
      return JXL_FAILURE("Failed to append ICC chunk");
    }
  }

  return jxl_enc_ok_status();
}

static jxl_enc_status jxl_parse_chunked_marker(const jxl_jpeg_data* src, uint8_t marker_type,
                          const jxl_bytes* tag, jxl_icc_bytes* output) {
  jxl_array_clear(output);

  jxl_context* mm = output->ctx;
  jxl_array_bytes chunks;
  jxl_array_construct_empty(&chunks, mm);
  jxl_array_u8 presence;
  jxl_array_construct_empty(&presence, mm);
  jxl_enc_status status =
      jxl_parse_chunked_marker_body(src, marker_type, tag, output, &chunks, &presence);
  jxl_array_destroy(&chunks);
  jxl_array_destroy(&presence);
  return status;
}

static inline bool jxl_is_jpg(const jxl_bytes* bytes) {
  return jxl_bytes_size(bytes) >= 2 && jxl_bytes_at(bytes, 0) == 0xFF && jxl_bytes_at(bytes, 1) == 0xD8;
}

jxl_enc_status jxl_set_color_encoding_from_jpeg_data(jxl_context* ctx,
                                    const jxl_cms_interface* cms,
                                    const jxl_jpeg_data* jpg,
                                    jxl_enc_color_encoding* color_encoding) {
  jxl_icc_bytes icc_profile;
  jxl_array_construct_empty(&icc_profile,
                            color_encoding->storage_.icc.ctx);
  jxl_bytes icc_tag = jxl_bytes_make(kIccProfileTag, sizeof(kIccProfileTag));
  if (!jxl_enc_status_ok(jxl_parse_chunked_marker(jpg, kApp2, &icc_tag, &icc_profile))) {
    JXL_WARNING("ReJPEG: corrupted ICC profile\n");
    jxl_array_clear(&icc_profile);
  }

  jxl_enc_status status = jxl_enc_ok_status();
  if (jxl_array_empty(&icc_profile)) {
    bool is_gray = (jxl_array_len(&jpg->components) == 1);
    const jxl_enc_color_encoding* srgb = jxl_context_srgb(ctx, is_gray);
    if (srgb == NULL) {
      jxl_array_destroy(&icc_profile);
      return JXL_FAILURE("Missing library context for sRGB encoding");
    }
    JXL_RETURN_IF_ERROR(jxl_enc_color_encoding_copy_from(color_encoding, srgb));
  } else {
    status = jxl_enc_color_encoding_set_icc(color_encoding, &icc_profile, cms);
  }
  jxl_array_destroy(&icc_profile);
  return status;
}

jxl_enc_status jxl_set_chroma_subsampling_from_jpeg_data(const jxl_jpeg_data* jpg,
                                        jxl_y_cb_cr_chroma_subsampling* cs) {
  size_t nbcomp = jxl_array_len(&jpg->components);
  if (nbcomp != 1 && nbcomp != 3) {
    return JXL_FAILURE("Cannot recompress JPEGs with neither 1 nor 3 channels");
  }
  if (nbcomp == 3) {
    uint8_t hsample[3];
    uint8_t vsample[3];
    for (size_t i = 0; i < nbcomp; i++) {
      hsample[i] = jxl_array_at_const(&jpg->components, i)->h_samp_factor;
      vsample[i] = jxl_array_at_const(&jpg->components, i)->v_samp_factor;
    }
    JXL_RETURN_IF_ERROR(jxl_y_cb_cr_chroma_subsampling_set(cs, hsample, vsample));
  } else if (nbcomp == 1) {
    uint8_t hsample[3];
    uint8_t vsample[3];
    for (size_t i = 0; i < 3; i++) {
      hsample[i] = jxl_array_at_const(&jpg->components, 0)->h_samp_factor;
      vsample[i] = jxl_array_at_const(&jpg->components, 0)->v_samp_factor;
    }
    JXL_RETURN_IF_ERROR(jxl_y_cb_cr_chroma_subsampling_set(cs, hsample, vsample));
  }
  return jxl_enc_ok_status();
}

jxl_enc_status jxl_set_color_transform_from_jpeg_data(const jxl_jpeg_data* jpg,
                                     jxl_color_transform* color_transform) {
  size_t nbcomp = jxl_array_len(&jpg->components);
  if (nbcomp != 1 && nbcomp != 3) {
    return JXL_UNSUPPORTED(
        "Cannot recompress JPEGs with neither 1 nor 3 channels");
  }
  bool is_rgb = false;
  {
    const jxl_array_u8* markers = &jpg->marker_order;
    // If there is a JFIF marker, this is YCbCr. Otherwise...
    if (jxl_u8_find_const(jxl_array_data_const(markers), jxl_array_data_const(markers) + jxl_array_len(markers),
               (uint8_t)(0xE0)) ==
        jxl_array_data_const(markers) + jxl_array_len(markers)) {
      // Try to find an 'Adobe' marker.
      size_t app_markers = 0;
      size_t i = 0;
      for (; i < jxl_array_len(markers); i++) {
        // This is an APP marker.
        if ((*jxl_array_at_const(markers, i) & 0xF0) == 0xE0) {
          JXL_ENSURE(app_markers < jxl_byte_chunks_size(&jpg->app_data));
          // APP14 marker
          if (*jxl_array_at_const(markers, i) == 0xEE) {
            jxl_bytes data = jxl_byte_chunks_at(&jpg->app_data, app_markers);
            if (jxl_bytes_size(&data) == 15 && jxl_bytes_at(&data, 3) == 'A' && jxl_bytes_at(&data, 4) == 'd' &&
                jxl_bytes_at(&data, 5) == 'o' && jxl_bytes_at(&data, 6) == 'b' && jxl_bytes_at(&data, 7) == 'e') {
              // 'Adobe' marker.
              is_rgb = jxl_bytes_at(&data, 14) == 0;
              break;
            }
          }
          app_markers++;
        }
      }

      if (i == jxl_array_len(markers)) {
        // No 'Adobe' marker, guess from component IDs.
        is_rgb = nbcomp == 3 && jxl_array_at_const(&jpg->components, 0)->id == 'R' &&
                 jxl_array_at_const(&jpg->components, 1)->id == 'G' && jxl_array_at_const(&jpg->components, 2)->id == 'B';
      }
    }
  }
  *color_transform =
      (!is_rgb || nbcomp == 1) ? kColorTransformYCbCr : kColorTransformNone;
  return jxl_enc_ok_status();
}

jxl_enc_status jxl_append_brotli_chunk(BrotliEncoderState* brotli_enc, const jxl_bytes* data, bool last,
                         jxl_array_u8* bytes, size_t initial_size,
                         size_t* enc_size, size_t* brotli_capacity) {
  size_t available_in = jxl_bytes_size(data);
  const uint8_t* in = jxl_bytes_data(data);
  uint8_t* out = jxl_array_at(bytes, initial_size + *enc_size);
  do {
    uint8_t* out_before = out;
    jxl_msan_memory_is_initialized(in, available_in);
    JXL_ENSURE(BrotliEncoderCompressStream(
        brotli_enc, last ? BROTLI_OPERATION_FINISH : BROTLI_OPERATION_PROCESS,
        &available_in, &in, brotli_capacity, &out, enc_size));
    jxl_msan_unpoison_memory(out_before, out - out_before);
  } while (FROM_JXL_BOOL(BrotliEncoderHasMoreOutput(brotli_enc)) ||
           available_in > 0);
  return jxl_enc_ok_status();
}

jxl_enc_status jxl_encode_jpeg_data(jxl_context* ctx, jxl_jpeg_data* jpeg_data,
                      jxl_array_u8* bytes, const jxl_compress_params* cparams){
  jxl_array_clear(bytes);
  jxl_array_clear(&jpeg_data->app_marker_type);
  // kAppMarkerUnknown == 0.
  JXL_RETURN_IF_ERROR(
      jxl_array_resize_zero(&jpeg_data->app_marker_type, jxl_byte_chunks_size(&jpeg_data->app_data)));
  JXL_RETURN_IF_ERROR(jxl_detect_icc_profile(jpeg_data));
  JXL_RETURN_IF_ERROR(jxl_detect_blobs(jpeg_data));

  size_t total_data = 0;
  for (size_t i = 0; i < jxl_byte_chunks_size(&jpeg_data->app_data); i++) {
    if (*jxl_array_at(&jpeg_data->app_marker_type, i) != kAppMarkerUnknown) {
      continue;
    }
    jxl_bytes app_chunk = jxl_byte_chunks_at(&jpeg_data->app_data, i);
    total_data += jxl_bytes_size(&app_chunk);
  }
  for (size_t i = 0; i < jxl_byte_chunks_size(&jpeg_data->com_data); i++) {
    jxl_bytes com_chunk = jxl_byte_chunks_at(&jpeg_data->com_data, i);
    total_data += jxl_bytes_size(&com_chunk);
  }
  for (size_t i = 0; i < jxl_byte_chunks_size(&jpeg_data->inter_marker_data); i++) {
    jxl_bytes inter_chunk = jxl_byte_chunks_at(&jpeg_data->inter_marker_data, i);
    total_data += jxl_bytes_size(&inter_chunk);
  }
  total_data += jxl_array_len(&jpeg_data->tail_data);
  size_t brotli_capacity = BrotliEncoderMaxCompressedSize(total_data);

  jxl_bit_writer writer;
  jxl_bit_writer_make(ctx, &writer);
  {
    jxl_enc_status write_status =
        jxl_bundle_write(&jpeg_data->fields, &writer, kLayerHeader);
    if (!jxl_enc_status_ok(write_status)) {
      jxl_bit_writer_destroy(&writer);
      return write_status;
    }
  }
  jxl_bit_writer_zero_pad_to_byte(&writer);
  {
    jxl_padded_bytes serialized_jpeg_data;
    jxl_bit_writer_take_bytes(&writer, &serialized_jpeg_data);
    size_t need = jxl_padded_bytes_size(&serialized_jpeg_data) + brotli_capacity;
    if (!jxl_enc_status_ok(jxl_array_reserve(bytes, need))) {
      jxl_padded_bytes_destroy(&serialized_jpeg_data);
      jxl_bit_writer_destroy(&writer);
      return JXL_FAILURE("Failed to reserve JPEG metadata buffer");
    }
    jxl_enc_status append_status =
        jxl_array_append(bytes, jxl_padded_bytes_data(&serialized_jpeg_data),
                    jxl_padded_bytes_size(&serialized_jpeg_data));
    jxl_padded_bytes_destroy(&serialized_jpeg_data);
    if (!jxl_enc_status_ok(append_status)) {
      jxl_bit_writer_destroy(&writer);
      return append_status;
    }
  }
  jxl_bit_writer_destroy(&writer);

  BrotliEncoderState* brotli_enc = jxl_brotli_encoder_create(ctx);
  if (brotli_enc == NULL) {
    return JXL_FAILURE("BrotliEncoderCreateInstance failed");
  }
  int effort = cparams->brotli_effort;
  if (effort < 0) effort = 11 - (int)(cparams->speed_tier);
  BrotliEncoderSetParameter(brotli_enc, BROTLI_PARAM_QUALITY, effort);
  size_t initial_size = jxl_array_len(bytes);
  BrotliEncoderSetParameter(brotli_enc, BROTLI_PARAM_SIZE_HINT, total_data);
  if (!jxl_enc_status_ok(jxl_array_resize_zero(bytes, initial_size + brotli_capacity))) {
    BrotliEncoderDestroyInstance(brotli_enc);
    return JXL_FAILURE("Failed to grow JPEG metadata buffer");
  }
  size_t enc_size = 0;

  for (size_t i = 0; i < jxl_byte_chunks_size(&jpeg_data->app_data); i++) {
    if (*jxl_array_at(&jpeg_data->app_marker_type, i) != kAppMarkerUnknown) {
      continue;
    }
    jxl_bytes app_chunk = jxl_byte_chunks_at(&jpeg_data->app_data, i);
    JXL_RETURN_IF_ERROR(jxl_append_brotli_chunk(brotli_enc, &app_chunk,
                                          /*last=*/false, bytes, initial_size,
                                          &enc_size, &brotli_capacity));
  }
  for (size_t i = 0; i < jxl_byte_chunks_size(&jpeg_data->com_data); i++) {
    jxl_bytes com_chunk = jxl_byte_chunks_at(&jpeg_data->com_data, i);
    JXL_RETURN_IF_ERROR(jxl_append_brotli_chunk(brotli_enc, &com_chunk,
                                          /*last=*/false, bytes, initial_size,
                                          &enc_size, &brotli_capacity));
  }
  for (size_t i = 0; i < jxl_byte_chunks_size(&jpeg_data->inter_marker_data); i++) {
    jxl_bytes inter_chunk = jxl_byte_chunks_at(&jpeg_data->inter_marker_data, i);
    JXL_RETURN_IF_ERROR(jxl_append_brotli_chunk(
        brotli_enc, &inter_chunk, /*last=*/false, bytes,
        initial_size, &enc_size, &brotli_capacity));
  }
  {
    jxl_bytes tail = jxl_bytes_make(jxl_array_data(&jpeg_data->tail_data),
                           jxl_array_len(&jpeg_data->tail_data));
    JXL_RETURN_IF_ERROR(jxl_append_brotli_chunk(
        brotli_enc, &tail,
        /*last=*/true, bytes, initial_size, &enc_size, &brotli_capacity));
  }
  BrotliEncoderDestroyInstance(brotli_enc);
  JXL_RETURN_IF_ERROR(jxl_array_resize_zero(bytes, initial_size + enc_size));
  return jxl_enc_ok_status();
}

jxl_enc_status jxl_parse_jpg(jxl_context* ctx, const jxl_bytes* bytes,
                jxl_jpeg_data* out) {
  if (ctx == NULL) return JXL_FAILURE("Missing memory manager");
  if (!jxl_is_jpg(bytes)) return JXL_FAILURE("Not JPEG");
  jxl_jpeg_data jpeg_data;
  jxl_jpeg_data_init(&jpeg_data, ctx);
  jxl_enc_status status = jxl_read_jpeg(jxl_bytes_data(bytes), jxl_bytes_size(bytes), &jpeg_data);
  if (!jxl_enc_status_ok(status)) {
    jxl_jpeg_data_destroy(&jpeg_data);
    return status;
  }
  jxl_jpeg_data_swap(out, &jpeg_data);
  jxl_jpeg_data_destroy(&jpeg_data);
  return jxl_enc_ok_status();
}

jxl_enc_status jxl_set_blobs_from_jpeg_data(const jxl_jpeg_data* jpeg_data, jxl_jpeg_blobs* blobs) {
  for (size_t i = 0; i < jxl_byte_chunks_size(&jpeg_data->app_data); ++i) {
    jxl_bytes marker = jxl_byte_chunks_at(&jpeg_data->app_data, i);
    if (jxl_bytes_is_empty(&marker) || jxl_bytes_at(&marker, 0) != kApp1) {
      continue;
    }
    jxl_bytes payload;
    if (!jxl_get_marker_payload(jxl_bytes_data(&marker), jxl_bytes_size(&marker), &payload)) {
      // Something is wrong with this marker; does not care.
      continue;
    }
    if (jxl_bytes_size(&payload) >= sizeof kExifTag &&
        !memcmp(jxl_bytes_data(&payload), kExifTag, sizeof kExifTag)) {
      if (jxl_array_empty(&blobs->exif)) {
        JXL_RETURN_IF_ERROR(jxl_array_assign(
            &blobs->exif, jxl_bytes_data(&payload) + sizeof kExifTag,
            jxl_bytes_size(&payload) - sizeof kExifTag));
      } else {
        JXL_WARNING(
            "ReJPEG: multiple Exif blobs, storing only first one in the JPEG "
            "XL container\n");
      }
    }
    if (jxl_bytes_size(&payload) >= sizeof kXMPTag &&
        !memcmp(jxl_bytes_data(&payload), kXMPTag, sizeof kXMPTag)) {
      if (jxl_array_empty(&blobs->xmp)) {
        JXL_RETURN_IF_ERROR(jxl_array_assign(
            &blobs->xmp, jxl_bytes_data(&payload) + sizeof kXMPTag,
            jxl_bytes_size(&payload) - sizeof kXMPTag));
      } else {
        JXL_WARNING(
            "ReJPEG: multiple XMP blobs, storing only first one in the JPEG "
            "XL container\n");
      }
    }
  }
  return jxl_enc_ok_status();
}
