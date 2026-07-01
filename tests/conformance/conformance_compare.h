// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_CONFORMANCE_COMPARE_H_
#define JXL_CONFORMANCE_COMPARE_H_

#include "conformance_npy.h"
#include "jxl_oxide/jxl_oxide.h"

/* Compare one keyframe. Returns 0 on match, 1 on pixel mismatch, -1 on error. */
int jxl_conformance_compare_render(const jxl_conformance_npy *reference, uint32_t frame_index,
                                   const jxl_render *render, float peak_limit, float rmse_limit,
                                   const char *case_name);

#endif /* JXL_CONFORMANCE_COMPARE_H_ */
