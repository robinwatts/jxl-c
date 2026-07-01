// SPDX-License-Identifier: MIT OR Apache-2.0
#include "transform.h"

#include "modular/util.h"

#include "allocator.h"
#include <string.h>

static const jxl_u32_spec k_rct_begin_c[4] = {JXL_U32_BITS(0, 3), JXL_U32_BITS(8, 6),
                                              JXL_U32_BITS(72, 10), JXL_U32_BITS(1096, 13)};
static const jxl_u32_spec k_rct_type[4] = {JXL_U32_C(6), JXL_U32_BITS(0, 2), JXL_U32_BITS(2, 4),
                                           JXL_U32_BITS(10, 6)};

#define k_palette_begin_c k_rct_begin_c
static const jxl_u32_spec k_palette_num_c[4] = {JXL_U32_C(1), JXL_U32_C(3), JXL_U32_C(4),
                                                JXL_U32_BITS(1, 13)};
static const jxl_u32_spec k_palette_nb_colours[4] = {JXL_U32_BITS(0, 8), JXL_U32_BITS(256, 10),
                                                     JXL_U32_BITS(1280, 12), JXL_U32_BITS(5376, 16)};
static const jxl_u32_spec k_palette_nb_deltas[4] = {JXL_U32_C(0), JXL_U32_BITS(1, 8),
                                                    JXL_U32_BITS(257, 10), JXL_U32_BITS(1281, 16)};

static const jxl_u32_spec k_squeeze_num_sq[4] = {JXL_U32_C(0), JXL_U32_BITS(1, 4), JXL_U32_BITS(9, 6),
                                                 JXL_U32_BITS(41, 8)};
#define k_squeeze_begin_c k_rct_begin_c
static const jxl_u32_spec k_squeeze_num_c[4] = {JXL_U32_C(1), JXL_U32_C(2), JXL_U32_C(3),
                                                JXL_U32_BITS(4, 4)};

void jxl_transform_rct_init_defaults(jxl_transform_rct *rct) {
    if (rct != NULL) {
        memset(rct, 0, sizeof(*rct));
    }
}

void jxl_transform_squeeze_init(jxl_transform_squeeze *sq) {
    if (sq != NULL) {
        memset(sq, 0, sizeof(*sq));
    }
}

void jxl_transform_squeeze_free(jxl_allocator_state *alloc, jxl_transform_squeeze *sq) {
    if (sq == NULL) {
        return;
    }
    jxl_free(alloc, sq->sp);
    sq->sp = NULL;
    sq->sp_len = 0;
}

void jxl_transform_info_free(jxl_allocator_state *alloc, jxl_transform_info *tr) {
    if (tr == NULL) {
        return;
    }
    if (tr->kind == JXL_TRANSFORM_KIND_SQUEEZE) {
        jxl_transform_squeeze_free(alloc, &tr->u.squeeze);
    }
    memset(tr, 0, sizeof(*tr));
}

static jxl_modular_status_t rct_parse(jxl_bs *bs, jxl_transform_rct *out) {
    JXL_MODULAR_TRY_BS(jxl_bs_read_u32(bs, k_rct_begin_c, &out->begin_c));
    JXL_MODULAR_TRY_BS(jxl_bs_read_u32(bs, k_rct_type, &out->rct_type));
    return JXL_MODULAR_OK;
}

static jxl_modular_status_t palette_parse(jxl_bs *bs, const jxl_wp_header *wp,
                                          jxl_transform_palette *out) {
    uint32_t pred_bits;
    JXL_MODULAR_TRY_BS(jxl_bs_read_u32(bs, k_palette_begin_c, &out->begin_c));
    JXL_MODULAR_TRY_BS(jxl_bs_read_u32(bs, k_palette_num_c, &out->num_c));
    JXL_MODULAR_TRY_BS(jxl_bs_read_u32(bs, k_palette_nb_colours, &out->nb_colours));
    JXL_MODULAR_TRY_BS(jxl_bs_read_u32(bs, k_palette_nb_deltas, &out->nb_deltas));
    pred_bits = 0;
    JXL_MODULAR_TRY_BS(jxl_bs_read_bits(bs, 4, &pred_bits));
    if (jxl_predictor_from_u32(pred_bits, &out->d_pred) != JXL_MODULAR_OK) {
        return JXL_MODULAR_BITSTREAM_ERROR;
    }
    out->has_wp_header = 0;
    if (out->d_pred == JXL_PREDICTOR_SELF_CORRECTING && wp != NULL) {
        out->wp_header = *wp;
        out->has_wp_header = 1;
    }
    return JXL_MODULAR_OK;
}

static jxl_modular_status_t squeeze_parse(jxl_allocator_state *alloc, jxl_bs *bs, jxl_transform_squeeze *out) {
    jxl_transform_squeeze_free(alloc, out);
    jxl_transform_squeeze_init(out);
    JXL_MODULAR_TRY_BS(jxl_bs_read_u32(bs, k_squeeze_num_sq, &out->num_sq));
    if (out->num_sq > 1024) {
        return JXL_MODULAR_BITSTREAM_ERROR;
    }
    out->sp_len = out->num_sq;
    if (out->sp_len > 0) {
        size_t i;
        out->sp = jxl_calloc(alloc, out->sp_len, sizeof(*out->sp));
        if (out->sp == NULL) {
            return JXL_MODULAR_OUT_OF_MEMORY;
        }
        for (i = 0; i < out->sp_len; ++i) {
            int horiz = 0;
            int inplace = 0;
            JXL_MODULAR_TRY_BS(jxl_bs_read_bool(bs, &horiz));
            JXL_MODULAR_TRY_BS(jxl_bs_read_bool(bs, &inplace));
            out->sp[i].horizontal = horiz;
            out->sp[i].in_place = inplace;
            JXL_MODULAR_TRY_BS(jxl_bs_read_u32(bs, k_squeeze_begin_c, &out->sp[i].begin_c));
            JXL_MODULAR_TRY_BS(jxl_bs_read_u32(bs, k_squeeze_num_c, &out->sp[i].num_c));
        }
    }
    return JXL_MODULAR_OK;
}

jxl_modular_status_t jxl_transform_info_parse(jxl_allocator_state *alloc, jxl_bs *bs,
                                                const jxl_wp_header *wp,
                                              jxl_transform_info *out) {
    uint32_t tr;
    if (bs == NULL || out == NULL) {
        return JXL_MODULAR_BITSTREAM_ERROR;
    }
    jxl_transform_info_free(alloc, out);
    tr = 0;
    JXL_MODULAR_TRY_BS(jxl_bs_read_bits(bs, 2, &tr));
    switch (tr) {
    case 0:
        out->kind = JXL_TRANSFORM_KIND_RCT;
        return rct_parse(bs, &out->u.rct);  /* no alloc */
    case 1:
        out->kind = JXL_TRANSFORM_KIND_PALETTE;
        return palette_parse(bs, wp, &out->u.palette);
    case 2:
        out->kind = JXL_TRANSFORM_KIND_SQUEEZE;
        return squeeze_parse(alloc, bs, &out->u.squeeze);
    default:
        return JXL_MODULAR_BITSTREAM_ERROR;
    }
}

static jxl_modular_status_t rct_prepare(jxl_transform_rct *rct, jxl_modular_channels *channels) {
    size_t i;
    uint32_t begin_c = rct->begin_c;
    uint32_t end_c = begin_c + 3;
    uint32_t width;
    uint32_t height;
    if (end_c > channels->info_len) {
        return JXL_MODULAR_INVALID_RCT_PARAMS;
    }
    width = channels->info[begin_c].width;
    height = channels->info[begin_c].height;
    for (i = (size_t)begin_c + 1; i < (size_t)end_c; ++i) {
        if (channels->info[i].width != width || channels->info[i].height != height) {
            return JXL_MODULAR_INVALID_RCT_PARAMS;
        }
    }
    return JXL_MODULAR_OK;
}

static jxl_modular_status_t palette_prepare(jxl_allocator_state *alloc, jxl_transform_palette *pal,
                                            jxl_modular_channels *channels) {
                                                size_t i;
    uint32_t begin_c = pal->begin_c;
    uint32_t end_c = begin_c + pal->num_c;
    uint32_t width;
    uint32_t height;
    jxl_modular_channel_info index_ch;
    if (end_c > channels->info_len) {
        return JXL_MODULAR_INVALID_PALETTE_PARAMS;
    }
    if (begin_c < channels->nb_meta_channels) {
        if (end_c > channels->nb_meta_channels) {
            return JXL_MODULAR_INVALID_PALETTE_PARAMS;
        }
        channels->nb_meta_channels = channels->nb_meta_channels + 2 - pal->num_c;
    } else {
        channels->nb_meta_channels += 1;
    }
    width = channels->info[begin_c].width;
    height = channels->info[begin_c].height;
    for (i = (size_t)begin_c + 1; i < (size_t)end_c; ++i) {
        if (channels->info[i].width != width || channels->info[i].height != height) {
            return JXL_MODULAR_INVALID_PALETTE_PARAMS;
        }
    }
    jxl_modular_channels_remove_range(channels, (size_t)begin_c + 1, (size_t)end_c);
    index_ch =
        jxl_modular_channel_info_new_unshiftable(pal->nb_colours, pal->num_c);
    if (jxl_modular_channels_insert(alloc, channels, 0, index_ch) != JXL_MODULAR_OK) {
        return JXL_MODULAR_OUT_OF_MEMORY;
    }
    return JXL_MODULAR_OK;
}

static int sq_params_push(jxl_allocator_state *alloc, jxl_squeeze_params **tmp, size_t *tmp_len,
                          size_t *tmp_cap, jxl_squeeze_params params) {
    size_t new_cap;
    jxl_squeeze_params *grown;

    if (*tmp_len + 1 > *tmp_cap) {
        new_cap = *tmp_cap == 0 ? 4 : *tmp_cap * 2;
        grown = jxl_realloc(alloc, *tmp, new_cap * sizeof(**tmp));
        if (grown == NULL) {
            return 0;
        }
        *tmp = grown;
        *tmp_cap = new_cap;
    }
    (*tmp)[*tmp_len] = params;
    *tmp_len += 1;
    return 1;
}

static void squeeze_set_default_params(jxl_allocator_state *alloc, jxl_transform_squeeze *sq, const jxl_modular_channels *channels) {
    size_t tmp_len;
    size_t tmp_cap;
    jxl_squeeze_params param_base;
    uint32_t first;
    uint32_t w;
    uint32_t h;
    jxl_squeeze_params *tmp;

    memset(&param_base, 0, sizeof(param_base));
    if (sq->sp_len > 0) {
        return;
    }
    first = channels->nb_meta_channels;
    if (first >= channels->info_len) {
        return;
    }
    w = channels->info[first].original_width;
    h = channels->info[first].original_height;
    if (w == 0 || h == 0) {
        w = channels->info[first].width;
        h = channels->info[first].height;
    }
    tmp = NULL;
    tmp_len = 0;
    tmp_cap = 0;

    if (channels->info_len - first >= 3) {
        uint32_t nw;
        uint32_t nh;
        jxl_squeeze_params base;
        jxl_squeeze_params horiz;
        const jxl_modular_channel_info *next;

        next = &channels->info[first + 1];
        nw = next->original_width != 0 ? next->original_width : next->width;
        nh = next->original_height != 0 ? next->original_height : next->height;
        if (nw == w && nh == h) {
            memset(&base, 0, sizeof(base));
            base.begin_c = first + 1;
            base.num_c = 2;
            base.in_place = 0;
            base.horizontal = 0;

            horiz = base;
            horiz.horizontal = 1;
            if (!sq_params_push(alloc, &tmp, &tmp_len, &tmp_cap, horiz)) {
                goto squeeze_defaults_done;
            }
            if (!sq_params_push(alloc, &tmp, &tmp_len, &tmp_cap, base)) {
                goto squeeze_defaults_done;
            }
        }
    }

    param_base.begin_c = first;
    param_base.num_c = (uint32_t)(channels->info_len - first);
    param_base.in_place = 1;
    param_base.horizontal = 0;

    if (h >= w && h > 8) {
        jxl_squeeze_params p;

        p = param_base;
        p.horizontal = 0;
        if (!sq_params_push(alloc, &tmp, &tmp_len, &tmp_cap, p)) {
            goto squeeze_defaults_done;
        }
        h = (h + 1) / 2;
    }
    while (w > 8 || h > 8) {
        jxl_squeeze_params p;

        if (w > 8) {
            p = param_base;
            p.horizontal = 1;
            if (!sq_params_push(alloc, &tmp, &tmp_len, &tmp_cap, p)) {
                goto squeeze_defaults_done;
            }
            w = (w + 1) / 2;
        }
        if (h > 8) {
            p = param_base;
            p.horizontal = 0;
            if (!sq_params_push(alloc, &tmp, &tmp_len, &tmp_cap, p)) {
                goto squeeze_defaults_done;
            }
            h = (h + 1) / 2;
        }
    }

squeeze_defaults_done:
    if (tmp_len > 0) {
        sq->sp = tmp;
        sq->sp_len = tmp_len;
        sq->num_sq = (uint32_t)tmp_len;
    } else {
        jxl_free(alloc, tmp);
    }
}

void jxl_transform_squeeze_rebuild_default_params(jxl_allocator_state *alloc,
                                                jxl_transform_squeeze *sq,
                                                const jxl_modular_channels *channels) {
    if (sq == NULL || channels == NULL) {
        return;
    }
    jxl_free(alloc, sq->sp);
    sq->sp = NULL;
    sq->sp_len = 0;
    sq->num_sq = 0;
    squeeze_set_default_params(alloc, sq, channels);
}

static jxl_modular_status_t squeeze_apply_step_channels(jxl_allocator_state *alloc, const jxl_squeeze_params *sp,
                                                          jxl_modular_channels *channels) {
    uint32_t idx;
    size_t residu_count;
    uint32_t begin;
    uint32_t end;
    jxl_modular_channel_info *residu;

    if (sp == NULL || channels == NULL) {
        return JXL_MODULAR_BITSTREAM_ERROR;
    }
    begin = sp->begin_c;
    end = begin + sp->num_c;
    if (end > channels->info_len) {
        return JXL_MODULAR_INVALID_SQUEEZE_PARAMS;
    }
    if (begin < channels->nb_meta_channels) {
        if (!sp->in_place || end > channels->nb_meta_channels) {
            return JXL_MODULAR_INVALID_SQUEEZE_PARAMS;
        }
        channels->nb_meta_channels += sp->num_c;
    }
    residu = jxl_calloc(alloc, sp->num_c, sizeof(*residu));
    if (residu == NULL) {
        return JXL_MODULAR_OUT_OF_MEMORY;
    }
    residu_count = 0;
    for (idx = 0; idx < sp->num_c; ++idx) {
        size_t i;
        jxl_modular_channel_info r;
        uint32_t len;
        jxl_modular_channel_info *ch;
        uint32_t *target_len;
        uint32_t *residu_len;
        int32_t *target_shift;
        int32_t *residu_shift;

        i = (size_t)begin + idx;
        ch = &channels->info[i];
        r = *ch;
        target_len = sp->horizontal ? &ch->width : &ch->height;
        residu_len = sp->horizontal ? &r.width : &r.height;
        target_shift = sp->horizontal ? &ch->hshift : &ch->vshift;
        residu_shift = sp->horizontal ? &r.hshift : &r.vshift;
        if (*target_len == 0 || (sp->horizontal ? ch->height : ch->width) == 0) {
            jxl_free(alloc, residu);
            return JXL_MODULAR_INVALID_SQUEEZE_PARAMS;
        }
        if (*target_shift > 30 || *residu_shift > 30) {
            jxl_free(alloc, residu);
            return JXL_MODULAR_INVALID_SQUEEZE_PARAMS;
        }
        len = *target_len;
        *target_len = (len + 1) / 2;
        *residu_len = len / 2;
        if (*target_shift >= 0) {
            *target_shift += 1;
            *residu_shift += 1;
        }
        residu[residu_count++] = r;
    }
    if (sp->in_place) {
        size_t tail;
        jxl_modular_channel_info *combined;

        tail = channels->info_len - (size_t)end;
        if (tail > 0) {
            combined = jxl_alloc(alloc, (residu_count + tail) * sizeof(*combined));
            if (combined == NULL) {
                jxl_free(alloc, residu);
                return JXL_MODULAR_OUT_OF_MEMORY;
            }
            memcpy(combined, residu, residu_count * sizeof(*combined));
            memcpy(combined + residu_count, &channels->info[end], tail * sizeof(*combined));
            jxl_free(alloc, residu);
            jxl_modular_channels_remove_range(channels, (size_t)end, channels->info_len);
            jxl_modular_channels_append_slice(alloc, channels, combined, residu_count + tail);
            jxl_free(alloc, combined);
        } else {
            jxl_modular_channels_append_slice(alloc, channels, residu, residu_count);
            jxl_free(alloc, residu);
        }
    } else {
        jxl_modular_channels_append_slice(alloc, channels, residu, residu_count);
        jxl_free(alloc, residu);
    }
    return JXL_MODULAR_OK;
}

static jxl_modular_status_t squeeze_prepare(jxl_allocator_state *alloc, jxl_transform_squeeze *sq, jxl_modular_channels *channels) {
    size_t si;
    squeeze_set_default_params(alloc, sq, channels);
    for (si = 0; si < sq->sp_len; ++si) {
        jxl_modular_status_t st = squeeze_apply_step_channels(alloc, &sq->sp[si], channels);
        if (st != JXL_MODULAR_OK) {
            return st;
        }
    }
    return JXL_MODULAR_OK;
}

size_t jxl_transform_squeeze_inverse_steps(jxl_allocator_state *alloc,
                                           const jxl_transform_squeeze *sq,
                                           const jxl_modular_params *params,
                                           jxl_squeeze_inverse_step *steps, size_t steps_cap) {
                                               size_t si;
    jxl_modular_channels sim;
    size_t n;
    if (sq == NULL || params == NULL || steps == NULL || steps_cap == 0) {
        return 0;
    }
    jxl_modular_channels_init(&sim);
    if (jxl_modular_channels_from_params(alloc, params, &sim) != JXL_MODULAR_OK) {
        return 0;
    }
    if (sq->sp == NULL || sq->sp_len == 0) {
        jxl_modular_channels_free(alloc, &sim);
        return 0;
    }
    n = 0;
    for (si = 0; si < sq->sp_len && n < steps_cap; ++si) {
        size_t begin;
        size_t end;
        const jxl_squeeze_params *sp;

        sp = &sq->sp[si];
        if (squeeze_apply_step_channels(alloc, sp, &sim) != JXL_MODULAR_OK) {
            break;
        }
        begin = sp->begin_c;
        end = begin + sp->num_c;
        if (end > sim.info_len) {
            break;
        }
        steps[n].begin = begin;
        steps[n].num_c = sp->num_c;
        steps[n].horizontal = sp->horizontal;
        steps[n].residual_start = sp->in_place ? end : (sim.info_len - sp->num_c);
        n++;
    }
    if (n != sq->sp_len) {
        jxl_modular_channels_free(alloc, &sim);
        return 0;
    }
    jxl_modular_channels_free(alloc, &sim);
    return n;
}

jxl_modular_status_t jxl_transform_prepare_channel_info(jxl_allocator_state *alloc,
                                                      jxl_transform_info *tr,
                                                          jxl_modular_channels *channels) {
    if (tr == NULL || channels == NULL) {
        return JXL_MODULAR_BITSTREAM_ERROR;
    }
    switch (tr->kind) {
    case JXL_TRANSFORM_KIND_RCT:
        return rct_prepare(&tr->u.rct, channels);
    case JXL_TRANSFORM_KIND_PALETTE:
        return palette_prepare(alloc, &tr->u.palette, channels);
    case JXL_TRANSFORM_KIND_SQUEEZE:
        return squeeze_prepare(alloc, &tr->u.squeeze, channels);
    }
    return JXL_MODULAR_BITSTREAM_ERROR;
}

int jxl_transform_is_palette(const jxl_transform_info *tr) {
    return tr != NULL && tr->kind == JXL_TRANSFORM_KIND_PALETTE;
}

int jxl_transform_is_squeeze(const jxl_transform_info *tr) {
    return tr != NULL && tr->kind == JXL_TRANSFORM_KIND_SQUEEZE;
}

jxl_modular_status_t jxl_transform_prepare_meta_channels(const jxl_transform_info *tr,
                                                         size_t *meta_palette_w,
                                                         size_t *meta_palette_h) {
    if (meta_palette_w != NULL) {
        *meta_palette_w = 0;
    }
    if (meta_palette_h != NULL) {
        *meta_palette_h = 0;
    }
    if (tr == NULL || tr->kind != JXL_TRANSFORM_KIND_PALETTE) {
        return JXL_MODULAR_OK;
    }
    if (meta_palette_w != NULL) {
        *meta_palette_w = tr->u.palette.nb_colours;
    }
    if (meta_palette_h != NULL) {
        *meta_palette_h = tr->u.palette.num_c;
    }
    return JXL_MODULAR_OK;
}
