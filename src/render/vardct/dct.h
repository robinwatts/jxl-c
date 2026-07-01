// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_RENDER_VARDCT_DCT_H_
#define JXL_RENDER_VARDCT_DCT_H_

#include "render/vardct/dct_common.h"

#include <stddef.h>

void jxl_dct_1d(float *io, size_t n, float *scratch, jxl_dct_direction direction);

#endif /* JXL_RENDER_VARDCT_DCT_H_ */
