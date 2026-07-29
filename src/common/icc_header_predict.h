// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_COMMON_ICC_HEADER_PREDICT_H_
#define JXL_COMMON_ICC_HEADER_PREDICT_H_

#include "compiler.h"

#include <stddef.h>
#include <stdint.h>

#include "common/icc_codec_params.h"

/* Predict ICC header byte at `idx` from output size and previously decoded /
 * original header bytes (`header` must be readable for the adaptive rules). */

jxl_inline uint8_t jxl_icc_predict_header_byte(size_t idx, uint32_t output_size,
                                               const uint8_t *header) {
  switch (idx) {
  case 0:
    return (uint8_t)(output_size >> 24);
  case 1:
    return (uint8_t)(output_size >> 16);
  case 2:
    return (uint8_t)(output_size >> 8);
  case 3:
    return (uint8_t)output_size;
  case 8:
    return 4;
  case 12:
    return 'm';
  case 13:
    return 'n';
  case 14:
    return 't';
  case 15:
    return 'r';
  case 16:
    return 'R';
  case 17:
    return 'G';
  case 18:
    return 'B';
  case 19:
    return ' ';
  case 20:
    return 'X';
  case 21:
    return 'Y';
  case 22:
    return 'Z';
  case 23:
    return ' ';
  case 36:
    return 'a';
  case 37:
    return 'c';
  case 38:
    return 's';
  case 39:
    return 'p';
  case 41:
    if (header[40] == 'A') {
      return 'P';
    }
    if (header[40] == 'M') {
      return 'S';
    }
    return 0;
  case 42:
    if (header[40] == 'A') {
      return 'P';
    }
    if (header[40] == 'M') {
      return 'F';
    }
    if (header[40] == 'S' && header[41] == 'G') {
      return 'I';
    }
    if (header[40] == 'S' && header[41] == 'U') {
      return 'N';
    }
    return 0;
  case 43:
    if (header[40] == 'A') {
      return 'L';
    }
    if (header[40] == 'M') {
      return 'T';
    }
    if (header[40] == 'S' && header[41] == 'G') {
      return ' ';
    }
    if (header[40] == 'S' && header[41] == 'U') {
      return 'W';
    }
    return 0;
  case 70:
    return 246;
  case 71:
    return 214;
  case 73:
    return 1;
  case 78:
    return 211;
  case 79:
    return 45;
  case 80:
  case 81:
  case 82:
  case 83:
    return header[idx - 76];
  default:
    return 0;
  }
}

/* Static template used by the encoder before adaptive `jxl_icc_predict_header`
 * patches; size bytes 0..3 are filled by the caller. Matches decode defaults. */
static const uint8_t jxl_icc_initial_header_prediction_bytes[JXL_ICC_HEADER_SIZE] = {
    0,   0,   0,   0,   0,   0,   0,   0,   4, 0, 0, 0, 'm', 'n', 't', 'r',
    'R', 'G', 'B', ' ', 'X', 'Y', 'Z', ' ', 0, 0, 0, 0, 0,   0,   0,   0,
    0,   0,   0,   0,   'a', 'c', 's', 'p', 0, 0, 0, 0, 0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0, 0, 0, 0, 0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   246, 214, 0, 1, 0, 0, 0,   0,   211, 45,
    0,   0,   0,   0,   0,   0,   0,   0,   0, 0, 0, 0, 0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0, 0, 0, 0, 0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0, 0, 0, 0, 0,   0,   0,   0,
};

#endif /* JXL_COMMON_ICC_HEADER_PREDICT_H_ */
