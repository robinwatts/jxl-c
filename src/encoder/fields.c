// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in LICENSE-BSD.

#include "fields.h"

#include <inttypes.h>  // PRIu64
#include <math.h>
#include <stddef.h>
#include <string.h>
#include "base/bits.h"

#include "base/bits.h"
#include "base/common.h"
#include "base/compiler_specific.h"
#include "base/printf_macros.h"
#include "base/enc_status.h"
#include "field_encodings.h"

jxl_enc_status jxl_visitor_default_bool(jxl_visitor* self, bool default_value,
                          bool* JXL_RESTRICT value) {
  uint32_t bits = *value ? 1 : 0;
  JXL_RETURN_IF_ERROR(
      jxl_visitor_bits(self, 1, (uint32_t)(default_value), &bits));
  JXL_DASSERT(bits <= 1);
  *value = bits == 1;
  return jxl_enc_ok_status();
}

jxl_enc_status jxl_visitor_default_conditional(jxl_visitor* /*self*/, bool condition) {
  return jxl_enc_status_from_bool(condition);
}

jxl_enc_status jxl_visitor_default_all_default(jxl_visitor* self, const jxl_fields* /*fields*/,
                                bool* JXL_RESTRICT all_default) {
  JXL_RETURN_IF_ERROR(jxl_visitor_bool(self, true, all_default));
  return jxl_enc_status_from_bool(*all_default);
}

void jxl_visitor_default_set_default(jxl_visitor* /*self*/, jxl_fields* /*fields*/) {}

jxl_enc_status jxl_visitor_default_visit_nested(jxl_visitor* self, jxl_fields* fields) {
  return jxl_visitor_visit(self, fields);
}

bool jxl_visitor_default_is_reading(const jxl_visitor* /*self*/) { return false; }

jxl_enc_status jxl_visitor_default_begin_extensions(jxl_visitor* self,
                                     uint64_t* JXL_RESTRICT extensions) {
  JXL_RETURN_IF_ERROR(jxl_visitor_u64(self, 0, extensions));
  jxl_extension_states_begin(&self->extension_states);
  return jxl_enc_ok_status();
}

jxl_enc_status jxl_visitor_default_end_extensions(jxl_visitor* self) {
  jxl_extension_states_end(&self->extension_states);
  return jxl_enc_ok_status();
}

static jxl_enc_status jxl_init_bits(jxl_visitor* /*self*/, size_t /*bits*/, uint32_t default_value,
                uint32_t* JXL_RESTRICT value) {
  *value = default_value;
  return jxl_enc_ok_status();
}
static jxl_enc_status jxl_init_u32(jxl_visitor* /*self*/, jxl_u32_enc /*enc*/, uint32_t default_value,
               uint32_t* JXL_RESTRICT value) {
  *value = default_value;
  return jxl_enc_ok_status();
}
static jxl_enc_status jxl_init_u64(jxl_visitor* /*self*/, uint64_t default_value,
               uint64_t* JXL_RESTRICT value) {
  *value = default_value;
  return jxl_enc_ok_status();
}
static jxl_enc_status jxl_init_f16(jxl_visitor* /*self*/, float default_value,
               float* JXL_RESTRICT value) {
  *value = default_value;
  return jxl_enc_ok_status();
}
static jxl_enc_status jxl_init_bool(jxl_visitor* /*self*/, bool default_value,
                bool* JXL_RESTRICT value) {
  *value = default_value;
  return jxl_enc_ok_status();
}
static jxl_enc_status jxl_init_conditional(jxl_visitor* /*self*/, bool /*condition*/) { return jxl_enc_ok_status(); }
static jxl_enc_status jxl_init_all_default(jxl_visitor* self, const jxl_fields* /*fields*/,
                      bool* JXL_RESTRICT all_default) {
  JXL_RETURN_IF_ERROR(jxl_visitor_bool(self, true, all_default));
  return jxl_enc_error_status();
}
static jxl_enc_status jxl_init_visit_nested(jxl_visitor* /*self*/, jxl_fields* /*fields*/) { return jxl_enc_ok_status(); }

static const jxl_visitor_ops kInitOps = {
    jxl_init_bits,
    jxl_init_u32,
    jxl_init_u64,
    jxl_init_f16,
    jxl_init_bool,
    jxl_init_conditional,
    jxl_init_all_default,
    jxl_visitor_default_set_default,
    jxl_init_visit_nested,
    jxl_visitor_default_is_reading,
    jxl_visitor_default_begin_extensions,
    jxl_visitor_default_end_extensions,
};

static jxl_enc_status jxl_set_default_visit_nested(jxl_visitor* self, jxl_fields* fields) {
  return jxl_visitor_visit(self, fields);
}

static const jxl_visitor_ops kSetDefaultOps = {
    jxl_init_bits,
    jxl_init_u32,
    jxl_init_u64,
    jxl_init_f16,
    jxl_init_bool,
    jxl_init_conditional,
    jxl_init_all_default,
    jxl_visitor_default_set_default,
    jxl_set_default_visit_nested,
    jxl_visitor_default_is_reading,
    jxl_visitor_default_begin_extensions,
    jxl_visitor_default_end_extensions,
};

typedef struct jxl_all_default_visitor {
  jxl_visitor visitor;
  bool all_default;
} jxl_all_default_visitor;

static bool jxl_all_default_visitor_all_default(const jxl_all_default_visitor* self) {
  return self->all_default;
}

static jxl_enc_status jxl_all_default_bits(jxl_visitor* self, size_t /*bits*/, uint32_t default_value,
                      uint32_t* JXL_RESTRICT value) {
  jxl_all_default_visitor* v = (jxl_all_default_visitor*)(self);
  v->all_default &= *value == default_value;
  return jxl_enc_ok_status();
}
static jxl_enc_status jxl_all_default_u32(jxl_visitor* self, jxl_u32_enc /*enc*/, uint32_t default_value,
                     uint32_t* JXL_RESTRICT value) {
  jxl_all_default_visitor* v = (jxl_all_default_visitor*)(self);
  v->all_default &= *value == default_value;
  return jxl_enc_ok_status();
}
static jxl_enc_status jxl_all_default_u64(jxl_visitor* self, uint64_t default_value,
                     uint64_t* JXL_RESTRICT value) {
  jxl_all_default_visitor* v = (jxl_all_default_visitor*)(self);
  v->all_default &= *value == default_value;
  return jxl_enc_ok_status();
}
static jxl_enc_status jxl_all_default_f16(jxl_visitor* self, float default_value,
                     float* JXL_RESTRICT value) {
  jxl_all_default_visitor* v = (jxl_all_default_visitor*)(self);
  v->all_default &= fabs(*value - default_value) < 1E-6f;
  return jxl_enc_ok_status();
}
static jxl_enc_status jxl_all_default_all_default(jxl_visitor* /*self*/, const jxl_fields* /*fields*/,
                            bool* JXL_RESTRICT /*all_default*/) {
  return jxl_enc_error_status();
}

static const jxl_visitor_ops kAllDefaultOps = {
    jxl_all_default_bits,
    jxl_all_default_u32,
    jxl_all_default_u64,
    jxl_all_default_f16,
    jxl_visitor_default_bool,
    jxl_visitor_default_conditional,
    jxl_all_default_all_default,
    jxl_visitor_default_set_default,
    jxl_visitor_default_visit_nested,
    jxl_visitor_default_is_reading,
    jxl_visitor_default_begin_extensions,
    jxl_visitor_default_end_extensions,
};

static void jxl_all_default_visitor_init(jxl_all_default_visitor* self) {
  jxl_visitor_construct_empty(&self->visitor);
  self->visitor.ops = &kAllDefaultOps;
  self->all_default = true;
}

typedef struct jxl_can_encode_visitor {
  jxl_visitor visitor;
  bool ok;
  size_t encoded_bits;
  uint64_t extensions;
  uint64_t pos_after_ext;
} jxl_can_encode_visitor;

static jxl_enc_status jxl_can_encode_visitor_get_sizes(jxl_can_encode_visitor* self,
                                size_t* JXL_RESTRICT extension_bits,
                                size_t* JXL_RESTRICT total_bits) {
  JXL_RETURN_IF_ERROR(jxl_enc_status_from_bool(self->ok));
  *extension_bits = 0;
  *total_bits = self->encoded_bits;
  if (self->pos_after_ext != 0) {
    JXL_ENSURE(self->encoded_bits >= self->pos_after_ext);
    *extension_bits = self->encoded_bits - self->pos_after_ext;
    size_t encoded = 0;
    self->ok =
        self->ok && jxl_enc_status_ok(jxl_u64_coder_can_encode(*extension_bits, &encoded));
    *total_bits += encoded;
    for (size_t i = 1; i < jxl_pop_count(self->extensions); ++i) {
      encoded = 0;
      self->ok = self->ok && jxl_enc_status_ok(jxl_u64_coder_can_encode(0, &encoded));
      *total_bits += encoded;
    }
  }
  return jxl_enc_ok_status();
}

static jxl_enc_status jxl_can_encode_bits(jxl_visitor* self, size_t bits, uint32_t /*default_value*/,
                     uint32_t* JXL_RESTRICT value) {
  jxl_can_encode_visitor* v = (jxl_can_encode_visitor*)(self);
  size_t encoded_bits = 0;
  v->ok = v->ok && jxl_enc_status_ok(jxl_bits_coder_can_encode(bits, *value, &encoded_bits));
  v->encoded_bits += encoded_bits;
  return jxl_enc_ok_status();
}
static jxl_enc_status jxl_can_encode_u32(jxl_visitor* self, jxl_u32_enc enc, uint32_t /*default_value*/,
                    uint32_t* JXL_RESTRICT value) {
  jxl_can_encode_visitor* v = (jxl_can_encode_visitor*)(self);
  size_t encoded_bits = 0;
  v->ok = v->ok && jxl_enc_status_ok(jxl_u32_coder_can_encode(enc, *value, &encoded_bits));
  v->encoded_bits += encoded_bits;
  return jxl_enc_ok_status();
}
static jxl_enc_status jxl_can_encode_u64(jxl_visitor* self, uint64_t /*default_value*/,
                    uint64_t* JXL_RESTRICT value) {
  jxl_can_encode_visitor* v = (jxl_can_encode_visitor*)(self);
  size_t encoded_bits = 0;
  v->ok = v->ok && jxl_enc_status_ok(jxl_u64_coder_can_encode(*value, &encoded_bits));
  v->encoded_bits += encoded_bits;
  return jxl_enc_ok_status();
}
static jxl_enc_status jxl_can_encode_f16(jxl_visitor* self, float /*default_value*/,
                    float* JXL_RESTRICT value) {
  jxl_can_encode_visitor* v = (jxl_can_encode_visitor*)(self);
  size_t encoded_bits = 0;
  v->ok = v->ok && jxl_enc_status_ok(jxl_f16_coder_can_encode(*value, &encoded_bits));
  v->encoded_bits += encoded_bits;
  return jxl_enc_ok_status();
}
static jxl_enc_status jxl_can_encode_all_default(jxl_visitor* self, const jxl_fields* fields,
                           bool* JXL_RESTRICT all_default) {
  *all_default = jxl_bundle_all_default(fields);
  JXL_RETURN_IF_ERROR(jxl_visitor_bool(self, true, all_default));
  return jxl_enc_status_from_bool(*all_default);
}
static jxl_enc_status jxl_can_encode_begin_extensions(jxl_visitor* self,
                                uint64_t* JXL_RESTRICT extensions) {
  JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_default_begin_extensions(self, extensions));
  jxl_can_encode_visitor* v = (jxl_can_encode_visitor*)(self);
  v->extensions = *extensions;
  if (*extensions != 0) {
    JXL_ENSURE(v->pos_after_ext == 0);
    v->pos_after_ext = v->encoded_bits;
    JXL_ENSURE(v->pos_after_ext != 0);
  }
  return jxl_enc_ok_status();
}

static const jxl_visitor_ops kCanEncodeOps = {
    jxl_can_encode_bits,
    jxl_can_encode_u32,
    jxl_can_encode_u64,
    jxl_can_encode_f16,
    jxl_visitor_default_bool,
    jxl_visitor_default_conditional,
    jxl_can_encode_all_default,
    jxl_visitor_default_set_default,
    jxl_visitor_default_visit_nested,
    jxl_visitor_default_is_reading,
    jxl_can_encode_begin_extensions,
    jxl_visitor_default_end_extensions,
};

static void jxl_can_encode_visitor_init(jxl_can_encode_visitor* self) {
  jxl_visitor_construct_empty(&self->visitor);
  self->visitor.ops = &kCanEncodeOps;
  self->ok = true;
  self->encoded_bits = 0;
  self->extensions = 0;
  self->pos_after_ext = 0;
}

void jxl_bundle_init(jxl_fields* fields) {
  jxl_visitor visitor;
  jxl_visitor_construct_empty(&visitor);
  visitor.ops = &kInitOps;
  if (!jxl_enc_status_ok(jxl_visitor_visit(&visitor, fields))) {
    JXL_DEBUG_ABORT("Init should never fail");
  }
}
void jxl_bundle_set_default(jxl_fields* fields) {
  jxl_visitor visitor;
  jxl_visitor_construct_empty(&visitor);
  visitor.ops = &kSetDefaultOps;
  if (!jxl_enc_status_ok(jxl_visitor_visit(&visitor, fields))) {
    JXL_DEBUG_ABORT("SetDefault should never fail");
  }
}
bool jxl_bundle_all_default(const jxl_fields* fields) {
  jxl_all_default_visitor visitor;
  jxl_all_default_visitor_init(&visitor);
  if (!jxl_enc_status_ok(jxl_visitor_visit_const(&visitor.visitor, fields))) {
    JXL_DEBUG_ABORT("AllDefault should never fail");
  }
  return jxl_all_default_visitor_all_default(&visitor);
}
jxl_enc_status jxl_bundle_can_encode(const jxl_fields* fields, size_t* extension_bits,
                       size_t* total_bits) {
  jxl_can_encode_visitor visitor;
  jxl_can_encode_visitor_init(&visitor);
  JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_visit_const(&visitor.visitor, fields));
  JXL_QUIET_RETURN_IF_ERROR(
      jxl_can_encode_visitor_get_sizes(&visitor, extension_bits, total_bits));
  return jxl_enc_ok_status();
}
jxl_enc_status jxl_bits_coder_can_encode(const size_t bits, const uint32_t value,
                            size_t* JXL_RESTRICT encoded_bits) {
  *encoded_bits = bits;
  if (value >= (1ULL << bits)) {
    return JXL_FAILURE("Value %u too large for %" PRIu64 " bits", value,
                       (uint64_t)(bits));
  }
  return jxl_enc_ok_status();
}

size_t jxl_u32_coder_max_encoded_bits(const jxl_u32_enc enc) {
  size_t extra_bits = 0;
  for (uint32_t selector = 0; selector < 4; ++selector) {
    const jxl_u32_distr d = jxl_u32_enc_get_distr(enc, selector);
    if (jxl_u32_distr_is_direct(d)) {
      continue;
    } else {
      extra_bits = JXL_MAX(extra_bits, jxl_u32_distr_extra_bits(d));
    }
  }
  return 2 + extra_bits;
}

jxl_enc_status jxl_u32_coder_can_encode(const jxl_u32_enc enc, const uint32_t value,
                           size_t* JXL_RESTRICT encoded_bits) {
  uint32_t selector;
  size_t total_bits;
  const jxl_enc_status ok = jxl_u32_coder_choose_selector(enc, value, &selector, &total_bits);
  *encoded_bits = jxl_enc_status_ok(ok) ? total_bits : 0;
  return ok;
}

jxl_enc_status jxl_u32_coder_choose_selector(const jxl_u32_enc enc, const uint32_t value,
                                uint32_t* JXL_RESTRICT selector,
                                size_t* JXL_RESTRICT total_bits) {
  const size_t bits_required = 32 - jxl_num0_bits_above_ms1_bit32(value);
  JXL_ENSURE(bits_required <= 32);

  *selector = 0;
  *total_bits = 0;

  // It is difficult to verify whether Dist32Byte are sorted, so check all
  // selectors and keep the one with the fewest total_bits.
  *total_bits = 64;  // more than any valid encoding
  for (uint32_t s = 0; s < 4; ++s) {
    const jxl_u32_distr d = jxl_u32_enc_get_distr(enc, s);
    if (jxl_u32_distr_is_direct(d)) {
      if (jxl_u32_distr_direct(d) == value) {
        *selector = s;
        *total_bits = 2;
        return jxl_enc_ok_status();  // Done, direct is always the best possible.
      }
      continue;
    }
    const size_t extra_bits = jxl_u32_distr_extra_bits(d);
    const uint32_t offset = jxl_u32_distr_offset(d);
    if (value < offset || value >= offset + (1ULL << extra_bits)) continue;

    // Better than prior encoding, remember it:
    if (2 + extra_bits < *total_bits) {
      *selector = s;
      *total_bits = 2 + extra_bits;
    }
  }

  if (*total_bits == 64) {
    return JXL_FAILURE("No feasible selector for %u", value);
  }

  return jxl_enc_ok_status();
}

// Can always encode, but useful because it also returns bit size.
jxl_enc_status jxl_u64_coder_can_encode(uint64_t value, size_t* JXL_RESTRICT encoded_bits) {
  if (value == 0) {
    *encoded_bits = 2;  // 2 selector bits
  } else if (value <= 16) {
    *encoded_bits = 2 + 4;  // 2 selector bits + 4 payload bits
  } else if (value <= 272) {
    *encoded_bits = 2 + 8;  // 2 selector bits + 8 payload bits
  } else {
    *encoded_bits = 2 + 12;  // 2 selector bits + 12 payload bits
    value >>= 12;
    int shift = 12;
    while (value > 0 && shift < 60) {
      *encoded_bits += 1 + 8;  // 1 continuation bit + 8 payload bits
      value >>= 8;
      shift += 8;
    }
    if (value > 0) {
      // This only could happen if shift == N - 4.
      *encoded_bits += 1 + 4;  // 1 continuation bit + 4 payload bits
    } else {
      *encoded_bits += 1;  // 1 stop bit
    }
  }

  return jxl_enc_ok_status();
}

jxl_enc_status jxl_f16_coder_project(float value, float* JXL_RESTRICT out) {
  uint32_t bits32;
  memcpy(&bits32, &value, sizeof(bits32));
  const uint32_t sign = bits32 >> 31;
  const uint32_t biased_exp32 = (bits32 >> 23) & 0xFF;
  const uint32_t mantissa32 = bits32 & 0x7FFFFF;

  const int32_t exp = (int32_t)(biased_exp32) - 127;
  if (JXL_UNLIKELY(exp > 15)) {
    return JXL_FAILURE("Too big to encode, CanEncode should return false");
  }

  uint32_t bits16;
  // Tiny or zero => zero.
  if (exp < -24) {
    bits16 = 0;
  } else {
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
    bits16 = (sign << 15) | (biased_exp16 << 10) | mantissa16;
  }

  const uint32_t sign_out = bits16 >> 15;
  const uint32_t biased_exp = (bits16 >> 10) & 0x1F;
  const uint32_t mantissa = bits16 & 0x3FF;

  if (JXL_UNLIKELY(biased_exp == 31)) {
    return JXL_FAILURE("F16 infinity or NaN are not supported");
  }

  // Subnormal or zero
  if (JXL_UNLIKELY(biased_exp == 0)) {
    *out = (1.0f / 16384) * (mantissa * (1.0f / 1024));
    if (sign_out) *out = -*out;
    return jxl_enc_ok_status();
  }

  // Normalized: convert the representation directly (faster than ldexp/tables).
  const uint32_t biased_exp32_out = biased_exp + (127 - 15);
  const uint32_t mantissa32_out = mantissa << (23 - 10);
  const uint32_t bits32_out =
      (sign_out << 31) | (biased_exp32_out << 23) | mantissa32_out;
  memcpy(out, &bits32_out, sizeof(bits32_out));
  return jxl_enc_ok_status();
}

jxl_enc_status jxl_f16_coder_can_encode(float value, size_t* JXL_RESTRICT encoded_bits) {
  *encoded_bits = jxl_f16_coder_max_encoded_bits();
  if (isnan(value) || isinf(value)) {
    return JXL_FAILURE("Should not attempt to store NaN and infinity");
  }
  return jxl_enc_status_from_bool(fabs(value) <= 65504.0f);
}

