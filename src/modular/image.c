// SPDX-License-Identifier: MIT OR Apache-2.0
#include "image.h"

#include "modular/group_subimage.h"

#include "jxl_oxide/jxl_types.h"
#include <string.h>

#define JXL_MODULAR_GRID_ALIGN 32

void jxl_modular_grid_i32_init_empty(jxl_modular_grid_i32 *g) {
    if (g != NULL) {
        memset(g, 0, sizeof(*g));
        g->kind = JXL_MODULAR_SAMPLE_I32;
    }
}

void jxl_modular_grid_normalize_stride(jxl_modular_grid *g) {
    if (g != NULL && g->stride == 0 && g->width > 0) {
        g->stride = g->width;
    }
}

static size_t align_offset_elements(const void *ptr, size_t elem_size) {
    const size_t align = JXL_MODULAR_GRID_ALIGN;
    const size_t extra = (size_t)((const uint8_t *)ptr) & (align - 1);
    return ((align - extra) % align) / elem_size;
}

jxl_modular_sample_kind jxl_modular_grid_sample_kind(const jxl_modular_grid_i32 *g) {
    if (g == NULL) {
        return JXL_MODULAR_SAMPLE_I32;
    }
    return g->kind;
}

size_t jxl_modular_grid_elem_size(const jxl_modular_grid_i32 *g) {
    return jxl_modular_grid_sample_kind(g) == JXL_MODULAR_SAMPLE_I16 ? sizeof(int16_t)
                                                                     : sizeof(int32_t);
}

static size_t grid_index(const jxl_modular_grid_i32 *g, size_t x, size_t y) {
    return g->offset + y * jxl_modular_grid_row_stride(g) + x;
}

int32_t jxl_modular_grid_sample_as_i32(const jxl_modular_grid_i32 *g, size_t x, size_t y) {
    size_t idx;
    if (g == NULL || g->buf == NULL || x >= g->width || y >= g->height) {
        return 0;
    }
    idx = grid_index(g, x, y);
    if (g->kind == JXL_MODULAR_SAMPLE_I16) {
        return (int32_t)((int16_t *)g->buf)[idx];
    }
    return ((int32_t *)g->buf)[idx];
}

void jxl_modular_grid_store_i32(jxl_modular_grid_i32 *g, size_t x, size_t y, int32_t sample) {
    size_t idx;
    if (g == NULL || g->buf == NULL || x >= g->width || y >= g->height) {
        return;
    }
    idx = grid_index(g, x, y);
    if (g->kind == JXL_MODULAR_SAMPLE_I16) {
        ((int16_t *)g->buf)[idx] = (int16_t)sample;
    } else {
        ((int32_t *)g->buf)[idx] = sample;
    }
}

int16_t *jxl_modular_grid_row_i16(jxl_modular_grid_i32 *g, size_t y) {
    if (g == NULL || g->buf == NULL || g->kind != JXL_MODULAR_SAMPLE_I16) {
        return NULL;
    }
    return (int16_t *)g->buf + grid_index(g, 0, y);
}

int32_t *jxl_modular_grid_row_i32(jxl_modular_grid_i32 *g, size_t y) {
    if (g == NULL || g->buf == NULL || g->kind != JXL_MODULAR_SAMPLE_I32) {
        return NULL;
    }
    return (int32_t *)g->buf + grid_index(g, 0, y);
}

const int16_t *jxl_modular_grid_row_i16_const(const jxl_modular_grid_i32 *g, size_t y) {
    if (g == NULL || g->buf == NULL || g->kind != JXL_MODULAR_SAMPLE_I16) {
        return NULL;
    }
    return (const int16_t *)g->buf + grid_index(g, 0, y);
}

const int32_t *jxl_modular_grid_row_i32_const(const jxl_modular_grid_i32 *g, size_t y) {
    if (g == NULL || g->buf == NULL || g->kind != JXL_MODULAR_SAMPLE_I32) {
        return NULL;
    }
    return (const int32_t *)g->buf + grid_index(g, 0, y);
}

jxl_modular_grid jxl_modular_grid_tile_view(jxl_modular_grid *parent, size_t tile_x, size_t tile_y,
                                            size_t tile_w, size_t tile_h) {
    jxl_modular_grid view;
    jxl_modular_grid_normalize_stride(parent);
    view = *parent;
    view.offset += tile_y * parent->stride + tile_x;
    view.width = tile_w;
    view.height = tile_h;
    view.stride = parent->stride;
    return view;
}

jxl_modular_grid jxl_modular_grid_split_horizontal_in_place(jxl_modular_grid *g) {
    size_t split_x = (g->width + 1) / 2;
    size_t orig_w = g->width;
    jxl_modular_grid right;
    jxl_modular_grid_normalize_stride(g);
    right = *g;
    g->width = split_x;
    right.offset = g->offset + split_x;
    right.width = orig_w - split_x;
    return right;
}

jxl_modular_grid jxl_modular_grid_split_vertical_in_place(jxl_modular_grid *g) {
    size_t split_y = (g->height + 1) / 2;
    size_t orig_h = g->height;
    jxl_modular_grid bottom;
    jxl_modular_grid_normalize_stride(g);
    bottom = *g;
    g->height = split_y;
    bottom.offset = g->offset + split_y * g->stride;
    bottom.height = orig_h - split_y;
    return bottom;
}

void jxl_modular_grid_split_h_in_place(jxl_modular_grid *g) {
    (void)jxl_modular_grid_split_horizontal_in_place(g);
}

void jxl_modular_grid_split_v_in_place(jxl_modular_grid *g) {
    (void)jxl_modular_grid_split_vertical_in_place(g);
}

int jxl_modular_grid_group_view_at(jxl_modular_grid *parent, size_t group_width,
                                   size_t group_height, size_t num_cols, size_t num_rows,
                                   size_t group_idx, jxl_modular_grid *out) {
    size_t gx;
    size_t gy;
    size_t stride;
    size_t y;
    size_t x;
    size_t gh;
    size_t gw;
    if (parent == NULL || out == NULL || group_width == 0 || group_height == 0 || num_cols == 0 ||
        num_rows == 0) {
        return 0;
    }
    if (group_idx >= num_cols * num_rows) {
        return 0;
    }
    gx = group_idx % num_cols;
    gy = group_idx / num_cols;
    jxl_modular_grid_normalize_stride(parent);
    stride = parent->stride;
    y = gy * group_height;
    if (y > parent->height) {
        y = parent->height;
    }
    gh = parent->height > y ? parent->height - y : 0;
    if (gh > group_height) {
        gh = group_height;
    }
    x = gx * group_width;
    if (x > parent->width) {
        x = parent->width;
    }
    gw = parent->width > x ? parent->width - x : 0;
    if (gw > group_width) {
        gw = group_width;
    }
    if (gh == 0 || gw == 0) {
        return 0;
    }
    *out = *parent;
    if (parent->width > 0) {
        out->offset += y * stride + x;
    }
    out->width = gw;
    out->height = gh;
    out->stride = stride;
    return 1;
}

int jxl_modular_grid_create(jxl_allocator_state *alloc, size_t width, size_t height,
                            jxl_grid_alloc_tracker *tracker,
                            jxl_modular_sample_kind kind, jxl_modular_grid_i32 *out) {
    size_t len;
    size_t buf_len;
    size_t elem_size;
    size_t pad;
    jxl_grid_alloc_handle *handle;
    void *buf;
    if (out == NULL) {
        return 0;
    }
    jxl_modular_grid_i32_init_empty(out);
    elem_size = kind == JXL_MODULAR_SAMPLE_I16 ? sizeof(int16_t) : sizeof(int32_t);
    len = 0;
    if (width != 0 && height != 0) {
        if (width > SIZE_MAX / height) {
            return 0;
        }
        len = width * height;
    }
    pad = (JXL_MODULAR_GRID_ALIGN - 1) / elem_size;
    buf_len = len + pad;

    handle = NULL;
    if (tracker != NULL) {
        size_t bytes = buf_len * elem_size;
        if (!jxl_grid_alloc_tracker_alloc(tracker, bytes, &handle)) {
            return 0;
        }
    }

    buf = jxl_calloc(alloc, buf_len, elem_size);
    if (buf == NULL) {
        jxl_grid_alloc_handle_release(handle);
        return 0;
    }

    out->kind = kind;
    out->width = width;
    out->height = height;
    out->stride = width;
    out->offset = align_offset_elements(buf, elem_size);
    out->buf = buf;
    out->buf_len = buf_len;
    out->handle = handle;
    return 1;
}

int jxl_modular_grid_i32_create(jxl_allocator_state *alloc, size_t width, size_t height,
                                jxl_grid_alloc_tracker *tracker,
                                jxl_modular_grid_i32 *out) {
    return jxl_modular_grid_create(alloc, width, height, tracker, JXL_MODULAR_SAMPLE_I32, out);
}

int jxl_modular_grid_i16_create(jxl_allocator_state *alloc, size_t width, size_t height,
                                jxl_grid_alloc_tracker *tracker,
                                jxl_modular_grid_i32 *out) {
    return jxl_modular_grid_create(alloc, width, height, tracker, JXL_MODULAR_SAMPLE_I16, out);
}

static int grid_buf_seen(void *buf, void **seen, size_t seen_len) {
    size_t i;
    if (buf == NULL) {
        return 1;
    }
    for (i = 0; i < seen_len; ++i) {
        if (seen[i] == buf) {
            return 1;
        }
    }
    return 0;
}

void jxl_modular_grid_i32_destroy(jxl_allocator_state *alloc, jxl_modular_grid_i32 *g) {
    if (g == NULL) {
        return;
    }
    jxl_free(alloc, g->buf);
    jxl_grid_alloc_handle_release(g->handle);
    jxl_modular_grid_i32_init_empty(g);
}

int jxl_modular_grid_clone(jxl_allocator_state *alloc, const jxl_modular_grid_i32 *src,
                           jxl_modular_grid_i32 *dst) {
    size_t y;
    size_t elem_size;
    size_t row_bytes;
    if (alloc == NULL || src == NULL || dst == NULL || src->buf == NULL || src->width == 0 ||
        src->height == 0) {
        return 0;
    }
    jxl_modular_grid_i32_init_empty(dst);
    if (!jxl_modular_grid_create(alloc, src->width, src->height, NULL, src->kind, dst)) {
        return 0;
    }
    elem_size = jxl_modular_grid_elem_size(src);
    row_bytes = src->width * elem_size;
    jxl_modular_grid_normalize_stride((jxl_modular_grid_i32 *)src);
    jxl_modular_grid_normalize_stride(dst);
    for (y = 0; y < src->height; ++y) {
        memcpy((uint8_t *)dst->buf + y * dst->stride * elem_size,
               (const uint8_t *)src->buf + (src->offset + y * src->stride) * elem_size, row_bytes);
    }
    return 1;
}

static void modular_grids_destroy_unique(jxl_allocator_state *alloc, jxl_modular_grid_i32 *grids, size_t len) {
    size_t i;
    size_t seen_len;
    size_t seen_handle_len;
    void **seen_bufs;
    jxl_grid_alloc_handle **seen_handles;
    if (grids == NULL || len == 0) {
        return;
    }
    seen_bufs = jxl_calloc(alloc, len, sizeof(*seen_bufs));
    seen_len = 0;
    seen_handles = jxl_calloc(alloc, len, sizeof(*seen_handles));
    seen_handle_len = 0;
    if (seen_bufs == NULL || seen_handles == NULL) {
        size_t i;
        jxl_free(alloc, seen_bufs);
        jxl_free(alloc, seen_handles);
        for (i = 0; i < len; ++i) {
            jxl_modular_grid_i32_destroy(alloc, &grids[i]);
        }
        return;
    }
    for (i = 0; i < len; ++i) {
        int handle_dup;
        jxl_modular_grid_i32 *g = &grids[i];
        int buf_dup = grid_buf_seen(g->buf, seen_bufs, seen_len);
        if (!buf_dup && g->buf != NULL) {
            seen_bufs[seen_len++] = g->buf;
        }
        handle_dup = 0;
        if (g->handle != NULL) {
            size_t h;
            for (h = 0; h < seen_handle_len; ++h) {
                if (seen_handles[h] == g->handle) {
                    handle_dup = 1;
                    break;
                }
            }
            if (!handle_dup) {
                seen_handles[seen_handle_len++] = g->handle;
            }
        }
        if (!buf_dup) {
            jxl_free(alloc, g->buf);
        }
        if (!handle_dup) {
            jxl_grid_alloc_handle_release(g->handle);
        }
        jxl_modular_grid_i32_init_empty(g);
    }
    jxl_free(alloc, seen_bufs);
    jxl_free(alloc, seen_handles);
}

void jxl_modular_image_destination_init(jxl_modular_image_destination *dest) {
    if (dest != NULL) {
        memset(dest, 0, sizeof(*dest));
        dest->sample_kind = JXL_MODULAR_SAMPLE_I32;
        jxl_modular_channels_init(&dest->channels);
        jxl_modular_channels_init(&dest->transformed_channels);
        jxl_ma_config_init(&dest->ma_ctx);
    }
}

void jxl_modular_image_destination_free(jxl_allocator_state *alloc,
                                       jxl_modular_image_destination *dest) {
    if (dest == NULL) {
        return;
    }
    jxl_modular_header_free(alloc, &dest->header);
    if (dest->ma_owns) {
        jxl_ma_config_destroy(alloc, &dest->ma_ctx);
    }
    if (dest->meta_channels != NULL) {
        modular_grids_destroy_unique(alloc, dest->meta_channels, dest->meta_channels_len);
        jxl_free(alloc, dest->meta_channels);
    }
    if (dest->transformed_grids != NULL) {
        jxl_transformed_grids_teardown(alloc, dest->transformed_grids, dest->transformed_grids_len);
        jxl_free(alloc, dest->transformed_grids);
    }
    if (dest->image_channels != NULL) {
        modular_grids_destroy_unique(alloc, dest->image_channels, dest->image_channels_len);
        jxl_free(alloc, dest->image_channels);
    }
    jxl_modular_clear_group_layout(alloc, dest);
    jxl_modular_channels_free(alloc, &dest->channels);
    jxl_modular_channels_free(alloc, &dest->transformed_channels);
    jxl_modular_image_destination_init(dest);
}

static jxl_modular_sample_kind alloc_kind_for_destination(jxl_modular_sample_kind requested) {
    return requested;
}

jxl_modular_status_t jxl_modular_image_destination_create(
    jxl_allocator_state *alloc, jxl_modular_header_ma *header_ma, uint32_t group_dim,
    uint32_t bit_depth, jxl_modular_sample_kind sample_kind, const jxl_modular_channels *channels,
    jxl_grid_alloc_tracker *tracker, jxl_modular_image_destination *out) {
    size_t i;
    jxl_modular_sample_kind grid_kind;
    if (header_ma == NULL || channels == NULL || out == NULL) {
        return JXL_MODULAR_BITSTREAM_ERROR;
    }
    jxl_modular_image_destination_free(alloc, out);
    jxl_modular_image_destination_init(out);

    out->header = header_ma->header;
    header_ma->header.transform = NULL;
    header_ma->header.transform_len = 0;
    out->ma_ctx = header_ma->ma_ctx;
    out->ma_owns = header_ma->ma_owns;
    header_ma->ma_owns = 0;
    jxl_ma_config_init(&header_ma->ma_ctx);

    out->group_dim = group_dim;
    out->bit_depth = bit_depth;
    out->sample_kind = sample_kind;
    grid_kind = alloc_kind_for_destination(sample_kind);

    if (channels->info_len > 0) {
        out->channels.info = jxl_alloc(alloc, channels->info_len * sizeof(*out->channels.info));
        if (out->channels.info == NULL) {
            jxl_modular_image_destination_free(alloc, out);
            return JXL_MODULAR_OUT_OF_MEMORY;
        }
        memcpy(out->channels.info, channels->info, channels->info_len * sizeof(*out->channels.info));
        out->channels.info_len = channels->info_len;
        out->channels.info_cap = channels->info_len;
        out->channels.nb_meta_channels = channels->nb_meta_channels;
    }

    for (i = 0; i < out->header.transform_len; ++i) {
        size_t w = 0;
        size_t h = 0;
        size_t new_len;
        jxl_modular_grid_i32 *grown;
        if (jxl_transform_prepare_meta_channels(&out->header.transform[i], &w, &h) !=
            JXL_MODULAR_OK) {
            jxl_modular_image_destination_free(alloc, out);
            return JXL_MODULAR_OUT_OF_MEMORY;
        }
        if (h == 0 ||
            (w == 0 && out->header.transform[i].kind != JXL_TRANSFORM_KIND_PALETTE)) {
            continue;
        }
        new_len = out->meta_channels_len + 1;
        grown =
            jxl_realloc(alloc, out->meta_channels, new_len * sizeof(*out->meta_channels));
        if (grown == NULL) {
            jxl_modular_image_destination_free(alloc, out);
            return JXL_MODULAR_OUT_OF_MEMORY;
        }
        out->meta_channels = grown;
        if (out->meta_channels_len > 0) {
            memmove(&out->meta_channels[1], &out->meta_channels[0],
                    out->meta_channels_len * sizeof(*out->meta_channels));
        }
        jxl_modular_grid_i32_init_empty(&out->meta_channels[0]);
        if (!jxl_modular_grid_create(alloc, w, h, tracker, grid_kind, &out->meta_channels[0])) {
            jxl_modular_image_destination_free(alloc, out);
            return JXL_MODULAR_OUT_OF_MEMORY;
        }
        out->meta_channels_len = new_len;
    }

    out->image_channels_len = out->channels.info_len;
    if (out->image_channels_len > 0) {
        size_t i;
        out->image_channels = jxl_calloc(alloc, out->image_channels_len, sizeof(*out->image_channels));
        if (out->image_channels == NULL) {
            jxl_modular_image_destination_free(alloc, out);
            return JXL_MODULAR_OUT_OF_MEMORY;
        }
        for (i = 0; i < out->image_channels_len; ++i) {
            size_t w = out->channels.info[i].width;
            size_t h = out->channels.info[i].height;
            if (!jxl_modular_grid_create(alloc, w, h, tracker, grid_kind, &out->image_channels[i])) {
                jxl_modular_image_destination_free(alloc, out);
                return JXL_MODULAR_OUT_OF_MEMORY;
            }
        }
    }

    return JXL_MODULAR_OK;
}

int jxl_modular_image_has_palette(const jxl_modular_image_destination *dest) {
    size_t i;
    if (dest == NULL) {
        return 0;
    }
    for (i = 0; i < dest->header.transform_len; ++i) {
        if (jxl_transform_is_palette(&dest->header.transform[i])) {
            return 1;
        }
    }
    return 0;
}

int jxl_modular_image_has_squeeze(const jxl_modular_image_destination *dest) {
    size_t i;
    if (dest == NULL) {
        return 0;
    }
    for (i = 0; i < dest->header.transform_len; ++i) {
        if (jxl_transform_is_squeeze(&dest->header.transform[i])) {
            return 1;
        }
    }
    return 0;
}

int jxl_modular_image_is_partial(const jxl_modular_image_destination *dest) {
    return dest != NULL && dest->gmodular_partial;
}

void jxl_modular_image_set_partial(jxl_modular_image_destination *dest, int partial) {
    if (dest != NULL) {
        dest->gmodular_partial = partial != 0;
    }
}

int64_t jxl_modular_dest_sample_sum(const jxl_modular_image_destination *dest, size_t max_px) {
    size_t c;
    int64_t sum = 0;
    if (dest == NULL) {
        return 0;
    }
    for (c = 0; c < dest->image_channels_len; ++c) {
        size_t y;
        const jxl_modular_grid_i32 *g = &dest->image_channels[c];
        size_t w;
        size_t h;
        if (g->buf == NULL) {
            continue;
        }
        w = g->width < max_px ? g->width : max_px;
        h = g->height < max_px ? g->height : max_px;
        for (y = 0; y < h; ++y) {
            size_t x;
            for (x = 0; x < w; ++x) {
                sum += (int64_t)jxl_modular_grid_sample_as_i32(g, x, y);
            }
        }
    }
    return sum;
}
