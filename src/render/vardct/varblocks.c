// SPDX-License-Identifier: MIT OR Apache-2.0
#include "varblocks.h"

#include "render/vardct/dct_2d.h"
#include "render/vardct/dct_common.h"
#include "render/vardct/transform.h"

#include <assert.h>

static size_t trailing_zeros_size(size_t n) {
    size_t c;
    if (n == 0) {
        return 0;
    }
#if defined(__GNUC__) || defined(__clang__)
    return (size_t)__builtin_ctz((unsigned)n);
#else
    c = 0;
    while ((n & 1u) == 0) {
        ++c;
        n >>= 1;
    }
    return c;
#endif
}

static const jxl_block_info *block_info_at(jxl_block_info_subgrid sg, size_t bx, size_t by) {
    return &sg.data[by * sg.stride + bx];
}

static int transform_uses_single_lf_sample(jxl_transform_type t) {
    switch (t) {
    case JXL_TRANSFORM_HORNUSS:
    case JXL_TRANSFORM_DCT2:
    case JXL_TRANSFORM_DCT4:
    case JXL_TRANSFORM_DCT8X4:
    case JXL_TRANSFORM_DCT4X8:
    case JXL_TRANSFORM_DCT8:
    case JXL_TRANSFORM_AFV0:
    case JXL_TRANSFORM_AFV1:
    case JXL_TRANSFORM_AFV2:
    case JXL_TRANSFORM_AFV3:
        return 1;
    default:
        return 0;
    }
}

void jxl_for_each_varblocks(jxl_block_info_subgrid block_info, jxl_channel_shift shift,
                          jxl_varblock_fn fn, void *ctx) {
    size_t by;
    int32_t vshift;
    int32_t hshift;

    if (fn == NULL) {
        return;
    }
    vshift = jxl_channel_shift_vshift(&shift);
    hshift = jxl_channel_shift_hshift(&shift);

    for (by = 0; by < block_info.height; ++by) {
        size_t bx;
        for (bx = 0; bx < block_info.width; ++bx) {
            jxl_varblock_info vb;
            size_t shifted_bx;
            size_t shifted_by;
            const jxl_block_info *info = block_info_at(block_info, bx, by);
            if (info->kind != JXL_BLOCK_INFO_DATA) {
                continue;
            }

            shifted_bx = bx >> (size_t)hshift;
            shifted_by = by >> (size_t)vshift;
            if (hshift != 0 || vshift != 0) {
                if ((shifted_bx << (size_t)hshift) != bx || (shifted_by << (size_t)vshift) != by) {
                    continue;
                }
                if (block_info_at(block_info, shifted_bx, shifted_by)->kind != JXL_BLOCK_INFO_DATA) {
                    continue;
                }
            }

            vb.shifted_bx = shifted_bx;
            vb.shifted_by = shifted_by;
            vb.dct_select = info->dct_select;
            vb.hf_mul = info->hf_mul;

            fn(&vb, ctx);
        }
    }
}

typedef struct {
    jxl_context *ctx;
    jxl_const_subgrid_f32 lf;
    jxl_subgrid_f32 coeff;
    jxl_allocator_state *alloc;
} jxl_transform_varblocks_ctx;

static void transform_varblock_cb(const jxl_varblock_info *info, void *ctx_void) {
    uint32_t bw;
    uint32_t bh;
    size_t left;
    size_t top;
    size_t logbw;
    size_t logbh;
    jxl_subgrid_f32 out;
    const float *lf;
    size_t lf_stride;
    float *out_data;
    size_t out_stride;
    jxl_subgrid_f32 block;
    jxl_transform_varblocks_ctx *ctx = (jxl_transform_varblocks_ctx *)ctx_void;
    if (ctx->lf.data == NULL) {
        return;
    }

    bw = 1;
    bh = 1;
    jxl_transform_dct_select_size(info->dct_select, &bw, &bh);
    left = info->shifted_bx * 8;
    top = info->shifted_by * 8;
    logbw = trailing_zeros_size((size_t)bw);
    logbh = trailing_zeros_size((size_t)bh);

    out = jxl_subgrid_f32_sub(ctx->coeff, left, top, (size_t)bw, (size_t)bh);
    lf = ctx->lf.data;
    lf_stride = ctx->lf.stride;
    out_data = out.data;
    out_stride = out.stride;

    if (transform_uses_single_lf_sample(info->dct_select)) {
        assert(bw * bh == 1);
        out_data[0] = lf[info->shifted_by * lf_stride + info->shifted_bx];
    } else {
        size_t y;
        for (y = 0; y < (size_t)bh; ++y) {
            size_t x;
            const float *lf_row = lf + (info->shifted_by + y) * lf_stride + info->shifted_bx;
            float *out_row = out_data + y * out_stride;
            for (x = 0; x < (size_t)bw; ++x) {
                out_row[x] = lf_row[x];
            }
        }
        jxl_dct_2d(ctx->alloc, out, JXL_DCT_FORWARD);
        for (y = 0; y < (size_t)bh; ++y) {
            size_t x;
            float *out_row = out_data + y * out_stride;
            float scale_y = jxl_scale_f(y, 5 - logbh);
            for (x = 0; x < (size_t)bw; ++x) {
                out_row[x] /= scale_y * jxl_scale_f(x, 5 - logbw);
            }
        }
    }

    block =
        jxl_subgrid_f32_sub(ctx->coeff, left, top, (size_t)bw * 8, (size_t)bh * 8);

    jxl_render_transform_varblock(ctx->ctx, ctx->alloc, block, info->dct_select);
}

static void transform_varblocks_channel(jxl_context *ctx, jxl_allocator_state *alloc,
                                      jxl_const_subgrid_f32 lf, jxl_subgrid_f32 coeff,
                                      jxl_channel_shift shift, jxl_block_info_subgrid block_info) {
    jxl_transform_varblocks_ctx vctx = {0};
    vctx.ctx = ctx;
    vctx.lf = lf;
    vctx.coeff = coeff;
    vctx.alloc = alloc;

    jxl_for_each_varblocks(block_info, shift, transform_varblock_cb, &vctx);
}

void jxl_render_transform_varblocks(jxl_context *ctx, jxl_allocator_state *alloc,
                                    const jxl_const_subgrid_f32 lf[3],
                                    jxl_subgrid_f32 coeff_out[3],
                                    const jxl_channel_shift shifts_cbycr[3],
                                    jxl_block_info_subgrid block_info) {
                                        size_t channel;
    assert(lf != NULL && coeff_out != NULL && shifts_cbycr != NULL);
    for (channel = 0; channel < 3; ++channel) {
        transform_varblocks_channel(ctx, alloc, lf[channel], coeff_out[channel],
                                    shifts_cbycr[channel], block_info);
    }
}
