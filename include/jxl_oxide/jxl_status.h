// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_OXIDE_STATUS_H_
#define JXL_OXIDE_STATUS_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    JXL_OK = 0,
    JXL_NEED_MORE_DATA,
    JXL_ERROR_INVALID_INPUT,
    JXL_ERROR_UNSUPPORTED,
    JXL_ERROR_ANIMATION_NOT_SUPPORTED,
    JXL_ERROR_OUT_OF_MEMORY,
    JXL_ERROR_LIMIT_EXCEEDED,
} jxl_status_t;

const char *jxl_status_string(jxl_status_t status);

#ifdef __cplusplus
}
#endif

#endif /* JXL_OXIDE_STATUS_H_ */
