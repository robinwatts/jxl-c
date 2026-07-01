// SPDX-License-Identifier: MIT OR Apache-2.0
#include "dct.h"

#include <assert.h>
#include <math.h>
#include <string.h>

#define JXL_SQRT2 1.4142135623730951f

static void dct4(float io[4], jxl_dct_direction direction) {
    const float sec0 = 0.5411961f;
    const float sec1 = 1.306563f;

    if (direction == JXL_DCT_FORWARD) {
        float sum03 = io[0] + io[3];
        float sum12 = io[1] + io[2];
        float tmp0 = (io[0] - io[3]) * sec0;
        float tmp1 = (io[1] - io[2]) * sec1;
        float out0 = (tmp0 + tmp1) / 4.0f;
        float out1 = (tmp0 - tmp1) / 4.0f;

        io[0] = (sum03 + sum12) / 4.0f;
        io[1] = out0 * JXL_SQRT2 + out1;
        io[2] = (sum03 - sum12) / 4.0f;
        io[3] = out1;
    } else {
        float tmp0 = io[1] * JXL_SQRT2;
        float tmp1 = io[1] + io[3];
        float out0 = (tmp0 + tmp1) * sec0;
        float out1 = (tmp0 - tmp1) * sec1;
        float sum02 = io[0] + io[2];
        float sub02 = io[0] - io[2];

        io[0] = sum02 + out0;
        io[1] = sub02 + out1;
        io[2] = sub02 - out1;
        io[3] = sum02 - out0;
    }
}

void jxl_dct_1d(float *io, size_t n, float *scratch, jxl_dct_direction direction) {
    float sec_stack[128];
    const float *sec_static;
    const float *sec;
    float *input0;
    float *input1;
    if (n <= 1) {
        return;
    }
    if (n == 2) {
        float tmp0 = io[0] + io[1];
        float tmp1 = io[0] - io[1];
        if (direction == JXL_DCT_FORWARD) {
            io[0] = tmp0 / 2.0f;
            io[1] = tmp1 / 2.0f;
        } else {
            io[0] = tmp0;
            io[1] = tmp1;
        }
        return;
    }
    if (n == 4) {
        float in4[4];
        in4[0] = io[0];
        in4[1] = io[1];
        in4[2] = io[2];
        in4[3] = io[3];

        dct4(in4, direction);
        memcpy(io, in4, sizeof(in4));
        return;
    }
    if (n == 8) {
        const float *sec = jxl_sec_half_small(8);
        if (direction == JXL_DCT_FORWARD) {
            size_t idx;
            float input0[4];
            float input1[4];
            input0[0] = (io[0] + io[7]) / 2.0f;
            input0[1] = (io[1] + io[6]) / 2.0f;
            input0[2] = (io[2] + io[5]) / 2.0f;
            input0[3] = (io[3] + io[4]) / 2.0f;

            input1[0] = (io[0] - io[7]) * sec[0] / 2.0f;
            input1[1] = (io[1] - io[6]) * sec[1] / 2.0f;
            input1[2] = (io[2] - io[5]) * sec[2] / 2.0f;
            input1[3] = (io[3] - io[4]) * sec[3] / 2.0f;

            dct4(input0, JXL_DCT_FORWARD);
            for (idx = 0; idx < 4; ++idx) {
                io[idx * 2] = input0[idx];
            }
            dct4(input1, JXL_DCT_FORWARD);
            input1[0] *= JXL_SQRT2;
            for (idx = 0; idx < 3; ++idx) {
                io[idx * 2 + 1] = input1[idx] + input1[idx + 1];
            }
            io[7] = input1[3];
        } else {
            size_t idx;
            float input0[4];
            float input1[4];
            input0[0] = io[0];
            input0[1] = io[2];
            input0[2] = io[4];
            input0[3] = io[6];

            input1[0] = io[1] * JXL_SQRT2;
            input1[1] = io[3] + io[1];
            input1[2] = io[5] + io[3];
            input1[3] = io[7] + io[5];

            dct4(input0, JXL_DCT_INVERSE);
            dct4(input1, JXL_DCT_INVERSE);
            for (idx = 0; idx < 4; ++idx) {
                float r = input1[idx] * sec[idx];
                io[idx] = input0[idx] + r;
                io[7 - idx] = input0[idx] - r;
            }
        }
        return;
    }

    assert(scratch != NULL);

    sec_static = jxl_sec_half_small(n);
    sec = sec_static;
    if (sec == NULL) {
        assert(n / 2 <= sizeof(sec_stack) / sizeof(sec_stack[0]));
        jxl_sec_half_fill(n, sec_stack);
        sec = sec_stack;
    }

    input0 = scratch;
    input1 = scratch + n / 2;

    if (direction == JXL_DCT_FORWARD) {
        size_t idx;
        for (idx = 0; idx < n / 2; ++idx) {
            input0[idx] = (io[idx] + io[n - idx - 1]) / 2.0f;
            input1[idx] = (io[idx] - io[n - idx - 1]) / 2.0f;
        }
        for (idx = 0; idx < n / 2; ++idx) {
            input1[idx] *= sec[idx];
        }
        jxl_dct_1d(input0, n / 2, io, JXL_DCT_FORWARD);
        jxl_dct_1d(input1, n / 2, io + n / 2, JXL_DCT_FORWARD);
        input1[0] *= JXL_SQRT2;
        for (idx = 0; idx < n / 2 - 1; ++idx) {
            input1[idx] += input1[idx + 1];
        }
        for (idx = 0; idx < n / 2; ++idx) {
            io[idx * 2] = input0[idx];
            io[idx * 2 + 1] = input1[idx];
        }
    } else {
        size_t idx;
        for (idx = 0; idx < n / 2; ++idx) {
            input0[idx] = io[idx * 2];
            input1[idx] = io[idx * 2 + 1];
        }
        for (idx = 1; idx < n / 2; ++idx) {
            input1[n / 2 - idx] += input1[n / 2 - idx - 1];
        }
        input1[0] *= JXL_SQRT2;
        jxl_dct_1d(input0, n / 2, io, JXL_DCT_INVERSE);
        jxl_dct_1d(input1, n / 2, io + n / 2, JXL_DCT_INVERSE);
        for (idx = 0; idx < n / 2; ++idx) {
            input1[idx] *= sec[idx];
        }
        for (idx = 0; idx < n / 2; ++idx) {
            io[idx] = input0[idx] + input1[idx];
            io[n - idx - 1] = input0[idx] - input1[idx];
        }
    }
}
