// SPDX-License-Identifier: MIT OR Apache-2.0
#include "jxl_oxide/jxl_status.h"

const char *jxl_status_string(jxl_status_t status) {
    switch (status) {
    case JXL_OK:
        return "ok";
    case JXL_NEED_MORE_DATA:
        return "need more data";
    case JXL_ERROR_INVALID_INPUT:
        return "invalid input";
    case JXL_ERROR_UNSUPPORTED:
        return "unsupported";
    case JXL_ERROR_ANIMATION_NOT_SUPPORTED:
        return "animation not supported";
    case JXL_ERROR_OUT_OF_MEMORY:
        return "out of memory";
    case JXL_ERROR_LIMIT_EXCEEDED:
        return "limit exceeded";
    }
    return "unknown status";
}
