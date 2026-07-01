// SPDX-License-Identifier: MIT OR Apache-2.0
#include "patch.h"

#include "bitstream/unpack.h"
#include "frame/util.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

void jxl_patches_init(jxl_patches *p) {
    if (p != NULL) {
        memset(p, 0, sizeof(*p));
    }
}

void jxl_patches_free(jxl_allocator_state *alloc, jxl_patches *p) {
    size_t i;
    if (p == NULL) {
        return;
    }
    for (i = 0; i < p->refs_len; ++i) {
        size_t j;
        jxl_patch_ref *ref = &p->refs[i];
        for (j = 0; j < ref->targets_len; ++j) {
            jxl_free(alloc, ref->targets[j].blending);
        }
        jxl_free(alloc, ref->targets);
    }
    jxl_free(alloc, p->refs);
    jxl_patches_init(p);
}

int jxl_frame_header_can_reference(const jxl_frame_header *h) {
    return h != NULL && !h->is_last &&
           (h->duration == 0 || h->save_as_reference != 0) &&
           h->frame_type != JXL_FRAME_TYPE_LF;
}

static jxl_frame_status_t patch_blend_from_raw(uint32_t raw, jxl_patch_blend_mode *out) {
    if (out == NULL || raw > 7u) {
        return JXL_FRAME_VALIDATION_ERROR;
    }
    *out = (jxl_patch_blend_mode)raw;
    return JXL_FRAME_OK;
}

static int patch_blend_uses_alpha(jxl_patch_blend_mode mode) {
    return mode == JXL_PATCH_BLEND_BLEND_ABOVE || mode == JXL_PATCH_BLEND_BLEND_BELOW ||
           mode == JXL_PATCH_BLEND_MULADD_ABOVE || mode == JXL_PATCH_BLEND_MULADD_BELOW;
}

jxl_frame_status_t jxl_patches_parse(jxl_allocator_state *alloc, jxl_bs *bs,
                                     const jxl_parsed_image_header *image,
                                     const jxl_frame_header *frame, jxl_patches *out) {
                                         uint32_t ri;
    uint32_t default_alpha;
    uint32_t num_patch_refs;
    uint32_t total_patches;
    jxl_coding_decoder *dec = NULL;
    size_t num_extra;
    uint32_t frame_width;
    uint32_t frame_height;
    uint64_t patch_ref_limit;
    uint32_t max_num_patch_refs;
    uint32_t max_num_patches;
    jxl_patch_ref *refs;
    if (alloc == NULL || bs == NULL || image == NULL || frame == NULL || out == NULL) {
        return JXL_FRAME_BITSTREAM_ERROR;
    }
    jxl_patches_free(alloc, out);

    num_extra = image->num_extra_channels;
    default_alpha = 0;

    JXL_FRAME_TRY_CODING(jxl_coding_decoder_parse(alloc, bs, 10, &dec));
    JXL_FRAME_TRY_CODING(jxl_coding_decoder_begin(dec, bs));

    frame_width = frame->width;
    frame_height = frame->height;
    patch_ref_limit = (uint64_t)frame_width * (uint64_t)frame_height / 16u;
    max_num_patch_refs =
        patch_ref_limit > (1u << 24) ? (1u << 24) : (uint32_t)patch_ref_limit;
    max_num_patches = max_num_patch_refs * 4u;

    num_patch_refs = 0;
    JXL_FRAME_TRY_CODING(jxl_coding_decoder_read_varint(dec, bs, 0, &num_patch_refs));
    if (num_patch_refs > max_num_patch_refs) {
        jxl_coding_decoder_destroy(alloc, dec);
        return JXL_FRAME_VALIDATION_ERROR;
    }

    if (num_patch_refs == 0) {
        JXL_FRAME_TRY_CODING(jxl_coding_decoder_finalize(dec));
        jxl_coding_decoder_destroy(alloc, dec);
        return JXL_FRAME_OK;
    }

    refs = jxl_alloc(alloc, (size_t)num_patch_refs * sizeof(*refs));
    if (refs == NULL) {
        jxl_coding_decoder_destroy(alloc, dec);
        return JXL_FRAME_OUT_OF_MEMORY;
    }
    memset(refs, 0, (size_t)num_patch_refs * sizeof(*refs));

    total_patches = 0;
    for (ri = 0; ri < num_patch_refs; ++ri) {
        uint32_t ti;
        uint32_t w;
        uint32_t h;
        uint32_t count;
        int32_t prev_x;
        int32_t prev_y;
        int have_prev;
        jxl_patch_ref *ref = &refs[ri];
        JXL_FRAME_TRY_CODING(jxl_coding_decoder_read_varint(dec, bs, 1, &ref->ref_idx));
        if (ref->ref_idx >= 4u) {
            jxl_patches_free(alloc, out);
            jxl_coding_decoder_destroy(alloc, dec);
            return JXL_FRAME_VALIDATION_ERROR;
        }
        JXL_FRAME_TRY_CODING(jxl_coding_decoder_read_varint(dec, bs, 3, &ref->x0));
        JXL_FRAME_TRY_CODING(jxl_coding_decoder_read_varint(dec, bs, 3, &ref->y0));
        w = 0;
        h = 0;
        JXL_FRAME_TRY_CODING(jxl_coding_decoder_read_varint(dec, bs, 2, &w));
        JXL_FRAME_TRY_CODING(jxl_coding_decoder_read_varint(dec, bs, 2, &h));
        ref->width = w + 1u;
        ref->height = h + 1u;
        count = 0;
        JXL_FRAME_TRY_CODING(jxl_coding_decoder_read_varint(dec, bs, 7, &count));
        count += 1u;
        total_patches += count;
        if (total_patches > max_num_patches) {
            jxl_patches_free(alloc, out);
            jxl_coding_decoder_destroy(alloc, dec);
            return JXL_FRAME_VALIDATION_ERROR;
        }

        if (count > 0) {
            ref->targets = jxl_alloc(alloc, (size_t)count * sizeof(*ref->targets));
            if (ref->targets == NULL) {
                jxl_patches_free(alloc, out);
                jxl_coding_decoder_destroy(alloc, dec);
                return JXL_FRAME_OUT_OF_MEMORY;
            }
            memset(ref->targets, 0, (size_t)count * sizeof(*ref->targets));
            ref->targets_len = count;
        }

        prev_x = 0;
        prev_y = 0;
        have_prev = 0;
        for (ti = 0; ti < count; ++ti) {
            size_t bi;
            jxl_patch_target *target = &ref->targets[ti];
            size_t blend_count;
            if (have_prev) {
                uint32_t dx = 0;
                uint32_t dy = 0;
                int64_t nx;
                int64_t ny;
                JXL_FRAME_TRY_CODING(jxl_coding_decoder_read_varint(dec, bs, 6, &dx));
                JXL_FRAME_TRY_CODING(jxl_coding_decoder_read_varint(dec, bs, 6, &dy));
                nx = (int64_t)prev_x + (int64_t)jxl_unpack_signed(dx);
                ny = (int64_t)prev_y + (int64_t)jxl_unpack_signed(dy);
                if (nx < INT32_MIN || nx > INT32_MAX || ny < INT32_MIN || ny > INT32_MAX) {
                    jxl_patches_free(alloc, out);
                    jxl_coding_decoder_destroy(alloc, dec);
                    return JXL_FRAME_VALIDATION_ERROR;
                }
                target->x = (int32_t)nx;
                target->y = (int32_t)ny;
            } else {
                uint32_t ux = 0;
                uint32_t uy = 0;
                JXL_FRAME_TRY_CODING(jxl_coding_decoder_read_varint(dec, bs, 4, &ux));
                JXL_FRAME_TRY_CODING(jxl_coding_decoder_read_varint(dec, bs, 4, &uy));
                target->x = (int32_t)ux;
                target->y = (int32_t)uy;
            }
            prev_x = target->x;
            prev_y = target->y;
            have_prev = 1;

            blend_count = num_extra + 1u;
            target->blending = jxl_alloc(alloc, blend_count * sizeof(*target->blending));
            if (target->blending == NULL) {
                jxl_patches_free(alloc, out);
                jxl_coding_decoder_destroy(alloc, dec);
                return JXL_FRAME_OUT_OF_MEMORY;
            }
            memset(target->blending, 0, blend_count * sizeof(*target->blending));
            target->blending_len = blend_count;

            for (bi = 0; bi < blend_count; ++bi) {
                uint32_t raw_mode = 0;
                jxl_patch_blend_mode mode = JXL_PATCH_BLEND_NONE;
                JXL_FRAME_TRY_CODING(jxl_coding_decoder_read_varint(dec, bs, 5, &raw_mode));
                if (patch_blend_from_raw(raw_mode, &mode) != JXL_FRAME_OK) {
                    jxl_patches_free(alloc, out);
                    jxl_coding_decoder_destroy(alloc, dec);
                    return JXL_FRAME_VALIDATION_ERROR;
                }
                target->blending[bi].mode = mode;
                if (raw_mode >= 4u && num_extra >= 2) {
                    JXL_FRAME_TRY_CODING(
                        jxl_coding_decoder_read_varint(dec, bs, 8, &target->blending[bi].alpha_channel));
                } else {
                    target->blending[bi].alpha_channel = default_alpha;
                }
                if (raw_mode >= 3u) {
                    uint32_t clamp = 0;
                    JXL_FRAME_TRY_CODING(jxl_coding_decoder_read_varint(dec, bs, 9, &clamp));
                    target->blending[bi].clamp = clamp != 0;
                }
                if (patch_blend_uses_alpha(mode) &&
                    target->blending[bi].alpha_channel >= num_extra) {
                    jxl_patches_free(alloc, out);
                    jxl_coding_decoder_destroy(alloc, dec);
                    return JXL_FRAME_VALIDATION_ERROR;
                }
            }
        }
    }

    JXL_FRAME_TRY_CODING(jxl_coding_decoder_finalize(dec));
    jxl_coding_decoder_destroy(alloc, dec);
    out->refs = refs;
    out->refs_len = num_patch_refs;
    return JXL_FRAME_OK;
}
