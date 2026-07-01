// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_FRAME_ERROR_H_
#define JXL_FRAME_ERROR_H_

typedef enum {
    JXL_FRAME_OK = 0,
    JXL_FRAME_BITSTREAM_ERROR,
    JXL_FRAME_DECODER_ERROR,
    JXL_FRAME_OUT_OF_MEMORY,
    JXL_FRAME_VALIDATION_ERROR,
} jxl_frame_status_t;

#endif /* JXL_FRAME_ERROR_H_ */
