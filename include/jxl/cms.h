// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in LICENSE-BSD.

#ifndef JXL_CMS_H_
#define JXL_CMS_H_

/*
 * Color management hooked up at jxl_context_create.
 *
 * Pass a jxl_cms in jxl_context_options::cms. NULL selects the built-in LCMS2
 * adapter when LCMS2 is compiled in. The table must outlive the context.
 * Pixel samples are planar float in [0, 1]. For CMYK, 0 is maximum ink and 1
 * is no ink.
 */

#include <jxl/color_encoding.h>
#include <jxl/context.h>
#include <jxl/jxl_export.h>
#include <jxl/status.h>
#include <jxl/types.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef JXL_BOOL (*jxl_cms_set_fields_from_icc_fn)(void *user_data, const uint8_t *icc,
                                                   size_t size, jxl_color_encoding *out,
                                                   JXL_BOOL *cmyk);

typedef jxl_status_t (*jxl_cms_transform_fn)(void *user_data, const uint8_t *src_icc,
                                             size_t src_len, const uint8_t *dst_icc,
                                             size_t dst_len, float **planes, size_t num_planes,
                                             size_t num_pixels);

struct jxl_cms {
    void *user_data;
    jxl_cms_set_fields_from_icc_fn set_fields_from_icc;
    jxl_cms_transform_fn transform;
};

/* Built-in LCMS2 table. user_data is NULL; jxl_context_create installs a copy
 * whose user_data is the context. Passing this pointer in the options is the
 * same as passing NULL. */
JXL_C_EXPORT const jxl_cms *jxl_get_default_cms(void);

/* Opaque LCMS cmsContext. Allocations go through ctx.
 * Destroy with jxl_cms_destroy_lcms_context. Used by the built-in adapter. */
JXL_C_EXPORT void *jxl_cms_create_lcms_context(jxl_context *ctx);
JXL_C_EXPORT void jxl_cms_destroy_lcms_context(void *lcms_context);

#ifdef __cplusplus
}
#endif

#endif /* JXL_CMS_H_ */
