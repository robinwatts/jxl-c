// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in LICENSE-BSD.

#ifndef JXL_CMS_H_
#define JXL_CMS_H_

// ICC profiles and color space conversions.

#include <jxl/cms_interface.h>
#include <jxl/context.h>
#include <jxl/jxl_cms_export.h>

#ifdef __cplusplus
extern "C" {
#endif

JXL_CMS_EXPORT const jxl_cms_interface* jxl_get_default_cms();

/* Opaque LCMS cmsContext for a jxl_context. Allocations go through ctx.
 * Destroy with jxl_cms_destroy_lcms_context. */
JXL_CMS_EXPORT void* jxl_cms_create_lcms_context(jxl_context* ctx);
JXL_CMS_EXPORT void jxl_cms_destroy_lcms_context(void* lcms_context);

#ifdef __cplusplus
}
#endif

#endif  // JXL_CMS_H_
