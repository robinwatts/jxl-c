// SPDX-License-Identifier: MIT OR Apache-2.0
#include "frame/filter.h"
#include "frame/frame_header.h"
#include "render/filter/epf.h"
#include "render/filter/filter_util.h"
#include "render/filter/padded_f32.h"
#include "render/subgrid_f32.h"

#include "allocator.h"
#include "context.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(JXL_HAVE_SIMD_SSE41)

static jxl_context test_ctx;
static int test_ctx_ready;

static jxl_context *test_ctx_get(void) {
    if (!test_ctx_ready) {
        jxl_context_init_inplace(&test_ctx, NULL);
        test_ctx_ready = 1;
    }
    return &test_ctx;
}

static void test_epf_force_scalar(int on) {
    test_ctx_get()->debug.epf_test_force_scalar = on;
}

#endif

static jxl_allocator_state *test_alloc_state(void) {
    static jxl_allocator_state alloc;
    static int init = 0;
    if (!init) {
        jxl_allocator_init(&alloc, NULL);
        init = 1;
    }
    return &alloc;
}

static void fill_channels(float *ch[3], size_t w, size_t h, unsigned seed) {
    size_t c;
    for (c = 0; c < 3; ++c) {
        size_t i;
        for (i = 0; i < w * h; ++i) {
            seed = seed * 1103515245u + 12345u;
            ch[c][i] = (float)(seed % 1000) * 0.001f - 0.2f;
        }
    }
}

static int quantize(float v) {
    return (int)(v * 65536.0f);
}

static int compare_outputs(const float *a, const float *b, size_t n, const char *tag) {
    size_t i;
    int max_diff = 0;
    size_t max_i = 0;
    for (i = 0; i < n; ++i) {
        int diff = quantize(a[i]) - quantize(b[i]);
        if (diff < 0) {
            diff = -diff;
        }
        if (diff > max_diff) {
            max_diff = diff;
            max_i = i;
        }
    }
    if (max_diff > 1) {
        printf("FAIL %s: max quant diff=%d at %zu (%.8f vs %.8f)\n", tag, max_diff, max_i, a[max_i],
               b[max_i]);
        return 0;
    }
    printf("OK %s\n", tag);
    return 1;
}

static int run_epf_step1_after_step0(size_t width, size_t height) {
    size_t c;
#if !defined(JXL_HAVE_SIMD_SSE41)
    (void)width;
    (void)height;
    return 1;
#else
    jxl_filter_extent channels[3];
    jxl_frame_header fh;
    int cmp;
    int ok;
    jxl_epf_filter epf;
    size_t w = width;
    size_t h = height;
    float *buf[3];
    float *after_s0[3];
    float *simd_out[3];
    float *scalar_out[3];
    for (c = 0; c < 3; ++c) {
        buf[c] = (float *)malloc(w * h * sizeof(float));
        after_s0[c] = (float *)malloc(w * h * sizeof(float));
        simd_out[c] = (float *)malloc(w * h * sizeof(float));
        scalar_out[c] = (float *)malloc(w * h * sizeof(float));
        assert(buf[c] && after_s0[c] && simd_out[c] && scalar_out[c]);
    }

    fill_channels(buf, w, h, 99u + (unsigned)width);
    for (c = 0; c < 3; ++c) {
        memcpy(after_s0[c], buf[c], w * h * sizeof(float));
    }

    for (c = 0; c < 3; ++c) {
        jxl_filter_extent compound_tmp;
        compound_tmp.full = jxl_subgrid_f32_from_buf(buf[c], w, h, w);
        compound_tmp.origin_x = 0;
        compound_tmp.origin_y = 0;
        compound_tmp.width = w;
        compound_tmp.height = h;
        channels[c] = compound_tmp;

    }

    epf.enabled = 1;
    epf.iters = 3;
    epf.channel_scale[0] = 1.0f;
    epf.channel_scale[1] = 0.5f;
    epf.channel_scale[2] = 0.25f;
    epf.sigma.pass0_sigma_scale = 1.25f;
    epf.sigma.pass2_sigma_scale = 1.1f;
    epf.sigma.border_sad_mul = 1.5f;
    epf.sigma_for_modular = 1.0f;


    jxl_frame_header_init(&fh);
    fh.width = (uint32_t)w;
    fh.height = (uint32_t)h;

    /* Run step 0 scalar only. */
    test_epf_force_scalar(1);
    if (!jxl_apply_epf_extent_step(test_ctx_get(), channels, &epf, &fh, NULL, 0, NULL, after_s0, 0)) {
        return 0;
    }
    for (c = 0; c < 3; ++c) {
        memcpy(buf[c], after_s0[c], w * h * sizeof(float));
        memcpy(simd_out[c], after_s0[c], w * h * sizeof(float));
        memcpy(scalar_out[c], after_s0[c], w * h * sizeof(float));
    }

    test_epf_force_scalar(0);
    ok = jxl_apply_epf_extent_step(test_ctx_get(), channels, &epf, &fh, NULL, 0, NULL, simd_out, 1);
    for (c = 0; c < 3; ++c) {
        memcpy(buf[c], after_s0[c], w * h * sizeof(float));
    }
    test_epf_force_scalar(1);
    ok = ok && jxl_apply_epf_extent_step(test_ctx_get(), channels, &epf, &fh, NULL, 0, NULL, scalar_out, 1);

    cmp = 1;
    for (c = 0; c < 3; ++c) {
        size_t i;
        char tag[80];
        snprintf(tag, sizeof(tag), "step1-after-s0 w%zu ch%zu", width, c);
        for (i = 0; i < w * h; ++i) {
            int diff = quantize(simd_out[c][i]) - quantize(scalar_out[c][i]);
            if (diff < 0) {
                diff = -diff;
            }
            if (diff > 1) {
                printf("FAIL %s: first diff at i=%zu (y=%zu x=%zu) %.8f vs %.8f\n", tag, i,
                       i / w, i % w, simd_out[c][i], scalar_out[c][i]);
                cmp = 0;
                break;
            }
        }
        if (cmp) {
            printf("OK %s\n", tag);
        }
    }

    for (c = 0; c < 3; ++c) {
        free(buf[c]);
        free(after_s0[c]);
        free(simd_out[c]);
        free(scalar_out[c]);
    }
    return ok && cmp;
#endif
}

static int run_epf_step1_padded(size_t width, size_t height, int32_t frame_left) {
    size_t c;
#if !defined(JXL_HAVE_SIMD_SSE41)
    (void)width;
    (void)height;
    (void)frame_left;
    return 1;
#else
    jxl_padded_f32 padded[3];
    jxl_restoration_filter rf;
    jxl_filter_pad_params pad;
    int setup_ok;
    jxl_filter_extent channels[3];
    jxl_frame_header fh;
    size_t scratch_count;
    int cmp;
    int ok;
    jxl_epf_filter epf;
    jxl_allocator_state *alloc = test_alloc_state();
    size_t w = width;
    size_t h = height;

    float *work[3];
    float *orig[3];
    float *simd_out[3];
    float *scalar_out[3];
    memset(padded, 0, sizeof(padded));

    for (c = 0; c < 3; ++c) {
        work[c] = (float *)malloc(w * h * sizeof(float));
        orig[c] = (float *)malloc(w * h * sizeof(float));
        simd_out[c] = (float *)malloc(w * h * sizeof(float));
        scalar_out[c] = (float *)malloc(w * h * sizeof(float));
        assert(work[c] && orig[c] && simd_out[c] && scalar_out[c]);
    }

    fill_channels(work, w, h, 77u + (unsigned)w + (unsigned)frame_left);
    for (c = 0; c < 3; ++c) {
        memcpy(orig[c], work[c], w * h * sizeof(float));
    }

    epf.enabled = 1;
    epf.iters = 3;
    epf.channel_scale[0] = 1.0f;
    epf.channel_scale[1] = 0.5f;
    epf.channel_scale[2] = 0.25f;
    epf.sigma.pass0_sigma_scale = 1.25f;
    epf.sigma.pass2_sigma_scale = 1.1f;
    epf.sigma.border_sad_mul = 1.5f;
    epf.sigma_for_modular = 1.0f;


    memset(&rf, 0, sizeof(rf));
    rf.epf = epf;

    jxl_filter_pad_params_compute(&pad, &rf, (uint32_t)w, (uint32_t)h, frame_left, 4);

    setup_ok = 1;
    for (c = 0; c < 3; ++c) {
        if (!jxl_padded_f32_alloc(alloc, pad.buf_width, pad.buf_height, &padded[c])) {
            setup_ok = 0;
            break;
        }
        jxl_subgrid_f32 src = jxl_subgrid_f32_from_buf(work[c], w, h, w);
        if (!jxl_padded_f32_place(&src, &padded[c], pad.pad_left, pad.pad_top)) {
            setup_ok = 0;
            break;
        }
        jxl_padded_f32_mirror_trailing(&padded[c], pad.pad_left, pad.pad_top, w, h);
    }
    if (!setup_ok) {
        size_t c;
        for (c = 0; c < 3; ++c) {
            jxl_padded_f32_free(alloc, &padded[c]);
            free(work[c]);
            free(orig[c]);
            free(simd_out[c]);
            free(scalar_out[c]);
        }
        return 0;
    }

    for (c = 0; c < 3; ++c) {
        jxl_filter_extent compound_tmp;
        compound_tmp.full = jxl_padded_f32_subgrid(&padded[c]);
        compound_tmp.origin_x = pad.pad_left;
        compound_tmp.origin_y = pad.pad_top;
        compound_tmp.width = w;
        compound_tmp.height = h;
        channels[c] = compound_tmp;

    }

    jxl_filter_frame_region region = pad.frame;

    jxl_frame_header_init(&fh);
    fh.width = (uint32_t)w + 8u;
    fh.height = (uint32_t)h + 8u;

    scratch_count = w * (h + 4);
    float *scratch_simd[3] = {NULL, NULL, NULL};
    float *scratch_scalar[3] = {NULL, NULL, NULL};
    for (c = 0; c < 3; ++c) {
        scratch_simd[c] = (float *)malloc(scratch_count * sizeof(float));
        scratch_scalar[c] = (float *)malloc(scratch_count * sizeof(float));
        assert(scratch_simd[c] && scratch_scalar[c]);
    }

    test_epf_force_scalar(1);
    ok = jxl_apply_epf_extent_step(test_ctx_get(), channels, &epf, &fh, NULL, 0, &region, scratch_scalar, 0);
    for (c = 0; c < 3; ++c) {
        jxl_subgrid_f32 step0_out = jxl_subgrid_f32_from_buf(scratch_scalar[c], w, h, w);
        jxl_padded_f32_place(&step0_out, &padded[c], pad.pad_left, pad.pad_top);
        jxl_padded_f32_mirror_trailing(&padded[c], pad.pad_left, pad.pad_top, w, h);
        memcpy(scratch_simd[c], scratch_scalar[c], scratch_count * sizeof(float));
    }

    test_epf_force_scalar(0);
    ok = ok && jxl_apply_epf_extent_step(test_ctx_get(), channels, &epf, &fh, NULL, 0, &region, scratch_simd, 1);

    for (c = 0; c < 3; ++c) {
        jxl_subgrid_f32 step0_out = jxl_subgrid_f32_from_buf(scratch_scalar[c], w, h, w);
        jxl_padded_f32_place(&step0_out, &padded[c], pad.pad_left, pad.pad_top);
        jxl_padded_f32_mirror_trailing(&padded[c], pad.pad_left, pad.pad_top, w, h);
    }
    test_epf_force_scalar(1);
    ok = ok && jxl_apply_epf_extent_step(test_ctx_get(), channels, &epf, &fh, NULL, 0, &region, scratch_scalar, 1);

    cmp = 1;
    for (c = 0; c < 3; ++c) {
        char tag[96];
        snprintf(tag, sizeof(tag), "padded step1 left=%d w%zu ch%zu", (int)frame_left, width, c);
        cmp &= compare_outputs(scratch_simd[c], scratch_scalar[c], w * h, tag);
    }

    for (c = 0; c < 3; ++c) {
        jxl_padded_f32_free(alloc, &padded[c]);
        free(work[c]);
        free(orig[c]);
        free(simd_out[c]);
        free(scalar_out[c]);
        free(scratch_simd[c]);
        free(scratch_scalar[c]);
    }
    return ok && cmp;
#endif
}

static int run_epf_step(unsigned step, size_t width, size_t height, int force_scalar) {
    size_t c;
#if !defined(JXL_HAVE_SIMD_SSE41)
    (void)step;
    (void)width;
    (void)height;
    (void)force_scalar;
    return 1;
#else
    jxl_filter_extent channels[3];
    jxl_frame_header fh;
    int ok;
    char tag[64];
    int cmp;
    jxl_epf_filter epf;
    size_t w = width;
    size_t h = height;
    float *buf[3];
    float *scratch[3];
    float *ref[3];
    for (c = 0; c < 3; ++c) {
        buf[c] = (float *)malloc(w * h * sizeof(float));
        scratch[c] = (float *)malloc(w * h * sizeof(float));
        ref[c] = (float *)malloc(w * h * sizeof(float));
        assert(buf[c] && scratch[c] && ref[c]);
    }

    fill_channels(buf, w, h, 17u + step * 31u + (unsigned)width);
    float *orig[3];
    for (c = 0; c < 3; ++c) {
        orig[c] = (float *)malloc(w * h * sizeof(float));
        assert(orig[c] != NULL);
        memcpy(orig[c], buf[c], w * h * sizeof(float));
        memcpy(scratch[c], buf[c], w * h * sizeof(float));
        memcpy(ref[c], buf[c], w * h * sizeof(float));
    }

    for (c = 0; c < 3; ++c) {
        jxl_filter_extent compound_tmp;
        compound_tmp.full = jxl_subgrid_f32_from_buf(buf[c], w, h, w);
        compound_tmp.origin_x = 0;
        compound_tmp.origin_y = 0;
        compound_tmp.width = w;
        compound_tmp.height = h;
        channels[c] = compound_tmp;

    }

    epf.enabled = 1;
    epf.iters = step == 0 ? 3u : (step == 1 ? 1u : 2u);
    epf.channel_scale[0] = 1.0f;
    epf.channel_scale[1] = 0.5f;
    epf.channel_scale[2] = 0.25f;
    epf.sigma.pass0_sigma_scale = 1.25f;
    epf.sigma.pass2_sigma_scale = 1.1f;
    epf.sigma.border_sad_mul = 1.5f;
    epf.sigma_for_modular = 1.0f;


    jxl_frame_header_init(&fh);
    fh.width = (uint32_t)w;
    fh.height = (uint32_t)h;

    ok = 1;
    test_epf_force_scalar(force_scalar);
    if (!jxl_apply_epf_extent(test_ctx_get(), channels, &epf, &fh, NULL, 0, NULL, scratch)) {
        ok = 0;
    }
    if (!force_scalar) {
        size_t c;
        for (c = 0; c < 3; ++c) {
            memcpy(ref[c], scratch[c], w * h * sizeof(float));
        }
    }

    if (!force_scalar) {
        size_t c;
        for (c = 0; c < 3; ++c) {
            memcpy(buf[c], orig[c], w * h * sizeof(float));
            memcpy(scratch[c], orig[c], w * h * sizeof(float));
        }
        test_epf_force_scalar(1);
        ok = ok && jxl_apply_epf_extent(test_ctx_get(), channels, &epf, &fh, NULL, 0, NULL, scratch);
    }

    snprintf(tag, sizeof(tag), "step%u w%zu scalar=%d", step, width, force_scalar);

    cmp = 1;
    if (!force_scalar) {
        size_t c;
        for (c = 0; c < 3; ++c) {
            char ctag[80];
            snprintf(ctag, sizeof(ctag), "%s ch%zu", tag, c);
            cmp &= compare_outputs(ref[c], scratch[c], w * h, ctag);
        }
    }

    for (c = 0; c < 3; ++c) {
        free(buf[c]);
        free(scratch[c]);
        free(ref[c]);
        free(orig[c]);
    }
    return ok && cmp;
#endif
}

int main(void) {
    size_t wi;
#if !defined(JXL_HAVE_SIMD_SSE41)
    printf("SSE4.1 not enabled\n");
    return 0;
#else
    int ok;
    unsigned step;
    static const size_t widths[] = {32, 64, 128, 256};
    ok = 1;
    for (wi = 0; wi < sizeof(widths) / sizeof(widths[0]); ++wi) {
        size_t w = widths[wi];
        size_t h = 16;
        if (!run_epf_step1_after_step0(w, h)) {
            ok = 0;
        }
        if (!run_epf_step1_padded(w, h, 4)) {
            ok = 0;
        }
        for (step = 0; step <= 2; ++step) {
            if (!run_epf_step(step, w, h, 0)) {
                ok = 0;
            }
        }
    }
    return ok ? 0 : 1;
#endif
}
