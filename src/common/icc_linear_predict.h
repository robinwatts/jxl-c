// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_COMMON_ICC_LINEAR_PREDICT_H_
#define JXL_COMMON_ICC_LINEAR_PREDICT_H_

#include <stddef.h>
#include <stdint.h>

/* Linear prediction for compressed ICC residuals (orders 0–2).
 * `data[start + i]` is the sample being predicted; callers must ensure
 * `start + i >= stride * 4`. */

uint8_t jxl_icc_linear_predict_value(const uint8_t *data, size_t start, size_t i,
                                     size_t stride, size_t width, int order);

#endif /* JXL_COMMON_ICC_LINEAR_PREDICT_H_ */
