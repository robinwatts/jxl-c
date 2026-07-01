// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_RENDER_MODULAR_RENDER_H_
#define JXL_RENDER_MODULAR_RENDER_H_

#include "bitstream/bitstream.h"
#include "image/image_internal.h"
#include "render/render_internal.h"

jxl_status_t jxl_render_modular_keyframe(const jxl_keyframe_render_params *params,
                                         const jxl_parsed_image_header *parsed,
                                         const uint8_t *codestream, size_t cs_len, jxl_bs *bs,
                                         jxl_reference_store *refs, jxl_render *r);

#endif /* JXL_RENDER_MODULAR_RENDER_H_ */
