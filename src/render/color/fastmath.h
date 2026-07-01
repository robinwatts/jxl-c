// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_RENDER_COLOR_FASTMATH_H_
#define JXL_RENDER_COLOR_FASTMATH_H_

#include <stddef.h>

typedef struct jxl_context jxl_context;

float jxl_fastmath_rational_eval3(float x, const float p[3], const float q[3]);
float jxl_fastmath_rational_eval5(float x, const float p[5], const float q[5]);
float jxl_fastmath_pow2f(float x);
float jxl_fastmath_log2f(float x);
float jxl_fastmath_powf(float base, float exponent);

void jxl_fastmath_powf_in_place(jxl_context *ctx, float *samples, size_t n, float exponent);

#endif /* JXL_RENDER_COLOR_FASTMATH_H_ */
