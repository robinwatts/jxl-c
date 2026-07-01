// SPDX-License-Identifier: MIT OR Apache-2.0
#include "cms_lcms.h"

#include "allocator.h"

#include <stddef.h>
#include "jxl_oxide/jxl_types.h"
#include <string.h>

#if defined(JXL_OXIDE_C_HAVE_LCMS2)
#include <lcms2.h>
#include <lcms2_plugin.h>

#include "linear_srgb_icc.inc"

/* Pixel format used by crates/jxl-oxide/src/lcms2.rs (not TYPE_RGB_FLT). */
#define JXL_CMS_PIXEL_FORMAT_RGB_FLT ((cmsUInt32Number)0x40001cu)

static jxl_allocator_state *cms_alloc_state(cmsContext ctx) {
    return (jxl_allocator_state *)cmsGetContextUserData(ctx);
}

static void *cms_lcms_malloc(cmsContext ctx, cmsUInt32Number size) {
    jxl_allocator_state *alloc = cms_alloc_state(ctx);
    if (alloc == NULL || size == 0) {
        return NULL;
    }
    return jxl_alloc(alloc, (size_t)size);
}

static void cms_lcms_free(cmsContext ctx, void *ptr) {
    jxl_allocator_state *alloc = cms_alloc_state(ctx);
    if (alloc == NULL) {
        return;
    }
    jxl_free(alloc, ptr);
}

static void *cms_lcms_realloc(cmsContext ctx, void *ptr, cmsUInt32Number new_size) {
    jxl_allocator_state *alloc = cms_alloc_state(ctx);
    if (alloc == NULL) {
        return NULL;
    }
    return jxl_realloc(alloc, ptr, (size_t)new_size);
}

static void *cms_lcms_malloc_zero(cmsContext ctx, cmsUInt32Number size) {
    jxl_allocator_state *alloc = cms_alloc_state(ctx);
    if (alloc == NULL || size == 0) {
        return NULL;
    }
    return jxl_calloc(alloc, 1, (size_t)size);
}

static void *cms_lcms_calloc(cmsContext ctx, cmsUInt32Number num, cmsUInt32Number size) {
    jxl_allocator_state *alloc = cms_alloc_state(ctx);
    if (alloc == NULL || num == 0 || size == 0) {
        return NULL;
    }
    return jxl_calloc(alloc, (size_t)num, (size_t)size);
}

static void *cms_lcms_dup(cmsContext ctx, const void *org, cmsUInt32Number size) {
    void *copy = cms_lcms_malloc(ctx, size);
    if (copy != NULL && org != NULL && size > 0) {
        memcpy(copy, org, (size_t)size);
    }
    return copy;
}

static cmsPluginMemHandler k_jxl_cms_mem_plugin = {
    {cmsPluginMagicNumber, LCMS_VERSION, cmsPluginMemHandlerSig, NULL},
    cms_lcms_malloc,
    cms_lcms_free,
    cms_lcms_realloc,
    cms_lcms_malloc_zero,
    cms_lcms_calloc,
    cms_lcms_dup,
};

static cmsContext cms_create_jxl_context(jxl_allocator_state *alloc) {
    if (alloc == NULL) {
        return NULL;
    }
    return cmsCreateContext(&k_jxl_cms_mem_plugin, alloc);
}

static cmsUInt32Number cms_pixel_format_channels(unsigned channels) {
    enum { k_base = 0x40001cu };
    return k_base | ((cmsUInt32Number)channels << 3);
}

static int cms_profile_channels(cmsHPROFILE profile) {
    if (profile == NULL) {
        return 0;
    }
    cmsColorSpaceSignature sig = cmsGetColorSpace(profile);
    switch (sig) {
    case cmsSigGrayData:
        return 1;
    case cmsSigRgbData:
        return 3;
    case cmsSigCmykData:
        return 4;
    default:
        return (int)cmsChannelsOf(sig);
    }
}
#endif

jxl_status_t jxl_cms_transform_linear_srgb_to_icc(jxl_allocator_state *alloc, float *r, float *g,
                                                  float *b, size_t num_pixels,
                                                  const uint8_t *dst_icc, size_t dst_icc_len) {
#if !defined(JXL_OXIDE_C_HAVE_LCMS2)
    (void)alloc;
    (void)r;
    (void)g;
    (void)b;
    (void)num_pixels;
    (void)dst_icc;
    (void)dst_icc_len;
    return JXL_ERROR_UNSUPPORTED;
#else
    size_t offset;
    cmsContext ctx;
    cmsHPROFILE src_profile;
    cmsHPROFILE dst_profile;
    int dst_channels;
    cmsHTRANSFORM transform;
    enum { k_chunk = 1024, k_src = 3 };
    float *in_buf;
    float *out_buf;

    if (alloc == NULL || r == NULL || g == NULL || b == NULL || dst_icc == NULL ||
        dst_icc_len == 0 || num_pixels == 0) {
        return JXL_ERROR_INVALID_INPUT;
    }

    ctx = cms_create_jxl_context(alloc);
    if (ctx == NULL) {
        return JXL_ERROR_OUT_OF_MEMORY;
    }

    src_profile =
        cmsOpenProfileFromMemTHR(ctx, k_jxl_linear_srgb_icc, (cmsUInt32Number)k_jxl_linear_srgb_icc_len);
    if (src_profile == NULL) {
        cmsDeleteContext(ctx);
        return JXL_ERROR_INVALID_INPUT;
    }

    dst_profile = cmsOpenProfileFromMemTHR(ctx, dst_icc, dst_icc_len);
    if (dst_profile == NULL) {
        cmsCloseProfile(src_profile);
        cmsDeleteContext(ctx);
        return JXL_ERROR_INVALID_INPUT;
    }

    dst_channels = cms_profile_channels(dst_profile);
    if (dst_channels <= 0) {
        cmsCloseProfile(src_profile);
        cmsCloseProfile(dst_profile);
        cmsDeleteContext(ctx);
        return JXL_ERROR_UNSUPPORTED;
    }

    transform = cmsCreateTransformTHR(
        ctx, src_profile, JXL_CMS_PIXEL_FORMAT_RGB_FLT, dst_profile,
        cms_pixel_format_channels((unsigned)dst_channels), INTENT_PERCEPTUAL, cmsFLAGS_NOCACHE);
    cmsCloseProfile(src_profile);
    cmsCloseProfile(dst_profile);
    if (transform == NULL) {
        cmsDeleteContext(ctx);
        return JXL_ERROR_UNSUPPORTED;
    }

    in_buf = (float *)jxl_alloc(alloc, (size_t)k_chunk * k_src * sizeof(float));
    out_buf =
        (float *)jxl_alloc(alloc, (size_t)k_chunk * (size_t)dst_channels * sizeof(float));
    if (in_buf == NULL || out_buf == NULL) {
        jxl_free(alloc, in_buf);
        jxl_free(alloc, out_buf);
        cmsDeleteTransform(transform);
        cmsDeleteContext(ctx);
        return JXL_ERROR_OUT_OF_MEMORY;
    }

    for (offset = 0; offset < num_pixels; offset += k_chunk) {
        size_t i;
        size_t count = num_pixels - offset;
        if (count > k_chunk) {
            count = k_chunk;
        }
        for (i = 0; i < count; ++i) {
            size_t idx = offset + i;
            in_buf[i * k_src + 0] = r[idx];
            in_buf[i * k_src + 1] = g[idx];
            in_buf[i * k_src + 2] = b[idx];
        }
        cmsDoTransform(transform, in_buf, out_buf, (cmsUInt32Number)count);
        for (i = 0; i < count; ++i) {
            size_t idx = offset + i;
            r[idx] = out_buf[i * (size_t)dst_channels + 0];
            if (dst_channels > 1 && g != NULL) {
                g[idx] = out_buf[i * (size_t)dst_channels + 1];
            }
            if (dst_channels > 2 && b != NULL) {
                b[idx] = out_buf[i * (size_t)dst_channels + 2];
            }
        }
    }

    jxl_free(alloc, in_buf);
    jxl_free(alloc, out_buf);

    cmsDeleteTransform(transform);
    cmsDeleteContext(ctx);
    return JXL_OK;
#endif
}

jxl_status_t jxl_cms_transform_icc_to_icc(jxl_allocator_state *alloc, float **planes,
                                          uint32_t num_planes, size_t num_pixels,
                                          const uint8_t *src_icc, size_t src_icc_len,
                                          const uint8_t *dst_icc, size_t dst_icc_len) {
#if !defined(JXL_OXIDE_C_HAVE_LCMS2)
    (void)alloc;
    (void)planes;
    (void)num_planes;
    (void)num_pixels;
    (void)src_icc;
    (void)src_icc_len;
    (void)dst_icc;
    (void)dst_icc_len;
    return JXL_ERROR_UNSUPPORTED;
#else
    size_t offset;
    cmsContext ctx;
    cmsHPROFILE src_profile;
    cmsHPROFILE dst_profile;
    int src_channels;
    int dst_channels;
    enum { k_chunk = 1024 };
    float *in_buf;
    float *out_buf;

    if (alloc == NULL || planes == NULL || num_planes == 0 || num_pixels == 0 || src_icc == NULL ||
        src_icc_len == 0 || dst_icc == NULL || dst_icc_len == 0) {
        return JXL_ERROR_INVALID_INPUT;
    }

    ctx = cms_create_jxl_context(alloc);
    if (ctx == NULL) {
        return JXL_ERROR_OUT_OF_MEMORY;
    }

    src_profile = cmsOpenProfileFromMemTHR(ctx, src_icc, (cmsUInt32Number)src_icc_len);
    if (src_profile == NULL) {
        cmsDeleteContext(ctx);
        return JXL_ERROR_INVALID_INPUT;
    }
    dst_profile = cmsOpenProfileFromMemTHR(ctx, dst_icc, (cmsUInt32Number)dst_icc_len);
    if (dst_profile == NULL) {
        cmsCloseProfile(src_profile);
        cmsDeleteContext(ctx);
        return JXL_ERROR_INVALID_INPUT;
    }

    src_channels = cms_profile_channels(src_profile);
    dst_channels = cms_profile_channels(dst_profile);
    if (src_channels <= 0 || dst_channels <= 0 ||
        (uint32_t)src_channels > num_planes) {
        cmsCloseProfile(src_profile);
        cmsCloseProfile(dst_profile);
        cmsDeleteContext(ctx);
        return JXL_ERROR_UNSUPPORTED;
    }

    cmsHTRANSFORM transform = cmsCreateTransformTHR(
        ctx, src_profile, cms_pixel_format_channels((unsigned)src_channels), dst_profile,
        cms_pixel_format_channels((unsigned)dst_channels), INTENT_PERCEPTUAL, cmsFLAGS_NOCACHE);
    cmsCloseProfile(src_profile);
    cmsCloseProfile(dst_profile);
    if (transform == NULL) {
        cmsDeleteContext(ctx);
        return JXL_ERROR_UNSUPPORTED;
    }

    in_buf = NULL;
    out_buf = NULL;
    in_buf = (float *)jxl_alloc(alloc, (size_t)k_chunk * (size_t)src_channels * sizeof(float));
    out_buf = (float *)jxl_alloc(alloc, (size_t)k_chunk * (size_t)dst_channels * sizeof(float));
    if (in_buf == NULL || out_buf == NULL) {
        jxl_free(alloc, in_buf);
        jxl_free(alloc, out_buf);
        cmsDeleteTransform(transform);
        cmsDeleteContext(ctx);
        return JXL_ERROR_OUT_OF_MEMORY;
    }

    for (offset = 0; offset < num_pixels; offset += k_chunk) {
        size_t i;
        size_t count = num_pixels - offset;
        if (count > k_chunk) {
            count = k_chunk;
        }
        for (i = 0; i < count; ++i) {
            int c;
            for (c = 0; c < src_channels; ++c) {
                in_buf[i * (size_t)src_channels + (size_t)c] = planes[c][offset + i];
            }
        }
        cmsDoTransform(transform, in_buf, out_buf, (cmsUInt32Number)count);
        for (i = 0; i < count; ++i) {
            int c;
            for (c = 0; c < dst_channels; ++c) {
                if ((uint32_t)c < num_planes && planes[c] != NULL) {
                    planes[c][offset + i] = out_buf[i * (size_t)dst_channels + (size_t)c];
                }
            }
        }
    }

    jxl_free(alloc, in_buf);
    jxl_free(alloc, out_buf);
    cmsDeleteTransform(transform);
    cmsDeleteContext(ctx);
    return JXL_OK;
#endif
}
