// SPDX-License-Identifier: MIT OR Apache-2.0
#include "jxl/status.h"

const char *jxl_status_string(jxl_status_t status) {
    switch (status) {
    case JXL_OK:
        return "ok";
    case JXL_NEED_MORE_DATA:
        return "need more data";
    case JXL_NEED_MORE_OUTPUT:
        return "need more output";
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
    case JXL_ERROR_GENERIC:
        return "generic error";
    case JXL_ERROR_API_USAGE:
        return "api usage error";
    case JXL_ERROR_JBRD:
        return "jpeg bitstream reconstruction error";
    }
    return "unknown status";
}
