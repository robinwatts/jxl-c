// SPDX-License-Identifier: MIT OR Apache-2.0
#include "dequant.h"

#include "context.h"
#include "modular/param.h"
#include "modular/prepare_subimage.h"
#include "modular/subimage_decode.h"
#include "modular/transform/inverse.h"
#include "vardct/dequant_expand.h"
#include "vardct/dct_select.h"
#include "vardct/util.h"

#include <string.h>

static const jxl_transform_type k_dct_select_list[JXL_DEQUANT_MATRIX_COUNT] = {
    JXL_TRANSFORM_DCT8,      JXL_TRANSFORM_HORNUSS,   JXL_TRANSFORM_DCT2,
    JXL_TRANSFORM_DCT4,      JXL_TRANSFORM_DCT16,     JXL_TRANSFORM_DCT32,
    JXL_TRANSFORM_DCT8X16,   JXL_TRANSFORM_DCT8X32,   JXL_TRANSFORM_DCT16X32,
    JXL_TRANSFORM_DCT4X8,    JXL_TRANSFORM_AFV0,      JXL_TRANSFORM_DCT64,
    JXL_TRANSFORM_DCT32X64,  JXL_TRANSFORM_DCT128,    JXL_TRANSFORM_DCT64X128,
    JXL_TRANSFORM_DCT256,    JXL_TRANSFORM_DCT128X256,
};

static const float k_seq_a[7] = {-1.025f, -0.78f, -0.65012f, -0.19041574f, -0.20819396f,
                                 -0.421064f, -0.32733846f};
static const float k_seq_b[7] = {-0.30419582f, -0.36330363f, -0.3566038f, -0.34430745f,
                                 -0.33699593f, -0.30180866f, -0.27321684f};
static const float k_seq_c[7] = {-1.2f, -1.2f, -0.8f, -0.7f, -0.7f, -0.4f, -0.5f};

void jxl_dequant_matrix_params_init(jxl_dequant_matrix_params *p) {
    if (p != NULL) {
        memset(p, 0, sizeof(*p));
    }
}

void jxl_dequant_matrix_params_free(jxl_allocator_state *alloc, jxl_dequant_matrix_params *p) {
    size_t c;
    if (p == NULL) {
        return;
    }
    for (c = 0; c < 3; ++c) {
        jxl_free(alloc, p->dct_bands[c]);
        p->dct_bands[c] = NULL;
        p->dct_band_lens[c] = 0;
        jxl_free(alloc, p->dct4x4_bands[c]);
        p->dct4x4_bands[c] = NULL;
        p->dct4x4_band_lens[c] = 0;
    }
}

void jxl_dequant_matrix_set_init(jxl_dequant_matrix_set *set) {
    size_t i;
    if (set == NULL) {
        return;
    }
    memset(set, 0, sizeof(*set));
    for (i = 0; i < JXL_DEQUANT_MATRIX_COUNT; ++i) {
        jxl_dequant_matrix_params_init(&set->matrices[i]);
    }
}

void jxl_dequant_matrix_set_free(jxl_dequant_matrix_set *set) {
    size_t i;
    jxl_allocator_state *alloc;
    if (set == NULL) {
        return;
    }
    alloc =
        set->ctx != NULL ? jxl_context_alloc_state(set->ctx) : NULL;
    if (set->ctx != NULL) {
        jxl_dequant_matrix_set_free_weights(set->ctx, set);
    }
    for (i = 0; i < JXL_DEQUANT_MATRIX_COUNT; ++i) {
        jxl_dequant_matrix_params_free(alloc, &set->matrices[i]);
    }
    memset(set, 0, sizeof(*set));
}

uint32_t jxl_dequant_matrix_set_stream_index(uint32_t num_lf_groups) {
    return 1u + num_lf_groups * 3u;
}

static jxl_vardct_status_t copy_dct_bands(jxl_allocator_state *alloc, jxl_dequant_matrix_params *p,
                                          size_t ch, const float *bands, size_t n) {
    p->dct_bands[ch] = jxl_alloc(alloc, n * sizeof(float));
    if (p->dct_bands[ch] == NULL) {
        return JXL_VARDCT_OUT_OF_MEMORY;
    }
    memcpy(p->dct_bands[ch], bands, n * sizeof(float));
    p->dct_band_lens[ch] = n;
    return JXL_VARDCT_OK;
}

static jxl_vardct_status_t set_dct_bands_3ch(jxl_allocator_state *alloc,
                                             jxl_dequant_matrix_params *p, const float *ch0,
                                             size_t n0, const float *ch1, size_t n1,
                                             const float *ch2, size_t n2) {
    jxl_vardct_status_t st = copy_dct_bands(alloc, p, 0, ch0, n0);
    if (st != JXL_VARDCT_OK) {
        return st;
    }
    st = copy_dct_bands(alloc, p, 1, ch1, n1);
    if (st != JXL_VARDCT_OK) {
        return st;
    }
    return copy_dct_bands(alloc, p, 2, ch2, n2);
}

static void set_dct_bands(jxl_allocator_state *alloc, jxl_dequant_matrix_params *p, float a0,
                          float b0, float c0, const float *seq_a, const float *seq_b,
                          const float *seq_c) {
                              size_t ch;
    for (ch = 0; ch < 3; ++ch) {
        const float *seq = ch == 0 ? seq_a : (ch == 1 ? seq_b : seq_c);
        float lead = ch == 0 ? a0 : (ch == 1 ? b0 : c0);
        p->dct_band_lens[ch] = 8;
        p->dct_bands[ch] = jxl_calloc(alloc, 8, sizeof(float));
        if (p->dct_bands[ch] == NULL) {
            return;
        }
        p->dct_bands[ch][0] = lead;
        memcpy(&p->dct_bands[ch][1], seq, 7 * sizeof(float));
    }
}

jxl_vardct_status_t jxl_dequant_matrix_params_default(jxl_allocator_state *alloc,
                                                     jxl_transform_type t,
                                                     jxl_dequant_matrix_params *out) {
    if (alloc == NULL || out == NULL) {
        return JXL_VARDCT_BITSTREAM_ERROR;
    }
    jxl_dequant_matrix_params_free(alloc, out);
    jxl_dequant_matrix_params_init(out);
    out->dct_select = t;
    out->encoding = JXL_DEQUANT_ENC_DEFAULT;

    switch (t) {
    case JXL_TRANSFORM_DCT8: {
        size_t ch;
        static const float ch0[] = {3150.0f, 0.0f, -0.4f, -0.4f, -0.4f, -2.0f};
        static const float ch1[] = {560.0f, 0.0f, -0.3f, -0.3f, -0.3f, -0.3f};
        static const float ch2[] = {512.0f, -2.0f, -1.0f, 0.0f, -1.0f, -2.0f};
        const float *bands[3] = {ch0, ch1, ch2};
        for (ch = 0; ch < 3; ++ch) {
            out->dct_band_lens[ch] = 6;
            out->dct_bands[ch] = jxl_alloc(alloc, 6 * sizeof(float));
            if (out->dct_bands[ch] == NULL) {
                return JXL_VARDCT_OUT_OF_MEMORY;
            }
            memcpy(out->dct_bands[ch], bands[ch], 6 * sizeof(float));
        }
        break;
    }
    case JXL_TRANSFORM_HORNUSS:
        out->hornuss[0][0] = 280.0f;
        out->hornuss[0][1] = 3160.0f;
        out->hornuss[0][2] = 3160.0f;
        out->hornuss[1][0] = 60.0f;
        out->hornuss[1][1] = 864.0f;
        out->hornuss[1][2] = 864.0f;
        out->hornuss[2][0] = 18.0f;
        out->hornuss[2][1] = 200.0f;
        out->hornuss[2][2] = 200.0f;
        break;
    case JXL_TRANSFORM_DCT2:
        out->dct2[0][0] = 3840.0f;
        out->dct2[0][1] = 2560.0f;
        out->dct2[0][2] = 1280.0f;
        out->dct2[0][3] = 640.0f;
        out->dct2[0][4] = 480.0f;
        out->dct2[0][5] = 300.0f;
        out->dct2[1][0] = 960.0f;
        out->dct2[1][1] = 640.0f;
        out->dct2[1][2] = 320.0f;
        out->dct2[1][3] = 180.0f;
        out->dct2[1][4] = 140.0f;
        out->dct2[1][5] = 120.0f;
        out->dct2[2][0] = 640.0f;
        out->dct2[2][1] = 320.0f;
        out->dct2[2][2] = 128.0f;
        out->dct2[2][3] = 64.0f;
        out->dct2[2][4] = 32.0f;
        out->dct2[2][5] = 16.0f;
        break;
    case JXL_TRANSFORM_DCT64:
        set_dct_bands(alloc, out, 23966.166f, 8380.191f, 4493.024f, k_seq_a, k_seq_b, k_seq_c);
        break;
    case JXL_TRANSFORM_DCT128:
        set_dct_bands(alloc, out, 47932.332f, 16760.383f, 8986.048f, k_seq_a, k_seq_b, k_seq_c);
        break;
    case JXL_TRANSFORM_DCT256:
        set_dct_bands(alloc, out, 95864.664f, 33520.766f, 17972.096f, k_seq_a, k_seq_b, k_seq_c);
        break;
    case JXL_TRANSFORM_DCT16X32:
    case JXL_TRANSFORM_DCT32X16: {
        static const float ch0[] = {13844.971f,  -0.971138f, -0.658f,    -0.42026f,
                                    -0.22712f,   -0.2206f,   -0.226f,    -0.6f};
        static const float ch1[] = {4798.964f,   -0.6112531f, -0.8377079f, -0.7901486f,
                                    -0.26927274f, -0.38272768f, -0.22924222f, -0.20719099f};
        static const float ch2[] = {1807.2369f,  -1.2f, -1.2f, -0.7f,
                                    -0.7f, -0.7f, -0.4f, -0.5f};
        return set_dct_bands_3ch(alloc, out, ch0, 8, ch1, 8, ch2, 8);
    }
    case JXL_TRANSFORM_DCT4X8:
    case JXL_TRANSFORM_DCT8X4: {
        size_t ch;
        static const float ch0[] = {2198.0505f, -0.96269625f, -0.7619425f, -0.65511405f};
        static const float ch1[] = {764.36554f, -0.926302f, -0.967523f, -0.2784529f};
        static const float ch2[] = {527.10754f, -1.4594386f, -1.4500821f, -1.5843723f};
        out->encoding = JXL_DEQUANT_ENC_DCT4X8;
        for (ch = 0; ch < 3; ++ch) {
            out->dct4x8_params[ch][0] = 1.0f;
        }
        return set_dct_bands_3ch(alloc, out, ch0, 4, ch1, 4, ch2, 4);
    }
    case JXL_TRANSFORM_DCT4: {
        size_t ch;
        static const float ch0[] = {2200.0f, 0.0f, 0.0f, 0.0f};
        static const float ch1[] = {392.0f, 0.0f, 0.0f, 0.0f};
        static const float ch2[] = {112.0f, -0.25f, -0.25f, -0.5f};
        out->encoding = JXL_DEQUANT_ENC_DCT4;
        for (ch = 0; ch < 3; ++ch) {
            out->dct4_params[ch][0] = 1.0f;
            out->dct4_params[ch][1] = 1.0f;
        }
        return set_dct_bands_3ch(alloc, out, ch0, 4, ch1, 4, ch2, 4);
    }
    case JXL_TRANSFORM_DCT16: {
        static const float ch0[] = {8996.873f,   -1.3000778f, -0.4942453f, -0.43909377f,
                                    -0.6350102f, -0.9017726f, -1.6162099f};
        static const float ch1[] = {3191.4836f,  -0.67424583f, -0.80745816f, -0.4492584f,
                                    -0.3586544f, -0.3132239f, -0.37615025f};
        static const float ch2[] = {1157.504f,   -2.0531423f, -1.4f, -0.5068713f,
                                    -0.4270873f, -1.4856834f, -4.920914f};
        return set_dct_bands_3ch(alloc, out, ch0, 7, ch1, 7, ch2, 7);
    }
    case JXL_TRANSFORM_DCT32: {
        static const float ch0[] = {15718.408f, -1.025f, -0.98f, -0.9012f, -0.4f,
                                    -0.48819396f, -0.421064f, -0.27f};
        static const float ch1[] = {7305.7637f, -0.8041958f, -0.76330364f, -0.5566038f,
                                    -0.49785304f, -0.43699592f, -0.40180868f, -0.27321684f};
        static const float ch2[] = {3803.5317f, -3.0607336f, -2.041327f, -2.023565f, -0.54953897f,
                                    -0.4f, -0.4f, -0.3f};
        return set_dct_bands_3ch(alloc, out, ch0, 8, ch1, 8, ch2, 8);
    }
    case JXL_TRANSFORM_DCT8X16:
    case JXL_TRANSFORM_DCT16X8: {
        static const float ch0[] = {7240.7734f, -0.7f, -0.7f, -0.2f, -0.2f, -0.2f, -0.5f};
        static const float ch1[] = {1448.1547f, -0.5f, -0.5f, -0.5f, -0.2f, -0.2f, -0.2f};
        static const float ch2[] = {506.85413f, -1.4f, -0.2f, -0.5f, -0.5f, -1.5f, -3.6f};
        return set_dct_bands_3ch(alloc, out, ch0, 7, ch1, 7, ch2, 7);
    }
    case JXL_TRANSFORM_DCT8X32:
    case JXL_TRANSFORM_DCT32X8: {
        static const float ch0[] = {16283.249f,  -1.7812846f, -1.6309059f, -1.0382179f,
                                    -0.85f, -0.7f, -0.9f, -1.2360638f};
        static const float ch1[] = {5089.1577f,  -0.3200494f, -0.3536285f, -0.3034f,
                                    -0.61f, -0.5f, -0.5f, -0.6f};
        static const float ch2[] = {3397.7761f,  -0.32132736f, -0.3450762f, -0.7034f,
                                    -0.9f, -1.0f, -1.0f, -1.1754606f};
        return set_dct_bands_3ch(alloc, out, ch0, 8, ch1, 8, ch2, 8);
    }
    case JXL_TRANSFORM_DCT32X64:
    case JXL_TRANSFORM_DCT64X32:
        set_dct_bands(alloc, out, 15358.898f, 5597.3604f, 2919.9617f, k_seq_a, k_seq_b, k_seq_c);
        break;
    case JXL_TRANSFORM_DCT64X128:
    case JXL_TRANSFORM_DCT128X64:
        set_dct_bands(alloc, out, 30717.797f, 11194.721f, 5839.9233f, k_seq_a, k_seq_b, k_seq_c);
        break;
    case JXL_TRANSFORM_DCT128X256:
    case JXL_TRANSFORM_DCT256X128:
        set_dct_bands(alloc, out, 61435.594f, 24209.441f, 12979.847f, k_seq_a, k_seq_b, k_seq_c);
        break;
    case JXL_TRANSFORM_AFV0:
    case JXL_TRANSFORM_AFV1:
    case JXL_TRANSFORM_AFV2:
    case JXL_TRANSFORM_AFV3: {
        size_t ch;
        static const float dct4x8_ch0[] = {2198.0505f, -0.96269625f, -0.7619425f, -0.65511405f};
        static const float dct4x8_ch1[] = {764.36554f, -0.926302f, -0.967523f, -0.2784529f};
        static const float dct4x8_ch2[] = {527.10754f, -1.4594386f, -1.4500821f, -1.5843723f};
        static const float dct4_ch0[] = {2200.0f, 0.0f, 0.0f, 0.0f};
        static const float dct4_ch1[] = {392.0f, 0.0f, 0.0f, 0.0f};
        static const float dct4_ch2[] = {112.0f, -0.25f, -0.25f, -0.5f};
        static const float afv[3][9] = {
            {3072.0f, 3072.0f, 256.0f, 256.0f, 256.0f, 414.0f, 0.0f, 0.0f, 0.0f},
            {1024.0f, 1024.0f, 50.0f, 50.0f, 50.0f, 58.0f, 0.0f, 0.0f, 0.0f},
            {384.0f, 384.0f, 12.0f, 12.0f, 12.0f, 22.0f, -0.25f, -0.25f, -0.25f},
        };
        jxl_vardct_status_t st;
	out->encoding = JXL_DEQUANT_ENC_AFV;
        for (ch = 0; ch < 3; ++ch) {
            out->dct4x8_params[ch][0] = 1.0f;
            memcpy(out->afv_params[ch], afv[ch], sizeof(afv[ch]));
        }
        st = set_dct_bands_3ch(alloc, out, dct4x8_ch0, 4, dct4x8_ch1, 4, dct4x8_ch2, 4);
        if (st != JXL_VARDCT_OK) {
            return st;
        }
        for (ch = 0; ch < 3; ++ch) {
            const float *src = ch == 0 ? dct4_ch0 : (ch == 1 ? dct4_ch1 : dct4_ch2);
            out->dct4x4_band_lens[ch] = 4;
            out->dct4x4_bands[ch] = jxl_alloc(alloc, 4 * sizeof(float));
            if (out->dct4x4_bands[ch] == NULL) {
                return JXL_VARDCT_OUT_OF_MEMORY;
            }
            memcpy(out->dct4x4_bands[ch], src, 4 * sizeof(float));
        }
        break;
    }
    default: {
        jxl_transform_type orig = t;
        jxl_vardct_status_t st = jxl_dequant_matrix_params_default(alloc, JXL_TRANSFORM_DCT8, out);
        if (st != JXL_VARDCT_OK) {
            return st;
        }
        out->dct_select = orig;
        return JXL_VARDCT_OK;
    }
    }
    return JXL_VARDCT_OK;
}

static jxl_vardct_status_t read_fixed_f16(jxl_bs *bs, float *out, size_t n) {
    size_t i;
    for (i = 0; i < n; ++i) {
        JXL_VARDCT_TRY_BS(jxl_bs_read_f16_as_f32(bs, &out[i]));
    }
    return JXL_VARDCT_OK;
}

static jxl_vardct_status_t read_dct_params(jxl_allocator_state *alloc, jxl_bs *bs,
                                             jxl_dequant_matrix_params *p) {
                                                 size_t ch;
    uint32_t num_params = 0;
    JXL_VARDCT_TRY_BS(jxl_bs_read_bits(bs, 4, &num_params));
    num_params += 1;
    for (ch = 0; ch < 3; ++ch) {
        size_t i;
        p->dct_band_lens[ch] = num_params;
        p->dct_bands[ch] = jxl_calloc(alloc, num_params, sizeof(float));
        if (p->dct_bands[ch] == NULL) {
            return JXL_VARDCT_OUT_OF_MEMORY;
        }
        for (i = 0; i < num_params; ++i) {
            JXL_VARDCT_TRY_BS(jxl_bs_read_f16_as_f32(bs, &p->dct_bands[ch][i]));
        }
        p->dct_bands[ch][0] *= 64.0f;
    }
    return JXL_VARDCT_OK;
}

static int encoding_allowed(uint32_t mode, jxl_transform_type t) {
    uint32_t idx;
    if (mode < 1 || mode > 5) {
        return 1;
    }
    idx = jxl_transform_dequant_matrix_param_index(t);
    return idx == 0 || idx == 1 || idx == 2 || idx == 3 || idx == 9 || idx == 10;
}

jxl_vardct_status_t jxl_dequant_matrix_params_parse(jxl_allocator_state *alloc, jxl_bs *bs,
                                                    jxl_transform_type dct_select,
                                                    const jxl_dequant_matrix_set_params *params,
                                                    jxl_dequant_matrix_params *out) {
    uint32_t mode;
    if (alloc == NULL || bs == NULL || out == NULL) {
        return JXL_VARDCT_BITSTREAM_ERROR;
    }
    jxl_dequant_matrix_params_free(alloc, out);
    jxl_dequant_matrix_params_init(out);
    out->dct_select = dct_select;

    mode = 0;
    JXL_VARDCT_TRY_BS(jxl_bs_read_bits(bs, 3, &mode));
    if (!encoding_allowed(mode, dct_select)) {
        return JXL_VARDCT_VALIDATION_ERROR;
    }

    out->encoding = (jxl_dequant_encoding)mode;
    switch (mode) {
        size_t ch;
    case 0:
        return jxl_dequant_matrix_params_default(alloc, dct_select, out);
    case 1:
        for (ch = 0; ch < 3; ++ch) {
            if (read_fixed_f16(bs, &out->hornuss[ch][0], 3) != JXL_VARDCT_OK) {
                jxl_dequant_matrix_params_free(alloc, out);
                return JXL_VARDCT_BITSTREAM_ERROR;
            }
        }
        return JXL_VARDCT_OK;
    case 2:
        for (ch = 0; ch < 3; ++ch) {
            if (read_fixed_f16(bs, &out->dct2[ch][0], 6) != JXL_VARDCT_OK) {
                jxl_dequant_matrix_params_free(alloc, out);
                return JXL_VARDCT_BITSTREAM_ERROR;
            }
        }
        return JXL_VARDCT_OK;
    case 3:
        for (ch = 0; ch < 3; ++ch) {
            if (read_fixed_f16(bs, &out->dct4_params[ch][0], 2) != JXL_VARDCT_OK) {
                jxl_dequant_matrix_params_free(alloc, out);
                return JXL_VARDCT_BITSTREAM_ERROR;
            }
        }
        return read_dct_params(alloc, bs, out);
    case 4:
        for (ch = 0; ch < 3; ++ch) {
            if (read_fixed_f16(bs, &out->dct4x8_params[ch][0], 1) != JXL_VARDCT_OK) {
                jxl_dequant_matrix_params_free(alloc, out);
                return JXL_VARDCT_BITSTREAM_ERROR;
            }
        }
        return read_dct_params(alloc, bs, out);
    case 5:
        for (ch = 0; ch < 3; ++ch) {
            size_t i;
            if (read_fixed_f16(bs, &out->afv_params[ch][0], 9) != JXL_VARDCT_OK) {
                jxl_dequant_matrix_params_free(alloc, out);
                return JXL_VARDCT_BITSTREAM_ERROR;
            }
            for (i = 0; i < 6; ++i) {
                out->afv_params[ch][i] *= 64.0f;
            }
        }
        if (read_dct_params(alloc, bs, out) != JXL_VARDCT_OK) {
            jxl_dequant_matrix_params_free(alloc, out);
            return JXL_VARDCT_BITSTREAM_ERROR;
        }
        for (ch = 0; ch < 3; ++ch) {
            jxl_free(alloc, out->dct4x4_bands[ch]);
            out->dct4x4_bands[ch] = out->dct_bands[ch];
            out->dct4x4_band_lens[ch] = out->dct_band_lens[ch];
            out->dct_bands[ch] = NULL;
            out->dct_band_lens[ch] = 0;
        }
        return read_dct_params(alloc, bs, out);
    case 6:
        return read_dct_params(alloc, bs, out);
    case 7: {
        size_t ch;
        uint32_t w = 0;
        uint32_t h = 0;
        jxl_modular_params mod_params;
        jxl_modular_image_destination dest;
        jxl_channel_shift shifts[3];
        jxl_modular_parse_ctx parse_ctx = {0};
        jxl_modular_status_t mst;
        size_t mat_len;
        jxl_transform_dequant_matrix_size(dct_select, &w, &h);
        JXL_VARDCT_TRY_BS(jxl_bs_read_f16_as_f32(bs, &out->raw_denominator));

        shifts[0] = jxl_channel_shift_from_shift(0);
        shifts[1] = jxl_channel_shift_from_shift(0);
        shifts[2] = jxl_channel_shift_from_shift(0);

        jxl_modular_params_init(&mod_params);
        if (!jxl_modular_params_set_channels(alloc, &mod_params, w, h, 256, params->bit_depth, shifts,
                                             3)) {
            jxl_modular_params_free(alloc, &mod_params);
            return JXL_VARDCT_OUT_OF_MEMORY;
        }

        parse_ctx.params = &mod_params;
        parse_ctx.global_ma = params->global_ma;
        parse_ctx.tracker = NULL;
        parse_ctx.ctx = params->ctx;

        jxl_modular_image_destination_init(&dest);

        mst = jxl_modular_dest_apply_local_header(alloc, bs, &parse_ctx, &dest);
        if (mst != JXL_MODULAR_OK) {
            jxl_modular_image_destination_free(alloc, &dest);
            jxl_modular_params_free(alloc, &mod_params);
            return jxl_vardct_from_modular(mst);
        }
        mst = jxl_modular_image_prepare_subimage_grids(alloc, &dest);
        if (mst != JXL_MODULAR_OK) {
            jxl_modular_image_destination_free(alloc, &dest);
            jxl_modular_params_free(alloc, &mod_params);
            return jxl_vardct_from_modular(mst);
        }
        mst = jxl_modular_subimage_decode(params->ctx, alloc, bs, &dest, params->stream_index, 0);
        if (mst != JXL_MODULAR_OK) {
            jxl_modular_image_destination_free(alloc, &dest);
            jxl_modular_params_free(alloc, &mod_params);
            return jxl_vardct_from_modular(mst);
        }
        mst = jxl_modular_gmodular_finish(params->ctx, alloc, &dest, w, h, params->bit_depth,
                                          &mod_params);
        if (mst != JXL_MODULAR_OK) {
            jxl_modular_image_destination_free(alloc, &dest);
            jxl_modular_params_free(alloc, &mod_params);
            return jxl_vardct_from_modular(mst);
        }

        mat_len = (size_t)w * (size_t)h;
        for (ch = 0; ch < 3; ++ch) {
            size_t y;
            const jxl_modular_grid_i32 *grid;
            if (ch >= dest.channels.info_len) {
                jxl_modular_image_destination_free(alloc, &dest);
                jxl_modular_params_free(alloc, &mod_params);
                return JXL_VARDCT_VALIDATION_ERROR;
            }
            grid = &dest.image_channels[ch];
            if (grid->buf == NULL || grid->width != w || grid->height != h) {
                jxl_modular_image_destination_free(alloc, &dest);
                jxl_modular_params_free(alloc, &mod_params);
                return JXL_VARDCT_VALIDATION_ERROR;
            }
            if (params != NULL && params->capture_jpeg_matrices && params->out_set != NULL &&
                dct_select == JXL_TRANSFORM_DCT8 && mat_len == 64) {
                float inv = 1.0f / out->raw_denominator;
                if ((int)(inv + 0.5f) == 2040) {
                    size_t y;
                    params->out_set->has_jpeg_matrices = 1;
                    for (y = 0; y < (size_t)h; ++y) {
                        size_t x;
                        for (x = 0; x < (size_t)w; ++x) {
                            params->out_set->jpeg_matrices[ch][y * (size_t)w + x] =
                                jxl_modular_grid_sample_as_i32(grid, x, y);
                        }
                    }
                }
            }
            out->dct_bands[ch] = jxl_alloc(alloc, mat_len * sizeof(float));
            if (out->dct_bands[ch] == NULL) {
                jxl_modular_image_destination_free(alloc, &dest);
                jxl_modular_params_free(alloc, &mod_params);
                return JXL_VARDCT_OUT_OF_MEMORY;
            }
            out->dct_band_lens[ch] = mat_len;
            for (y = 0; y < (size_t)h; ++y) {
                size_t x;
                for (x = 0; x < (size_t)w; ++x) {
                    int32_t sample = jxl_modular_grid_sample_as_i32(grid, x, y);
                    out->dct_bands[ch][y * (size_t)w + x] =
                        (float)sample * out->raw_denominator;
                }
            }
        }

        jxl_modular_image_destination_free(alloc, &dest);
        jxl_modular_params_free(alloc, &mod_params);
        return JXL_VARDCT_OK;
    }
    default:
        return JXL_VARDCT_BITSTREAM_ERROR;
    }
}

jxl_vardct_status_t jxl_dequant_matrix_set_parse(jxl_context *ctx, jxl_allocator_state *alloc,
                                                 jxl_bs *bs,
                                                 const jxl_dequant_matrix_set_params *params,
                                                 jxl_dequant_matrix_set *out) {
                                                     size_t i;
    int all_default;
    if (ctx == NULL || alloc == NULL || bs == NULL || params == NULL || out == NULL) {
        return JXL_VARDCT_BITSTREAM_ERROR;
    }
    jxl_dequant_matrix_set_free(out);
    jxl_dequant_matrix_set_init(out);
    out->ctx = ctx;

    all_default = 0;
    JXL_VARDCT_TRY_BS(jxl_bs_read_bool(bs, &all_default));
    if (all_default) {
        size_t i;
        for (i = 0; i < JXL_DEQUANT_MATRIX_COUNT; ++i) {
            jxl_vardct_status_t st =
                jxl_dequant_matrix_params_default(alloc, k_dct_select_list[i], &out->matrices[i]);
            if (st != JXL_VARDCT_OK) {
                jxl_dequant_matrix_set_free(out);
                return st;
            }
        }
        return jxl_dequant_matrix_set_build_weights(ctx, out);
    }

    for (i = 0; i < JXL_DEQUANT_MATRIX_COUNT; ++i) {
        jxl_vardct_status_t st;
	jxl_dequant_matrix_set_params local = *params;
        local.stream_index = params->stream_index + (uint32_t)i;
        local.capture_jpeg_matrices = (i == 0);
        local.out_set = out;
        st = jxl_dequant_matrix_params_parse(
            alloc, bs, k_dct_select_list[i], &local, &out->matrices[i]);
        if (st != JXL_VARDCT_OK) {
            jxl_dequant_matrix_set_free(out);
            return st;
        }
    }
    return jxl_dequant_matrix_set_build_weights(ctx, out);
}

const int32_t *jxl_dequant_matrix_set_jpeg_quant(const jxl_dequant_matrix_set *set, size_t channel) {
    if (set == NULL || !set->has_jpeg_matrices || channel >= 3) {
        return NULL;
    }
    return set->jpeg_matrices[channel];
}
