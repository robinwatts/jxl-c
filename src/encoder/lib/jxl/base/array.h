// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef LIB_JXL_BASE_ARRAY_H_
#define LIB_JXL_BASE_ARRAY_H_

// MemoryManager-backed growable arrays (concrete POD peels).
// C-shaped storage (ptr/len/capacity); prefer jxl_array_len/jxl_array_at/jxl_array_data/
// jxl_array_empty.
// ctx must be non-NULL before any growth (reserve/resize/push).
// jxl_array_copy_from may JXL_CRASH() on allocation failure (no exceptions).
// Element types are specialized via JXL_DEFINE_POD_ARRAY; no Array<T>.
// Note: jxl_array_size is a concrete array-of-size_t type; length helpers are
// named jxl_array_len to avoid colliding with that type name.
//
// Operations are macros + field-pointer helpers (no C++ overloads, no
// ArrayCommon* casts that break strict aliasing).

#include <jxl/context.h>
#include "lib/jxl/allocator.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "lib/jxl/base/common.h"
#include "lib/jxl/base/compiler_specific.h"
#include "lib/jxl/base/status.h"

#define jxl_array_len(a) ((a)->len)
#define jxl_array_empty(a) ((a)->len == 0)
#define jxl_array_data(a) ((a)->ptr)
#define jxl_array_data_const(a) ((a)->ptr)
#define jxl_array_at(a, i) (&(a)->ptr[(i)])
#define jxl_array_at_const(a, i) (&(a)->ptr[(i)])
#define jxl_array_clear(a)                                                          \
  do {                                                                         \
    JXL_DASSERT((a) != NULL);                                                  \
    (a)->len = 0;                                                              \
  } while (0)
#define jxl_array_pop_back(a)                                                        \
  do {                                                                         \
    JXL_DASSERT((a) != NULL && (a)->len > 0);                                  \
    --(a)->len;                                                                \
  } while (0)
#define jxl_array_back(a) ((a)->ptr[(a)->len - 1])
#define jxl_array_back_ptr(a) (&(a)->ptr[(a)->len - 1])
#define jxl_array_back_ptr_const(a) (&(a)->ptr[(a)->len - 1])

#define jxl_array_construct_empty(a, mm)                                             \
  do {                                                                         \
    (a)->ctx = (mm);                                                \
    (a)->ptr = NULL;                                                           \
    (a)->len = 0;                                                              \
    (a)->capacity = 0;                                                         \
  } while (0)

static inline void jxl_array_destroy_fields(jxl_context** mm, void** ptr,
                                      size_t* len, size_t* capacity) {
  if (ptr == NULL) return;
  if (*ptr != NULL) {
    JXL_DASSERT(*mm != NULL);
    jxl_free((*mm), *ptr);
  }
  *ptr = NULL;
  *len = 0;
  *capacity = 0;
}

static inline void jxl_array_swap_fields(jxl_context** mm_a, void** ptr_a,
                                   size_t* len_a, size_t* cap_a,
                                   jxl_context** mm_b, void** ptr_b,
                                   size_t* len_b, size_t* cap_b) {
  jxl_context* tmp_mm = *mm_a;
  *mm_a = *mm_b;
  *mm_b = tmp_mm;
  void* tmp_ptr = *ptr_a;
  *ptr_a = *ptr_b;
  *ptr_b = tmp_ptr;
  jxl_swap(len_a, len_b);
  jxl_swap(cap_a, cap_b);
}

static inline jxl_status jxl_array_init_fields(jxl_context** mm, void** ptr,
                                     size_t* len, size_t* capacity,
                                     jxl_context* ctx) {
  JXL_ENSURE(mm != NULL && ptr != NULL && len != NULL && capacity != NULL);
  jxl_array_destroy_fields(mm, ptr, len, capacity);
  *mm = ctx;
  return jxl_ok_status();
}

static inline jxl_status jxl_array_reserve_fields(jxl_context** mm, void** ptr,
                                        size_t* len, size_t* capacity,
                                        size_t want_capacity, size_t elem_size) {
  JXL_ENSURE(mm != NULL && ptr != NULL && len != NULL && capacity != NULL);
  if (want_capacity <= *capacity) return jxl_ok_status();
  size_t new_capacity = *capacity;
  if (new_capacity == 0) new_capacity = 16;
  while (new_capacity < want_capacity) {
    size_t grown;
    if (!jxl_safe_add(new_capacity, new_capacity / 2, &grown) ||
        grown <= new_capacity) {
      new_capacity = want_capacity;
      break;
    }
    new_capacity = grown;
  }
  if (new_capacity < want_capacity) new_capacity = want_capacity;
  size_t bytes;
  if (!jxl_safe_mul(new_capacity, elem_size, &bytes)) {
    return JXL_FAILURE("jxl_array_reserve: size overflow");
  }
  void* neu;
  if (*mm == NULL) {
    return JXL_FAILURE("jxl_array_reserve: missing memory manager");
  }
  neu = jxl_alloc((*mm), bytes);
  if (neu == NULL) {
    return JXL_FAILURE("jxl_array_reserve: allocation failed");
  }
  if (*ptr != NULL && *len > 0) {
    memcpy(neu, *ptr, (*len) * elem_size);
  }
  jxl_free((*mm), *ptr);
  *ptr = neu;
  *capacity = new_capacity;
  return jxl_ok_status();
}

static inline jxl_status jxl_array_resize_fields(jxl_context** mm, void** ptr,
                                       size_t* len, size_t* capacity,
                                       size_t size, size_t elem_size) {
  JXL_RETURN_IF_ERROR(
      jxl_array_reserve_fields(mm, ptr, len, capacity, size, elem_size));
  *len = size;
  return jxl_ok_status();
}

static inline jxl_status jxl_array_resize_zero_fields(jxl_context** mm, void** ptr,
                                           size_t* len, size_t* capacity,
                                           size_t size, size_t elem_size) {
  size_t old = *len;
  JXL_RETURN_IF_ERROR(
      jxl_array_resize_fields(mm, ptr, len, capacity, size, elem_size));
  if (size > old) {
    memset((char*)(*ptr) + old * elem_size, 0, (size - old) * elem_size);
  }
  return jxl_ok_status();
}

static inline jxl_status jxl_array_copy_from_fields(jxl_context** mm_a, void** ptr_a,
                                         size_t* len_a, size_t* cap_a,
                                         jxl_context* const* mm_b,
                                         void* const* ptr_b, const size_t* len_b,
                                         size_t elem_size) {
  JXL_ENSURE(mm_a != NULL && ptr_a != NULL && len_a != NULL && cap_a != NULL);
  JXL_ENSURE(mm_b != NULL && ptr_b != NULL && len_b != NULL);
  if (ptr_a == (void**)(void*)ptr_b) return jxl_ok_status();
  jxl_array_destroy_fields(mm_a, ptr_a, len_a, cap_a);
  *mm_a = *mm_b;
  if (*len_b == 0) return jxl_ok_status();
  if (!jxl_status_ok(jxl_array_reserve_fields(mm_a, ptr_a, len_a, cap_a, *len_b, elem_size))) {
    JXL_CRASH();
  }
  memcpy(*ptr_a, *ptr_b, (*len_b) * elem_size);
  *len_a = *len_b;
  return jxl_ok_status();
}

static inline jxl_status jxl_array_append_fields(jxl_context** mm, void** ptr,
                                       size_t* len, size_t* capacity,
                                       const void* begin, size_t count,
                                       size_t elem_size) {
  if (count == 0) return jxl_ok_status();
  size_t new_size;
  if (!jxl_safe_add(*len, count, &new_size)) {
    return JXL_FAILURE("jxl_array_append: overflow");
  }
  JXL_RETURN_IF_ERROR(
      jxl_array_reserve_fields(mm, ptr, len, capacity, new_size, elem_size));
  memcpy((char*)(*ptr) + (*len) * elem_size, begin, count * elem_size);
  *len = new_size;
  return jxl_ok_status();
}

static inline jxl_status jxl_array_assign_fields(jxl_context** mm, void** ptr,
                                       size_t* len, size_t* capacity,
                                       const void* data, size_t count,
                                       size_t elem_size) {
  JXL_ENSURE(mm != NULL && ptr != NULL && len != NULL && capacity != NULL);
  *len = 0;
  return jxl_array_append_fields(mm, ptr, len, capacity, data, count, elem_size);
}

static inline void jxl_array_erase_fields(void** ptr, size_t* len, void* first,
                                    void* last, size_t elem_size) {
  JXL_DASSERT(ptr != NULL && len != NULL);
  char* base = (char*)(*ptr);
  char* f = (char*)first;
  char* l = (char*)last;
  JXL_DASSERT(f >= base && l <= base + (*len) * elem_size && f <= l);
  size_t n = (size_t)(l - f) / elem_size;
  if (n == 0) return;
  size_t idx = (size_t)(f - base) / elem_size;
  size_t tail = *len - (idx + n);
  if (tail != 0) {
    memmove(f, l, tail * elem_size);
  }
  *len -= n;
}

static inline jxl_status jxl_array_push_back_prep_fields(jxl_context** mm, void** ptr,
                                             size_t* len, size_t* capacity,
                                             size_t elem_size) {
  if (*len == *capacity) {
    size_t need;
    if (!jxl_safe_add(*capacity, (size_t)1, &need)) {
      return JXL_FAILURE("jxl_array_push_back: overflow");
    }
    return jxl_array_reserve_fields(mm, ptr, len, capacity, need, elem_size);
  }
  return jxl_ok_status();
}

static inline jxl_status jxl_array_resize_fill_fields(jxl_context** mm, void** ptr,
                                           size_t* len, size_t* capacity,
                                           size_t size, const void* fill,
                                           size_t elem_size) {
  size_t old = *len;
  JXL_RETURN_IF_ERROR(
      jxl_array_resize_fields(mm, ptr, len, capacity, size, elem_size));
  for (size_t i = old; i < size; ++i) {
    memcpy((char*)(*ptr) + i * elem_size, fill, elem_size);
  }
  return jxl_ok_status();
}

static inline jxl_status jxl_array_push_back_copy_fields(jxl_context** mm, void** ptr,
                                             size_t* len, size_t* capacity,
                                             const void* value,
                                             size_t elem_size) {
  jxl_status st = jxl_array_push_back_prep_fields(mm, ptr, len, capacity, elem_size);
  if (!jxl_status_ok(st)) return st;
  memcpy((char*)(*ptr) + (*len) * elem_size, value, elem_size);
  (*len) += 1;
  return jxl_ok_status();
}

/* void** of TYPE* field: cast through void* to avoid pedantic alias warnings. */
#define JXL_ARRAY_PTR_SLOT(a) ((void**)(void*)&(a)->ptr)

#define jxl_array_destroy(a)                                                        \
  jxl_array_destroy_fields(&(a)->ctx, JXL_ARRAY_PTR_SLOT(a), &(a)->len,   \
                     &(a)->capacity)
#define jxl_array_swap(a, b)                                                        \
  jxl_array_swap_fields(&(a)->ctx, JXL_ARRAY_PTR_SLOT(a), &(a)->len,      \
                  &(a)->capacity, &(b)->ctx, JXL_ARRAY_PTR_SLOT(b), \
                  &(b)->len, &(b)->capacity)
#define jxl_array_init(a, mm)                                                       \
  jxl_array_init_fields(&(a)->ctx, JXL_ARRAY_PTR_SLOT(a), &(a)->len,      \
                  &(a)->capacity, (mm))
#define jxl_array_reserve(a, cap)                                                   \
  jxl_array_reserve_fields(&(a)->ctx, JXL_ARRAY_PTR_SLOT(a), &(a)->len,   \
                     &(a)->capacity, (cap), sizeof(*(a)->ptr))
#define jxl_array_resize(a, size)                                                   \
  jxl_array_resize_fields(&(a)->ctx, JXL_ARRAY_PTR_SLOT(a), &(a)->len,    \
                    &(a)->capacity, (size), sizeof(*(a)->ptr))
#define jxl_array_resize_zero(a, size)                                               \
  jxl_array_resize_zero_fields(&(a)->ctx, JXL_ARRAY_PTR_SLOT(a),           \
                        &(a)->len, &(a)->capacity, (size), sizeof(*(a)->ptr))
#define jxl_array_copy_from(a, other)                                                \
  jxl_array_copy_from_fields(&(a)->ctx, JXL_ARRAY_PTR_SLOT(a), &(a)->len,  \
                      &(a)->capacity, &(other)->ctx,                \
                      (void* const*)(const void*)&(other)->ptr, &(other)->len, \
                      sizeof(*(a)->ptr))
#define jxl_array_append(a, begin, count)                                           \
  jxl_array_append_fields(&(a)->ctx, JXL_ARRAY_PTR_SLOT(a), &(a)->len,    \
                    &(a)->capacity, (const void*)(begin), (count),             \
                    sizeof(*(a)->ptr))
#define jxl_array_assign(a, data, count)                                            \
  jxl_array_assign_fields(&(a)->ctx, JXL_ARRAY_PTR_SLOT(a), &(a)->len,    \
                    &(a)->capacity, (const void*)(data), (count),              \
                    sizeof(*(a)->ptr))
#define jxl_array_erase(a, first, last)                                             \
  jxl_array_erase_fields(JXL_ARRAY_PTR_SLOT(a), &(a)->len, (void*)(first),           \
                   (void*)(last), sizeof(*(a)->ptr))

#ifdef __cplusplus
#define JXL_ARRAY_PUSHBACK_OVERLOAD(NAME, TYPE)                                \
  static inline jxl_status jxl_array_push_back(NAME* a, TYPE value) {                    \
    return NAME##_push_back(a, value);                                           \
  }
#define JXL_ARRAY_RESIZEFILL_OVERLOAD(NAME, TYPE)                              \
  static inline jxl_status jxl_array_resize_fill(NAME* a, size_t size, TYPE fill) {      \
    return NAME##_resize_fill(a, size, fill);                                    \
  }
#else
#define JXL_ARRAY_PUSHBACK_OVERLOAD(NAME, TYPE)
#define JXL_ARRAY_RESIZEFILL_OVERLOAD(NAME, TYPE)
#endif

/* Typed PushBack / ResizeFill: C++ overloads; C uses NAME##* (C99, no _Generic). */
#define JXL_DEFINE_POD_ARRAY(NAME, TYPE)                                       \
  typedef struct NAME {                                                        \
    jxl_context* ctx;                                          \
    TYPE* ptr;                                                                 \
    size_t len;                                                                \
    size_t capacity;                                                           \
  } NAME;                                                                      \
  static inline jxl_status NAME##_push_back(NAME* a, TYPE value) {                   \
    return jxl_array_push_back_copy_fields(&(a)->ctx, JXL_ARRAY_PTR_SLOT(a),\
                                   &(a)->len, &(a)->capacity, &value,          \
                                   sizeof(TYPE));                              \
  }                                                                            \
  static inline jxl_status NAME##_resize_fill(NAME* a, size_t size, TYPE fill) {     \
    return jxl_array_resize_fill_fields(&(a)->ctx, JXL_ARRAY_PTR_SLOT(a),  \
                                 &(a)->len, &(a)->capacity, size, &fill,       \
                                 sizeof(TYPE));                                \
  }                                                                            \
  JXL_ARRAY_PUSHBACK_OVERLOAD(NAME, TYPE)                                      \
  JXL_ARRAY_RESIZEFILL_OVERLOAD(NAME, TYPE)

JXL_DEFINE_POD_ARRAY(jxl_array_u8, uint8_t)
JXL_DEFINE_POD_ARRAY(jxl_array_u16, uint16_t)
JXL_DEFINE_POD_ARRAY(jxl_array_u32, uint32_t)
JXL_DEFINE_POD_ARRAY(jxl_array_u64, uint64_t)
JXL_DEFINE_POD_ARRAY(jxl_array_i16, int16_t)
JXL_DEFINE_POD_ARRAY(jxl_array_i32, int32_t)
JXL_DEFINE_POD_ARRAY(jxl_array_i64, int64_t)
JXL_DEFINE_POD_ARRAY(jxl_array_size, size_t)
JXL_DEFINE_POD_ARRAY(jxl_array_int, int)
JXL_DEFINE_POD_ARRAY(jxl_array_float, float)
JXL_DEFINE_POD_ARRAY(jxl_array_char, char)
/* JXL_DEFINE_POD_ARRAY stays defined for type-local peels. */

static inline uint8_t jxl_u8_max_element(const jxl_array_u8* a) {
  JXL_DASSERT(!jxl_array_empty(a));
  uint8_t m = *jxl_array_at_const(a, 0);
  for (size_t i = 1; i < jxl_array_len(a); ++i) {
    if (*jxl_array_at_const(a, i) > m) m = *jxl_array_at_const(a, i);
  }
  return m;
}

static inline int32_t jxl_i32_min_element(const jxl_array_i32* a) {
  JXL_DASSERT(!jxl_array_empty(a));
  int32_t m = *jxl_array_at_const(a, 0);
  for (size_t i = 1; i < jxl_array_len(a); ++i) {
    if (*jxl_array_at_const(a, i) < m) m = *jxl_array_at_const(a, i);
  }
  return m;
}

static inline uint8_t* jxl_u8_find(uint8_t* begin, uint8_t* end, uint8_t value) {
  for (uint8_t* p = begin; p != end; ++p) {
    if (*p == value) return p;
  }
  return end;
}

static inline const uint8_t* jxl_u8_find_const(const uint8_t* begin,
                                         const uint8_t* end, uint8_t value) {
  for (const uint8_t* p = begin; p != end; ++p) {
    if (*p == value) return p;
  }
  return end;
}

static inline uint32_t* jxl_u32_remove(uint32_t* begin, uint32_t* end,
                                  uint32_t value) {
  uint32_t* dest = begin;
  for (uint32_t* p = begin; p != end; ++p) {
    if (*p != value) {
      if (dest != p) *dest = *p;
      ++dest;
    }
  }
  return dest;
}

static inline uint64_t jxl_u32_accumulate(const uint32_t* begin, const uint32_t* end,
                                     uint64_t init) {
  for (const uint32_t* p = begin; p != end; ++p) init = init + *p;
  return init;
}

static inline bool jxl_u8_array_equal(const jxl_array_u8* a, const uint8_t* b, size_t n) {
  if (jxl_array_len(a) != n) return false;
  for (size_t i = 0; i < n; ++i) {
    if (*jxl_array_at_const(a, i) != b[i]) return false;
  }
  return true;
}

static inline void jxl_i32_swap_at(int32_t* a, size_t i, size_t j) {
  int32_t tmp = a[i];
  a[i] = a[j];
  a[j] = tmp;
}

static inline void jxl_i32_sift_down(int32_t* a, size_t len, size_t hole) {
  for (;;) {
    size_t child = 2 * hole + 1;
    if (child >= len) break;
    size_t right = child + 1;
    if (right < len && a[child] < a[right]) child = right;
    if (!(a[hole] < a[child])) break;
    jxl_i32_swap_at(a, hole, child);
    hole = child;
  }
}

static inline void jxl_i32_sort(jxl_array_i32* arr) {
  int32_t* a = jxl_array_data(arr);
  size_t n = jxl_array_len(arr);
  if (n < 2) return;
  for (size_t i = n / 2; i > 0; --i) {
    jxl_i32_sift_down(a, n, i - 1);
  }
  while (n > 1) {
    jxl_i32_swap_at(a, 0, n - 1);
    --n;
    jxl_i32_sift_down(a, n, 0);
  }
}

static inline size_t jxl_i32_unique(int32_t* begin, int32_t* end) {
  size_t n = (size_t)(end - begin);
  if (n < 2) return n;
  size_t w = 1;
  for (size_t r = 1; r < n; ++r) {
    if (!(begin[r] == begin[w - 1])) {
      begin[w++] = begin[r];
    }
  }
  return w;
}

static inline size_t jxl_i32_unique_in_array(jxl_array_i32* a) {
  return jxl_i32_unique(jxl_array_data(a), jxl_array_data(a) + jxl_array_len(a));
}

#endif  // LIB_JXL_BASE_ARRAY_H_
