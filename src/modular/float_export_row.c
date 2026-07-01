// SPDX-License-Identifier: MIT OR Apache-2.0
#include "float_export.h"

#include "modular/transform/rct_internal.h"
#include "render/simd/features.h"

void jxl_modular_float_export_ctx_init(jxl_modular_float_export_ctx *ctx) {
    if (ctx != NULL) {
        ctx->active = 0;
        ctx->color_exported = 0;
        ctx->cpu = NULL;
    }
}

void jxl_modular_export_i16_row_to_plane(const int16_t *src, float *dst, size_t n, float scale,
                                         const jxl_cpu_features *cpu) {
    size_t x;
    JXL_CPU_FEATURES_LOCAL(local_feat);
    const jxl_cpu_features *feat = cpu != NULL ? cpu : local_feat;
#if defined(JXL_HAVE_SIMD_AVX2)
    if (feat->avx2 != 0 && n >= 8) {
        extern void jxl_modular_blit_i16_row_to_plane_avx2(const int16_t *src, float *dst,
                                                           size_t n, float scale);
        jxl_modular_blit_i16_row_to_plane_avx2(src, dst, n, scale);
        return;
    }
#endif
    for (x = 0; x < n; ++x) {
        dst[x] = (float)src[x] * scale;
    }
}

void jxl_modular_export_rct_row(const jxl_modular_float_export_ctx *ctx, uint32_t grid_y,
                                const int16_t *row0, const int16_t *row1, const int16_t *row2) {
    uint32_t rel_y;
    size_t dst_row;
    const int16_t *src0;
    const int16_t *src1;
    const int16_t *src2;
    float *dst0;
    float *dst1;
    float *dst2;

    if (ctx == NULL || !ctx->active || !ctx->rct.enabled || row0 == NULL || row1 == NULL ||
        row2 == NULL) {
        return;
    }
    if (grid_y < ctx->src_y0) {
        return;
    }
    rel_y = grid_y - ctx->src_y0;
    if (rel_y >= ctx->blit_h || ctx->blit_w == 0) {
        return;
    }
    if (ctx->num_color_planes < 3u || ctx->planes[0] == NULL || ctx->planes[1] == NULL ||
        ctx->planes[2] == NULL) {
        return;
    }

    dst_row = (size_t)(ctx->dst_y0 + rel_y) * (size_t)ctx->canvas_stride + (size_t)ctx->dst_x0;
    src0 = row0 + ctx->src_x0;
    src1 = row1 + ctx->src_x0;
    src2 = row2 + ctx->src_x0;
    dst0 = ctx->planes[0] + dst_row;
    dst1 = ctx->planes[1] + dst_row;
    dst2 = ctx->planes[2] + dst_row;

    jxl_modular_export_i16_row_to_plane(src0, dst0, ctx->blit_w, ctx->scale, ctx->cpu);
    jxl_modular_export_i16_row_to_plane(src1, dst1, ctx->blit_w, ctx->scale, ctx->cpu);
    jxl_modular_export_i16_row_to_plane(src2, dst2, ctx->blit_w, ctx->scale, ctx->cpu);
}

void jxl_modular_rct_inverse_export_row3_i16(uint32_t ty, int16_t *row0, int16_t *row1,
                                             int16_t *row2, size_t width,
                                             const jxl_modular_float_export_ctx *ctx,
                                             uint32_t grid_y) {
    int export_row = 0;
    uint32_t export_x0 = 0;
    uint32_t export_w = 0;
    float scale;
    float *dst0 = NULL;
    float *dst1 = NULL;
    float *dst2 = NULL;
    scale = 0.0f;

    if (ctx != NULL && ctx->active && ctx->rct.enabled && grid_y >= ctx->src_y0 &&
        grid_y - ctx->src_y0 < ctx->blit_h && ctx->blit_w > 0 && ctx->num_color_planes >= 3u &&
        ctx->planes[0] != NULL && ctx->planes[1] != NULL && ctx->planes[2] != NULL) {
        uint32_t rel_y = grid_y - ctx->src_y0;
        size_t dst_row =
            (size_t)(ctx->dst_y0 + rel_y) * (size_t)ctx->canvas_stride + (size_t)ctx->dst_x0;
        export_row = 1;
        export_x0 = ctx->src_x0;
        export_w = ctx->blit_w;
        scale = ctx->scale;
        dst0 = ctx->planes[0] + dst_row;
        dst1 = ctx->planes[1] + dst_row;
        dst2 = ctx->planes[2] + dst_row;
    }

#if defined(JXL_HAVE_SIMD_AVX2)
    if (ctx != NULL && ctx->cpu != NULL && ctx->cpu->avx2) {
        jxl_rct_inverse_export_row3_i16_avx2(ty, row0, row1, row2, width, export_row, export_x0,
                                           export_w, scale, dst0, dst1, dst2);
        return;
    }
#endif

    {
        size_t x;
        for (x = 0; x < width; ++x) {
            jxl_rct_inverse_row_i16_pixel(ty, &row0[x], &row1[x], &row2[x]);
            if (export_row && x >= export_x0 && x < export_x0 + export_w) {
                size_t out_x = x - export_x0;
                dst0[out_x] = (float)row0[x] * scale;
                dst1[out_x] = (float)row1[x] * scale;
                dst2[out_x] = (float)row2[x] * scale;
            }
        }
    }
}
