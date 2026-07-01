// SPDX-License-Identifier: MIT OR Apache-2.0
#include "render/features/upsampling.h"

#include <math.h>
#include <string.h>

static const jxl_upsampling_weights k_default_weights = {
    {
        -0.01716200f, -0.03452303f, -0.04022174f, -0.02921014f, -0.00624645f,
        0.14111091f, 0.28896755f, 0.00278718f, -0.01610267f, 0.56661550f,
        0.03777607f, -0.01986694f, -0.03144731f, -0.01185068f, -0.00213539f,
    },
    {
        -0.02419067f, -0.03491987f, -0.03693351f, -0.03094285f, -0.00529785f,
        -0.01663432f, -0.03556863f, -0.03888905f, -0.03516850f, -0.00989469f,
        0.23651958f, 0.33392945f, -0.01073543f, -0.01313181f, -0.03556694f,
        0.13048175f, 0.40103025f, 0.03951150f, -0.02077584f, 0.46914198f,
        -0.00209270f, -0.01484589f, -0.04064806f, 0.18942530f, 0.56279892f,
        0.06674400f, -0.02335494f, -0.03551682f, -0.00754830f, -0.02267919f,
        -0.02363578f, 0.00315804f, -0.03399098f, -0.01359519f, -0.00091653f,
        -0.00335467f, -0.01163294f, -0.01610294f, -0.00974088f, -0.00191622f,
        -0.01095446f, -0.03198464f, -0.04455121f, -0.02799790f, -0.00645912f,
        0.06390599f, 0.22963888f, 0.00630981f, -0.01897349f, 0.67537268f,
        0.08483369f, -0.02534994f, -0.02205197f, -0.01667999f, -0.00384443f,
    },
    {
        -0.02928613f, -0.03706353f, -0.03783812f, -0.03324558f, -0.00447632f,
        -0.02519406f, -0.03752601f, -0.03901508f, -0.03663285f, -0.00646649f,
        -0.02066407f, -0.03838633f, -0.04002101f, -0.03900035f, -0.00901973f,
        -0.01626393f, -0.03954148f, -0.04046620f, -0.03979621f, -0.01224485f,
        0.29895328f, 0.35757708f, -0.02447552f, -0.01081748f, -0.04314594f,
        0.23903219f, 0.41119301f, -0.00573046f, -0.01450239f, -0.04246845f,
        0.17567618f, 0.45220643f, 0.02287757f, -0.01936783f, -0.03583255f,
        0.11572472f, 0.47416733f, 0.06284440f, -0.02685066f, 0.42720050f,
        -0.02248939f, -0.01155273f, -0.04562755f, 0.28689496f, 0.49093869f,
        -0.00007891f, -0.01545926f, -0.04562659f, 0.21238920f, 0.53980934f,
        0.03369474f, -0.02070211f, -0.03866988f, 0.14229550f, 0.56593398f,
        0.08045181f, -0.02888298f, -0.03680918f, -0.00542229f, -0.02920477f,
        -0.02788574f, -0.02118180f, -0.03942402f, -0.00775547f, -0.02433614f,
        -0.03193943f, -0.02030828f, -0.04044014f, -0.01074016f, -0.01930822f,
        -0.03620399f, -0.01974125f, -0.03919545f, -0.01456093f, -0.00045072f,
        -0.00360110f, -0.01020207f, -0.01231907f, -0.00638988f, -0.00071592f,
        -0.00279122f, -0.00957115f, -0.01288327f, -0.00730937f, -0.00107783f,
        -0.00210156f, -0.00890705f, -0.01317668f, -0.00813895f, -0.00153491f,
        -0.02128481f, -0.04173044f, -0.04831487f, -0.03293190f, -0.00525260f,
        -0.01720322f, -0.04052736f, -0.05045706f, -0.03607317f, -0.00738030f,
        -0.01341764f, -0.03965629f, -0.05151616f, -0.03814886f, -0.01005819f,
        0.18968273f, 0.33063684f, -0.01300105f, -0.01372950f, -0.04017465f,
        0.13727832f, 0.36402234f, 0.01027890f, -0.01832107f, -0.03365072f,
        0.08734506f, 0.38194295f, 0.04338228f, -0.02525993f, 0.56408126f,
        0.00458352f, -0.01648227f, -0.04887868f, 0.24585519f, 0.62026135f,
        0.04314807f, -0.02213737f, -0.04158014f, 0.16637289f, 0.65027023f,
        0.09621636f, -0.03101388f, -0.04082742f, -0.00904519f, -0.02790922f,
        -0.02117818f, 0.00798662f, -0.03995711f, -0.01243427f, -0.02231705f,
        -0.02946266f, 0.00992055f, -0.03600283f, -0.01684920f, -0.00111684f,
        -0.00411204f, -0.01297130f, -0.01723725f, -0.01022545f, -0.00165306f,
        -0.00313110f, -0.01218016f, -0.01763266f, -0.01125620f, -0.00231663f,
        -0.01374149f, -0.03797620f, -0.05142937f, -0.03117307f, -0.00581914f,
        -0.01064003f, -0.03608089f, -0.05272168f, -0.03375670f, -0.00795586f,
        0.09628104f, 0.27129991f, -0.00353779f, -0.01734151f, -0.03153981f,
        0.05686230f, 0.28500998f, 0.02230594f, -0.02374955f, 0.68214326f,
        0.05018048f, -0.02320852f, -0.04383616f, 0.18459474f, 0.71517975f,
        0.10805613f, -0.03263677f, -0.03637639f, -0.01394373f, -0.02511203f,
        -0.01728636f, 0.05407331f, -0.02867568f, -0.01893131f, -0.00240854f,
        -0.00446511f, -0.01636187f, -0.02377053f, -0.01522848f, -0.00333334f,
        -0.00819975f, -0.02964169f, -0.04499287f, -0.02745350f, -0.00612408f,
        0.02727416f, 0.19446600f, 0.00159832f, -0.02232473f, 0.74982506f,
        0.11452620f, -0.03348048f, -0.01605681f, -0.02070339f, -0.00458223f,
    },
};

void jxl_upsampling_weights_set_defaults(jxl_upsampling_weights *out) {
    if (out != NULL) {
        *out = k_default_weights;
    }
}

typedef struct {
    float *data;
    uint32_t width;
    uint32_t height;
    size_t stride;
} jxl_owned_grid;

static void owned_grid_free(jxl_allocator_state *alloc, jxl_owned_grid *g) {
    if (g != NULL && g->data != NULL) {
        jxl_free(alloc, g->data);
        g->data = NULL;
    }
}

static int owned_grid_alloc(jxl_allocator_state *alloc, uint32_t width, uint32_t height,
                            jxl_owned_grid *out) {
    size_t stride;
    size_t count;
    float *data;
    if (alloc == NULL || out == NULL || width == 0 || height == 0) {
        return 0;
    }
    stride = width;
    count = stride * (size_t)height;
    data = jxl_alloc(alloc, count * sizeof(float));
    if (data == NULL) {
        return 0;
    }
    out->data = data;
    out->width = width;
    out->height = height;
    out->stride = stride;
    return 1;
}

static jxl_const_subgrid_f32 owned_grid_const(const jxl_owned_grid *g) {
    return jxl_const_subgrid_f32_from_buf(g->data, g->width, g->height, g->stride);
}

static void mirror_edges_padding(float *buf, size_t padded_width, size_t padded_height,
                                 size_t padding, size_t grid_width, size_t grid_height) {
                                     size_t y;
    size_t height = grid_height;

    for (y = padding; y < height + padding; ++y) {
        size_t x;
        for (x = 0; x < padding; ++x) {
            buf[y * padded_width + x] = buf[y * padded_width + padding * 2 - x - 1];
            buf[(y + 1) * padded_width - x - 1] =
                buf[(y + 1) * padded_width - padding * 2 + x];
        }
    }

    for (y = 0; y < padding; ++y) {
        memcpy(buf + y * padded_width, buf + (padding * 2 - y - 1) * padded_width,
               padded_width * sizeof(float));
    }

    for (y = 0; y < padding; ++y) {
        memcpy(buf + (height + padding + y) * padded_width,
               buf + (height + padding - y - 1) * padded_width, padded_width * sizeof(float));
    }
}

static int build_weights_quarter(uint32_t k, const float *weights, float (*out)[25]) {
    size_t i;
    uint32_t y;
    uint32_t mat_n = k / 2u;
    size_t weight_idx = 0;
    size_t quarter_count = (size_t)mat_n * (size_t)mat_n;
    for (i = 0; i < quarter_count; ++i) {
        memset(out[i], 0, sizeof(out[i]));
    }
    for (y = 0; y < 5u * mat_n; ++y) {
        uint32_t x;
        uint32_t mat_y = y / 5u;
        uint32_t ky = y % 5u;
        for (x = y; x < 5u * mat_n; ++x) {
            uint32_t mat_x = x / 5u;
            uint32_t kx = x % 5u;
            float w = weights[weight_idx++];
            out[mat_y * mat_n + mat_x][ky * 5u + kx] = w;
            out[mat_x * mat_n + mat_y][kx * 5u + ky] = w;
        }
    }
    return 1;
}

static int upsample_inner(jxl_allocator_state *alloc, jxl_const_subgrid_f32 grid, uint32_t k,
                          const float *weights, jxl_owned_grid *out) {
                              uint32_t t;
                              uint32_t y;
    uint32_t k_log2;
    uint32_t frame_width;
    uint32_t frame_height;
    float weights_quarter[16][25];
    uint32_t mat_n_u;
    uint32_t mat_n;
    uint32_t grid_width;
    uint32_t grid_height;
    const size_t padding = 2;
    size_t padded_width;
    size_t padded_height;
    float *padded_buf;
    if (alloc == NULL || grid.data == NULL || out == NULL || k != 2u && k != 4u && k != 8u) {
        return 0;
    }
    grid_width = grid.width;
    grid_height = grid.height;
    if (grid_width == 0 || grid_height == 0) {
        return 0;
    }
    k_log2 = 0;
    for (t = k; t > 1u; t >>= 1) {
        k_log2++;
    }
    frame_width = grid_width << k_log2;
    frame_height = grid_height << k_log2;

    padded_width = (size_t)grid_width + padding * 2;
    padded_height = (size_t)grid_height + padding * 2;
    padded_buf = jxl_alloc(alloc, padded_width * padded_height * sizeof(float));
    if (padded_buf == NULL) {
        return 0;
    }
    memset(padded_buf, 0, padded_width * padded_height * sizeof(float));
    for (y = 0; y < grid_height; ++y) {
        uint32_t x;
        for (x = 0; x < grid_width; ++x) {
            padded_buf[(y + padding) * padded_width + padding + x] =
                jxl_const_subgrid_f32_get(grid, x, y);
        }
    }
    mirror_edges_padding(padded_buf, padded_width, padded_height, padding, grid_width, grid_height);

    mat_n = k / 2u;
    if (!build_weights_quarter(k, weights, weights_quarter)) {
        jxl_free(alloc, padded_buf);
        return 0;
    }

    if (!owned_grid_alloc(alloc, frame_width, frame_height, out)) {
        jxl_free(alloc, padded_buf);
        return 0;
    }

    mat_n_u = mat_n;
    for (y = 0; y < frame_height; ++y) {
        uint32_t x;
        uint32_t ref_y = y / k;
        uint32_t rem_y = y % k;
        uint32_t mat_y = rem_y < k - rem_y - 1u ? rem_y : k - rem_y - 1u;
        int flip_v = rem_y >= mat_n_u;
        for (x = 0; x < frame_width; ++x) {
            uint32_t iy;
            uint32_t ref_x = x / k;
            uint32_t rem_x = x % k;
            uint32_t mat_x = rem_x < k - rem_x - 1u ? rem_x : k - rem_x - 1u;
            int flip_h = rem_x >= mat_n_u;
            float sum = 0.0f;
            float out_v;
            const float *kernel = weights_quarter[mat_y * mat_n_u + mat_x];
            float min_v = INFINITY;
            float max_v = -INFINITY;
            for (iy = 0; iy < 5u; ++iy) {
                uint32_t ix;
                uint32_t ky = flip_v ? 4u - iy : iy;
                for (ix = 0; ix < 5u; ++ix) {
                    uint32_t kx = flip_h ? 4u - ix : ix;
                    float sample =
                        padded_buf[(ref_y + iy) * padded_width + (ref_x + ix)];
                    sum += kernel[ky * 5u + kx] * sample;
                    if (sample < min_v) {
                        min_v = sample;
                    }
                    if (sample > max_v) {
                        max_v = sample;
                    }
                }
            }
            if (!isfinite(min_v)) {
                out_v = NAN;
            } else if (sum < min_v) {
                out_v = min_v;
            } else if (sum > max_v) {
                out_v = max_v;
            } else {
                out_v = sum;
            }
            out->data[(size_t)y * out->stride + x] = out_v;
        }
    }

    jxl_free(alloc, padded_buf);
    return 1;
}

int jxl_apply_nonseparable_upsampling_single(jxl_allocator_state *alloc, jxl_const_subgrid_f32 src,
                                             const jxl_upsampling_weights *weights,
                                             uint32_t factor_log2, uint32_t target_w,
                                             uint32_t target_h, float *dst, size_t dst_stride) {
                                                 uint32_t i;
                                                 uint32_t y;
                                                 size_t j;
    jxl_owned_grid owned[4];
    size_t owned_count;
    jxl_const_subgrid_f32 grid;
    uint32_t up8;
    uint32_t last_up;
    if (alloc == NULL || src.data == NULL || dst == NULL || target_w == 0 || target_h == 0) {
        return 0;
    }
    const jxl_upsampling_weights *w = weights != NULL ? weights : &k_default_weights;
    if (factor_log2 == 0) {
        uint32_t y;
        if (src.width != target_w || src.height != target_h) {
            return 0;
        }
        for (y = 0; y < target_h; ++y) {
            uint32_t x;
            for (x = 0; x < target_w; ++x) {
                dst[(size_t)y * dst_stride + x] = jxl_const_subgrid_f32_get(src, x, y);
            }
        }
        return 1;
    }

    grid = src;
    memset(owned, 0, sizeof(owned));
    owned_count = 0;

    up8 = factor_log2 / 3u;
    last_up = factor_log2 % 3u;
    for (i = 0; i < up8; ++i) {
        if (!upsample_inner(alloc, grid, 8u, w->up8, &owned[owned_count])) {
            size_t j;
            for (j = 0; j < owned_count; ++j) {
                owned_grid_free(alloc, &owned[j]);
            }
            return 0;
        }
        grid = owned_grid_const(&owned[owned_count]);
        owned_count++;
    }

    if (last_up == 1u) {
        if (!upsample_inner(alloc, grid, 2u, w->up2, &owned[owned_count])) {
            size_t j;
            for (j = 0; j < owned_count; ++j) {
                owned_grid_free(alloc, &owned[j]);
            }
            return 0;
        }
        grid = owned_grid_const(&owned[owned_count]);
        owned_count++;
    } else if (last_up == 2u) {
        if (!upsample_inner(alloc, grid, 4u, w->up4, &owned[owned_count])) {
            size_t j;
            for (j = 0; j < owned_count; ++j) {
                owned_grid_free(alloc, &owned[j]);
            }
            return 0;
        }
        grid = owned_grid_const(&owned[owned_count]);
        owned_count++;
    }

    if (grid.width != target_w || grid.height != target_h) {
        size_t j;
        for (j = 0; j < owned_count; ++j) {
            owned_grid_free(alloc, &owned[j]);
        }
        return 0;
    }

    for (y = 0; y < target_h; ++y) {
        uint32_t x;
        for (x = 0; x < target_w; ++x) {
            dst[(size_t)y * dst_stride + x] = jxl_const_subgrid_f32_get(grid, x, y);
        }
    }

    for (j = 0; j < owned_count; ++j) {
        owned_grid_free(alloc, &owned[j]);
    }
    return 1;
}
