// SPDX-License-Identifier: MIT OR Apache-2.0
#include "render/color/tone_map.h"

#include "render/color/pq_internal.h"
#include "render/color/rec2408.h"
#include "render/color/tone_map_internal.h"
#include "render/simd/features.h"

#include <math.h>

jxl_tone_map_params jxl_tone_map_prep(const jxl_hdr_params *hdr, float target_display_luminance,
                                      float peak_luminance) {
    jxl_tone_map_params params = {0};
    jxl_luminance_nits_range from;
    jxl_luminance_nits_range to;
    from.lo = hdr->min_nits;
    from.hi = peak_luminance;

    to.lo = 0.0f;
    to.hi = target_display_luminance;

    params.eetf = jxl_rec2408_eetf_prep(hdr->intensity_target, from, to);
    params.scale = hdr->intensity_target / target_display_luminance;
    return params;
}

float jxl_color_detect_peak_luminance_base(const float *r, const float *g, const float *b, size_t n,
                                           const float luminances[3]) {
    size_t i;
    float peak;
    float lr;
    float lg;
    float lb;

    if (r == NULL || g == NULL || b == NULL || luminances == NULL || n == 0) {
        return 1.0f;
    }
    lr = luminances[0];
    lg = luminances[1];
    lb = luminances[2];
    peak = 0.0f;
    for (i = 0; i < n; ++i) {
        const float y = r[i] * lr + g[i] * lg + b[i] * lb;
        if (y > peak) {
            peak = y;
        }
    }
    return peak <= 0.0f ? 1.0f : peak;
}

float jxl_color_detect_peak_luminance(jxl_context *ctx, const float *r, const float *g,
                                      const float *b, size_t n, const float luminances[3]) {
    const jxl_cpu_features *feat;
    if (r == NULL || g == NULL || b == NULL || luminances == NULL || n == 0) {
        return 1.0f;
    }
    feat = jxl_context_cpu_features(ctx);
#if defined(JXL_HAVE_SIMD_NEON)
    if (feat->neon) {
        return jxl_color_detect_peak_luminance_neon(r, g, b, n, luminances);
    }
#endif
#if defined(JXL_HAVE_SIMD_AVX2)
    if (feat->fma && feat->sse41 && feat->avx2) {
        return jxl_color_detect_peak_luminance_x86_avx2(r, g, b, n, luminances);
    }
#endif
#if defined(JXL_HAVE_SIMD_SSE41)
    if (feat->sse41) {
        return jxl_color_detect_peak_luminance_x86_sse2(r, g, b, n, luminances);
    }
#endif
    return jxl_color_detect_peak_luminance_base(r, g, b, n, luminances);
}

void jxl_color_tone_map_base(float *r, float *g, float *b, size_t n, const jxl_hdr_params *hdr,
                             const jxl_tone_map_params *params) {
    size_t i;
    float lr;
    float lg;
    float lb;
    float intensity_target;

    if (r == NULL || g == NULL || b == NULL || hdr == NULL || params == NULL || n == 0) {
        return;
    }
    lr = hdr->luminances[0];
    lg = hdr->luminances[1];
    lb = hdr->luminances[2];
    intensity_target = hdr->intensity_target;

    for (i = 0; i < n; ++i) {
        const float y = r[i] * lr + g[i] * lg + b[i] * lb;
        const float y_pq = jxl_color_linear_to_pq_sample(y, intensity_target);
        const float y_mapped = jxl_rec2408_eetf_with_params(
            y_pq, &params->eetf);
        const float y_linear = jxl_color_pq_to_linear_sample(y_mapped, intensity_target);
        const float ratio =
            fabsf(y) <= 1e-7f ? y_linear * params->scale : (y_linear / y) * params->scale;
        r[i] *= ratio;
        g[i] *= ratio;
        b[i] *= ratio;
    }
}

void jxl_color_tone_map_luma_base(float *luma, size_t n, const jxl_tone_map_params *params,
                                  float intensity_target) {
    size_t i;

    if (luma == NULL || params == NULL || n == 0) {
        return;
    }
    for (i = 0; i < n; ++i) {
        const float y_pq = jxl_color_linear_to_pq_sample(luma[i], intensity_target);
        const float y_mapped = jxl_rec2408_eetf_with_params(y_pq, &params->eetf);
        const float y_linear = jxl_color_pq_to_linear_sample(y_mapped, intensity_target);
        luma[i] = y_linear * params->scale;
    }
}

static void tone_map_dispatch(const jxl_cpu_features *feat, float *r, float *g, float *b, size_t n,
                              const jxl_hdr_params *hdr, const jxl_tone_map_params *params) {
#if defined(JXL_HAVE_SIMD_NEON)
    if (feat->neon) {
        jxl_color_tone_map_neon(r, g, b, n, hdr, params);
        return;
    }
#endif
#if defined(JXL_HAVE_SIMD_AVX2)
    if (feat->fma && feat->sse41) {
        if (feat->avx2) {
            jxl_color_tone_map_x86_avx2(r, g, b, n, hdr, params);
        } else {
            jxl_color_tone_map_x86_fma(r, g, b, n, hdr, params);
        }
        return;
    }
#endif
#if defined(JXL_HAVE_SIMD_SSE41)
    jxl_color_tone_map_x86_sse2(r, g, b, n, hdr, params);
    return;
#endif
    jxl_color_tone_map_base(r, g, b, n, hdr, params);
}

static void tone_map_luma_dispatch(const jxl_cpu_features *feat, float *luma, size_t n,
                                   float intensity_target, const jxl_tone_map_params *params) {
#if defined(JXL_HAVE_SIMD_NEON)
    if (feat->neon) {
        jxl_color_tone_map_luma_neon(luma, n, intensity_target, params);
        return;
    }
#endif
#if defined(JXL_HAVE_SIMD_AVX2)
    if (feat->fma && feat->sse41) {
        if (feat->avx2) {
            jxl_color_tone_map_luma_x86_avx2(luma, n, intensity_target, params);
        } else {
            jxl_color_tone_map_luma_x86_fma(luma, n, intensity_target, params);
        }
        return;
    }
#endif
#if defined(JXL_HAVE_SIMD_SSE41)
    jxl_color_tone_map_luma_x86_sse2(luma, n, intensity_target, params);
    return;
#endif
    jxl_color_tone_map_luma_base(luma, n, params, intensity_target);
}

void jxl_color_tone_map(jxl_context *ctx, float *r, float *g, float *b, size_t n,
                        const jxl_hdr_params *hdr, float target_display_luminance, int detect_peak) {
    const jxl_cpu_features *feat;
    float peak_luminance;
    jxl_tone_map_params params;

    if (r == NULL || g == NULL || b == NULL || hdr == NULL || n == 0 ||
        target_display_luminance <= 0.0f) {
        return;
    }

    feat = jxl_context_cpu_features(ctx);
    peak_luminance = hdr->intensity_target;
    if (detect_peak) {
        peak_luminance =
            fminf(hdr->intensity_target,
                  jxl_color_detect_peak_luminance(ctx, r, g, b, n, hdr->luminances) *
                      hdr->intensity_target);
    }

    params = jxl_tone_map_prep(hdr, target_display_luminance, peak_luminance);
    tone_map_dispatch(feat, r, g, b, n, hdr, &params);
}

void jxl_color_tone_map_luma(jxl_context *ctx, float *luma, size_t n, const jxl_hdr_params *hdr,
                             float target_display_luminance, int detect_peak) {
    const jxl_cpu_features *feat;
    float peak_luminance;
    jxl_tone_map_params params;

    if (luma == NULL || hdr == NULL || n == 0 || target_display_luminance <= 0.0f) {
        return;
    }

    feat = jxl_context_cpu_features(ctx);
    peak_luminance = hdr->intensity_target;
    if (detect_peak) {
        size_t i;
        float max_luma;

        max_luma = 0.0f;
        for (i = 0; i < n; ++i) {
            if (luma[i] > max_luma) {
                max_luma = luma[i];
            }
        }
        peak_luminance = max_luma == 0.0f
                             ? hdr->intensity_target
                             : fminf(hdr->intensity_target, max_luma * hdr->intensity_target);
    }

    params = jxl_tone_map_prep(hdr, target_display_luminance, peak_luminance);
    tone_map_luma_dispatch(feat, luma, n, hdr->intensity_target, &params);
}
