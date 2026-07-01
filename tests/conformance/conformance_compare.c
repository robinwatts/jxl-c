// SPDX-License-Identifier: MIT OR Apache-2.0
#include "conformance_compare.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

int jxl_conformance_compare_render(const jxl_conformance_npy *reference, uint32_t frame_index,
                                   const jxl_render *render, float peak_limit, float rmse_limit,
                                   const char *case_name) {
                                       uint32_t c;
    int failed;
    if (reference == NULL || reference->samples == NULL || render == NULL) {
        return -1;
    }
    if (frame_index >= reference->num_frames) {
        fprintf(stderr, "%s: frame index %u out of range (%u frames)\n",
                case_name != NULL ? case_name : "", frame_index, reference->num_frames);
        return -1;
    }

    size_t frame_stride =
        (size_t)reference->width * (size_t)reference->height * (size_t)reference->channels;
    const float *reference_frame = reference->samples + frame_index * frame_stride;

    uint32_t width = jxl_render_width(render);
    uint32_t height = jxl_render_height(render);
    uint32_t planes = jxl_render_num_planes(render);

    if (width != reference->width || height != reference->height) {
        fprintf(stderr, "%s: size %ux%u != reference %ux%u\n", case_name != NULL ? case_name : "",
                width, height, reference->width, reference->height);
        return 1;
    }
    if (planes != reference->channels) {
        fprintf(stderr, "%s: planes %u != reference channels %u\n",
                case_name != NULL ? case_name : "", planes, reference->channels);
        return 1;
    }

    size_t pixels = (size_t)width * (size_t)height;
    int verbose = getenv("JXL_CONFORMANCE_VERBOSE") != NULL;
    const char *dump_path = getenv("JXL_DUMP_RENDER");
    FILE *dump = NULL;
    if (dump_path != NULL && dump_path[0] != '\0') {
        dump = fopen(dump_path, "wb");
    }
    failed = 0;
    if (dump != NULL && planes >= 3) {
        const float *p0 = jxl_render_plane(render, 0);
        const float *p1 = jxl_render_plane(render, 1);
        const float *p2 = jxl_render_plane(render, 2);
        if (p0 != NULL && p1 != NULL && p2 != NULL) {
            size_t i;
            for (i = 0; i < pixels; ++i) {
                float px[3];
                px[0] = p0[i];
                px[1] = p1[i];
                px[2] = p2[i];

                fwrite(px, sizeof(float), 3, dump);
            }
        }
    }
    for (c = 0; c < planes; ++c) {
        size_t i;
        double sum_se;
        float peak;
        const float *actual = jxl_render_plane(render, c);
        if (actual == NULL) {
            return -1;
        }

        sum_se = 0.0;
        peak = 0.0f;
        for (i = 0; i < pixels; ++i) {
            size_t x = i % width;
            size_t y = i / width;
            size_t ref_idx = c + (x + y * width) * planes;
            float expected = reference_frame[ref_idx];
            float output = actual[i];
            float abs_error = fabsf(output - expected);
            if (abs_error > peak) {
                peak = abs_error;
            }
            double err = (double)output - (double)expected;
            sum_se += err * err;
        }
        double mse = sum_se / (double)pixels;
        double rmse = sqrt(mse);
        if (verbose) {
            fprintf(stderr, "%s channel %u: peak=%g rmse=%g\n",
                    case_name != NULL ? case_name : "", c, peak, rmse);
        }
        if (peak > peak_limit || (float)rmse > rmse_limit) {
            if (!verbose) {
                fprintf(stderr, "%s channel %u: peak=%g (limit %g) rmse=%g (limit %g)\n",
                        case_name != NULL ? case_name : "", c, peak, peak_limit, (float)rmse,
                        rmse_limit);
            }
            failed = 1;
        }
    }

    if (dump != NULL) {
        fclose(dump);
    }
    return failed;
}
