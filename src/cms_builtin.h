// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_CMS_BUILTIN_H_
#define JXL_CMS_BUILTIN_H_

#include <jxl/cms.h>

/* user_data is the jxl_context that owns the built-in adapter. */
jxl_status_t jxl_cms_builtin_transform(void *user_data, const uint8_t *src_icc, size_t src_len,
                                       const uint8_t *dst_icc, size_t dst_len, float **planes,
                                       size_t num_planes, size_t num_pixels);

#if defined(JXL_C_ENABLE_JPEG_ENCODER)
JXL_BOOL jxl_cms_builtin_set_fields(void *user_data, const uint8_t *icc, size_t size,
                                    jxl_color_encoding *out, JXL_BOOL *cmyk);
#endif

#endif /* JXL_CMS_BUILTIN_H_ */
