// SPDX-License-Identifier: MIT OR Apache-2.0
#include "render/color/gamut.h"

#include "render/color/gamut_internal.h"
#include "render/simd/features.h"

#include <math.h>

void jxl_color_map_gamut_rgb_base(const float rgb[3], float out[3], const float luminances[3],
                                    float saturation_factor) {
                                        size_t i;
    const float r = rgb[0];
    const float g = rgb[1];
    const float b = rgb[2];
    const float lr = luminances[0];
    const float lg = luminances[1];
    const float lb = luminances[2];
    const float y = r * lr + g * lg + b * lb;

    float gray_saturation = 0.0f;
    float gray_luminance = 0.0f;
    float channels[3];
    float gray_mix;
    float mixed_r;
    float mixed_g;
    float mixed_b;
    float max_color_val;
    channels[0] = r;
    channels[1] = g;
    channels[2] = b;

    for (i = 0; i < 3; ++i) {
        const float v = channels[i];
        const float v_sub_y = v - y;
        const float inv_v_sub_y = v_sub_y == 0.0f ? 1.0f : 1.0f / v_sub_y;
        const float v_over_v_sub_y = v * inv_v_sub_y;
        float gl;
        if (v_sub_y < 0.0f) {
            gray_saturation = fmaxf(gray_saturation, v_over_v_sub_y);
        }
        gl = v_sub_y <= 0.0f ? gray_saturation : v_over_v_sub_y - inv_v_sub_y;
        gray_luminance = fmaxf(gray_luminance, gl);
    }

    gray_mix = saturation_factor * (gray_saturation - gray_luminance) + gray_luminance;
    if (gray_mix < 0.0f) {
        gray_mix = 0.0f;
    } else if (gray_mix > 1.0f) {
        gray_mix = 1.0f;
    }

    mixed_r = gray_mix * (y - r) + r;
    mixed_g = gray_mix * (y - g) + g;
    mixed_b = gray_mix * (y - b) + b;
    max_color_val = fmaxf(1.0f, fmaxf(fmaxf(mixed_r, mixed_g), mixed_b));
    out[0] = mixed_r / max_color_val;
    out[1] = mixed_g / max_color_val;
    out[2] = mixed_b / max_color_val;
}

void jxl_color_gamut_map_base(float *r, float *g, float *b, size_t n, const float luminances[3],
                              float saturation_factor) {
                                  size_t i;
    if (r == NULL || g == NULL || b == NULL || luminances == NULL || n == 0) {
        return;
    }
    for (i = 0; i < n; ++i) {
        float rgb[3];
        float mapped[3];
        rgb[0] = r[i];
        rgb[1] = g[i];
        rgb[2] = b[i];

        jxl_color_map_gamut_rgb_base(rgb, mapped, luminances, saturation_factor);
        r[i] = mapped[0];
        g[i] = mapped[1];
        b[i] = mapped[2];
    }
}

void jxl_color_gamut_map(jxl_context *ctx, float *r, float *g, float *b, size_t n,
                         const float luminances[3], float saturation_factor) {
    const jxl_cpu_features *feat;
    if (r == NULL || g == NULL || b == NULL || luminances == NULL || n == 0) {
        return;
    }
    feat = jxl_context_cpu_features(ctx);

#if defined(JXL_HAVE_SIMD_NEON)
    if (feat->neon) {
            jxl_color_gamut_map_neon(r, g, b, n, luminances, saturation_factor);
            return;
        
    }
#endif
#if defined(JXL_HAVE_SIMD_AVX2)
    if (feat->fma && feat->sse41) {
            if (feat->avx2) {
                jxl_color_gamut_map_x86_avx2(r, g, b, n, luminances, saturation_factor);
            } else {
                jxl_color_gamut_map_x86_fma(r, g, b, n, luminances, saturation_factor);
            }
            return;
        
    }
#endif
#if defined(JXL_HAVE_SIMD_SSE41)
    jxl_color_gamut_map_x86_sse2(r, g, b, n, luminances, saturation_factor);
    return;
#endif
    jxl_color_gamut_map_base(r, g, b, n, luminances, saturation_factor);
}
