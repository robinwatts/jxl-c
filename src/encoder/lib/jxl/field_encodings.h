// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef LIB_JXL_FIELD_ENCODINGS_H_
#define LIB_JXL_FIELD_ENCODINGS_H_

// Constants needed to encode/decode fields; avoids including the full fields.h.

#include <stddef.h>
#include <stdint.h>

#include "lib/jxl/base/compiler_specific.h"
#include "lib/jxl/base/enc_status.h"

typedef struct jxl_visitor jxl_visitor;
typedef struct jxl_fields jxl_fields;

// Declares TypeVisitFieldsThunk (and debug TypeNameThunk) for a jxl_fields-bearing
// Type. Type must embed `jxl_fields fields;` as its first member and provide free
// function TypeVisitFields(Type*, jxl_visitor*). Place JXL_FIELDS_NAME(Type) after
// the TypeVisitFields declaration. Call JXL_FIELDS_REGISTER_PTR from TypeInit
// before jxl_bundle_init.
#if (JXL_IS_DEBUG_BUILD)
#define JXL_FIELDS_NAME(Type)                                                 \
  static inline jxl_status Type##_visit_fields_thunk(                         \
      jxl_fields* self, jxl_visitor* JXL_RESTRICT visitor) {                  \
    return Type##_visit_fields((Type*)(self), visitor);                       \
  }                                                                           \
  static inline const char* Type##_name_thunk(const jxl_fields*) {            \
    return #Type;                                                             \
  }
#define JXL_FIELDS_REGISTER(Type)                                             \
  do {                                                                        \
    fields.visit_fields_fn = Type##_visit_fields_thunk;                       \
    fields.name_fn = Type##_name_thunk;                                       \
  } while (0)
#define JXL_FIELDS_REGISTER_PTR(Type, fields_ptr)                             \
  do {                                                                        \
    (fields_ptr)->visit_fields_fn = Type##_visit_fields_thunk;                \
    (fields_ptr)->name_fn = Type##_name_thunk;                                \
  } while (0)
#else
#define JXL_FIELDS_NAME(Type)                                                 \
  static inline jxl_status Type##_visit_fields_thunk(                         \
      jxl_fields* self, jxl_visitor* JXL_RESTRICT visitor) {                  \
    return Type##_visit_fields((Type*)(self), visitor);                       \
  }
#define JXL_FIELDS_REGISTER(Type)                                             \
  do {                                                                        \
    fields.visit_fields_fn = Type##_visit_fields_thunk;                       \
  } while (0)
#define JXL_FIELDS_REGISTER_PTR(Type, fields_ptr)                             \
  do {                                                                        \
    (fields_ptr)->visit_fields_fn = Type##_visit_fields_thunk;                \
  } while (0)
#endif  // JXL_IS_DEBUG_BUILD

// C-shaped jxl_fields: embed as the first member of each bundle type. Bundles set
// fields.visit_fields_fn (via JXL_FIELDS_REGISTER) so Bundle/jxl_visitor dispatch
// without a C++ vtable. TypeVisitFieldsThunk casts jxl_fields* back to the outer
// type. Call jxl_fields_construct_empty before Register if the member is not
// otherwise zeroed.
typedef struct jxl_fields {
  jxl_status (*visit_fields_fn)(jxl_fields*, jxl_visitor*);
#if (JXL_IS_DEBUG_BUILD)
  const char* (*name_fn)(const jxl_fields*);
#endif  // JXL_IS_DEBUG_BUILD
} jxl_fields;

static inline void jxl_fields_construct_empty(jxl_fields* self) {
  self->visit_fields_fn = NULL;
#if (JXL_IS_DEBUG_BUILD)
  self->name_fn = NULL;
#endif
}

static inline jxl_status jxl_fields_visit_fields(jxl_fields* self,
                                       jxl_visitor* JXL_RESTRICT visitor) {
  return self->visit_fields_fn(self, visitor);
}

#if (JXL_IS_DEBUG_BUILD)
static inline const char* jxl_fields_name(const jxl_fields* self) {
  return self->name_fn(self);
}
#endif  // JXL_IS_DEBUG_BUILD

// Distribution of U32 values for one particular selector. Represents either a
// power of two-sized range, or a single value. A separate type ensures this is
// only passed to the jxl_u32_enc ctor.
#define kU32DistrDirect 0x80000000u

typedef struct jxl_u32_distr {
  uint32_t d;
} jxl_u32_distr;

static inline jxl_u32_distr jxl_u32_distr_make(uint32_t d) {
  jxl_u32_distr self;
  self.d = d;
  return self;
}

static inline bool jxl_u32_distr_is_direct(jxl_u32_distr self) {
  return (self.d & kU32DistrDirect) != 0;
}

// Only call if jxl_u32_distr_is_direct().
static inline uint32_t jxl_u32_distr_direct(jxl_u32_distr self) {
  return self.d & (kU32DistrDirect - 1);
}

// Only call if !jxl_u32_distr_is_direct().
static inline size_t jxl_u32_distr_extra_bits(jxl_u32_distr self) {
  return (self.d & 0x1F) + 1;
}
static inline uint32_t jxl_u32_distr_offset(jxl_u32_distr self) {
  return (self.d >> 5) & 0x3FFFFFF;
}

// A direct-coded 31-bit value occupying 2 bits in the bitstream.
static inline jxl_u32_distr jxl_val(uint32_t value) {
  return jxl_u32_distr_make(value | kU32DistrDirect);
}

// Value - `offset` will be signaled in `bits` extra bits.
static inline jxl_u32_distr jxl_bits_offset(uint32_t bits, uint32_t offset) {
  return jxl_u32_distr_make(((bits - 1) & 0x1F) + ((offset & 0x3FFFFFF) << 5));
}

// Value will be signaled in `bits` extra bits.
static inline jxl_u32_distr jxl_bits(uint32_t bits) { return jxl_bits_offset(bits, 0); }

// See U32Coder documentation in fields.h.
typedef struct jxl_u32_enc {
  jxl_u32_distr d_[4];
} jxl_u32_enc;

static inline jxl_u32_enc jxl_u32_enc_make(jxl_u32_distr d0, jxl_u32_distr d1, jxl_u32_distr d2,
                                jxl_u32_distr d3) {
  jxl_u32_enc self;
  self.d_[0] = d0;
  self.d_[1] = d1;
  self.d_[2] = d2;
  self.d_[3] = d3;
  return self;
}

// Returns the jxl_u32_distr at `selector` = 0..3, least-significant first.
static inline jxl_u32_distr jxl_u32_enc_get_distr(jxl_u32_enc self, const uint32_t selector) {
  JXL_DASSERT(selector < 4);
  return self.d_[selector];
}

// Returns bit with the given `index` (0 = least significant).
static inline uint64_t jxl_make_bit(uint32_t index) { return 1ULL << index; }

// Returns true if `value` is listed in `allowed_bits`.
static inline jxl_status jxl_enum_valid(uint32_t value, uint64_t allowed_bits,
                               const char* name) {
  if (value >= 64) {
    return JXL_FAILURE("Value %u too large for %s\n", value, name);
  }
  const uint64_t bit = jxl_make_bit(value);
  if ((allowed_bits & bit) == 0) {
    return JXL_FAILURE("Invalid value %u for %s\n", value, name);
  }
  return jxl_ok_status();
}

#endif  // LIB_JXL_FIELD_ENCODINGS_H_
