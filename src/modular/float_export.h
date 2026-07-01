// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_MODULAR_FLOAT_EXPORT_H_
#define JXL_MODULAR_FLOAT_EXPORT_H_

#include "render/simd/features.h"

#include <stddef.h>
#include <stdint.h>

#define JXL_MODULAR_FLOAT_EXPORT_MAX_PLANES 4

typedef struct {
    int active;
    int color_exported;
    float scale;
    uint32_t src_x0;
    uint32_t src_y0;
    uint32_t dst_x0;
    uint32_t dst_y0;
    uint32_t blit_w;
    uint32_t blit_h;
    uint32_t canvas_stride;
    size_t first_plane;
    uint32_t num_color_planes;
    float *planes[JXL_MODULAR_FLOAT_EXPORT_MAX_PLANES];
    const jxl_cpu_features *cpu;
    struct {
        int enabled;
        uint32_t begin_c;
    } rct;
} jxl_modular_float_export_ctx;

void jxl_modular_float_export_ctx_init(jxl_modular_float_export_ctx *ctx);

void jxl_modular_export_i16_row_to_plane(const int16_t *src, float *dst, size_t n, float scale,
                                         const jxl_cpu_features *cpu);

void jxl_modular_export_rct_row(const jxl_modular_float_export_ctx *ctx, uint32_t grid_y,
                                const int16_t *row0, const int16_t *row1, const int16_t *row2);

void jxl_modular_rct_inverse_export_row3_i16(uint32_t ty, int16_t *row0, int16_t *row1,
                                             int16_t *row2, size_t width,
                                             const jxl_modular_float_export_ctx *ctx,
                                             uint32_t grid_y);

#endif /* JXL_MODULAR_FLOAT_EXPORT_H_ */
