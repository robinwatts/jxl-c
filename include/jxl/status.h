// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_STATUS_H_
#define JXL_STATUS_H_

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Shared success / progress / error codes for decode and encode APIs.
 *
 * Progress (non-fatal): JXL_NEED_MORE_DATA (decode input), JXL_NEED_MORE_OUTPUT
 * (encode output). Everything else with JXL_ERROR_* prefix is a hard failure.
 */
typedef enum {
    JXL_OK = 0,
    JXL_NEED_MORE_DATA,
    JXL_NEED_MORE_OUTPUT,
    JXL_ERROR_INVALID_INPUT,
    JXL_ERROR_UNSUPPORTED,
    JXL_ERROR_ANIMATION_NOT_SUPPORTED,
    JXL_ERROR_OUT_OF_MEMORY,
    JXL_ERROR_LIMIT_EXCEEDED,
    JXL_ERROR_GENERIC,
    JXL_ERROR_API_USAGE,
    JXL_ERROR_JBRD,
} jxl_status_t;

const char *jxl_status_string(jxl_status_t status);

#ifdef __cplusplus
}
#endif

#endif /* JXL_STATUS_H_ */
