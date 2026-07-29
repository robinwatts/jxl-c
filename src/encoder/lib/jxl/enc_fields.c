// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "lib/jxl/enc_fields.h"

#include <inttypes.h>  // PRIu64
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "lib/jxl/base/enc_status.h"
#include "lib/jxl/layer_type.h"
#include "lib/jxl/fields.h"
#include "lib/jxl/enc_frame_header.h"
#include "lib/jxl/headers.h"
#include "lib/jxl/enc_image_metadata.h"
#include "lib/jxl/quantizer.h"

typedef struct jxl_write_visitor {
  jxl_visitor visitor;
  size_t extension_bits;
  jxl_bit_writer* JXL_RESTRICT writer;
  bool ok;
} jxl_write_visitor;

static jxl_enc_status jxl_write_visitor_ok(const jxl_write_visitor* self) {
  return jxl_enc_status_from_bool(self->ok);
}

static jxl_enc_status jxl_write_bits(jxl_visitor* self, size_t bits, uint32_t /*default_value*/,
                 uint32_t* JXL_RESTRICT value) {
  jxl_write_visitor* v = (jxl_write_visitor*)(self);
  v->ok = v->ok && jxl_enc_status_ok(jxl_bits_coder_write(bits, *value, v->writer));
  return jxl_enc_ok_status();
}
static jxl_enc_status jxl_write_u32(jxl_visitor* self, jxl_u32_enc enc, uint32_t /*default_value*/,
                uint32_t* JXL_RESTRICT value) {
  jxl_write_visitor* v = (jxl_write_visitor*)(self);
  v->ok = v->ok && jxl_enc_status_ok(jxl_u32_coder_write(enc, *value, v->writer));
  return jxl_enc_ok_status();
}
static jxl_enc_status jxl_write_u64(jxl_visitor* self, uint64_t /*default_value*/,
                uint64_t* JXL_RESTRICT value) {
  jxl_write_visitor* v = (jxl_write_visitor*)(self);
  v->ok = v->ok && jxl_enc_status_ok(jxl_u64_coder_write(*value, v->writer));
  return jxl_enc_ok_status();
}
static jxl_enc_status jxl_write_f16(jxl_visitor* self, float /*default_value*/,
                float* JXL_RESTRICT value) {
  jxl_write_visitor* v = (jxl_write_visitor*)(self);
  v->ok = v->ok && jxl_enc_status_ok(jxl_f16_coder_write(*value, v->writer));
  return jxl_enc_ok_status();
}
static jxl_enc_status jxl_write_begin_extensions(jxl_visitor* self, uint64_t* JXL_RESTRICT extensions) {
  JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_default_begin_extensions(self, extensions));
  jxl_write_visitor* v = (jxl_write_visitor*)(self);
  if (*extensions == 0) {
    JXL_ENSURE(v->extension_bits == 0);
    return jxl_enc_ok_status();
  }
  // TODO(janwas): extend API to pass in array of extension_bits, one per
  // extension. We currently ascribe all bits to the first extension, but
  // this is only an encoder limitation. NOTE: extension_bits can be zero
  // if an extension does not require any additional fields.
  v->ok = v->ok && jxl_enc_status_ok(jxl_u64_coder_write(v->extension_bits, v->writer));
  for (uint64_t remaining_extensions = *extensions & (*extensions - 1);
       remaining_extensions != 0;
       remaining_extensions &= remaining_extensions - 1) {
    v->ok = v->ok && jxl_enc_status_ok(jxl_u64_coder_write(0, v->writer));
  }
  return jxl_enc_ok_status();
}

static const jxl_visitor_ops kWriteOps = {
    jxl_write_bits,
    jxl_write_u32,
    jxl_write_u64,
    jxl_write_f16,
    jxl_visitor_default_bool,
    jxl_visitor_default_conditional,
    jxl_visitor_default_all_default,
    jxl_visitor_default_set_default,
    jxl_visitor_default_visit_nested,
    jxl_visitor_default_is_reading,
    jxl_write_begin_extensions,
    jxl_visitor_default_end_extensions,
};

static void jxl_write_visitor_init(jxl_write_visitor* self, size_t extension_bits,
                      jxl_bit_writer* JXL_RESTRICT writer) {
  jxl_visitor_construct_empty(&self->visitor);
  self->visitor.ops = &kWriteOps;
  self->extension_bits = extension_bits;
  self->writer = writer;
  self->ok = true;
}

typedef struct jxl_bundle_write_ctx {
  const jxl_fields* fields;
  jxl_bit_writer* writer;
  size_t extension_bits;
} jxl_bundle_write_ctx;

static jxl_enc_status jxl_bundle_write_body(void* opaque) {
  jxl_bundle_write_ctx* c = (jxl_bundle_write_ctx*)(opaque);
  jxl_write_visitor visitor;
  jxl_write_visitor_init(&visitor, c->extension_bits, c->writer);
  JXL_RETURN_IF_ERROR(jxl_visitor_visit_const(&visitor.visitor, c->fields));
  return jxl_write_visitor_ok(&visitor);
}

static jxl_enc_status jxl_write_codestream_marker_body(void* opaque) {
  jxl_bit_writer* w = (jxl_bit_writer*)(opaque);
  jxl_bit_writer_write(w, 8, 0xFF);
  jxl_bit_writer_write(w, 8, kCodestreamMarker);
  return jxl_enc_ok_status();
}

jxl_enc_status jxl_bundle_write(const jxl_fields* fields, jxl_bit_writer* writer, jxl_layer_type layer) {
  size_t extension_bits;
  size_t total_bits;
  JXL_RETURN_IF_ERROR(jxl_bundle_can_encode(fields, &extension_bits, &total_bits));

  jxl_bundle_write_ctx ctx = {fields, writer, extension_bits};
  return jxl_bit_writer_with_max_bits(writer, total_bits, layer, jxl_bundle_write_body, &ctx);
}

// Returns false if the value is too large to encode.
jxl_enc_status jxl_bits_coder_write(const size_t bits, const uint32_t value,
                        jxl_bit_writer* JXL_RESTRICT writer) {
  if (value >= (1ULL << bits)) {
    return JXL_FAILURE("Value %d too large to encode in %" PRIu64 " bits",
                       value, (uint64_t)(bits));
  }
  jxl_bit_writer_write(writer, bits, value);
  return jxl_enc_ok_status();
}

// Returns false if the value is too large to encode.
jxl_enc_status jxl_u32_coder_write(const jxl_u32_enc enc, const uint32_t value,
                       jxl_bit_writer* JXL_RESTRICT writer) {
  uint32_t selector;
  size_t total_bits;
  JXL_RETURN_IF_ERROR(jxl_u32_coder_choose_selector(enc, value, &selector, &total_bits));

  jxl_bit_writer_write(writer, 2, selector);

  const jxl_u32_distr d = jxl_u32_enc_get_distr(enc, selector);
  if (!jxl_u32_distr_is_direct(d)) {  // Nothing more to write for direct encoding
    const uint32_t offset = jxl_u32_distr_offset(d);
    JXL_ENSURE(value >= offset);
    jxl_bit_writer_write(writer, total_bits - 2, value - offset);
  }

  return jxl_enc_ok_status();
}

// Returns false if the value is too large to encode.
jxl_enc_status jxl_u64_coder_write(uint64_t value, jxl_bit_writer* JXL_RESTRICT writer) {
  if (value == 0) {
    // Selector: use 0 bits, value 0
    jxl_bit_writer_write(writer, 2, 0);
  } else if (value <= 16) {
    // Selector: use 4 bits, value 1..16
    jxl_bit_writer_write(writer, 2, 1);
    jxl_bit_writer_write(writer, 4, value - 1);
  } else if (value <= 272) {
    // Selector: use 8 bits, value 17..272
    jxl_bit_writer_write(writer, 2, 2);
    jxl_bit_writer_write(writer, 8, value - 17);
  } else {
    // Selector: varint, first a 12-bit group, after that per 8-bit group.
    jxl_bit_writer_write(writer, 2, 3);
    jxl_bit_writer_write(writer, 12, value & 4095);
    value >>= 12;
    int shift = 12;
    while (value > 0 && shift < 60) {
      // Indicate varint not done
      jxl_bit_writer_write(writer, 1, 1);
      jxl_bit_writer_write(writer, 8, value & 255);
      value >>= 8;
      shift += 8;
    }
    if (value > 0) {
      // This only could happen if shift == N - 4.
      jxl_bit_writer_write(writer, 1, 1);
      jxl_bit_writer_write(writer, 4, value & 15);
      // Implicitly closed sequence, no extra stop bit is required.
    } else {
      // Indicate end of varint
      jxl_bit_writer_write(writer, 1, 0);
    }
  }

  return jxl_enc_ok_status();
}

jxl_enc_status jxl_f16_coder_write(float value, jxl_bit_writer* JXL_RESTRICT writer) {
  uint32_t bits32;
  memcpy(&bits32, &value, sizeof(bits32));
  const uint32_t sign = bits32 >> 31;
  const uint32_t biased_exp32 = (bits32 >> 23) & 0xFF;
  const uint32_t mantissa32 = bits32 & 0x7FFFFF;

  const int32_t exp = (int32_t)(biased_exp32) - 127;
  if (JXL_UNLIKELY(exp > 15)) {
    return JXL_FAILURE("Too big to encode, CanEncode should return false");
  }

  // Tiny or zero => zero.
  if (exp < -24) {
    jxl_bit_writer_write(writer, 16, 0);
    return jxl_enc_ok_status();
  }

  uint32_t biased_exp16;
  uint32_t mantissa16;

  // exp = [-24, -15] => subnormal
  if (JXL_UNLIKELY(exp < -14)) {
    biased_exp16 = 0;
    const uint32_t sub_exp = (uint32_t)(-14 - exp);
    JXL_ENSURE(1 <= sub_exp && sub_exp < 11);
    mantissa16 = (1 << (10 - sub_exp)) + (mantissa32 >> (13 + sub_exp));
  } else {
    // exp = [-14, 15]
    biased_exp16 = (uint32_t)(exp + 15);
    JXL_ENSURE(1 <= biased_exp16 && biased_exp16 < 31);
    mantissa16 = mantissa32 >> 13;
  }

  JXL_ENSURE(mantissa16 < 1024);
  const uint32_t bits16 = (sign << 15) | (biased_exp16 << 10) | mantissa16;
  JXL_ENSURE(bits16 < 0x10000);
  jxl_bit_writer_write(writer, 16, bits16);
  return jxl_enc_ok_status();
}

jxl_enc_status jxl_write_codestream_headers_impl(jxl_codec_metadata* metadata, jxl_bit_writer* writer) {
  // JPEG VisitFields invariants: nested preview/animation/EC/opsin bodies are
  // never entered; AllDefault/Bool/U32 gates above them still write bits.
  JXL_DASSERT(!metadata->m.xyb_encoded);
  JXL_DASSERT(!metadata->m.have_preview);
  JXL_DASSERT(!metadata->m.have_animation);
  JXL_DASSERT(!metadata->m.have_intrinsic_size);
  JXL_DASSERT(metadata->m.num_extra_channels == 0);
  JXL_DASSERT(jxl_extra_channel_infos_empty(&metadata->m.extra_channel_info));
  JXL_DASSERT(!metadata->m.bit_depth.floating_point_sample);
  JXL_DASSERT(metadata->m.bit_depth.bits_per_sample == 8);

  // Marker/signature
  JXL_RETURN_IF_ERROR(jxl_bit_writer_with_max_bits(writer, 
      16, kLayerHeader, jxl_write_codestream_marker_body, writer));

  JXL_RETURN_IF_ERROR(
      jxl_write_size_header(&metadata->size, writer, kLayerHeader));

  JXL_RETURN_IF_ERROR(
      jxl_write_image_metadata(&metadata->m, writer, kLayerHeader));

  metadata->transform_data.nonserialized_xyb_encoded = metadata->m.xyb_encoded;
  JXL_RETURN_IF_ERROR(jxl_bundle_write(&metadata->transform_data.fields, writer,
                                    kLayerHeader));

  return jxl_enc_ok_status();
}

jxl_enc_status jxl_write_frame_header_impl(const jxl_enc_frame_header* frame,
                        jxl_bit_writer* JXL_RESTRICT writer){
  // JPEG VisitFields invariants: multi-pass / gab / EPF / DC-frame / animation
  // nests are never entered; their false gates still write bits.
  JXL_DASSERT(frame->frame_type == kRegularFrame);
  JXL_DASSERT(frame->encoding == kVarDCT);
  JXL_DASSERT(frame->passes.num_passes == 1);
  JXL_DASSERT((frame->flags & kUseDcFrame) == 0);
  JXL_DASSERT(!frame->loop_filter.gab);
  JXL_DASSERT(frame->loop_filter.epf_iters == 0);
  JXL_DASSERT(!frame->custom_size_or_origin);
  JXL_DASSERT(frame->upsampling == 1);
  JXL_DASSERT(frame->blending_info.mode == kReplace);
  JXL_DASSERT(jxl_array_empty(&frame->extra_channel_upsampling));
  JXL_DASSERT(jxl_blending_infos_empty(&frame->extra_channel_blending_info));
  return jxl_bundle_write(&frame->fields, writer, kLayerHeader);
}

jxl_enc_status jxl_write_image_metadata(const jxl_image_metadata* metadata,
                          jxl_bit_writer* JXL_RESTRICT writer, jxl_layer_type layer) {
  return jxl_bundle_write(&metadata->fields, writer, layer);
}

jxl_enc_status jxl_write_quantizer_params_impl(const jxl_quantizer_params* params,
                            jxl_bit_writer* JXL_RESTRICT writer, jxl_layer_type layer){
  return jxl_bundle_write(&params->fields, writer, layer);
}

jxl_enc_status jxl_write_size_header_impl(const jxl_enc_size_header* size, jxl_bit_writer* JXL_RESTRICT writer,
                       jxl_layer_type layer){
  return jxl_bundle_write(&size->fields, writer, layer);
}


jxl_enc_status jxl_write_codestream_headers(jxl_codec_metadata* metadata, jxl_bit_writer* writer) {
  return jxl_write_codestream_headers_impl(metadata, writer);
}

jxl_enc_status jxl_write_frame_header(const jxl_enc_frame_header* frame, jxl_bit_writer* JXL_RESTRICT writer) {
  return jxl_write_frame_header_impl(frame, writer);
}

jxl_enc_status jxl_write_quantizer_params(const jxl_quantizer_params* params, jxl_bit_writer* JXL_RESTRICT writer, jxl_layer_type layer) {
  return jxl_write_quantizer_params_impl(params, writer, layer);
}

jxl_enc_status jxl_write_size_header(const jxl_enc_size_header* size, jxl_bit_writer* JXL_RESTRICT writer, jxl_layer_type layer) {
  return jxl_write_size_header_impl(size, writer, layer);
}
