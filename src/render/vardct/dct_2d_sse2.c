// SPDX-License-Identifier: MIT OR Apache-2.0
#include "render/vardct/dct_2d_sse2.h"

#include "render/vardct/dct_2d.h"
#include "render/vardct/sse2_lane.h"
#include "render/vardct/dct_common.h"
#include "render/vardct/dct.h"

#include "allocator.h"

#include <emmintrin.h>
#include <stddef.h>
#include "jxl_oxide/jxl_types.h"

enum { JXL_LANE_SIZE = 4 };

typedef __m128 jxl_lane;

typedef struct {
    jxl_lane *data;
    size_t width;
    size_t height;
    size_t stride;
} jxl_lane_subgrid;

static jxl_lane lane_splat(float v) {
    return _mm_set1_ps(v);
}

static jxl_lane lane_get(const jxl_lane_subgrid *g, size_t x, size_t y) {
    return g->data[y * g->stride + x];
}

static void lane_set_at(jxl_lane_subgrid *g, size_t x, size_t y, jxl_lane v) {
    g->data[y * g->stride + x] = v;
}

static void dct4_forward_lanes(jxl_lane out[4], const jxl_lane in[4]) {
    const jxl_lane sec0 = lane_splat(0.5411961f / 4.0f);
    const jxl_lane sec1 = lane_splat(1.306563f / 4.0f);
    const jxl_lane quarter = lane_splat(0.25f);
    const jxl_lane sqrt2 = lane_splat(1.4142135623730951f);

    const jxl_lane sum03 = _mm_add_ps(in[0], in[3]);
    const jxl_lane sum12 = _mm_add_ps(in[1], in[2]);
    const jxl_lane tmp0 = _mm_mul_ps(_mm_sub_ps(in[0], in[3]), sec0);
    const jxl_lane tmp1 = _mm_mul_ps(_mm_sub_ps(in[1], in[2]), sec1);
    const jxl_lane out1 = _mm_sub_ps(tmp0, tmp1);
    const jxl_lane out0 = _mm_add_ps(tmp0, tmp1);

    out[0] = _mm_mul_ps(_mm_add_ps(sum03, sum12), quarter);
    out[1] = _mm_add_ps(_mm_mul_ps(out0, sqrt2), out1);
    out[2] = _mm_mul_ps(_mm_sub_ps(sum03, sum12), quarter);
    out[3] = out1;
}

static void dct4_inverse_lanes(jxl_lane out[4], const jxl_lane in[4]) {
    jxl_sse2_dct4_inverse_lanes((jxl_sse2_lane *)out, (const jxl_sse2_lane *)in);
}

static void dct_lanes(jxl_lane *io, jxl_lane *scratch, size_t n, jxl_dct_direction direction) {
    float sec_buf[32];
    if (n <= 1) {
        return;
    }

    const jxl_lane half = lane_splat(0.5f);
    const jxl_lane sqrt2 = lane_splat(1.4142135623730951f);

    if (n == 2) {
        const jxl_lane tmp0 = _mm_add_ps(io[0], io[1]);
        const jxl_lane tmp1 = _mm_sub_ps(io[0], io[1]);
        if (direction == JXL_DCT_FORWARD) {
            io[0] = _mm_mul_ps(tmp0, half);
            io[1] = _mm_mul_ps(tmp1, half);
        } else {
            io[0] = tmp0;
            io[1] = tmp1;
        }
        return;
    }

    if (n == 4) {
        jxl_lane in[4];
        in[0] = io[0];
        in[1] = io[1];
        in[2] = io[2];
        in[3] = io[3];

        if (direction == JXL_DCT_FORWARD) {
            dct4_forward_lanes(io, in);
        } else {
            dct4_inverse_lanes(io, in);
        }
        return;
    }

    if (n == 8) {
        if (direction == JXL_DCT_FORWARD) {
            jxl_sse2_dct8_forward_lanes((jxl_sse2_lane *)io);
        } else {
            jxl_sse2_dct8_inverse_lanes((jxl_sse2_lane *)io);
        }
        return;
    }

    const float *sec_small = jxl_sec_half_small(n);
    const float *sec = sec_small;
    if (sec == NULL) {
        jxl_sec_half_fill(n, sec_buf);
        sec = sec_buf;
    }

    if (direction == JXL_DCT_FORWARD) {
        size_t idx;
        jxl_lane *input0 = scratch;
        jxl_lane *input1 = scratch + n / 2;
        for (idx = 0; idx < n / 2; ++idx) {
            input0[idx] = _mm_mul_ps(_mm_add_ps(io[idx], io[n - idx - 1]), half);
            input1[idx] = _mm_mul_ps(_mm_sub_ps(io[idx], io[n - idx - 1]), lane_splat(sec[idx] / 2.0f));
        }
        dct_lanes(input0, io, n / 2, JXL_DCT_FORWARD);
        dct_lanes(input1, io + n / 2, n / 2, JXL_DCT_FORWARD);
        for (idx = 0; idx < n / 2; ++idx) {
            io[idx * 2] = input0[idx];
        }
        input1[0] = _mm_mul_ps(input1[0], sqrt2);
        for (idx = 0; idx < n / 2 - 1; ++idx) {
            io[idx * 2 + 1] = _mm_add_ps(input1[idx], input1[idx + 1]);
        }
        io[n - 1] = input1[n / 2 - 1];
    } else {
        size_t i;
        size_t idx;
        size_t ri;
        jxl_lane *input0 = scratch;
        jxl_lane *input1 = scratch + n / 2;
        for (i = 1; i < n / 2; ++i) {
            ri = n / 2 - i;
            input0[ri] = io[ri * 2];
            input1[ri] = _mm_add_ps(io[ri * 2 + 1], io[ri * 2 - 1]);
        }
        input0[0] = io[0];
        input1[0] = _mm_mul_ps(io[1], sqrt2);
        dct_lanes(input0, io, n / 2, JXL_DCT_INVERSE);
        dct_lanes(input1, io + n / 2, n / 2, JXL_DCT_INVERSE);
        for (idx = 0; idx < n / 2; ++idx) {
            const jxl_lane r = _mm_mul_ps(input1[idx], lane_splat(sec[idx]));
            io[idx] = _mm_add_ps(input0[idx], r);
            io[n - idx - 1] = _mm_sub_ps(input0[idx], r);
        }
    }
}

static void column_dct_lane(jxl_lane_subgrid *io, jxl_lane *scratch, jxl_dct_direction direction) {
    size_t x;
    const size_t width = io->width;
    const size_t height = io->height;
    jxl_lane *io_lanes = scratch;
    jxl_lane *scratch_lanes = scratch + height;

    for (x = 0; x < width; ++x) {
        size_t y;
        for (y = 0; y < height; ++y) {
            io_lanes[y] = lane_get(io, x, y);
        }
        dct_lanes(io_lanes, scratch_lanes, height, direction);
        for (y = 0; y < height; y += JXL_LANE_SIZE) {
            size_t dy;
            jxl_lane block[JXL_LANE_SIZE];
            for (dy = 0; dy < JXL_LANE_SIZE; ++dy) {
                block[dy] = io_lanes[y + dy];
            }
            jxl_sse2_transpose_lane((jxl_sse2_lane *)block);
            for (dy = 0; dy < JXL_LANE_SIZE; ++dy) {
                lane_set_at(io, x, y + dy, block[dy]);
            }
        }
    }
}

static void row_dct_lane(jxl_lane_subgrid *io, jxl_lane *scratch, jxl_dct_direction direction) {
    size_t y;
    const size_t width = io->width * JXL_LANE_SIZE;
    const size_t height = io->height;
    jxl_lane *io_lanes = scratch;
    jxl_lane *scratch_lanes = scratch + width;

    for (y = 0; y < height; y += JXL_LANE_SIZE) {
        size_t x;
        for (x = 0; x < width; ++x) {
            io_lanes[x] = lane_get(io, x / JXL_LANE_SIZE, y + (x % JXL_LANE_SIZE));
        }
        dct_lanes(io_lanes, scratch_lanes, width, direction);
        for (x = 0; x < width; x += JXL_LANE_SIZE) {
            size_t dy;
            jxl_lane block[JXL_LANE_SIZE];
            for (dy = 0; dy < JXL_LANE_SIZE; ++dy) {
                block[dy] = io_lanes[x + dy];
            }
            jxl_sse2_transpose_lane((jxl_sse2_lane *)block);
            for (dy = 0; dy < JXL_LANE_SIZE; ++dy) {
                lane_set_at(io, x / JXL_LANE_SIZE, y + dy, block[dy]);
            }
        }
    }
}

static void dct8_column_lanes(jxl_lane_subgrid *io, size_t x, jxl_dct_direction direction) {
    size_t y;
    jxl_lane col[8];
    for (y = 0; y < 8; ++y) {
        col[y] = lane_get(io, x, y);
    }
    if (direction == JXL_DCT_FORWARD) {
        jxl_sse2_dct8_forward_lanes((jxl_sse2_lane *)col);
    } else {
        jxl_sse2_dct8_inverse_lanes((jxl_sse2_lane *)col);
    }
    for (y = 0; y < 8; ++y) {
        lane_set_at(io, x, y, col[y]);
    }
}

static void dct8x8_lane(jxl_lane_subgrid *io, jxl_dct_direction direction) {
    size_t y;
    dct8_column_lanes(io, 0, direction);
    dct8_column_lanes(io, 1, direction);
    for (y = 0; y < 8; ++y) {
        const jxl_lane vl = lane_get(io, 0, y);
        const jxl_lane vr = lane_get(io, 1, y);
        jxl_lane out_l;
        jxl_lane out_r;
        if (direction == JXL_DCT_FORWARD) {
            jxl_sse2_dct8_vec_forward(vl, vr, &out_l, &out_r);
        } else {
            jxl_sse2_dct8_vec_inverse(vl, vr, &out_l, &out_r);
        }
        lane_set_at(io, 0, y, out_l);
        lane_set_at(io, 1, y, out_r);
    }
}

static void dct_2d_lane(jxl_lane_subgrid *io, jxl_allocator_state *alloc, jxl_dct_direction direction) {
    enum { JXL_DCT_STACK_SCRATCH_LANES = 256 };
    jxl_lane stack_scratch[JXL_DCT_STACK_SCRATCH_LANES];
    const size_t scratch_size =
        (io->height > io->width * JXL_LANE_SIZE ? io->height : io->width * JXL_LANE_SIZE) * 2;
    jxl_lane *scratch = stack_scratch;
    int scratch_on_stack = scratch_size <= JXL_DCT_STACK_SCRATCH_LANES;

    if (!scratch_on_stack) {
        scratch = (jxl_lane *)jxl_alloc_aligned(alloc, JXL_ALLOC_ALIGN_SIMD128,
                                                  scratch_size * sizeof(jxl_lane));
        if (scratch == NULL) {
            return;
        }
    }
    column_dct_lane(io, scratch, direction);
    row_dct_lane(io, scratch, direction);
    if (!scratch_on_stack) {
        jxl_free_aligned(alloc, scratch);
    }
}

static int subgrid_as_lane(jxl_subgrid_f32 sg, jxl_lane_subgrid *out) {
    const uintptr_t ptr = (uintptr_t)sg.data;
    if ((ptr & 15u) != 0 || (sg.width & 3u) != 0 || (sg.height & 3u) != 0 || (sg.stride & 3u) != 0) {
        return 0;
    }
    out->data = (jxl_lane *)sg.data;
    out->width = sg.width / JXL_LANE_SIZE;
    out->height = sg.height;
    out->stride = sg.stride / JXL_LANE_SIZE;
    return 1;
}

int jxl_dct_2d_x86_64_sse2(jxl_allocator_state *alloc, jxl_subgrid_f32 io, jxl_dct_direction direction) {
    jxl_lane_subgrid lane_io;
    if (io.width % JXL_LANE_SIZE != 0 || io.height % JXL_LANE_SIZE != 0) {
        return 0;
    }

    if (!subgrid_as_lane(io, &lane_io)) {
        return 0;
    }

    if (lane_io.width == 2 && lane_io.height == 8) {
        dct8x8_lane(&lane_io, direction);
        return 1;
    }

    dct_2d_lane(&lane_io, alloc, direction);
    return 1;
}
