// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_COMMON_ICC_CODEC_PARAMS_H_
#define JXL_COMMON_ICC_CODEC_PARAMS_H_

#include <stdint.h>

/* Shared ICC compressed-profile wire constants (ISO/IEC 18181-1). Used by
 * decode (`icc_decode.c`) and the JPEG→JXL encoder. */

enum { JXL_ICC_HEADER_SIZE = 128 };
enum { JXL_ICC_TAG_SIZE = 4 };
enum { JXL_NUM_ICC_CONTEXTS = 41 };

enum {
  JXL_ICC_CMD_INSERT = 1,
  JXL_ICC_CMD_SHUFFLE2 = 2,
  JXL_ICC_CMD_SHUFFLE4 = 3,
  JXL_ICC_CMD_PREDICT = 4,
  JXL_ICC_CMD_XYZ = 10,
  JXL_ICC_CMD_TYPE_START_FIRST = 16
};

enum { JXL_ICC_FLAG_OFFSET = 64, JXL_ICC_FLAG_SIZE = 128 };

enum {
  JXL_ICC_TAG_UNKNOWN = 1,
  JXL_ICC_TAG_TRC = 2,
  JXL_ICC_TAG_XYZ = 3,
  JXL_ICC_TAG_STRING_FIRST = 4
};

enum { JXL_ICC_NUM_COMMON_TAGS = 19, JXL_ICC_NUM_TYPE_STRINGS = 8 };

/* Indexed as jxl_icc_common_tags[tagcode - 2] for tagcode in [2, 20].
 * Codes 2/3 are multi-tag specials (rTRC / rXYZ); 4..20 are single tags. */
static const uint8_t jxl_icc_common_tags[JXL_ICC_NUM_COMMON_TAGS][JXL_ICC_TAG_SIZE] = {
    {'r', 'T', 'R', 'C'}, {'r', 'X', 'Y', 'Z'}, {'c', 'p', 'r', 't'}, {'w', 't', 'p', 't'},
    {'b', 'k', 'p', 't'}, {'r', 'X', 'Y', 'Z'}, {'g', 'X', 'Y', 'Z'}, {'b', 'X', 'Y', 'Z'},
    {'k', 'X', 'Y', 'Z'}, {'r', 'T', 'R', 'C'}, {'g', 'T', 'R', 'C'}, {'b', 'T', 'R', 'C'},
    {'k', 'T', 'R', 'C'}, {'c', 'h', 'a', 'd'}, {'d', 'e', 's', 'c'}, {'c', 'h', 'r', 'm'},
    {'d', 'm', 'n', 'd'}, {'d', 'm', 'd', 'd'}, {'l', 'u', 'm', 'i'},
};

/* Indexed as jxl_icc_type_strings[command - JXL_ICC_CMD_TYPE_START_FIRST]. */
static const uint8_t jxl_icc_type_strings[JXL_ICC_NUM_TYPE_STRINGS][JXL_ICC_TAG_SIZE] = {
    {'X', 'Y', 'Z', ' '}, {'d', 'e', 's', 'c'}, {'t', 'e', 'x', 't'}, {'m', 'l', 'u', 'c'},
    {'p', 'a', 'r', 'a'}, {'c', 'u', 'r', 'v'}, {'s', 'f', '3', '2'}, {'g', 'b', 'd', ' '},
};

#endif /* JXL_COMMON_ICC_CODEC_PARAMS_H_ */
