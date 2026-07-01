// SPDX-License-Identifier: MIT OR Apache-2.0
#include "blend.h"

float jxl_blend_clamp01(float v, int clamp) {
    if (!clamp) {
        return v;
    }
    if (v < 0.0f) {
        return 0.0f;
    }
    if (v > 1.0f) {
        return 1.0f;
    }
    return v;
}

float jxl_blend_samples(float base_sample, float new_sample, float base_alpha, float new_alpha,
                        const jxl_blend_params *params) {
    if (params == NULL) {
        return new_sample;
    }

    switch (params->mode) {
    case JXL_BLEND_REPLACE:
        return new_sample;
    case JXL_BLEND_ADD:
        return base_sample + new_sample;
    case JXL_BLEND_MUL:
        return base_sample * jxl_blend_clamp01(new_sample, params->clamp);
    case JXL_BLEND_BLEND:
        if (params->premultiplied) {
            return new_sample + base_sample * (1.0f - new_alpha);
        }
        {
            float base_alpha_rev = 1.0f - base_alpha;
            float new_alpha_rev = 1.0f - new_alpha;
            float mixed_alpha = 1.0f - new_alpha_rev * base_alpha_rev;
            float mixed_alpha_recip = mixed_alpha > 0.0f ? 1.0f / mixed_alpha : 0.0f;
            return (new_alpha * new_sample + base_alpha * base_sample * new_alpha_rev) *
                   mixed_alpha_recip;
        }
    case JXL_BLEND_MUL_ADD:
        return base_sample + new_alpha * new_sample;
    default:
        return new_sample;
    }
}
