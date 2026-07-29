// SPDX-License-Identifier: MIT OR Apache-2.0
#include "common/icc_shuffle.h"

void jxl_icc_shuffle2(const uint8_t *in, size_t len, uint8_t *out) {
  size_t idx;
  size_t height = len / 2;
  size_t odd = len % 2;
  size_t o = 0;
  for (idx = 0; idx < height; ++idx) {
    out[o++] = in[idx];
    out[o++] = in[idx + height + odd];
  }
  if (odd != 0) {
    out[o++] = in[height];
  }
}

void jxl_icc_shuffle4(const uint8_t *in, size_t len, uint8_t *out) {
  size_t idx;
  size_t step = len / 4;
  size_t wide_count = len % 4;
  size_t o = 0;
  for (idx = 0; idx < step; ++idx) {
    size_t j;
    size_t base = idx;
    for (j = 0; j < wide_count; ++j) {
      out[o++] = in[base];
      base += step + 1;
    }
    for (j = wide_count; j < 4; ++j) {
      out[o++] = in[base];
      base += step;
    }
  }
  for (idx = 1; idx <= wide_count; ++idx) {
    out[o++] = in[(step + 1) * idx - 1];
  }
}

void jxl_icc_unshuffle(const uint8_t *in, size_t size, size_t width, uint8_t *out) {
  size_t height = (size + width - 1) / width;
  size_t s = 0;
  size_t j = 0;
  size_t i;
  for (i = 0; i < size; i++) {
    out[j] = in[i];
    j += height;
    if (j >= size) {
      j = ++s;
    }
  }
}
