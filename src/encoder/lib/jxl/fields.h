// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef LIB_JXL_FIELDS_H_
#define LIB_JXL_FIELDS_H_

// Forward/backward-compatible 'bundles' with auto-serialized 'fields'.

#include <stddef.h>
#include <stdint.h>

#include "lib/jxl/base/compiler_specific.h"
#include "lib/jxl/base/enc_status.h"
#include "lib/jxl/enc_bit_writer.h"
#include "lib/jxl/field_encodings.h"

#include "lib/jxl/layer_type.h"
// Integer coders: jxl_bits_coder (raw), U32Coder (table), U64Coder (varint).

// Writes a given (fixed) number of bits <= 32.
jxl_enc_status jxl_bits_coder_can_encode(size_t bits, uint32_t value,
                          size_t* JXL_RESTRICT encoded_bits);

// Returns false if the value is too large to encode.
jxl_enc_status jxl_bits_coder_write(size_t bits, uint32_t value,
                      jxl_bit_writer* JXL_RESTRICT writer);

// Encodes u32 using a lookup table and/or extra bits, governed by a per-field
// encoding `enc` which consists of four distributions `d` chosen via a 2-bit
// selector (least significant = 0). Each d may have two modes:
// - direct: if jxl_u32_distr_is_direct(d), the value is jxl_u32_distr_direct(d);
// - offset: the value is derived from jxl_u32_distr_extra_bits(d) extra bits plus jxl_u32_distr_offset(d);
// This encoding is denser than Exp-Golomb or Gamma codes when both small and
// large values occur.
//
// Examples:
// Direct: jxl_u32_enc_make(jxl_val(8), jxl_val(16), jxl_val(32), jxl_bits(6)), value 32 => 10b.
// Offset: jxl_u32_enc_make(jxl_val(0), jxl_bits_offset(1, 1), jxl_bits_offset(2, 3), jxl_bits_offset(8, 8))
//   defines the following prefix code:
//   00 -> 0
//   01x -> 1..2
//   10xx -> 3..7
//   11xxxxxxxx -> 8..263
size_t jxl_u32_coder_max_encoded_bits(jxl_u32_enc enc);
jxl_enc_status jxl_u32_coder_can_encode(jxl_u32_enc enc, uint32_t value,
                         size_t* JXL_RESTRICT encoded_bits);

// Returns false if the value is too large to encode.
jxl_enc_status jxl_u32_coder_write(jxl_u32_enc enc, uint32_t value,
                     jxl_bit_writer* JXL_RESTRICT writer);

jxl_enc_status jxl_u32_coder_choose_selector(jxl_u32_enc enc, uint32_t value,
                              uint32_t* JXL_RESTRICT selector,
                              size_t* JXL_RESTRICT total_bits);

// Encodes 64-bit unsigned integers with a fixed distribution, taking 2 bits
// to encode 0, 6 bits to encode 1 to 16, 10 bits to encode 17 to 272, 15 bits
// to encode up to 4095, and on the order of log2(value) * 1.125 bits for
// larger values.
// Returns false if the value is too large to encode.
jxl_enc_status jxl_u64_coder_write(uint64_t value, jxl_bit_writer* JXL_RESTRICT writer);

// Can always encode, but useful because it also returns bit size.
jxl_enc_status jxl_u64_coder_can_encode(uint64_t value, size_t* JXL_RESTRICT encoded_bits);

// IEEE 754 half-precision (binary16). Refuses to read/write NaN/Inf.
static inline size_t jxl_f16_coder_max_encoded_bits() { return 16; }

// Bit-exact roundtrip through binary16 without a BitReader/jxl_bit_writer.
// Equivalent to Write then Read of the same value.
jxl_enc_status jxl_f16_coder_project(float value, float* JXL_RESTRICT out);

// Returns false if the value is too large to encode.
jxl_enc_status jxl_f16_coder_write(float value, jxl_bit_writer* JXL_RESTRICT writer);
jxl_enc_status jxl_f16_coder_can_encode(float value, size_t* JXL_RESTRICT encoded_bits);

// A "bundle" is a forward- and backward compatible collection of fields.
// They are used for jxl_enc_size_header/jxl_enc_frame_header/jxl_group_header. Bundles can be
// extended by appending(!) fields. Optional fields may be omitted from the
// bitstream by conditionally visiting them. When reading new bitstreams with
// old code, we skip unknown fields at the end of the bundle. This requires
// storing the amount of extra appended bits, and that fields are visited in
// chronological order of being added to the format, because old decoders
// cannot skip some future fields and resume reading old fields. Similarly,
// new readers query bits in an "extensions" field to skip (groups of) fields
// not present in old bitstreams. Note that each bundle must include an
// "extensions" field prior to freezing the format, otherwise it cannot be
// extended.
//
// To ensure interoperability, there will be no opaque fields.
//
// HOWTO:
// - basic usage: define a struct with member variables ("fields") and a
//   TypeVisitFields(self, v) free function that calls jxl_visitor_u32/jxl_visitor_bool etc.
//   for each field, specifying their default values. Place JXL_FIELDS_NAME(Type)
//   after the TypeVisitFields declaration (defines free thunks) and call
//   JXL_FIELDS_REGISTER_PTR(Type, &fields) then jxl_bundle_init(&fields) from
//   TypeInit (sets the C-shaped visit_fields_fn).
//
// - print a trace of visitors: JXL_FIELDS_NAME also defines a debug
//   TypeNameThunk in debug builds; change BundlePrint* to return true.
//
// - optional fields: in VisitFields, add if (jxl_visitor_conditional(v, your_condition))
//   { jxl_visitor_bool(v, default, &field); }. This prevents reading/writing field
//   if !your_condition, which is typically computed from a prior field.
//   WARNING: to ensure all fields are initialized, do not add an else branch;
//   instead add another if (jxl_visitor_conditional(v, !your_condition)).
//
// - repeated fields: for dynamic sizes, use a concrete move-only list (or
//   Array for POD) and in VisitFields, if (jxl_visitor_is_reading(v)) jxl_array_resize_zero(&field, size)
//   before accessing field. For static or bounded sizes, use a C array. In all
//   cases, simply visit each array element as if it were a normal field.
//
// - nested bundles: add a bundle as a normal field and in VisitFields call
//   JXL_RETURN_IF_ERROR(jxl_visitor_visit_nested(v, &nested.fields));
//
// - allow future extensions: define a "uint64_t extensions" field and call
//   jxl_visitor_begin_extensions(v, &extensions) after visiting all non-extension fields,
//   and `return jxl_visitor_end_extensions(v);` after the last extension field.
//
// - encode an entire bundle in one bit if ALL its fields equal their default
//   values: add a "bool all_default" field and as the first visitor:
//   if (jxl_visitor_all_default(v, this, &all_default)) {
//     // Overwrite all serialized fields, but not any nonserialized_*.
//     jxl_visitor_set_default(v, this);
//     return true;
//   }
//   Note: if extensions are present, AllDefault() == false.

enum { kBundleMaxExtensions = 64 };  // bits in u64

// Initializes fields to the default values. It is not recursive to nested
// fields, this function is intended to be called in the constructors so
// each nested field will already Init itself.
void jxl_bundle_init(jxl_fields* JXL_RESTRICT fields);

// Similar to Init, but recursive to nested fields.
void jxl_bundle_set_default(jxl_fields* JXL_RESTRICT fields);

// Returns whether ALL fields (including `extensions`, if present) are equal
// to their default value.
bool jxl_bundle_all_default(const jxl_fields* fields);

// Returns whether a header's fields can all be encoded, i.e. they have a
// valid representation. If so, "*total_bits" is the exact number of bits
// required. Called by Write.
jxl_enc_status jxl_bundle_can_encode(const jxl_fields* fields, size_t* JXL_RESTRICT extension_bits,
                       size_t* JXL_RESTRICT total_bits);

jxl_enc_status jxl_bundle_write(const jxl_fields* fields, jxl_bit_writer* JXL_RESTRICT writer,
                   jxl_layer_type layer);

// A bundle can be in one of three states concerning extensions: not-begun,
// active, ended. Bundles may be nested, so we need a stack of states.
typedef struct jxl_extension_states {
  uint64_t begun_;
  uint64_t ended_;
} jxl_extension_states;

static inline void jxl_extension_states_push(jxl_extension_states* self) {
  self->begun_ <<= 1;
  self->ended_ <<= 1;
}
static inline void jxl_extension_states_pop(jxl_extension_states* self) {
  self->begun_ >>= 1;
  self->ended_ >>= 1;
}
static inline bool jxl_extension_states_is_begun(const jxl_extension_states* self) {
  return (self->begun_ & 1) != 0;
}
static inline bool jxl_extension_states_is_ended(const jxl_extension_states* self) {
  return (self->ended_ & 1) != 0;
}
static inline void jxl_extension_states_begin(jxl_extension_states* self) {
  JXL_DASSERT(!jxl_extension_states_is_begun(self));
  JXL_DASSERT(!jxl_extension_states_is_ended(self));
  self->begun_ += 1;
}
static inline void jxl_extension_states_end(jxl_extension_states* self) {
  JXL_DASSERT(jxl_extension_states_is_begun(self));
  JXL_DASSERT(!jxl_extension_states_is_ended(self));
  self->ended_ += 1;
}

// C-shaped visitor dispatch (function pointers instead of vtables).
typedef struct jxl_visitor_ops {
  jxl_enc_status (*bits)(jxl_visitor* self, size_t bits, uint32_t default_value,
                     uint32_t* JXL_RESTRICT value);
  jxl_enc_status (*u32)(jxl_visitor* self, jxl_u32_enc enc, uint32_t default_value,
                    uint32_t* JXL_RESTRICT value);
  jxl_enc_status (*u64)(jxl_visitor* self, uint64_t default_value,
                    uint64_t* JXL_RESTRICT value);
  jxl_enc_status (*f16)(jxl_visitor* self, float default_value,
                    float* JXL_RESTRICT value);
  jxl_enc_status (*boolean)(jxl_visitor* self, bool default_value,
                        bool* JXL_RESTRICT value);
  jxl_enc_status (*conditional)(jxl_visitor* self, bool condition);
  jxl_enc_status (*all_default)(jxl_visitor* self, const jxl_fields* fields,
                            bool* JXL_RESTRICT all_default);
  void (*set_default)(jxl_visitor* self, jxl_fields* fields);
  jxl_enc_status (*visit_nested)(jxl_visitor* self, jxl_fields* fields);
  bool (*is_reading)(const jxl_visitor* self);
  jxl_enc_status (*begin_extensions)(jxl_visitor* self,
                                 uint64_t* JXL_RESTRICT extensions);
  jxl_enc_status (*end_extensions)(jxl_visitor* self);
} jxl_visitor_ops;

// Default ops shared by encode visitors (Bool-via-jxl_bits, extensions, etc.).
jxl_enc_status jxl_visitor_default_bool(jxl_visitor* self, bool default_value,
                          bool* JXL_RESTRICT value);
jxl_enc_status jxl_visitor_default_conditional(jxl_visitor* self, bool condition);
jxl_enc_status jxl_visitor_default_all_default(jxl_visitor* self, const jxl_fields* fields,
                                bool* JXL_RESTRICT all_default);
void jxl_visitor_default_set_default(jxl_visitor* self, jxl_fields* fields);
jxl_enc_status jxl_visitor_default_visit_nested(jxl_visitor* self, jxl_fields* fields);
bool jxl_visitor_default_is_reading(const jxl_visitor* self);
jxl_enc_status jxl_visitor_default_begin_extensions(jxl_visitor* self,
                                     uint64_t* JXL_RESTRICT extensions);
jxl_enc_status jxl_visitor_default_end_extensions(jxl_visitor* self);

// Visitors generate Init/AllDefault/Write logic for all fields. Each bundle's
// TypeVisitFields calls jxl_visitor_u32 etc. Ops tables replace C++ vtables.
// Specialized visitors embed jxl_visitor as their first member (C-shaped layout).
typedef struct jxl_visitor {
  const jxl_visitor_ops* ops;
  size_t depth;
  jxl_extension_states extension_states;
} jxl_visitor;

static inline void jxl_visitor_construct_empty(jxl_visitor* self) {
  self->ops = NULL;
  self->depth = 0;
  self->extension_states.begun_ = 0;
  self->extension_states.ended_ = 0;
}

static inline jxl_enc_status jxl_visitor_visit(jxl_visitor* self, jxl_fields* fields) {
  JXL_ENSURE(self->depth < kBundleMaxExtensions);
  self->depth += 1;
  jxl_extension_states_push(&self->extension_states);
  const jxl_enc_status ok = jxl_fields_visit_fields(fields, self);
  if (jxl_enc_status_ok(ok)) {
    JXL_DASSERT(!jxl_extension_states_is_begun(&self->extension_states) ||
                jxl_extension_states_is_ended(&self->extension_states));
  }
  jxl_extension_states_pop(&self->extension_states);
  JXL_DASSERT(self->depth != 0);
  self->depth -= 1;
  // Balanced nesting: every jxl_visitor_visit increment has a matching decrement.
  return ok;
}

static inline jxl_enc_status jxl_visitor_visit_const(jxl_visitor* self, const jxl_fields* t) {
  return jxl_visitor_visit(self, (jxl_fields*)(t));
}

static inline jxl_enc_status jxl_visitor_bool(jxl_visitor* self, bool default_value,
                          bool* JXL_RESTRICT value) {
  return self->ops->boolean(self, default_value, value);
}
static inline jxl_enc_status jxl_visitor_u32(jxl_visitor* self, jxl_u32_enc enc, uint32_t default_value,
                         uint32_t* JXL_RESTRICT value) {
  return self->ops->u32(self, enc, default_value, value);
}

static inline jxl_enc_status jxl_visitor_enum(jxl_visitor* self, uint32_t default_value,
                          uint32_t* JXL_RESTRICT value, uint64_t allowed_bits,
                          const char* name) {
  JXL_RETURN_IF_ERROR(jxl_visitor_u32(self, jxl_u32_enc_make(jxl_val(0), jxl_val(1), jxl_bits_offset(4, 2), jxl_bits_offset(6, 18)), default_value, value));
  return jxl_enum_valid(*value, allowed_bits, name);
}

static inline jxl_enc_status jxl_visitor_bits(jxl_visitor* self, size_t bits, uint32_t default_value,
                          uint32_t* JXL_RESTRICT value) {
  return self->ops->bits(self, bits, default_value, value);
}
static inline jxl_enc_status jxl_visitor_u64(jxl_visitor* self, uint64_t default_value,
                         uint64_t* JXL_RESTRICT value) {
  return self->ops->u64(self, default_value, value);
}
static inline jxl_enc_status jxl_visitor_f16(jxl_visitor* self, float default_value,
                         float* JXL_RESTRICT value) {
  return self->ops->f16(self, default_value, value);
}
static inline jxl_enc_status jxl_visitor_conditional(jxl_visitor* self, bool condition) {
  return self->ops->conditional(self, condition);
}
static inline jxl_enc_status jxl_visitor_all_default(jxl_visitor* self, const jxl_fields* fields,
                                bool* JXL_RESTRICT all_default) {
  return self->ops->all_default(self, fields, all_default);
}
static inline void jxl_visitor_set_default(jxl_visitor* self, jxl_fields* fields) {
  self->ops->set_default(self, fields);
}
static inline jxl_enc_status jxl_visitor_visit_nested(jxl_visitor* self, jxl_fields* fields) {
  return self->ops->visit_nested(self, fields);
}
static inline bool jxl_visitor_is_reading(const jxl_visitor* self) {
  return self->ops->is_reading(self);
}
static inline jxl_enc_status jxl_visitor_begin_extensions(jxl_visitor* self,
                                     uint64_t* JXL_RESTRICT extensions) {
  return self->ops->begin_extensions(self, extensions);
}
static inline jxl_enc_status jxl_visitor_end_extensions(jxl_visitor* self) {
  return self->ops->end_extensions(self);
}


#endif  // LIB_JXL_FIELDS_H_
