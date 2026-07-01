// SPDX-License-Identifier: MIT OR Apache-2.0
#include "aligned_grid.h"

#include <assert.h>
#include <string.h>

static size_t align_offset_elements(const void *ptr, size_t elem_size) {
    const size_t align = JXL_GRID_ALIGN;
    const size_t extra = (size_t)((const uint8_t *)ptr) & (align - 1);
    return ((align - extra) % align) / elem_size;
}

static int grid_u32_empty_aligned(jxl_allocator_state *alloc, size_t width, size_t height,
                                  jxl_grid_alloc_tracker *tracker, jxl_grid_u32 *out,
                                  jxl_grid_oom *oom) {
    size_t len;
    size_t buf_len;
    size_t pad;
    jxl_grid_alloc_handle *handle;
    uint32_t *buf;
    size_t offset;
    if (alloc == NULL) {
        return 0;
    }
    len = 0;
    if (width != 0 && height != 0) {
        assert(width <= SIZE_MAX / height);
        len = width * height;
    }
    pad = (JXL_GRID_ALIGN - 1) / sizeof(uint32_t);
    buf_len = len + pad;
    assert(buf_len >= len);

    handle = NULL;
    if (tracker != NULL) {
        size_t bytes = buf_len * sizeof(uint32_t);
        if (!jxl_grid_alloc_tracker_alloc(tracker, bytes, &handle)) {
            if (oom != NULL) {
                oom->bytes = bytes;
            }
            return 0;
        }
    }

    buf = jxl_calloc(alloc, buf_len, sizeof(uint32_t));
    if (buf == NULL) {
        jxl_grid_alloc_handle_release(handle);
        if (oom != NULL) {
            oom->bytes = buf_len * sizeof(uint32_t);
        }
        return 0;
    }

    offset = align_offset_elements(buf, sizeof(uint32_t));
    assert(offset + len >= offset);

    out->width = width;
    out->height = height;
    out->offset = offset;
    out->buf = buf;
    out->buf_len = buf_len;
    out->handle = handle;
    return 1;
}

void jxl_grid_u32_init_empty(jxl_grid_u32 *g) {
    memset(g, 0, sizeof(*g));
}

int jxl_grid_u32_create(jxl_allocator_state *alloc, size_t width, size_t height,
                        jxl_grid_alloc_tracker *tracker, jxl_grid_u32 *out, jxl_grid_oom *oom) {
    size_t len;
    if (out == NULL || alloc == NULL) {
        return 0;
    }
    jxl_grid_u32_init_empty(out);

    if (!grid_u32_empty_aligned(alloc, width, height, tracker, out, oom)) {
        return 0;
    }

    len = width * height;
    if (len > 0) {
        memset(out->buf + out->offset, 0, len * sizeof(uint32_t));
    }
    return 1;
}

void jxl_grid_u32_destroy(jxl_allocator_state *alloc, jxl_grid_u32 *g) {
    if (g == NULL || alloc == NULL) {
        return;
    }
    jxl_free(alloc, g->buf);
    jxl_grid_alloc_handle_release(g->handle);
    jxl_grid_u32_init_empty(g);
}

size_t jxl_grid_u32_width(const jxl_grid_u32 *g) { return g != NULL ? g->width : 0; }
size_t jxl_grid_u32_height(const jxl_grid_u32 *g) { return g != NULL ? g->height : 0; }

uint32_t *jxl_grid_u32_buf(jxl_grid_u32 *g) {
    return g != NULL && g->buf != NULL ? g->buf + g->offset : NULL;
}

const uint32_t *jxl_grid_u32_buf_const(const jxl_grid_u32 *g) {
    return g != NULL && g->buf != NULL ? g->buf + g->offset : NULL;
}

int jxl_grid_u32_try_get(const jxl_grid_u32 *g, size_t x, size_t y, uint32_t *out) {
    if (g == NULL || out == NULL || x >= g->width || y >= g->height) {
        return 0;
    }
    *out = g->buf[y * g->width + x + g->offset];
    return 1;
}

uint32_t jxl_grid_u32_get(const jxl_grid_u32 *g, size_t x, size_t y) {
    uint32_t v = 0;
    if (!jxl_grid_u32_try_get(g, x, y, &v)) {
        assert(0);
    }
    return v;
}

void jxl_grid_u32_set(jxl_grid_u32 *g, size_t x, size_t y, uint32_t v) {
    assert(g != NULL && x < g->width && y < g->height);
    g->buf[y * g->width + x + g->offset] = v;
}

const uint32_t *jxl_grid_u32_row(const jxl_grid_u32 *g, size_t row) {
    assert(g != NULL && row < g->height);
    return &g->buf[row * g->width + g->offset];
}

void jxl_grid_f32_init_empty(jxl_grid_f32 *g) {
    memset(g, 0, sizeof(*g));
}

int jxl_grid_f32_create(jxl_allocator_state *alloc, size_t width, size_t height,
                        jxl_grid_alloc_tracker *tracker, jxl_grid_f32 *out, jxl_grid_oom *oom) {
    size_t len;
    size_t buf_len;
    size_t pad;
    jxl_grid_alloc_handle *handle;
    float *buf;
    size_t offset;
    if (out == NULL || alloc == NULL) {
        return 0;
    }
    jxl_grid_f32_init_empty(out);

    len = 0;
    if (width != 0 && height != 0) {
        assert(width <= SIZE_MAX / height);
        len = width * height;
    }
    pad = (JXL_GRID_ALIGN - 1) / sizeof(float);
    buf_len = len + pad;
    assert(buf_len >= len);

    handle = NULL;
    if (tracker != NULL) {
        size_t bytes = buf_len * sizeof(float);
        if (!jxl_grid_alloc_tracker_alloc(tracker, bytes, &handle)) {
            if (oom != NULL) {
                oom->bytes = bytes;
            }
            return 0;
        }
    }

    buf = jxl_calloc(alloc, buf_len, sizeof(float));
    if (buf == NULL) {
        jxl_grid_alloc_handle_release(handle);
        if (oom != NULL) {
            oom->bytes = buf_len * sizeof(float);
        }
        return 0;
    }

    offset = align_offset_elements(buf, sizeof(float));
    out->width = width;
    out->height = height;
    out->offset = offset;
    out->buf = buf;
    out->buf_len = buf_len;
    out->handle = handle;
    return 1;
}

void jxl_grid_f32_destroy(jxl_allocator_state *alloc, jxl_grid_f32 *g) {
    if (g == NULL || alloc == NULL) {
        return;
    }
    jxl_free(alloc, g->buf);
    jxl_grid_alloc_handle_release(g->handle);
    jxl_grid_f32_init_empty(g);
}

size_t jxl_grid_f32_width(const jxl_grid_f32 *g) { return g != NULL ? g->width : 0; }
size_t jxl_grid_f32_height(const jxl_grid_f32 *g) { return g != NULL ? g->height : 0; }

float *jxl_grid_f32_buf(jxl_grid_f32 *g) {
    return g != NULL && g->buf != NULL ? g->buf + g->offset : NULL;
}

const float *jxl_grid_f32_buf_const(const jxl_grid_f32 *g) {
    return g != NULL && g->buf != NULL ? g->buf + g->offset : NULL;
}
