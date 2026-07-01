// SPDX-License-Identifier: MIT OR Apache-2.0
#include "dequant_expand.h"

#include "context.h"
#include "vardct/dct_select.h"

#include <math.h>
#include <string.h>

static void buf_free(jxl_context *ctx, jxl_context_dequant_buf *b) {
    jxl_ctx_free(ctx, b->data);
    b->data = NULL;
    b->len = 0;
}

static jxl_vardct_status_t buf_alloc(jxl_context *ctx, jxl_context_dequant_buf *b, size_t len) {
    buf_free(ctx, b);
    if (len == 0) {
        return JXL_VARDCT_OK;
    }
    b->data = jxl_ctx_calloc(ctx, len, sizeof(float));
    if (b->data == NULL) {
        return JXL_VARDCT_OUT_OF_MEMORY;
    }
    b->len = len;
    return JXL_VARDCT_OK;
}

static float mult_factor(float x) {
    if (x > 0.0f) {
        return 1.0f + x;
    }
    return 1.0f / (1.0f - x);
}

static float interpolate(float pos, float max, const float *bands, size_t band_len) {
    float a;
    float b;
    float scaled_pos;
    size_t scaled_index;
    float frac_index;
    if (band_len == 0) {
        return 0.0f;
    }
    if (band_len == 1) {
        return bands[0];
    }
    if (pos < 0.0f || max <= 0.0f) {
        return bands[0];
    }
    scaled_pos = pos * (float)(band_len - 1) / max;
    scaled_index = (size_t)scaled_pos;
    frac_index = scaled_pos - (float)scaled_index;
    if (scaled_index + 1 >= band_len) {
        return bands[band_len - 1];
    }
    a = bands[scaled_index];
    b = bands[scaled_index + 1];
    return a * powf(b / a, frac_index);
}

static jxl_vardct_status_t dct_quant_weights(jxl_context *ctx, const float *params,
                                             size_t param_len, uint32_t width, uint32_t height,
                                             jxl_context_dequant_buf *out) {
                                                 size_t i;
                                                 uint32_t y;
    float max_dist;
    float last_band;
    float *bands = jxl_ctx_calloc(ctx, param_len, sizeof(float));
    jxl_vardct_status_t st;
    if (bands == NULL) {
        return JXL_VARDCT_OUT_OF_MEMORY;
    }
    last_band = params[0];
    bands[0] = last_band;
    for (i = 1; i < param_len; ++i) {
        float band = last_band * mult_factor(params[i]);
        if (band <= 0.0f) {
            jxl_ctx_free(ctx, bands);
            return JXL_VARDCT_VALIDATION_ERROR;
        }
        bands[i] = band;
        last_band = band;
    }

    st = buf_alloc(ctx, out, (size_t)width * (size_t)height);
    if (st != JXL_VARDCT_OK) {
        jxl_ctx_free(ctx, bands);
        return st;
    }

    max_dist = 1.4142135623730951f + 1e-6f;
    for (y = 0; y < height; ++y) {
        uint32_t x;
        for (x = 0; x < width; ++x) {
            float dx = width > 1 ? (float)x / (float)(width - 1) : 0.0f;
            float dy = height > 1 ? (float)y / (float)(height - 1) : 0.0f;
            float distance = sqrtf(dx * dx + dy * dy);
            out->data[(size_t)y * width + x] = interpolate(distance, max_dist, bands, param_len);
        }
    }
    jxl_ctx_free(ctx, bands);
    return JXL_VARDCT_OK;
}

static void invert_buf(jxl_context_dequant_buf *b) {
    size_t i;
    for (i = 0; i < b->len; ++i) {
        b->data[i] = 1.0f / b->data[i];
    }
}

static jxl_vardct_status_t expand_dct4_channel(jxl_context *ctx, const jxl_dequant_matrix_params *p,
                                               size_t ch, jxl_context_dequant_buf *out) {
                                                   uint32_t y;
    jxl_context_dequant_buf mat = {0};
    jxl_vardct_status_t st;
    if (p->dct_band_lens[ch] == 0 || p->dct_bands[ch] == NULL) {
        return JXL_VARDCT_VALIDATION_ERROR;
    }
    st =
        dct_quant_weights(ctx, p->dct_bands[ch], p->dct_band_lens[ch], 4, 4, &mat);
    if (st != JXL_VARDCT_OK) {
        return st;
    }
    st = buf_alloc(ctx, out, 64);
    if (st != JXL_VARDCT_OK) {
        buf_free(ctx, &mat);
        return st;
    }
    memset(out->data, 0, 64 * sizeof(float));
    for (y = 0; y < 4; ++y) {
        uint32_t x;
        for (x = 0; x < 4; ++x) {
            float w = mat.data[(size_t)y * 4 + x];
            out->data[(size_t)y * 16 + x * 2] = w;
            out->data[(size_t)y * 16 + x * 2 + 1] = w;
            out->data[(size_t)(y * 2 + 1) * 8 + x * 2] = w;
            out->data[(size_t)(y * 2 + 1) * 8 + x * 2 + 1] = w;
        }
    }
    out->data[1] /= p->dct4_params[ch][0];
    out->data[8] /= p->dct4_params[ch][0];
    out->data[9] /= p->dct4_params[ch][1];
    buf_free(ctx, &mat);
    return JXL_VARDCT_OK;
}

static jxl_vardct_status_t expand_dct4x8_channel(jxl_context *ctx,
                                                 const jxl_dequant_matrix_params *p, size_t ch,
                                                 jxl_context_dequant_buf *out) {
    size_t row;
    jxl_vardct_status_t st;
    jxl_context_dequant_buf mat = {0};
    if (p->dct_band_lens[ch] == 0 || p->dct_bands[ch] == NULL) {
        return JXL_VARDCT_VALIDATION_ERROR;
    }
    st = dct_quant_weights(ctx, p->dct_bands[ch], p->dct_band_lens[ch], 8, 4, &mat);
    if (st != JXL_VARDCT_OK) {
        return st;
    }
    st = buf_alloc(ctx, out, 64);
    if (st != JXL_VARDCT_OK) {
        buf_free(ctx, &mat);
        return st;
    }
    for (row = 0; row < 4; ++row) {
        const float *src = mat.data + row * 8;
        memcpy(&out->data[(row * 2) * 8], src, 8 * sizeof(float));
        memcpy(&out->data[(row * 2 + 1) * 8], src, 8 * sizeof(float));
    }
    out->data[8] /= p->dct4x8_params[ch][0];
    buf_free(ctx, &mat);
    return JXL_VARDCT_OK;
}

static const float k_afv_freqs[16] = {
    0.0f, 0.0f, 0.8517779f, 5.3777843f, 0.0f, 0.0f, 4.734748f, 5.4492455f,
    1.659827f, 4.0f, 7.275749f, 10.423227f, 2.6629324f, 7.6306577f, 8.962389f, 12.971662f,
};

static jxl_vardct_status_t expand_afv_channel(jxl_context *ctx, const jxl_dequant_matrix_params *p,
                                              size_t ch, jxl_context_dequant_buf *out) {
                                                  uint32_t y;
    jxl_context_dequant_buf weights_4x8 = {0};
    jxl_context_dequant_buf weights_4x4 = {0};
    float bands[4];
    jxl_vardct_status_t st;
    const float *params;
    float freq_lo;
    float freq_hi;
    if (p->dct_band_lens[ch] == 0 || p->dct_bands[ch] == NULL || p->dct4x4_band_lens[ch] == 0 ||
        p->dct4x4_bands[ch] == NULL) {
        return JXL_VARDCT_VALIDATION_ERROR;
    }
    st = dct_quant_weights(ctx, p->dct_bands[ch], p->dct_band_lens[ch], 8, 4,
                           &weights_4x8);
    if (st == JXL_VARDCT_OK) {
        st = dct_quant_weights(ctx, p->dct4x4_bands[ch], p->dct4x4_band_lens[ch], 4, 4,
                               &weights_4x4);
    }
    if (st != JXL_VARDCT_OK) {
        buf_free(ctx, &weights_4x8);
        buf_free(ctx, &weights_4x4);
        return st;
    }

    params = p->afv_params[ch];
    bands[0] = params[5];
    bands[1] = bands[0] * mult_factor(params[6]);
    bands[2] = bands[1] * mult_factor(params[7]);
    bands[3] = bands[2] * mult_factor(params[8]);
    freq_lo = k_afv_freqs[2];
    freq_hi = k_afv_freqs[15];

    st = buf_alloc(ctx, out, 64);
    if (st != JXL_VARDCT_OK) {
        buf_free(ctx, &weights_4x8);
        buf_free(ctx, &weights_4x4);
        return st;
    }
    memset(out->data, 0, 64 * sizeof(float));
    for (y = 0; y < 4; ++y) {
        uint32_t x;
        for (x = 0; x < 4; ++x) {
            size_t fi = (size_t)y * 4 + x;
            float v;
            if (x == 0 && y == 0) {
                v = 1.0f;
            } else if (x == 0 && y == 1) {
                v = params[2];
            } else if (x == 1 && y == 0) {
                v = params[3];
            } else if (x == 1 && y == 1) {
                v = params[4];
            } else {
                v = interpolate(k_afv_freqs[fi] - freq_lo, freq_hi - freq_lo + 1e-6f, bands, 4);
            }
            out->data[(size_t)y * 16 + x * 2] = v;
        }
    }

    for (y = 0; y < 4; ++y) {
        size_t x;
        float *rows = out->data + y * 16;
        float *row0 = rows;
        float *row1 = rows + 8;
        const float *weights_8 = weights_4x8.data + y * 8;
        const float *weights_4 = weights_4x4.data + y * 4;
        for (x = 0; x < 8; ++x) {
            row1[x] = (y == 0 && x == 0) ? params[0] : weights_8[x];
        }
        for (x = 0; x < 4; ++x) {
            row0[x * 2 + 1] = (y == 0 && x == 0) ? params[1] : weights_4[x];
        }
    }

    buf_free(ctx, &weights_4x8);
    buf_free(ctx, &weights_4x4);
    return JXL_VARDCT_OK;
}

static jxl_vardct_status_t expand_dct_channel(jxl_context *ctx, const jxl_dequant_matrix_params *p,
                                              size_t ch, jxl_context_dequant_buf *out) {
    uint32_t w = 0;
    uint32_t h = 0;
    jxl_transform_dequant_matrix_size(p->dct_select, &w, &h);
    if (p->dct_band_lens[ch] == 0 || p->dct_bands[ch] == NULL) {
        return JXL_VARDCT_VALIDATION_ERROR;
    }
    return dct_quant_weights(ctx, p->dct_bands[ch], p->dct_band_lens[ch], w, h, out);
}

static jxl_vardct_status_t expand_encoding(jxl_context *ctx, const jxl_dequant_matrix_params *p,
                                           size_t matrix_idx) {
                                               size_t ch;
    uint32_t w;
    uint32_t h;
    jxl_context_dequant *expanded;
    jxl_vardct_status_t st;
    size_t mat_len;
    if (ctx == NULL) {
        return JXL_VARDCT_BITSTREAM_ERROR;
    }
    expanded = &ctx->dequant;
    st = JXL_VARDCT_OK;
    w = 0;
    h = 0;
    jxl_transform_dequant_matrix_size(p->dct_select, &w, &h);
    mat_len = (size_t)w * (size_t)h;

    for (ch = 0; ch < 3 && st == JXL_VARDCT_OK; ++ch) {
        uint32_t y;
        jxl_context_dequant_buf *out = &expanded->weights[matrix_idx][ch];
        switch (p->encoding) {
        case JXL_DEQUANT_ENC_DEFAULT:
            if (p->dct_select == JXL_TRANSFORM_HORNUSS) {
                st = buf_alloc(ctx, out, 64);
                if (st == JXL_VARDCT_OK) {
                    size_t i;
                    const float *params = p->hornuss[ch];
                    for (i = 0; i < 64; ++i) {
                        out->data[i] = params[0];
                    }
                    out->data[0] = 1.0f;
                    out->data[1] = params[1];
                    out->data[8] = params[1];
                    out->data[9] = params[2];
                }
                break;
            }
            if (p->dct_select == JXL_TRANSFORM_DCT2) {
                st = buf_alloc(ctx, out, 64);
                if (st == JXL_VARDCT_OK) {
                    size_t idx;
                    const float *params;
		    memset(out->data, 0, 64 * sizeof(float));
                    out->data[0] = 1.0f;
                    params = p->dct2[ch];
                    for (idx = 0; idx < 6; ++idx) {
                        size_t shift = idx / 2;
                        size_t dim = (size_t)1 << shift;
                        float val = params[idx];
                        if (idx % 2 == 0) {
                            size_t y;
                            for (y = 0; y < dim; ++y) {
                                size_t x;
                                for (x = dim; x < dim * 2; ++x) {
                                    out->data[y * 8 + x] = val;
                                    out->data[x * 8 + y] = val;
                                }
                            }
                        } else {
                            size_t y;
                            for (y = dim; y < dim * 2; ++y) {
                                size_t x;
                                for (x = dim; x < dim * 2; ++x) {
                                    out->data[y * 8 + x] = val;
                                }
                            }
                        }
                    }
                }
                break;
            }
            /* fall through */
        case JXL_DEQUANT_ENC_DCT:
            st = expand_dct_channel(ctx, p, ch, out);
            break;
        case JXL_DEQUANT_ENC_HORNUSS:
            st = buf_alloc(ctx, out, 64);
            if (st == JXL_VARDCT_OK) {
                size_t i;
                const float *params = p->hornuss[ch];
                for (i = 0; i < 64; ++i) {
                    out->data[i] = params[0];
                }
                out->data[0] = 1.0f;
                out->data[1] = params[1];
                out->data[8] = params[1];
                out->data[9] = params[2];
            }
            break;
        case JXL_DEQUANT_ENC_DCT2: {
            size_t idx;
            st = buf_alloc(ctx, out, 64);
            if (st != JXL_VARDCT_OK) {
                break;
            }
            memset(out->data, 0, 64 * sizeof(float));
            out->data[0] = 1.0f;
            const float *params = p->dct2[ch];
            for (idx = 0; idx < 6; ++idx) {
                size_t shift = idx / 2;
                size_t dim = (size_t)1 << shift;
                float val = params[idx];
                if (idx % 2 == 0) {
                    size_t y;
                    for (y = 0; y < dim; ++y) {
                        size_t x;
                        for (x = dim; x < dim * 2; ++x) {
                            out->data[y * 8 + x] = val;
                            out->data[x * 8 + y] = val;
                        }
                    }
                } else {
                    size_t y;
                    for (y = dim; y < dim * 2; ++y) {
                        size_t x;
                        for (x = dim; x < dim * 2; ++x) {
                            out->data[y * 8 + x] = val;
                        }
                    }
                }
            }
            break;
        }
        case JXL_DEQUANT_ENC_DCT4:
            st = expand_dct4_channel(ctx, p, ch, out);
            break;
        case JXL_DEQUANT_ENC_DCT4X8:
            st = expand_dct4x8_channel(ctx, p, ch, out);
            break;
        case JXL_DEQUANT_ENC_AFV:
            st = expand_afv_channel(ctx, p, ch, out);
            break;
        case JXL_DEQUANT_ENC_RAW:
            st = buf_alloc(ctx, out, mat_len);
            if (st == JXL_VARDCT_OK) {
                if (p->dct_bands[ch] == NULL || p->dct_band_lens[ch] != mat_len) {
                    st = JXL_VARDCT_VALIDATION_ERROR;
                } else {
                    memcpy(out->data, p->dct_bands[ch], mat_len * sizeof(float));
                }
            }
            break;
        default:
            st = JXL_VARDCT_VALIDATION_ERROR;
            break;
        }

        if (st != JXL_VARDCT_OK) {
            continue;
        }

        if (p->encoding != JXL_DEQUANT_ENC_RAW) {
            invert_buf(out);
        }

        jxl_context_dequant_buf *tr = &expanded->weights_tr[matrix_idx][ch];
        st = buf_alloc(ctx, tr, mat_len);
        if (st != JXL_VARDCT_OK) {
            continue;
        }
        for (y = 0; y < h; ++y) {
            uint32_t x;
            for (x = 0; x < w; ++x) {
                tr->data[(size_t)x * h + y] = out->data[(size_t)y * w + x];
            }
        }
    }
    return st;
}

void jxl_context_dequant_free(jxl_context *ctx) {
    size_t i;
    if (ctx == NULL) {
        return;
    }
    for (i = 0; i < JXL_DEQUANT_MATRIX_COUNT; ++i) {
        size_t ch;
        for (ch = 0; ch < 3; ++ch) {
            buf_free(ctx, &ctx->dequant.weights[i][ch]);
            buf_free(ctx, &ctx->dequant.weights_tr[i][ch]);
        }
    }
}

void jxl_dequant_matrix_set_free_weights(jxl_context *ctx, const jxl_dequant_matrix_set *set) {
    (void)set;
    jxl_context_dequant_free(ctx);
}

jxl_vardct_status_t jxl_dequant_matrix_set_build_weights(jxl_context *ctx,
                                                         jxl_dequant_matrix_set *set) {
                                                             size_t i;
    if (ctx == NULL || set == NULL) {
        return JXL_VARDCT_BITSTREAM_ERROR;
    }
    jxl_dequant_matrix_set_free_weights(ctx, set);
    for (i = 0; i < JXL_DEQUANT_MATRIX_COUNT; ++i) {
        jxl_vardct_status_t st = expand_encoding(ctx, &set->matrices[i], i);
        if (st != JXL_VARDCT_OK) {
            jxl_dequant_matrix_set_free_weights(ctx, set);
            return st;
        }
    }
    return JXL_VARDCT_OK;
}

const float *jxl_dequant_matrix_weights(jxl_context *ctx, const jxl_dequant_matrix_set *set,
                                        size_t matrix_idx, size_t channel, size_t *len_out) {
    (void)set;
    if (ctx == NULL || matrix_idx >= JXL_DEQUANT_MATRIX_COUNT || channel >= 3) {
        if (len_out != NULL) {
            *len_out = 0;
        }
        return NULL;
    }
    if (len_out != NULL) {
        *len_out = ctx->dequant.weights[matrix_idx][channel].len;
    }
    return ctx->dequant.weights[matrix_idx][channel].data;
}

const float *jxl_dequant_matrix_weights_transposed(jxl_context *ctx,
                                                   const jxl_dequant_matrix_set *set,
                                                   size_t matrix_idx, size_t channel,
                                                   size_t *len_out) {
    (void)set;
    if (ctx == NULL || matrix_idx >= JXL_DEQUANT_MATRIX_COUNT || channel >= 3) {
        if (len_out != NULL) {
            *len_out = 0;
        }
        return NULL;
    }
    if (len_out != NULL) {
        *len_out = ctx->dequant.weights_tr[matrix_idx][channel].len;
    }
    return ctx->dequant.weights_tr[matrix_idx][channel].data;
}
