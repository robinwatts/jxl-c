// SPDX-License-Identifier: MIT OR Apache-2.0
#include "frame/filter.h"

#include <string.h>

static const float k_gabor_default[3][2] = {
    {0.115169525f, 0.061248592f},
    {0.115169525f, 0.061248592f},
    {0.115169525f, 0.061248592f},
};

static const float k_epf_sharp_lut_default[8] = {
    0.0f, 1.0f / 7.0f, 2.0f / 7.0f, 3.0f / 7.0f,
    4.0f / 7.0f, 5.0f / 7.0f, 6.0f / 7.0f, 1.0f,
};

static const float k_epf_channel_scale_default[3] = {40.0f, 5.0f, 3.5f};

void jxl_restoration_filter_init(jxl_restoration_filter *rf) {
    if (rf == NULL) {
        return;
    }
    memset(rf, 0, sizeof(*rf));
}

void jxl_restoration_filter_set_defaults(jxl_restoration_filter *rf) {
    size_t i;
    if (rf == NULL) {
        return;
    }
    jxl_restoration_filter_init(rf);

    rf->gab.enabled = 1;
    for (i = 0; i < 3; ++i) {
        rf->gab.weights[i].w0 = k_gabor_default[i][0];
        rf->gab.weights[i].w1 = k_gabor_default[i][1];
    }

    rf->epf.enabled = 1;
    rf->epf.iters = 2;
    memcpy(rf->epf.sharp_lut, k_epf_sharp_lut_default, sizeof(rf->epf.sharp_lut));
    memcpy(rf->epf.channel_scale, k_epf_channel_scale_default, sizeof(rf->epf.channel_scale));
    rf->epf.sigma.quant_mul = 0.46f;
    rf->epf.sigma.pass0_sigma_scale = 0.9f;
    rf->epf.sigma.pass2_sigma_scale = 6.5f;
    rf->epf.sigma.border_sad_mul = 2.0f / 3.0f;
    rf->epf.sigma_for_modular = 1.0f;
}

int jxl_gabor_enabled(const jxl_restoration_filter *rf) {
    return rf != NULL && rf->gab.enabled;
}

int jxl_epf_enabled(const jxl_restoration_filter *rf) {
    return rf != NULL && rf->epf.enabled;
}
