// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef LIB_JXL_BASE_MATRIX_OPS_H_
#define LIB_JXL_BASE_MATRIX_OPS_H_

// 3x3 matrix operations.

#include <math.h>
#include <stddef.h>

#include "base/compiler_specific.h"
#include "base/enc_status.h"

typedef struct jxl_vector3 {
  float v[3];
} jxl_vector3;

static inline float* jxl_vector3_at(jxl_vector3* self, size_t i) { return &self->v[i]; }
static inline const float* jxl_vector3_at_const(const jxl_vector3* self, size_t i) {
  return &self->v[i];
}

static inline jxl_vector3 jxl_vector3_make(float a, float b, float c) {
  jxl_vector3 self;
  self.v[0] = a;
  self.v[1] = b;
  self.v[2] = c;
  return self;
}

typedef struct jxl_matrix3x3 {
  float m[3][3];
} jxl_matrix3x3;

static inline float* jxl_matrix3x3_at(jxl_matrix3x3* self, size_t i) {
  return self->m[i];
}
static inline const float* jxl_matrix3x3_at_const(const jxl_matrix3x3* self, size_t i) {
  return self->m[i];
}

static inline void jxl_matrix3x3_construct_empty(jxl_matrix3x3* self) {
  size_t y;
  size_t x;
  for (y = 0; y < 3; ++y) {
    for (x = 0; x < 3; ++x) {
      jxl_matrix3x3_at(self, y)[x] = 0;
    }
  }
}

static inline void jxl_matrix3x3_set_row(jxl_matrix3x3* self, size_t row, float a,
                                   float b, float c) {
  jxl_matrix3x3_at(self, row)[0] = a;
  jxl_matrix3x3_at(self, row)[1] = b;
  jxl_matrix3x3_at(self, row)[2] = c;
}

static inline void jxl_matrix3x3_set_diagonal(jxl_matrix3x3* self, float d0, float d1,
                                        float d2) {
  jxl_matrix3x3_construct_empty(self);
  jxl_matrix3x3_at(self, 0)[0] = d0;
  jxl_matrix3x3_at(self, 1)[1] = d1;
  jxl_matrix3x3_at(self, 2)[2] = d2;
}

static inline jxl_matrix3x3 jxl_matrix3x3_make_rows(float r00, float r01, float r02,
                                          float r10, float r11, float r12,
                                          float r20, float r21, float r22) {
  jxl_matrix3x3 m;
  jxl_matrix3x3_set_row(&m, 0, r00, r01, r02);
  jxl_matrix3x3_set_row(&m, 1, r10, r11, r12);
  jxl_matrix3x3_set_row(&m, 2, r20, r21, r22);
  return m;
}

// Computes C = A * B, where A, B, C are 3x3 matrices.
static inline void jxl_mul3x3_matrix(const jxl_matrix3x3* a, const jxl_matrix3x3* b,
                                jxl_matrix3x3* c) {
  size_t x;
  size_t y;
  for (x = 0; x < 3; x++) {
    JXL_ALIGNAS(16) double temp[3] = {(double)(jxl_matrix3x3_at_const(b, 0)[x]),
                                  (double)(jxl_matrix3x3_at_const(b, 1)[x]),
                                  (double)(jxl_matrix3x3_at_const(b, 2)[x])};
    for (y = 0; y < 3; y++) {
      jxl_matrix3x3_at(c, y)[x] = jxl_matrix3x3_at_const(a, y)[0] * temp[0] +
                             jxl_matrix3x3_at_const(a, y)[1] * temp[1] +
                             jxl_matrix3x3_at_const(a, y)[2] * temp[2];
    }
  }
}

// Computes C = A * B, where A is 3x3 matrix and B is vector.
static inline void jxl_mul3x3_vector(const jxl_matrix3x3* a, const jxl_vector3* b,
                                jxl_vector3* c) {
  size_t y;
  size_t x;
  for (y = 0; y < 3; y++) {
    double e = 0;
    for (x = 0; x < 3; x++) {
      e += (double)(jxl_matrix3x3_at_const(a, y)[x]) * *jxl_vector3_at_const(b, x);
    }
    *jxl_vector3_at(c, y) = e;
  }
}

// Inverts a 3x3 matrix in place.
static inline jxl_enc_status jxl_inv3x3_matrix(jxl_matrix3x3* matrix) {
  // Intermediate computation is done in double precision.
  double temp[3][3];
  size_t j;
  size_t i;
  double det;
  double idet;
  temp[0][0] = (double)(jxl_matrix3x3_at(matrix, 1)[1]) * jxl_matrix3x3_at(matrix, 2)[2] -
               (double)(jxl_matrix3x3_at(matrix, 1)[2]) * jxl_matrix3x3_at(matrix, 2)[1];
  temp[0][1] = (double)(jxl_matrix3x3_at(matrix, 0)[2]) * jxl_matrix3x3_at(matrix, 2)[1] -
               (double)(jxl_matrix3x3_at(matrix, 0)[1]) * jxl_matrix3x3_at(matrix, 2)[2];
  temp[0][2] = (double)(jxl_matrix3x3_at(matrix, 0)[1]) * jxl_matrix3x3_at(matrix, 1)[2] -
               (double)(jxl_matrix3x3_at(matrix, 0)[2]) * jxl_matrix3x3_at(matrix, 1)[1];
  temp[1][0] = (double)(jxl_matrix3x3_at(matrix, 1)[2]) * jxl_matrix3x3_at(matrix, 2)[0] -
               (double)(jxl_matrix3x3_at(matrix, 1)[0]) * jxl_matrix3x3_at(matrix, 2)[2];
  temp[1][1] = (double)(jxl_matrix3x3_at(matrix, 0)[0]) * jxl_matrix3x3_at(matrix, 2)[2] -
               (double)(jxl_matrix3x3_at(matrix, 0)[2]) * jxl_matrix3x3_at(matrix, 2)[0];
  temp[1][2] = (double)(jxl_matrix3x3_at(matrix, 0)[2]) * jxl_matrix3x3_at(matrix, 1)[0] -
               (double)(jxl_matrix3x3_at(matrix, 0)[0]) * jxl_matrix3x3_at(matrix, 1)[2];
  temp[2][0] = (double)(jxl_matrix3x3_at(matrix, 1)[0]) * jxl_matrix3x3_at(matrix, 2)[1] -
               (double)(jxl_matrix3x3_at(matrix, 1)[1]) * jxl_matrix3x3_at(matrix, 2)[0];
  temp[2][1] = (double)(jxl_matrix3x3_at(matrix, 0)[1]) * jxl_matrix3x3_at(matrix, 2)[0] -
               (double)(jxl_matrix3x3_at(matrix, 0)[0]) * jxl_matrix3x3_at(matrix, 2)[1];
  temp[2][2] = (double)(jxl_matrix3x3_at(matrix, 0)[0]) * jxl_matrix3x3_at(matrix, 1)[1] -
               (double)(jxl_matrix3x3_at(matrix, 0)[1]) * jxl_matrix3x3_at(matrix, 1)[0];
  det = jxl_matrix3x3_at(matrix, 0)[0] * temp[0][0] +
        jxl_matrix3x3_at(matrix, 0)[1] * temp[1][0] +
        jxl_matrix3x3_at(matrix, 0)[2] * temp[2][0];
  if (fabs(det) < 1e-10) {
    return JXL_FAILURE("Matrix determinant is too close to 0");
  }
  idet = 1.0 / det;
  for (j = 0; j < 3; j++) {
    for (i = 0; i < 3; i++) {
      jxl_matrix3x3_at(matrix, j)[i] = temp[j][i] * idet;
    }
  }
  return jxl_enc_ok_status();
}

#endif  // LIB_JXL_BASE_MATRIX_OPS_H_
