// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_JBR_RECONSTRUCT_H_
#define JXL_JBR_RECONSTRUCT_H_

#include "allocator.h"
#include "context.h"
#include "frame/frame.h"
#include "image/image_internal.h"
#include "jbr/data.h"
#include "jbr/error.h"
#include "jbr/output.h"

#include <stddef.h>
#include "jxl_oxide/jxl_types.h"

jxl_jbr_status jxl_jbr_reconstruct(jxl_allocator_state *alloc, jxl_context *ctx,
                                   const jxl_jbr_data *jbrd, const jxl_frame *frame,
                                   const jxl_parsed_image_header *image, const uint8_t *icc,
                                   size_t icc_len, const uint8_t *exif, size_t exif_len,
                                   const uint8_t *xmp, size_t xmp_len, jxl_jbr_output *out);

#endif /* JXL_JBR_RECONSTRUCT_H_ */
