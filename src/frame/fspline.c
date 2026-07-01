// SPDX-License-Identifier: MIT OR Apache-2.0
#include "spline.h"

#include "bitstream/unpack.h"
#include "frame/util.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MAX_NUM_SPLINES (1u << 24)
#define MAX_NUM_CONTROL_POINTS (1u << 20)

void jxl_splines_init(jxl_splines *s) {
    if (s != NULL) {
        memset(s, 0, sizeof(*s));
    }
}

static void quant_spline_free(jxl_allocator_state *alloc, jxl_quant_spline *q) {
    if (q == NULL) {
        return;
    }
    jxl_free(alloc, q->points);
    memset(q, 0, sizeof(*q));
}

void jxl_splines_free(jxl_allocator_state *alloc, jxl_splines *s) {
    size_t i;
    if (s == NULL) {
        return;
    }
    for (i = 0; i < s->quant_splines_len; ++i) {
        quant_spline_free(alloc, &s->quant_splines[i]);
    }
    jxl_free(alloc, s->quant_splines);
    jxl_splines_init(s);
}

static uint32_t log2_ceil_u64(uint64_t x) {
    uint64_t p;
    uint32_t bits;
    if (x <= 1u) {
        return 0u;
    }
    p = 1ull;
    while (p < x) {
        p <<= 1;
    }
    bits = 0;
    while (p > 1u) {
        bits++;
        p >>= 1;
    }
    return bits;
}

static uint64_t div_ceil_qa(uint32_t dividend, int32_t quant_adjust) {
    uint64_t d = (uint64_t)dividend;
    uint64_t abs_qa;
    if (quant_adjust >= 0) {
        uint64_t qa = (uint64_t)quant_adjust;
        return (8u * d + 7u + qa) / (8u + qa);
    }
    abs_qa = (uint64_t)(-quant_adjust);
    return d + (d * abs_qa + 7u) / 8u;
}

uint64_t jxl_splines_estimate_area(const jxl_splines *splines, float corr_x, float corr_b) {
    size_t si;
    uint64_t total;
    float corr_x_a;
    float corr_b_a;
    uint64_t corr_x_u;
    uint64_t corr_b_u;
    if (splines == NULL) {
        return 0;
    }
    corr_x_a = corr_x < 0.0f ? -corr_x : corr_x;
    corr_b_a = corr_b < 0.0f ? -corr_b : corr_b;
    corr_x_u = (uint64_t)ceilf(corr_x_a);
    corr_b_u = (uint64_t)ceilf(corr_b_a);

    total = 0;
    for (si = 0; si < splines->quant_splines_len; ++si) {
        int ch;
        int i;
        uint64_t color_xyb[3] = {0, 0, 0};
        uint64_t max_color;
        uint64_t width_estimate;
        const jxl_quant_spline *qs = &splines->quant_splines[si];
        for (ch = 0; ch < 3; ++ch) {
            int i;
            for (i = 0; i < 32; ++i) {
                int32_t q = qs->xyb_dct[ch][i];
                uint32_t aq = q < 0 ? (uint32_t)(-q) : (uint32_t)q;
                color_xyb[ch] += div_ceil_qa(aq, splines->quant_adjust);
            }
        }
        color_xyb[0] += corr_x_u * color_xyb[1];
        color_xyb[2] += corr_b_u * color_xyb[1];
        max_color = color_xyb[0];
        if (color_xyb[1] > max_color) {
            max_color = color_xyb[1];
        }
        if (color_xyb[2] > max_color) {
            max_color = color_xyb[2];
        }
        uint64_t log_color = (uint64_t)log2_ceil_u64(1u + max_color);

        width_estimate = 0;
        for (i = 0; i < 32; ++i) {
            int32_t q = qs->sigma_dct[i];
            uint32_t aq = q < 0 ? (uint32_t)(-q) : (uint32_t)q;
            uint64_t weight = 1u + div_ceil_qa(aq, splines->quant_adjust);
            width_estimate += weight * weight * log_color;
        }
        total += width_estimate * qs->manhattan_distance;
    }
    return total;
}

static jxl_frame_status_t parse_quant_spline(jxl_allocator_state *alloc, jxl_coding_decoder *dec,
                                             jxl_bs *bs, int64_t start_x, int64_t start_y,
                                             uint32_t num_pixels, size_t acc_control_points,
                                             jxl_quant_spline *out) {
                                                 uint32_t i;
                                                 int ch;
    uint32_t num_points = 0;
    int64_t cur_delta_x;
    int64_t cur_delta_y;
    uint64_t manhattan;
    size_t n;
    int64_t cur_value_x;
    int64_t cur_value_y;
    size_t acc_num_points;
    size_t max_num_points;
    size_t cap;
    int64_t *points;
    JXL_FRAME_TRY_CODING(jxl_coding_decoder_read_varint(dec, bs, 3, &num_points));
    acc_num_points = acc_control_points + (size_t)num_points;
    max_num_points = MAX_NUM_CONTROL_POINTS;
    if (num_pixels / 2 < max_num_points) {
        max_num_points = (size_t)(num_pixels / 2);
    }
    if (acc_num_points > max_num_points) {
        return JXL_FRAME_VALIDATION_ERROR;
    }

    cap = 1u + (size_t)num_points;
    points = jxl_alloc(alloc, cap * 2u * sizeof(int64_t));
    if (points == NULL) {
        return JXL_FRAME_OUT_OF_MEMORY;
    }

    cur_value_x = start_x;
    cur_value_y = start_y;
    cur_delta_x = 0;
    cur_delta_y = 0;
    manhattan = 0;
    n = 0;
    points[n * 2] = cur_value_x;
    points[n * 2 + 1] = cur_value_y;
    n++;

    for (i = 0; i < num_points; ++i) {
        int64_t prev_x = cur_value_x;
        int64_t prev_y = cur_value_y;
        uint32_t raw_dx = 0;
        uint32_t raw_dy = 0;
        int64_t delta_x;
        int64_t delta_y;
        JXL_FRAME_TRY_CODING(jxl_coding_decoder_read_varint(dec, bs, 4, &raw_dx));
        JXL_FRAME_TRY_CODING(jxl_coding_decoder_read_varint(dec, bs, 4, &raw_dy));
        delta_x = (int64_t)jxl_unpack_signed(raw_dx);
        delta_y = (int64_t)jxl_unpack_signed(raw_dy);
        cur_delta_x += delta_x;
        cur_delta_y += delta_y;
        manhattan += (uint64_t)(cur_delta_x < 0 ? -cur_delta_x : cur_delta_x);
        manhattan += (uint64_t)(cur_delta_y < 0 ? -cur_delta_y : cur_delta_y);

        if (cur_delta_x > 0 ? cur_value_x > INT64_MAX - cur_delta_x
                             : cur_value_x < INT64_MIN - cur_delta_x) {
            jxl_free(alloc, points);
            return JXL_FRAME_VALIDATION_ERROR;
        }
        if (cur_delta_y > 0 ? cur_value_y > INT64_MAX - cur_delta_y
                             : cur_value_y < INT64_MIN - cur_delta_y) {
            jxl_free(alloc, points);
            return JXL_FRAME_VALIDATION_ERROR;
        }
        cur_value_x += cur_delta_x;
        cur_value_y += cur_delta_y;
        if (cur_value_x == prev_x && cur_value_y == prev_y) {
            jxl_free(alloc, points);
            return JXL_FRAME_VALIDATION_ERROR;
        }
        points[n * 2] = cur_value_x;
        points[n * 2 + 1] = cur_value_y;
        n++;
    }

    for (ch = 0; ch < 3; ++ch) {
        int i;
        for (i = 0; i < 32; ++i) {
            uint32_t raw = 0;
            JXL_FRAME_TRY_CODING(jxl_coding_decoder_read_varint(dec, bs, 5, &raw));
            out->xyb_dct[ch][i] = jxl_unpack_signed(raw);
        }
    }
    for (i = 0; i < 32; ++i) {
        uint32_t raw = 0;
        JXL_FRAME_TRY_CODING(jxl_coding_decoder_read_varint(dec, bs, 5, &raw));
        out->sigma_dct[i] = jxl_unpack_signed(raw);
    }

    out->points = points;
    out->points_len = n;
    out->manhattan_distance = manhattan;
    return JXL_FRAME_OK;
}

jxl_frame_status_t jxl_splines_parse(jxl_allocator_state *alloc, jxl_bs *bs,
                                     const jxl_frame_header *frame, jxl_splines *out) {
                                         uint32_t i;
    uint32_t num_splines_raw;
    uint32_t num_splines;
    uint32_t raw_x;
    uint32_t raw_y;
    uint32_t raw_qa;
    size_t acc_control_points;
    jxl_coding_decoder *dec = NULL;
    uint32_t num_pixels;
    uint32_t max_num_splines;
    int64_t *start_points;
    int64_t prev_x;
    int64_t prev_y;
    jxl_quant_spline *splines;
    if (alloc == NULL || bs == NULL || frame == NULL || out == NULL) {
        return JXL_FRAME_BITSTREAM_ERROR;
    }
    jxl_splines_free(alloc, out);

    JXL_FRAME_TRY_CODING(jxl_coding_decoder_parse(alloc, bs, 6, &dec));
    JXL_FRAME_TRY_CODING(jxl_coding_decoder_begin(dec, bs));

    num_splines_raw = 0;
    JXL_FRAME_TRY_CODING(jxl_coding_decoder_read_varint(dec, bs, 2, &num_splines_raw));
    num_pixels = frame->width * frame->height;
    max_num_splines = MAX_NUM_SPLINES;
    if (num_pixels / 4u < max_num_splines) {
        max_num_splines = num_pixels / 4u;
    }
    if (max_num_splines == 0) {
        max_num_splines = 1;
    }
    if (num_splines_raw >= max_num_splines) {
        jxl_coding_decoder_destroy(alloc, dec);
        return JXL_FRAME_VALIDATION_ERROR;
    }
    num_splines = num_splines_raw + 1u;

    start_points = jxl_alloc(alloc, (size_t)num_splines * 2u * sizeof(int64_t));
    if (start_points == NULL) {
        jxl_coding_decoder_destroy(alloc, dec);
        return JXL_FRAME_OUT_OF_MEMORY;
    }

    raw_x = 0;
    raw_y = 0;
    JXL_FRAME_TRY_CODING(jxl_coding_decoder_read_varint(dec, bs, 1, &raw_x));
    JXL_FRAME_TRY_CODING(jxl_coding_decoder_read_varint(dec, bs, 1, &raw_y));
    prev_x = (int64_t)raw_x;
    prev_y = (int64_t)raw_y;
    start_points[0] = prev_x;
    start_points[1] = prev_y;
    for (i = 1; i < num_splines; ++i) {
        uint32_t dx = 0;
        uint32_t dy = 0;
        JXL_FRAME_TRY_CODING(jxl_coding_decoder_read_varint(dec, bs, 1, &dx));
        JXL_FRAME_TRY_CODING(jxl_coding_decoder_read_varint(dec, bs, 1, &dy));
        prev_x += (int64_t)jxl_unpack_signed(dx);
        prev_y += (int64_t)jxl_unpack_signed(dy);
        start_points[(size_t)i * 2] = prev_x;
        start_points[(size_t)i * 2 + 1] = prev_y;
    }

    raw_qa = 0;
    JXL_FRAME_TRY_CODING(jxl_coding_decoder_read_varint(dec, bs, 0, &raw_qa));
    out->quant_adjust = jxl_unpack_signed(raw_qa);

    splines =
        jxl_alloc(alloc, (size_t)num_splines * sizeof(jxl_quant_spline));
    if (splines == NULL) {
        jxl_free(alloc, start_points);
        jxl_coding_decoder_destroy(alloc, dec);
        return JXL_FRAME_OUT_OF_MEMORY;
    }
    memset(splines, 0, (size_t)num_splines * sizeof(jxl_quant_spline));

    acc_control_points = 0;
    for (i = 0; i < num_splines; ++i) {
        jxl_frame_status_t pst = parse_quant_spline(
            alloc, dec, bs, start_points[(size_t)i * 2], start_points[(size_t)i * 2 + 1],
            num_pixels, acc_control_points, &splines[i]);
        if (pst != JXL_FRAME_OK) {
            uint32_t k;
            for (k = 0; k < i; ++k) {
                quant_spline_free(alloc, &splines[k]);
            }
            jxl_free(alloc, splines);
            jxl_free(alloc, start_points);
            jxl_coding_decoder_destroy(alloc, dec);
            return pst;
        }
        acc_control_points += splines[i].points_len > 0 ? splines[i].points_len - 1 : 0;
    }

    JXL_FRAME_TRY_CODING(jxl_coding_decoder_finalize(dec));
    jxl_coding_decoder_destroy(alloc, dec);
    jxl_free(alloc, start_points);

    out->quant_splines = splines;
    out->quant_splines_len = num_splines;
    return JXL_FRAME_OK;
}
