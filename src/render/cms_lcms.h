// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_OXIDE_CMS_LCMS_H_
#define JXL_OXIDE_CMS_LCMS_H_

#include "allocator.h"
#include "jxl_oxide/jxl_status.h"

#include <stddef.h>
#include "jxl_oxide/jxl_types.h"

/* Linear sRGB (D65) float RGB [0,1] -> target ICC profile. */
jxl_status_t jxl_cms_transform_linear_srgb_to_icc(jxl_allocator_state *alloc, float *r, float *g,
                                                  float *b, size_t num_pixels,
                                                  const uint8_t *dst_icc, size_t dst_icc_len);

/* Planar float [0,1] through ICC profile transform (matches jxl-oxide Lcms2). */
jxl_status_t jxl_cms_transform_icc_to_icc(jxl_allocator_state *alloc, float **planes,
                                          uint32_t num_planes, size_t num_pixels,
                                          const uint8_t *src_icc, size_t src_icc_len,
                                          const uint8_t *dst_icc, size_t dst_icc_len);

#endif /* JXL_OXIDE_CMS_LCMS_H_ */
