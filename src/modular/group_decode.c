// SPDX-License-Identifier: MIT OR Apache-2.0
#include "group_decode.h"

#include "bitstream/bitstream.h"
#include "frame/frame.h"
#include "frame/lf_global_modular.h"
#include "frame/lf_group.h"
#include "frame/pass_group.h"
#include "modular/group_subimage.h"
#include "modular/region.h"

#include "jxl_oxide/jxl_types.h"
#include <stdio.h>
#include <stdlib.h>

static int lf_pg_filter(jxl_context *ctx, uint32_t group) {
    if (JXL_DEBUG_FLAG(ctx, only_lf_pg_active)) {
        return JXL_DEBUG_FLAG(ctx, only_lf_pg) != group;
    }
    if (JXL_DEBUG_FLAG(ctx, skip_all_lf_pg)) {
        return 1;
    }
    if (!JXL_DEBUG_FLAG(ctx, skip_lf_pg_active)) {
        return 0;
    }
    return JXL_DEBUG_FLAG(ctx, skip_lf_pg) == group;
}

static jxl_status_t frame_to_status(jxl_frame_status_t st) {
    switch (st) {
    case JXL_FRAME_OK:
        return JXL_OK;
    case JXL_FRAME_OUT_OF_MEMORY:
        return JXL_ERROR_OUT_OF_MEMORY;
    default:
        return JXL_ERROR_INVALID_INPUT;
    }
}

jxl_status_t jxl_modular_decode_pass_group_fallback(jxl_context *ctx, jxl_allocator_state *alloc,
                                                    const jxl_parsed_image_header *parsed,
                                                    const jxl_frame *frame, int has_ma,
                                                    jxl_ma_config *global_ma,
                                                    jxl_modular_params *mod_params,
                                                    jxl_modular_image_destination *dest) {
    uint32_t pass;
    uint32_t pass_idx;
    uint32_t group_idx;
    jxl_bs pg_bs;
    jxl_pass_group_modular_params pg = {0};
    uint32_t ng;
    uint32_t np;
    const jxl_frame_group_data *best_pg;
    jxl_frame_status_t fst;
    if (alloc == NULL || parsed == NULL || frame == NULL || global_ma == NULL ||
        mod_params == NULL || dest == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }

    pass_idx = 0;
    group_idx = 0;
    ng = jxl_frame_header_num_groups(&frame->header);
    np = frame->header.passes.num_passes;
    best_pg = NULL;
    for (pass = 0; pass < np; ++pass) {
        uint32_t group;
        for (group = 0; group < ng; ++group) {
            const jxl_frame_group_data *pgd =
                jxl_frame_group_by_kind(frame, JXL_TOC_KIND_GROUP_PASS, pass * ng + group);
            if (pgd != NULL && (best_pg == NULL || pgd->bytes_len > best_pg->bytes_len)) {
                best_pg = pgd;
            }
        }
    }
    if (best_pg != NULL) {
        pass_idx = best_pg->toc_group.pass_idx;
        group_idx = best_pg->toc_group.group_idx;
    }

    fst = jxl_frame_modular_pass_group_bitstream(
        ctx, alloc, frame, parsed, pass_idx, group_idx, global_ma, &has_ma, &pg_bs, 1);
    if (fst != JXL_FRAME_OK) {
        return frame_to_status(fst);
    }

    pg.ctx = ctx;
    pg.alloc = alloc;
    pg.frame_header = &frame->header;
    pg.global_ma = has_ma ? global_ma : NULL;
    pg.modular_params = mod_params;
    pg.modular_dest = dest;
    pg.pass_idx = pass_idx;
    pg.group_idx = group_idx;
    pg.allow_partial = 1;

    fst = jxl_decode_pass_group_modular_coefficients(&pg_bs, &pg);
    if (fst != JXL_FRAME_OK) {
        return frame_to_status(fst);
    }
    if (jxl_modular_dest_sample_sum(dest, 8) == 0) {
        return JXL_ERROR_INVALID_INPUT;
    }
    return JXL_OK;
}

jxl_status_t jxl_modular_decode_frame_group_coefficients(
    jxl_context *ctx, jxl_allocator_state *alloc, const jxl_frame *frame,
    const jxl_parsed_image_header *parsed, const jxl_ma_config *global_ma, int has_ma,
    const jxl_modular_params *mod_params, jxl_modular_image_destination *dest, int multi_group,
    int allow_partial, const jxl_modular_region *filter_region) {
    if (alloc == NULL || frame == NULL || parsed == NULL || dest == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }
    if (multi_group) {
        jxl_modular_region pg_filter_storage;
        jxl_modular_region lf_filter_storage;
        const jxl_modular_region *pg_ptr = NULL;
        const jxl_modular_region *lf_ptr = NULL;
        jxl_status_t st;
	if (mod_params == NULL) {
            return JXL_ERROR_INVALID_INPUT;
        }
        if (filter_region != NULL) {
            pg_filter_storage =
                jxl_modular_compute_region(&frame->header, dest, *filter_region, 0);
            lf_filter_storage = jxl_modular_region_downsample(pg_filter_storage, 3);
            pg_ptr = &pg_filter_storage;
            lf_ptr = &lf_filter_storage;
        }

        st = jxl_modular_decode_frame_pass_groups(ctx, alloc, frame, global_ma, has_ma,
                                                  mod_params, dest, allow_partial,
                                                  pg_ptr);
        if (st != JXL_OK) {
            return st;
        }
        return jxl_modular_decode_frame_lf_groups(ctx, alloc, frame, global_ma, has_ma, dest,
                                                  allow_partial, lf_ptr);
    }
    if (jxl_modular_dest_sample_sum(dest, 8) == 0) {
        if (global_ma == NULL || mod_params == NULL) {
            return JXL_ERROR_INVALID_INPUT;
        }
        return jxl_modular_decode_pass_group_fallback(ctx, alloc, parsed, frame, has_ma,
                                                      (jxl_ma_config *)global_ma,
                                                      (jxl_modular_params *)mod_params, dest);
    }
    return JXL_OK;
}

jxl_status_t jxl_modular_decode_frame_lf_groups(jxl_context *ctx, jxl_allocator_state *alloc,
                                                const jxl_frame *frame, const jxl_ma_config *global_ma,
                                                int has_ma, jxl_modular_image_destination *dest,
                                                int allow_partial,
                                                const jxl_modular_region *filter_region) {
    uint32_t i;
    jxl_lf_group_params lgp = {0};
    jxl_modular_status_t layout_st;
    uint32_t num_lf;
    if (alloc == NULL || frame == NULL || dest == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }

    layout_st = jxl_modular_ensure_group_layout(alloc, dest, &frame->header);
    if (layout_st != JXL_MODULAR_OK) {
        return JXL_ERROR_INVALID_INPUT;
    }

    num_lf = jxl_frame_header_num_lf_groups(&frame->header);
    lgp.ctx = ctx;
    lgp.image = NULL;
    lgp.frame = &frame->header;
    lgp.quantizer = NULL;
    lgp.global_ma = has_ma ? global_ma : NULL;
    lgp.gmodular = dest;
    lgp.lf_group_idx = 0;
    lgp.tracker = NULL;
    lgp.allow_partial = allow_partial;
    lgp.modular_from_pass_group = 0;

    for (i = 0; i < num_lf; ++i) {
        jxl_bs lg_bs;
        const jxl_frame_group_data *src;
        jxl_frame_status_t fst;
	if (lf_pg_filter(ctx, i)) {
            continue;
        }
        if (filter_region != NULL &&
            !jxl_modular_pass_group_intersects(&frame->header, i, filter_region,
                                               jxl_frame_header_group_dim(&frame->header))) {
            continue;
        }

        src = jxl_frame_group_by_kind(frame, JXL_TOC_KIND_LF_GROUP, i);
        if (src == NULL || src->bytes_len == 0) {
            continue;
        }

        jxl_bs_init(&lg_bs, src->bytes, src->bytes_len);
        lgp.lf_group_idx = i;
        lgp.modular_from_pass_group = 0;
        lgp.allow_partial = jxl_frame_group_allow_partial(src);
        fst = jxl_decode_lf_group_modular_coefficients(alloc, &lg_bs, &lgp);
        if (fst == JXL_FRAME_OUT_OF_MEMORY) {
            return JXL_ERROR_OUT_OF_MEMORY;
        }
        if (fst != JXL_FRAME_OK) {
            if (!lgp.allow_partial) {
                return frame_to_status(fst);
            }
            continue;
        }
    }
    return JXL_OK;
}

jxl_status_t jxl_modular_decode_frame_pass_groups(jxl_context *ctx, jxl_allocator_state *alloc,
                                                  const jxl_frame *frame,
                                                  const jxl_ma_config *global_ma, int has_ma,
                                                  const jxl_modular_params *mod_params,
                                                  jxl_modular_image_destination *dest,
                                                  int allow_partial,
                                                  const jxl_modular_region *filter_region) {
    uint32_t pass;
    int any_ok;
    jxl_pass_group_modular_params pg = {0};
    uint32_t ng;
    uint32_t np;
    uint32_t group_dim;
    if (alloc == NULL || frame == NULL || mod_params == NULL || dest == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }

    ng = jxl_frame_header_num_groups(&frame->header);
    np = frame->header.passes.num_passes;
    group_dim = jxl_frame_header_group_dim(&frame->header);
    pg.ctx = ctx;
    pg.alloc = alloc;
    pg.frame_header = &frame->header;
    pg.global_ma = has_ma ? global_ma : NULL;
    pg.modular_params = mod_params;
    pg.modular_dest = dest;
    pg.allow_partial = allow_partial;

    any_ok = 0;

    for (pass = 0; pass < np; ++pass) {
        uint32_t group;
        for (group = 0; group < ng; ++group) {
            jxl_bs pg_bs;
            jxl_frame_status_t fst;
            const jxl_frame_group_data *pgd =
                jxl_frame_group_by_kind(frame, JXL_TOC_KIND_GROUP_PASS, pass * ng + group);
            if (pgd == NULL || pgd->bytes_len == 0) {
                continue;
            }
            if (lf_pg_filter(ctx, group)) {
                continue;
            }
            if (!jxl_modular_pass_group_intersects(&frame->header, group, filter_region,
                                                 group_dim)) {
                continue;
            }
            jxl_bs_init(&pg_bs, pgd->bytes, pgd->bytes_len);
            pg.pass_idx = pass;
            pg.group_idx = group;
            pg.allow_partial = jxl_frame_group_allow_partial(pgd);
            fst = jxl_decode_pass_group_modular_coefficients(&pg_bs, &pg);
            if (fst == JXL_FRAME_OUT_OF_MEMORY) {
                return JXL_ERROR_OUT_OF_MEMORY;
            }
            if (fst != JXL_FRAME_OK) {
                if (JXL_DEBUG_FLAG(ctx, debug_pg_fail)) {
                    fprintf(stderr, "pg modular fail pass=%u group=%u fst=%d partial=%d\n", pass,
                            group, (int)fst, pg.allow_partial);
                }
                if (!pg.allow_partial) {
                    return frame_to_status(fst);
                }
                continue;
            }
            any_ok = 1;
        }
    }

    if (any_ok || jxl_modular_dest_sample_sum(dest, 8) != 0) {
        return JXL_OK;
    }
    return JXL_ERROR_INVALID_INPUT;
}
