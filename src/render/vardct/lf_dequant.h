// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_RENDER_VARDCT_LF_DEQUANT_H_
#define JXL_RENDER_VARDCT_LF_DEQUANT_H_

#include "render/subgrid_f32.h"
#include "vardct/hf_coeff.h"
#include "vardct/lf.h"

#include "jxl_oxide/jxl_types.h"

void jxl_copy_lf_dequant(jxl_subgrid_f32 grid, const jxl_quantizer *quantizer, float m_lf,
                         const jxl_lf_quant_subgrid_u32 *channel_data, uint8_t extra_precision);

#endif /* JXL_RENDER_VARDCT_LF_DEQUANT_H_ */
