// SPDX-License-Identifier: MIT OR Apache-2.0
#include "render/color/rec2408.h"

#include "render/color/pq_internal.h"
#include "render/color/rec2408_internal.h"
#include "render/simd/features.h"

jxl_rec2408_eetf_params jxl_rec2408_eetf_prep(float intensity_target,
                                              jxl_luminance_nits_range from_luminance_range,
                                              jxl_luminance_nits_range to_luminance_range) {
                                                  size_t i;
    jxl_rec2408_eetf_params params;
    float luminances[4];
    luminances[0] = from_luminance_range.lo / intensity_target;
    luminances[1] = from_luminance_range.hi / intensity_target;
    luminances[2] = to_luminance_range.lo / intensity_target;
    luminances[3] = to_luminance_range.hi / intensity_target;

    for (i = 0; i < 4; ++i) {
        luminances[i] = jxl_color_linear_to_pq_sample(luminances[i], intensity_target);
    }

    params.min_source_luminance = luminances[0];
    params.source_pq_diff = luminances[1] - luminances[0];
    params.min_luminance = (luminances[2] - luminances[0]) / params.source_pq_diff;
    params.max_luminance = (luminances[3] - luminances[0]) / params.source_pq_diff;
    params.ks = 1.5f * params.max_luminance - 0.5f;
    params.b = params.min_luminance;
    params.one_sub_ks = 1.0f - params.ks;
    return params;
}

float jxl_rec2408_eetf_with_params(float from_pq_sample, const jxl_rec2408_eetf_params *params) {
    float compressed_pq_sample;
    const float normalized_source_pq_sample =
        (from_pq_sample - params->min_source_luminance) / params->source_pq_diff;
    float one_sub_compressed;
    float one_sub_compressed_p4;
    float normalized_target_pq_sample;

    if (normalized_source_pq_sample < params->ks) {
        compressed_pq_sample = normalized_source_pq_sample;
    } else {
        const float t = (normalized_source_pq_sample - params->ks) / params->one_sub_ks;
        const float t_p2 = t * t;
        const float t_p3 = t_p2 * t;
        compressed_pq_sample = (2.0f * t_p3 - 3.0f * t_p2 + 1.0f) * params->ks +
                               (t_p3 - 2.0f * t_p2 + t) * params->one_sub_ks +
                               (-2.0f * t_p3 + 3.0f * t_p2) * params->max_luminance;
    }

    one_sub_compressed = 1.0f - compressed_pq_sample;
    one_sub_compressed_p4 = one_sub_compressed * one_sub_compressed * one_sub_compressed *
                            one_sub_compressed;
    normalized_target_pq_sample =
        one_sub_compressed_p4 * params->b + compressed_pq_sample;
    return normalized_target_pq_sample * params->source_pq_diff + params->min_source_luminance;
}

float jxl_rec2408_eetf_base(float from_pq_sample, float intensity_target,
                            jxl_luminance_nits_range from_luminance_range,
                            jxl_luminance_nits_range to_luminance_range) {
    const jxl_rec2408_eetf_params params =
        jxl_rec2408_eetf_prep(intensity_target, from_luminance_range, to_luminance_range);
    return jxl_rec2408_eetf_with_params(from_pq_sample, &params);
}

void jxl_rec2408_eetf_pq_base(float *samples, size_t n, const jxl_rec2408_eetf_params *params) {
    size_t i;
    if (samples == NULL || n == 0 || params == NULL) {
        return;
    }
    for (i = 0; i < n; ++i) {
        samples[i] = jxl_rec2408_eetf_with_params(samples[i], params);
    }
}

void jxl_rec2408_eetf_pq(jxl_context *ctx, float *samples, size_t n, float intensity_target,
                         jxl_luminance_nits_range from_luminance_range,
                         jxl_luminance_nits_range to_luminance_range) {
    const jxl_cpu_features *feat;
    jxl_rec2408_eetf_params params;

    if (samples == NULL || n == 0) {
        return;
    }
    if (intensity_target <= 0.0f) {
        intensity_target = 10000.0f;
    }

    params = jxl_rec2408_eetf_prep(intensity_target, from_luminance_range, to_luminance_range);
    feat = jxl_context_cpu_features(ctx);

#if defined(JXL_HAVE_SIMD_NEON)
    if (feat->neon) {
        jxl_rec2408_eetf_pq_neon(samples, n, &params);
        return;
    }
#endif
#if defined(JXL_HAVE_SIMD_AVX2)
    if (feat->fma && feat->sse41) {
        if (feat->avx2) {
            jxl_rec2408_eetf_pq_x86_avx2(samples, n, &params);
        } else {
            jxl_rec2408_eetf_pq_x86_fma(samples, n, &params);
        }
        return;
    }
#endif
#if defined(JXL_HAVE_SIMD_SSE41)
    jxl_rec2408_eetf_pq_x86_sse2(samples, n, &params);
    return;
#endif
    jxl_rec2408_eetf_pq_base(samples, n, &params);
}
