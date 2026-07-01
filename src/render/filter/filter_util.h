// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_RENDER_FILTER_UTIL_H_
#define JXL_RENDER_FILTER_UTIL_H_

#include "render/subgrid_f32.h"

#include <stddef.h>
#include "jxl_oxide/jxl_types.h"

jxl_inline size_t jxl_filter_mirror(int64_t offset, size_t len) {
    for (;;) {
        if (offset < 0) {
            offset = -(offset + 1);
        } else if ((size_t)offset >= len) {
            offset = (int64_t)(-(offset + 1)) + (int64_t)(len * 2);
        } else {
            return (size_t)offset;
        }
    }
}

typedef struct {
    jxl_subgrid_f32 full;
    size_t origin_x;
    size_t origin_y;
    size_t width;
    size_t height;
} jxl_filter_extent;

jxl_inline jxl_subgrid_f32 jxl_filter_extent_view(const jxl_filter_extent *ext) {
    if (ext == NULL) {
                jxl_subgrid_f32 result = {0};
        return result;

    }
    return jxl_subgrid_f32_sub(ext->full, ext->origin_x, ext->origin_y, ext->width, ext->height);
}

jxl_inline const float *jxl_filter_extent_row(const jxl_filter_extent *ext, size_t y) {
    if (ext == NULL || ext->full.data == NULL || y >= ext->height) {
        return NULL;
    }
    return ext->full.data + (ext->origin_y + y) * ext->full.stride + ext->origin_x;
}

jxl_inline float jxl_filter_extent_get(const jxl_filter_extent *ext, int64_t x, int64_t y) {
    if (ext == NULL || ext->full.data == NULL) {
        return 0.0f;
    }
    size_t mx = jxl_filter_mirror(x, ext->width);
    size_t my = jxl_filter_mirror(y, ext->height);
    return ext->full.data[(ext->origin_y + my) * ext->full.stride + (ext->origin_x + mx)];
}

#endif /* JXL_RENDER_FILTER_UTIL_H_ */
