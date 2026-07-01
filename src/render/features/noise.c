// SPDX-License-Identifier: MIT OR Apache-2.0
#include "render/features/noise.h"

#include "allocator.h"

#include <string.h>

#define NOISE_PADDING 2
#define NOISE_N 8
#define NOISE_RING_ROWS 5

typedef struct {
    uint64_t s0[NOISE_N];
    uint64_t s1[NOISE_N];
} jxl_xorshift128plus;

typedef struct {
    float *buf[3];
    size_t width;
    size_t height;
    size_t stride;
} jxl_noise_group;

typedef struct {
    const jxl_noise_group *group;
    size_t channel;
} jxl_noise_subgrid;

static uint64_t split_mix_64(uint64_t z) {
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

static void xorshift_init(jxl_xorshift128plus *rng, uint64_t seed0, uint64_t seed1) {
    size_t i;
    rng->s0[0] = split_mix_64(seed0 + 0x9E3779B97F4A7C15ull);
    rng->s1[0] = split_mix_64(seed1 + 0x9E3779B97F4A7C15ull);
    for (i = 1; i < NOISE_N; ++i) {
        rng->s0[i] = split_mix_64(rng->s0[i - 1]);
        rng->s1[i] = split_mix_64(rng->s1[i - 1]);
    }
}

static void xorshift_fill_batch(jxl_xorshift128plus *rng, uint64_t out[NOISE_N]) {
    size_t i;
    for (i = 0; i < NOISE_N; ++i) {
        uint64_t s1 = rng->s0[i];
        uint64_t s0 = rng->s1[i];
        out[i] = s1 + s0;
        rng->s0[i] = s0;
        s1 ^= s1 << 23;
        rng->s1[i] = s1 ^ (s0 ^ (s1 >> 18) ^ (s0 >> 5));
    }
}

static jxl_noise_group *noise_group_new(jxl_allocator_state *alloc, size_t width, size_t height,
                                        uint64_t seed0, uint64_t seed1) {
                                            size_t ch;
    size_t width_n2 = (width + NOISE_N * 2 - 1) / (NOISE_N * 2);
    size_t stride = width_n2 * NOISE_N * 2;
    size_t num_iters = width_n2 * height;
    size_t elems_per_ch = num_iters * NOISE_N * 2;

    jxl_xorshift128plus rng;
    jxl_noise_group *group = jxl_calloc(alloc, 1, sizeof(*group));
    if (group == NULL) {
        return NULL;
    }
    group->width = width;
    group->height = height;
    group->stride = stride;

    xorshift_init(&rng, seed0, seed1);

    for (ch = 0; ch < 3; ++ch) {
        size_t it;
        size_t pos;
        group->buf[ch] = jxl_alloc(alloc, elems_per_ch * sizeof(float));
        if (group->buf[ch] == NULL) {
            size_t i;
            for (i = 0; i < ch; ++i) {
                jxl_free(alloc, group->buf[i]);
            }
            jxl_free(alloc, group);
            return NULL;
        }
        pos = 0;
        for (it = 0; it < num_iters; ++it) {
            size_t i;
            uint64_t batch[NOISE_N];
            xorshift_fill_batch(&rng, batch);
            for (i = 0; i < NOISE_N * 2; ++i) {
                uint32_t bits = (uint32_t)(batch[i / 2] >> ((i % 2) * 32));
                uint32_t u = (bits >> 9) | 0x3f800000u;
                float f;
                memcpy(&f, &u, sizeof(f));
                group->buf[ch][pos++] = f;
            }
        }
    }
    return group;
}

static void noise_group_free(jxl_allocator_state *alloc, jxl_noise_group *group) {
    size_t ch;
    if (group == NULL) {
        return;
    }
    for (ch = 0; ch < 3; ++ch) {
        jxl_free(alloc, group->buf[ch]);
    }
    jxl_free(alloc, group);
}

static const float *noise_subgrid_row(const jxl_noise_subgrid *sg, size_t y) {
    if (sg == NULL || sg->group == NULL) {
        return NULL;
    }
    return sg->group->buf[sg->channel] + y * sg->group->stride;
}

static size_t noise_subgrid_width(const jxl_noise_subgrid *sg) {
    return sg != NULL && sg->group != NULL ? sg->group->width : 0;
}

static size_t noise_subgrid_height(const jxl_noise_subgrid *sg) {
    return sg != NULL && sg->group != NULL ? sg->group->height : 0;
}

static size_t wrapping_add_signed(size_t value, int delta) {
    if (delta >= 0) {
        return value + (size_t)delta;
    }
    return value - (size_t)(-delta);
}

static void fill_padded_row(float *out, size_t out_len, const float *this_row, size_t this_len,
                            const float *left, size_t left_len, const float *right,
                            size_t right_len) {
    if (left != NULL) {
        out[0] = left[left_len - 2];
        out[1] = left[left_len - 1];
    } else if (this_len >= NOISE_PADDING) {
        out[0] = this_row[1];
        out[1] = this_row[0];
    } else if (this_len > 0) {
        out[0] = this_row[0];
        out[1] = this_row[0];
    }

    memcpy(out + NOISE_PADDING, this_row, this_len * sizeof(float));

    if (right != NULL) {
        if (right_len >= NOISE_PADDING) {
            out[out_len - 2] = right[0];
            out[out_len - 1] = right[1];
        } else {
            out[out_len - 2] = right[0];
            out[out_len - 1] = right[0];
        }
    } else if (out_len >= NOISE_PADDING * 2) {
        out[out_len - 2] = out[out_len - 3];
        out[out_len - 1] = out[out_len - 4];
    }
}

static void fill_once(float *out, size_t out_len, size_t fill_y,
                      const jxl_noise_subgrid adjacent[9]) {
    size_t source_y;
    const jxl_noise_subgrid *this_sg = &adjacent[4];
    size_t height = noise_subgrid_height(this_sg);

    const jxl_noise_subgrid *c;
    const jxl_noise_subgrid *l;
    const jxl_noise_subgrid *r;

    const float *crow;
    const float *lrow;
    const float *rrow;

    if (fill_y >= height) {
        source_y = fill_y - height;
        c = &adjacent[7];
        l = &adjacent[6];
        r = &adjacent[8];
    } else {
        source_y = fill_y;
        c = &adjacent[4];
        l = &adjacent[3];
        r = &adjacent[5];
    }

    if (c->group != NULL) {
        /* keep source_y, c, l, r */
    } else if (height - 1 >= source_y) {
        source_y = height - 1 - source_y;
        c = &adjacent[4];
        l = &adjacent[3];
        r = &adjacent[5];
    } else {
        size_t dy = source_y - height + 1;
        if (adjacent[1].group != NULL) {
            source_y = noise_subgrid_height(&adjacent[1]) - dy;
            c = &adjacent[1];
            l = &adjacent[0];
            r = &adjacent[2];
        } else {
            source_y = 0;
            c = &adjacent[4];
            l = &adjacent[3];
            r = &adjacent[5];
        }
    }

    crow = noise_subgrid_row(c, source_y);
    lrow = l->group != NULL ? noise_subgrid_row(l, source_y) : NULL;
    rrow = r->group != NULL ? noise_subgrid_row(r, source_y) : NULL;
    fill_padded_row(out, out_len, crow, noise_subgrid_width(c), lrow, noise_subgrid_width(l), rrow,
                    noise_subgrid_width(r));
}

static void convolve_fill(jxl_allocator_state *alloc, float *out, size_t width, size_t height,
                          size_t out_stride, const jxl_noise_subgrid adjacent[9]) {
                              size_t y;
    const jxl_noise_subgrid *this_sg = &adjacent[4];
    size_t padded_w;
    float *rows;
    if (this_sg->group == NULL) {
        return;
    }

    padded_w = width + NOISE_PADDING * 2;
    rows = jxl_calloc(alloc, padded_w * NOISE_RING_ROWS, sizeof(float));
    if (rows == NULL) {
        return;
    }

    if (adjacent[1].group != NULL) {
        int offset_y;
        const jxl_noise_subgrid *north = &adjacent[1];
        for (offset_y = -2; offset_y < 0; ++offset_y) {
            size_t row_idx = (size_t)((int)2 + offset_y);
            size_t cy = wrapping_add_signed(noise_subgrid_height(north), offset_y);
            const float *crow = noise_subgrid_row(north, cy);
            const float *lrow =
                adjacent[0].group != NULL ? noise_subgrid_row(&adjacent[0], cy) : NULL;
            const float *rrow =
                adjacent[2].group != NULL ? noise_subgrid_row(&adjacent[2], cy) : NULL;
            fill_padded_row(rows + row_idx * padded_w, padded_w, crow, noise_subgrid_width(north),
                            lrow, noise_subgrid_width(&adjacent[0]), rrow,
                            noise_subgrid_width(&adjacent[2]));
        }
    } else if (height >= 2) {
        int offset_y;
        for (offset_y = -2; offset_y < 0; ++offset_y) {
            y = (size_t)(-(offset_y + 1));
            size_t row_idx = (size_t)((int)2 + offset_y);
            const float *crow = noise_subgrid_row(this_sg, y);
            const float *lrow =
                adjacent[3].group != NULL ? noise_subgrid_row(&adjacent[3], y) : NULL;
            const float *rrow =
                adjacent[5].group != NULL ? noise_subgrid_row(&adjacent[5], y) : NULL;
            fill_padded_row(rows + row_idx * padded_w, padded_w, crow, width, lrow,
                            noise_subgrid_width(&adjacent[3]), rrow,
                            noise_subgrid_width(&adjacent[5]));
        }
    } else {
        size_t y;
        const float *crow = noise_subgrid_row(this_sg, 0);
        const float *lrow =
            adjacent[3].group != NULL ? noise_subgrid_row(&adjacent[3], 0) : NULL;
        const float *rrow =
            adjacent[5].group != NULL ? noise_subgrid_row(&adjacent[5], 0) : NULL;
        for (y = 0; y < 2; ++y) {
            fill_padded_row(rows + y * padded_w, padded_w, crow, width, lrow,
                            noise_subgrid_width(&adjacent[3]), rrow,
                            noise_subgrid_width(&adjacent[5]));
        }
    }

    for (y = 0; y < 3; ++y) {
        fill_once(rows + (2 + y) * padded_w, padded_w, y, adjacent);
    }

    for (y = 0; y < height; ++y) {
        size_t x;
        size_t center_y = (y + 2) % NOISE_RING_ROWS;
        float *out_row = out + y * out_stride;
        for (x = 0; x < width; ++x) {
            size_t dy;
            float sum = 0.0f;
            for (dy = 0; dy < 5; ++dy) {
                size_t dx;
                const float *input_row = rows + dy * padded_w;
                for (dx = 0; dx < 5; ++dx) {
                    sum += input_row[x + dx] * 0.16f;
                }
            }
            out_row[x] = sum - rows[center_y * padded_w + x + 2] * 4.0f;
        }

        if (y + 1 < height) {
            size_t next_y = y + 3;
            size_t fill_y = (next_y + 2) % NOISE_RING_ROWS;
            fill_once(rows + fill_y * padded_w, padded_w, next_y, adjacent);
        }
    }

    jxl_free(alloc, rows);
}

static int init_noise_planes(jxl_allocator_state *alloc, const jxl_frame_header *header,
                             uint32_t visible_frames, uint32_t invisible_frames, float **out_x,
                             float **out_y, float **out_b) {
                                 size_t group_idx;
                                 size_t ch;
                                 size_t i;
    uint32_t width = header->width;
    uint32_t height = header->height;
    uint32_t group_dim = jxl_frame_header_group_dim(header);
    size_t groups_per_row = ((size_t)width + group_dim - 1) / group_dim;
    size_t num_groups = groups_per_row * (((size_t)height + group_dim - 1) / group_dim);

    jxl_noise_group **groups = jxl_calloc(alloc, num_groups, sizeof(*groups));
    uint64_t seed0;
    float *conv[3];
    if (groups == NULL) {
        return 0;
    }

    seed0 = ((uint64_t)visible_frames << 32) + invisible_frames;
    for (group_idx = 0; group_idx < num_groups; ++group_idx) {
        size_t group_x = group_idx % groups_per_row;
        size_t group_y = group_idx / groups_per_row;
        size_t x0 = group_x * group_dim;
        size_t y0 = group_y * group_dim;
        uint64_t seed1 = ((uint64_t)x0 << 32) + y0;
        size_t gw = group_dim < width - x0 ? group_dim : width - x0;
        size_t gh = group_dim < height - y0 ? group_dim : height - y0;
        groups[group_idx] = noise_group_new(alloc, gw, gh, seed0, seed1);
        if (groups[group_idx] == NULL) {
            size_t i;
            for (i = 0; i < group_idx; ++i) {
                noise_group_free(alloc, groups[i]);
            }
            jxl_free(alloc, groups);
            return 0;
        }
    }

    for (ch = 0; ch < 3; ++ch) {
        conv[ch] = jxl_calloc(alloc, (size_t)width * height, sizeof(float));
        if (conv[ch] == NULL) {
            size_t i;
            for (i = 0; i < 3; ++i) {
                jxl_free(alloc, conv[i]);
            }
            for (i = 0; i < num_groups; ++i) {
                noise_group_free(alloc, groups[i]);
            }
            jxl_free(alloc, groups);
            return 0;
        }
    }

    for (group_idx = 0; group_idx < num_groups; ++group_idx) {
        size_t i;
        int dy;
        size_t ch;
        size_t group_x = group_idx % groups_per_row;
        size_t group_y = group_idx / groups_per_row;
        size_t x0 = group_x * group_dim;
        size_t y0 = group_y * group_dim;
        size_t gw = groups[group_idx]->width;
        size_t gh = groups[group_idx]->height;

        jxl_noise_subgrid adjacent[9];
        for (i = 0; i < 9; ++i) {
            adjacent[i].group = NULL;
            adjacent[i].channel = 0;
        }
        for (dy = -1; dy <= 1; ++dy) {
            int dx;
            for (dx = -1; dx <= 1; ++dx) {
                long gx = (long)group_x + dx;
                long gy = (long)group_y + dy;
                size_t slot = (size_t)(dy + 1) * 3 + (size_t)(dx + 1);
                if (gx >= 0 && gy >= 0 && (size_t)gx < groups_per_row) {
                    size_t idx = (size_t)gy * groups_per_row + (size_t)gx;
                    if (idx < num_groups) {
                        adjacent[slot].group = groups[idx];
                    }
                }
            }
        }

        for (ch = 0; ch < 3; ++ch) {
            size_t i;
            jxl_noise_subgrid adjacent_ch[9];
            for (i = 0; i < 9; ++i) {
                adjacent_ch[i].group = adjacent[i].group;
                adjacent_ch[i].channel = ch;
            }
            float *out_sub = conv[ch] + y0 * width + x0;
            convolve_fill(alloc, out_sub, gw, gh, width, adjacent_ch);
        }
    }

    for (i = 0; i < num_groups; ++i) {
        noise_group_free(alloc, groups[i]);
    }
    jxl_free(alloc, groups);

    *out_x = conv[0];
    *out_y = conv[1];
    *out_b = conv[2];
    return 1;
}

int jxl_render_noise(jxl_allocator_state *alloc, const jxl_frame_header *header,
                     uint32_t visible_frames, uint32_t invisible_frames, float corr_x, float corr_b,
                     float *plane_x, float *plane_y, float *plane_b, uint32_t width,
                     uint32_t height, const jxl_noise_parameters *params,
                     const jxl_modular_region *render_region) {
                         uint32_t fy;
    float lut[9];
    uint32_t frame_w;
    float *noise_x;
    float *noise_y;
    float *noise_b;
    int32_t origin_left;
    int32_t origin_top;
    if (header == NULL || plane_x == NULL || plane_y == NULL || plane_b == NULL ||
        params == NULL || width == 0 || height == 0) {
        return 0;
    }

    noise_x = NULL;
    noise_y = NULL;
    noise_b = NULL;
    if (!init_noise_planes(alloc, header, visible_frames, invisible_frames, &noise_x, &noise_y,
                           &noise_b)) {
        return 0;
    }

    frame_w = header->width;
    origin_left = render_region != NULL ? render_region->left : 0;
    origin_top = render_region != NULL ? render_region->top : 0;

    memcpy(lut, params->lut, 8 * sizeof(float));
    lut[8] = params->lut[7];

    for (fy = 0; fy < height; ++fy) {
        uint32_t fx;
        uint32_t y = (uint32_t)(fy + (uint32_t)origin_top);
        if (y >= header->height) {
            break;
        }
        for (fx = 0; fx < width; ++fx) {
            float in_x;
            float in_y;
            float in_scaled_x;
            float in_scaled_y;
            float nxf;
            float nyf;
            size_t grid_i;
            size_t noise_i;
            float grid_x;
            float grid_y;
            float nx;
            float ny;
            float nb;
            size_t in_x_int;
            float in_x_frac;
            size_t in_y_int;
            float in_y_frac;
            float sx;
            float sy;
            uint32_t x = (uint32_t)(fx + (uint32_t)origin_left);
            if (x >= header->width) {
                break;
            }
            grid_i = (size_t)fy * (size_t)width + (size_t)fx;
            noise_i = (size_t)y * (size_t)frame_w + (size_t)x;
            grid_x = plane_x[grid_i];
            grid_y = plane_y[grid_i];
            nx = noise_x[noise_i];
            ny = noise_y[noise_i];
            nb = noise_b[noise_i];

            in_x = grid_x + grid_y;
            in_y = grid_y - grid_x;
            in_scaled_x = in_x > 0.0f ? in_x * 3.0f : 0.0f;
            in_scaled_y = in_y > 0.0f ? in_y * 3.0f : 0.0f;

            in_x_int = (size_t)in_scaled_x;
            if (in_x_int > 7) {
                in_x_int = 7;
            }
            in_x_frac = in_scaled_x - (float)in_x_int;
            in_y_int = (size_t)in_scaled_y;
            if (in_y_int > 7) {
                in_y_int = 7;
            }
            in_y_frac = in_scaled_y - (float)in_y_int;

            sx = (lut[in_x_int + 1] - lut[in_x_int]) * in_x_frac + lut[in_x_int];
            sy = (lut[in_y_int + 1] - lut[in_y_int]) * in_y_frac + lut[in_y_int];
            nxf = 0.22f * sx * (0.0078125f * nx + 0.9921875f * nb);
            nyf = 0.22f * sy * (0.0078125f * ny + 0.9921875f * nb);

            plane_x[grid_i] += corr_x * (nxf + nyf) + nxf - nyf;
            plane_y[grid_i] += nxf + nyf;
            plane_b[grid_i] += corr_b * (nxf + nyf);
        }
    }

    jxl_free(alloc, noise_x);
    jxl_free(alloc, noise_y);
    jxl_free(alloc, noise_b);
    return 1;
}

#ifdef JXL_OXIDE_TESTING
uint64_t jxl_noise_planes_ch0_checksum(const jxl_frame_header *header, uint32_t visible_frames,
                                       uint32_t invisible_frames) {
                                           size_t i;
    jxl_allocator_state alloc;
    uint64_t h;
    jxl_allocator_init(&alloc, NULL);

    float *noise_x = NULL;
    float *noise_y = NULL;
    float *noise_b = NULL;
    if (header == NULL ||
        !init_noise_planes(&alloc, header, visible_frames, invisible_frames, &noise_x, &noise_y,
                           &noise_b)) {
        return 0;
    }
    size_t pixels = (size_t)header->width * (size_t)header->height;
    h = 0;
    for (i = 0; i < pixels; ++i) {
        uint32_t bits = 0;
        memcpy(&bits, &noise_x[i], sizeof(bits));
        h = h * (uint64_t)31 + ((uint64_t)bits ^ (uint64_t)i);
    }
    jxl_free(&alloc, noise_x);
    jxl_free(&alloc, noise_y);
    jxl_free(&alloc, noise_b);
    return h;
}
#endif
