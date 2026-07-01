// SPDX-License-Identifier: MIT OR Apache-2.0
#include "spline.h"

#include "allocator.h"

#include <math.h>
#include <string.h>

typedef struct {
    float x;
    float y;
} jxl_spline_point;

typedef struct {
    jxl_spline_point point;
    float length;
} jxl_spline_arc;

typedef struct {
    jxl_spline_point *points;
    size_t points_len;
    float xyb_dct[3][32];
    float sigma_dct[32];
} jxl_spline_dequant;

static jxl_spline_point spline_point_new(float x, float y) {
        jxl_spline_point result;
    result.x = x;
    result.y = y;
    return result;

}

static jxl_spline_point spline_point_add(jxl_spline_point a, jxl_spline_point b) {
    return spline_point_new(a.x + b.x, a.y + b.y);
}

static jxl_spline_point spline_point_sub(jxl_spline_point a, jxl_spline_point b) {
    return spline_point_new(a.x - b.x, a.y - b.y);
}

static jxl_spline_point spline_point_mul(jxl_spline_point a, float s) {
    return spline_point_new(a.x * s, a.y * s);
}

static float spline_point_norm(jxl_spline_point p) {
    return sqrtf(p.x * p.x + p.y * p.y);
}

static float spline_point_norm_sq(jxl_spline_point p) {
    return p.x * p.x + p.y * p.y;
}

static jxl_spline_point spline_point_mirror(jxl_spline_point p, jxl_spline_point center) {
    return spline_point_new(center.x + center.x - p.x, center.y + center.y - p.y);
}

static float spline_erf(float x) {
    float ax = x < 0 ? -x : x;
    float denom1 = ax * 7.77394369e-2f + 2.05260015e-4f;
    float denom2 = denom1 * ax + 2.32120216e-1f;
    float denom3 = denom2 * ax + 2.77820801e-1f;
    float denom4 = denom3 * ax + 1.0f;
    float denom5 = denom4 * denom4;
    float inv_denom5 = 1.0f / denom5;
    float result = -inv_denom5 * inv_denom5 + 1.0f;
    return x < 0 ? -result : result;
}

static float continuous_idct(const float dct[32], float t) {
    int i;
    float res = dct[0];
    for (i = 1; i < 32; ++i) {
        float theta = (float)i * (3.14159265f / 32.0f) * (t + 0.5f);
        res += 1.41421356f * dct[i] * cosf(theta);
    }
    return res;
}

static void spline_dequant_free(jxl_allocator_state *alloc, jxl_spline_dequant *s) {
    if (s == NULL) {
        return;
    }
    jxl_free(alloc, s->points);
    memset(s, 0, sizeof(*s));
}

static int spline_dequant_from_quant(jxl_allocator_state *alloc, const jxl_quant_spline *qs,
                                     int32_t quant_adjust, float corr_x, float corr_b,
                                     jxl_spline_dequant *out) {
    size_t i;
    int ch;
    float inverted_qa;
    static const float k_channel_weights[4] = {0.0042f, 0.075f, 0.07f, 0.3333f};
    if (alloc == NULL || qs == NULL || out == NULL || qs->points_len == 0) {
        return 0;
    }
    memset(out, 0, sizeof(*out));
    out->points = jxl_alloc(alloc, qs->points_len * sizeof(jxl_spline_point));
    if (out->points == NULL) {
        return 0;
    }
    for (i = 0; i < qs->points_len; ++i) {
        out->points[i] = spline_point_new((float)qs->points[i * 2], (float)qs->points[i * 2 + 1]);
    }
    out->points_len = qs->points_len;

    inverted_qa =
        quant_adjust >= 0 ? 1.0f / (1.0f + (float)quant_adjust / 8.0f) : 1.0f - (float)quant_adjust / 8.0f;
    for (ch = 0; ch < 3; ++ch) {
        int i;
        for (i = 0; i < 32; ++i) {
            out->xyb_dct[ch][i] =
                (float)qs->xyb_dct[ch][i] * k_channel_weights[ch] * inverted_qa;
        }
    }
    for (i = 0; i < 32; ++i) {
        out->xyb_dct[0][i] += corr_x * out->xyb_dct[1][i];
        out->xyb_dct[2][i] += corr_b * out->xyb_dct[1][i];
    }
    for (i = 0; i < 32; ++i) {
        out->sigma_dct[i] = (float)qs->sigma_dct[i] * k_channel_weights[3] * inverted_qa;
    }
    return 1;
}

static jxl_spline_point *spline_get_upsampled_points(jxl_allocator_state *alloc, const jxl_spline_dequant *s, size_t *out_len) {
    size_t i;
    size_t ups_cap;
    size_t nu;
    size_t ext_cap;
    jxl_spline_point *extended;
    jxl_spline_point *upsampled;
    if (s == NULL || s->points_len == 0 || out_len == NULL) {
        return NULL;
    }
    if (s->points_len == 1) {
        jxl_spline_point *one = jxl_alloc(alloc, sizeof(jxl_spline_point));
        if (one == NULL) {
            return NULL;
        }
        one[0] = s->points[0];
        *out_len = 1;
        return one;
    }

    ext_cap = s->points_len + 2u;
    extended = jxl_alloc(alloc, ext_cap * sizeof(jxl_spline_point));
    if (extended == NULL) {
        return NULL;
    }
    extended[0] = spline_point_mirror(s->points[1], s->points[0]);
    memcpy(&extended[1], s->points, s->points_len * sizeof(jxl_spline_point));
    extended[s->points_len + 1] =
        spline_point_mirror(s->points[s->points_len - 2], s->points[s->points_len - 1]);

    ups_cap = 16u * (ext_cap - 3u) + 1u;
    upsampled = jxl_alloc(alloc, ups_cap * sizeof(jxl_spline_point));
    if (upsampled == NULL) {
        jxl_free(alloc, extended);
        return NULL;
    }
    nu = 0;
    for (i = 0; i + 3 < ext_cap; ++i) {
        int k;
        int step;
        jxl_spline_point p[4];
        float t[4];
        jxl_spline_point a[3];
        jxl_spline_point b[2];
        memcpy(p, &extended[i], 4 * sizeof(jxl_spline_point));
        upsampled[nu++] = p[1];
        t[0] = 0.0f;
        for (k = 1; k < 4; ++k) {
            jxl_spline_point diff = spline_point_sub(p[k], p[k - 1]);
            t[k] = t[k - 1] + powf(spline_point_norm_sq(diff), 0.25f);
        }
        for (step = 1; step < 16; ++step) {
            int k;
            float knot = t[1] + ((float)step / 16.0f) * (t[2] - t[1]);
            float scale;
            for (k = 0; k < 3; ++k) {
                float scale = (t[k + 1] > t[k]) ? (knot - t[k]) / (t[k + 1] - t[k]) : 0.0f;
                a[k] = spline_point_add(p[k], spline_point_mul(spline_point_sub(p[k + 1], p[k]), scale));
            }
            for (k = 0; k < 2; ++k) {
                float scale = (t[k + 2] > t[k]) ? (knot - t[k]) / (t[k + 2] - t[k]) : 0.0f;
                b[k] = spline_point_add(a[k], spline_point_mul(spline_point_sub(a[k + 1], a[k]), scale));
            }
            scale = (t[2] > t[1]) ? (knot - t[1]) / (t[2] - t[1]) : 0.0f;
            upsampled[nu++] =
                spline_point_add(b[0], spline_point_mul(spline_point_sub(b[1], b[0]), scale));
        }
    }
    upsampled[nu++] = s->points[s->points_len - 1];
    jxl_free(alloc, extended);
    *out_len = nu;
    return upsampled;
}

static jxl_spline_arc *spline_get_samples(jxl_allocator_state *alloc, const jxl_spline_point *upsampled, size_t upsampled_len,
                                          size_t *out_len) {
    size_t n;
    size_t next_idx;
    jxl_spline_arc compound_tmp;
    size_t cap;
    jxl_spline_point current;
    jxl_spline_arc *all;
    if (upsampled == NULL || upsampled_len == 0 || out_len == NULL) {
        return NULL;
    }
    cap = upsampled_len * 4u;
    if (cap < 64u) {
        cap = 64u;
    }
    all = jxl_alloc(alloc, cap * sizeof(jxl_spline_arc));
    if (all == NULL) {
        return NULL;
    }
    n = 0;
    current = upsampled[0];
    next_idx = 0;

    while (n >= cap) {
        size_t new_cap = cap * 2u;
        jxl_spline_arc *grown = jxl_realloc(alloc, all, new_cap * sizeof(jxl_spline_arc));
        if (grown == NULL) {
            jxl_free(alloc, all);
            return NULL;
        }
        all = grown;
        cap = new_cap;
    }
    compound_tmp.point = current;
    compound_tmp.length = 1.0f;
    all[n++] = compound_tmp;


    while (next_idx < upsampled_len) {
        jxl_spline_point prev = current;
        float arclength = 0.0f;
        for (;;) {
            jxl_spline_point next;
            float arclength_to_next;
            if (next_idx >= upsampled_len) {
                jxl_spline_arc compound_tmp;
                while (n >= cap) {
                    size_t new_cap = cap * 2u;
                    jxl_spline_arc *grown = jxl_realloc(alloc, all, new_cap * sizeof(jxl_spline_arc));
                    if (grown == NULL) {
                        jxl_free(alloc, all);
                        return NULL;
                    }
                    all = grown;
                    cap = new_cap;
                }
                compound_tmp.point = prev;
                compound_tmp.length = arclength;
                all[n++] = compound_tmp;

                goto done;
            }
            next = upsampled[next_idx];
            arclength_to_next = spline_point_norm(spline_point_sub(next, prev));
            if (arclength + arclength_to_next >= 1.0f) {
                jxl_spline_arc compound_tmp;
                float scale =
                    arclength_to_next > 0.0f ? (1.0f - arclength) / arclength_to_next : 0.0f;
                current = spline_point_add(prev, spline_point_mul(spline_point_sub(next, prev), scale));
                while (n >= cap) {
                    size_t new_cap = cap * 2u;
                    jxl_spline_arc *grown = jxl_realloc(alloc, all, new_cap * sizeof(jxl_spline_arc));
                    if (grown == NULL) {
                        jxl_free(alloc, all);
                        return NULL;
                    }
                    all = grown;
                    cap = new_cap;
                }
                compound_tmp.point = current;
                compound_tmp.length = 1.0f;
                all[n++] = compound_tmp;

                break;
            }
            arclength += arclength_to_next;
            prev = next;
            next_idx++;
        }
    }
done:
    *out_len = n;
    return all;
}

int jxl_render_splines(jxl_allocator_state *alloc, const jxl_frame_header *fh, const jxl_splines *splines, float corr_x,
                       float corr_b, float *planes[3], uint32_t width, uint32_t height,
                       const jxl_modular_region *render_region) {
    size_t si;
    int32_t origin_left;
    int32_t origin_top;
    if (alloc == NULL || fh == NULL || splines == NULL || planes == NULL || planes[0] == NULL ||
        planes[1] == NULL || planes[2] == NULL || width == 0 || height == 0) {
        return 0;
    }

    origin_left = render_region != NULL ? render_region->left : 0;
    origin_top = render_region != NULL ? render_region->top : 0;

    for (si = 0; si < splines->quant_splines_len; ++si) {
        size_t i;
        jxl_spline_dequant spline;
        size_t ups_len;
        size_t arc_len;
        jxl_spline_point *upsampled;
        jxl_spline_arc *arcs;
        float total_arclength;
        if (!spline_dequant_from_quant(alloc, &splines->quant_splines[si], splines->quant_adjust,
                                       corr_x, corr_b, &spline)) {
            return 0;
        }

        ups_len = 0;
        upsampled = spline_get_upsampled_points(alloc, &spline, &ups_len);
        if (upsampled == NULL) {
            spline_dequant_free(alloc, &spline);
            return 0;
        }
        arc_len = 0;
        arcs = spline_get_samples(alloc, upsampled, ups_len, &arc_len);
        jxl_free(alloc, upsampled);
        if (arcs == NULL) {
            spline_dequant_free(alloc, &spline);
            return 0;
        }

        total_arclength = (float)arc_len - 2.0f + arcs[arc_len - 1].length;
        for (i = 0; i < arc_len; ++i) {
            int ch;
            float arclength_from_start = (float)i / total_arclength;
            float t;
            float inv_sigma;
            float max_color;
            float values[3];
            float sigma;
            float max_distance;
            int32_t xbegin;
            int32_t xend;
            int32_t ybegin;
            int32_t yend;
            if (arclength_from_start > 1.0f) {
                arclength_from_start = 1.0f;
            }
            t = 31.0f * arclength_from_start;
            sigma = continuous_idct(spline.sigma_dct, t);
            inv_sigma = sigma != 0.0f ? 1.0f / sigma : 0.0f;
            values[0] = continuous_idct(spline.xyb_dct[0], t) * arcs[i].length;
            values[1] = continuous_idct(spline.xyb_dct[1], t) * arcs[i].length;
            values[2] = continuous_idct(spline.xyb_dct[2], t) * arcs[i].length;

            max_color = 0.01f;
            for (ch = 0; ch < 3; ++ch) {
                if (values[ch] > max_color) {
                    max_color = values[ch];
                }
            }
            max_distance =
                sqrtf(2.0f * (logf(10.0f) * 3.0f + max_color)) * fabsf(sigma);

            xbegin = (int32_t)floorf(arcs[i].point.x - max_distance + 0.5f);
            if (xbegin < 0) {
                xbegin = 0;
            }
            xend = (int32_t)floorf(arcs[i].point.x + max_distance + 1.5f);
            if ((uint32_t)xend > fh->width) {
                xend = (int32_t)fh->width;
            }
            ybegin = (int32_t)floorf(arcs[i].point.y - max_distance + 0.5f);
            if (ybegin < 0) {
                ybegin = 0;
            }
            yend = (int32_t)floorf(arcs[i].point.y + max_distance + 1.5f);
            if ((uint32_t)yend > fh->height) {
                yend = (int32_t)fh->height;
            }

            for (ch = 0; ch < 3; ++ch) {
                int32_t y;
                float *plane = planes[ch];
                for (y = ybegin; y < yend; ++y) {
                    int32_t x;
                    int32_t fy = y - origin_top;
                    if (fy < 0) {
                        continue;
                    }
                    if ((uint32_t)fy >= height) {
                        break;
                    }
                    for (x = xbegin; x < xend; ++x) {
                        const float k_sqrt_0125 = 0.35355338f;
                        float dx;
                        float dy;
                        float distance;
                        float factor;
                        float extra;
                        int32_t fx = x - origin_left;
                        if (fx < 0) {
                            continue;
                        }
                        if ((uint32_t)fx >= width) {
                            break;
                        }
                        dx = (float)x - arcs[i].point.x;
                        dy = (float)y - arcs[i].point.y;
                        distance = sqrtf(dx * dx + dy * dy);
                        factor =
                            spline_erf((0.5f * distance + k_sqrt_0125) * inv_sigma) -
                            spline_erf((0.5f * distance - k_sqrt_0125) * inv_sigma);
                        extra = 0.25f * values[ch] * sigma * factor * factor;
                        plane[(size_t)fy * width + (size_t)fx] += extra;
                    }
                }
            }
        }

        jxl_free(alloc, arcs);
        spline_dequant_free(alloc, &spline);
    }
    return 1;
}
