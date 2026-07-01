// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_RENDER_ERROR_H_
#define JXL_RENDER_ERROR_H_

typedef enum {
    JXL_RENDER_OK = 0,
    JXL_RENDER_ERROR,
    JXL_RENDER_INCOMPLETE_FRAME,
    JXL_RENDER_OUT_OF_MEMORY,
    JXL_RENDER_VALIDATION_ERROR,
} jxl_render_status_t;

#endif /* JXL_RENDER_ERROR_H_ */
