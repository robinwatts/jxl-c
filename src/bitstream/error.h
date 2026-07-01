// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_BITSTREAM_ERROR_H_
#define JXL_BITSTREAM_ERROR_H_

typedef enum {
    JXL_BS_OK = 0,
    JXL_BS_EOF,
    JXL_BS_INVALID_BOX,
    JXL_BS_NON_ZERO_PADDING,
    JXL_BS_INVALID_FLOAT,
    JXL_BS_INVALID_ENUM,
    JXL_BS_VALIDATION_FAILED,
    JXL_BS_PROFILE_CONFORMANCE,
    JXL_BS_CANNOT_SKIP,
    JXL_BS_NOT_ALIGNED,
} jxl_bs_status_t;

const char *jxl_bs_status_string(jxl_bs_status_t status);

#endif /* JXL_BITSTREAM_ERROR_H_ */
