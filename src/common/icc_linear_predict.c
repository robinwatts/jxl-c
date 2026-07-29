// SPDX-License-Identifier: MIT OR Apache-2.0
#include "common/icc_linear_predict.h"

static uint8_t predict_u8(uint8_t p1, uint8_t p2, uint8_t p3, int order) {
  if (order == 0) return p1;
  if (order == 1) return (uint8_t)(2 * p1 - p2);
  if (order == 2) return (uint8_t)(3 * p1 - 3 * p2 + p3);
  return 0;
}

static uint16_t predict_u16(uint16_t p1, uint16_t p2, uint16_t p3, int order) {
  if (order == 0) return p1;
  if (order == 1) return (uint16_t)(2 * p1 - p2);
  if (order == 2) return (uint16_t)(3 * p1 - 3 * p2 + p3);
  return 0;
}

static uint32_t predict_u32(uint32_t p1, uint32_t p2, uint32_t p3, int order) {
  if (order == 0) return p1;
  if (order == 1) return 2 * p1 - p2;
  if (order == 2) return 3 * p1 - 3 * p2 + p3;
  return 0;
}

static uint32_t load_be32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) |
         (uint32_t)p[3];
}

uint8_t jxl_icc_linear_predict_value(const uint8_t *data, size_t start, size_t i,
                                     size_t stride, size_t width, int order) {
  size_t pos = start + i;
  if (width == 1) {
    uint8_t p1 = data[pos - stride];
    uint8_t p2 = data[pos - stride * 2];
    uint8_t p3 = data[pos - stride * 3];
    return predict_u8(p1, p2, p3, order);
  }
  if (width == 2) {
    size_t p = start + (i & ~(size_t)1);
    uint16_t p1 = (uint16_t)((data[p - stride * 1] << 8) + data[p - stride * 1 + 1]);
    uint16_t p2 = (uint16_t)((data[p - stride * 2] << 8) + data[p - stride * 2 + 1]);
    uint16_t p3 = (uint16_t)((data[p - stride * 3] << 8) + data[p - stride * 3 + 1]);
    uint16_t pred = predict_u16(p1, p2, p3, order);
    return (i & 1) ? (uint8_t)(pred & 255) : (uint8_t)((pred >> 8) & 255);
  }
  {
    size_t p = start + (i & ~(size_t)3);
    uint32_t p1 = load_be32(data + (p - stride));
    uint32_t p2 = load_be32(data + (p - stride * 2));
    uint32_t p3 = load_be32(data + (p - stride * 3));
    uint32_t pred = predict_u32(p1, p2, p3, order);
    unsigned shiftbytes = 3u - (unsigned)(i & 3);
    return (uint8_t)((pred >> (shiftbytes * 8)) & 255);
  }
}
