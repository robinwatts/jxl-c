// SPDX-License-Identifier: MIT OR Apache-2.0
#include "error.h"

const char *jxl_bs_status_string(jxl_bs_status_t status) {
    switch (status) {
    case JXL_BS_OK:
        return "ok";
    case JXL_BS_EOF:
        return "unexpected end of bitstream";
    case JXL_BS_INVALID_BOX:
        return "invalid container";
    case JXL_BS_NON_ZERO_PADDING:
        return "PadZeroToByte() read non-zero bits";
    case JXL_BS_INVALID_FLOAT:
        return "F16() read NaN or Infinity";
    case JXL_BS_INVALID_ENUM:
        return "invalid enum value";
    case JXL_BS_VALIDATION_FAILED:
        return "bitstream validation failed";
    case JXL_BS_PROFILE_CONFORMANCE:
        return "not supported by current profile";
    case JXL_BS_CANNOT_SKIP:
        return "target bookmark already passed";
    case JXL_BS_NOT_ALIGNED:
        return "bitstream is unaligned";
    }
    return "unknown bitstream error";
}
