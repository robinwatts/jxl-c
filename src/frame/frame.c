// SPDX-License-Identifier: MIT OR Apache-2.0
#include "frame.h"

#include "frame/util.h"

#include <stdlib.h>
#include <string.h>

void jxl_frame_init(jxl_frame *f) {
    if (f != NULL) {
        memset(f, 0, sizeof(*f));
        jxl_frame_header_init(&f->header);
        jxl_toc_init(&f->toc);
    }
}

static void group_data_free(jxl_allocator_state *alloc, jxl_frame_group_data *g) {
    if (g == NULL) {
        return;
    }
    jxl_free(alloc, g->bytes);
    memset(g, 0, sizeof(*g));
}

void jxl_frame_free(jxl_allocator_state *alloc, jxl_frame *f) {
    if (f == NULL) {
        return;
    }
    if (f->data != NULL) {
        size_t i;
        for (i = 0; i < f->data_len; ++i) {
            group_data_free(alloc, &f->data[i]);
        }
    }
    jxl_free(alloc, f->data);
    jxl_frame_header_free(alloc, &f->header);
    jxl_toc_free(alloc, &f->toc);
    jxl_frame_init(f);
}

static jxl_frame_status_t ensure_group_cap(jxl_allocator_state *alloc, jxl_frame_group_data *g,
                                           size_t need) {
    size_t new_cap;
    uint8_t *p;
    if (need <= g->bytes_cap) {
        return JXL_FRAME_OK;
    }
    new_cap = g->bytes_cap ? g->bytes_cap : 16;
    while (new_cap < need) {
        new_cap *= 2;
    }
    p = jxl_alloc(alloc, new_cap);
    if (p == NULL) {
        return JXL_FRAME_OUT_OF_MEMORY;
    }
    if (g->bytes != NULL && g->bytes_len > 0) {
        memcpy(p, g->bytes, g->bytes_len);
    }
    jxl_free(alloc, g->bytes);
    g->bytes = p;
    g->bytes_cap = new_cap;
    return JXL_FRAME_OK;
}

jxl_frame_status_t jxl_frame_parse(jxl_allocator_state *alloc, jxl_bs *bs,
                                   const jxl_parsed_image_header *image, jxl_frame *out) {
    size_t base_offset;
    if (alloc == NULL || bs == NULL || out == NULL) {
        return JXL_FRAME_BITSTREAM_ERROR;
    }
    jxl_frame_free(alloc, out);
    out->alloc = alloc;

    JXL_FRAME_TRY_BS(jxl_bs_zero_pad_to_byte(bs));
    base_offset = bs->num_read_bits / 8;

    if (jxl_frame_header_parse(alloc, bs, image, &out->header) != JXL_FRAME_OK) {
        return JXL_FRAME_BITSTREAM_ERROR;
    }

    if (out->header.width == 0 || out->header.height == 0) {
        return JXL_FRAME_VALIDATION_ERROR;
    }
    if (out->header.width > (1u << 30) || out->header.height > (1u << 30)) {
        return JXL_FRAME_VALIDATION_ERROR;
    }
    if ((uint64_t)out->header.width * (uint64_t)out->header.height > (1ull << 40)) {
        return JXL_FRAME_VALIDATION_ERROR;
    }

    if (jxl_toc_parse(alloc, bs, &out->header, &out->toc) != JXL_FRAME_OK) {
        return JXL_FRAME_BITSTREAM_ERROR;
    }
    jxl_toc_adjust_offsets(&out->toc, base_offset);

    out->data_len = out->toc.groups_len;
    if (out->data_len > 0) {
        size_t i;
        out->data = jxl_alloc(alloc, out->data_len * sizeof(jxl_frame_group_data));
        if (out->data == NULL) {
            jxl_frame_free(alloc, out);
            return JXL_FRAME_OUT_OF_MEMORY;
        }
        memset(out->data, 0, out->data_len * sizeof(jxl_frame_group_data));
        for (i = 0; i < out->data_len; ++i) {
            size_t group_idx = i;
            if (out->toc.bitstream_to_original_len > 0 &&
                i < out->toc.bitstream_to_original_len) {
                group_idx = out->toc.bitstream_to_original[i];
            }
            out->data[i].toc_group = out->toc.groups[group_idx];
        }
    }
    out->reading_data_index = 0;
    return JXL_FRAME_OK;
}

jxl_frame_status_t jxl_frame_parse_nth_keyframe(jxl_allocator_state *alloc, jxl_bs *bs,
                                                const jxl_parsed_image_header *image,
                                                const uint8_t *codestream, size_t cs_len,
                                                uint32_t keyframe_index, jxl_frame *out) {
    uint32_t keyframes_seen;
    if (alloc == NULL || bs == NULL || out == NULL) {
        return JXL_FRAME_BITSTREAM_ERROR;
    }

    keyframes_seen = 0;
    for (;;) {
        size_t meta_end;
	size_t payload;
        jxl_frame_status_t st = jxl_frame_parse(alloc, bs, image, out);
        if (st != JXL_FRAME_OK) {
            return st;
        }

        meta_end = bs->num_read_bits / 8;
        payload = out->toc.total_size;
        if (meta_end > cs_len || payload > cs_len - meta_end) {
            jxl_frame_free(alloc, out);
            return JXL_FRAME_BITSTREAM_ERROR;
        }

        if (jxl_frame_header_is_keyframe(&out->header)) {
            if (keyframes_seen == keyframe_index) {
                return JXL_FRAME_OK;
            }
            keyframes_seen++;
        }

        if (jxl_bs_skip_bits(bs, payload * 8) != JXL_BS_OK) {
            jxl_frame_free(alloc, out);
            return JXL_FRAME_BITSTREAM_ERROR;
        }
        jxl_frame_free(alloc, out);
        jxl_frame_init(out);
    }
}

jxl_frame_status_t jxl_frame_parse_keyframe(jxl_allocator_state *alloc, jxl_bs *bs,
                                            const jxl_parsed_image_header *image,
                                            const uint8_t *codestream, size_t cs_len,
                                            jxl_frame *out) {
    return jxl_frame_parse_nth_keyframe(alloc, bs, image, codestream, cs_len, 0, out);
}

jxl_frame_status_t jxl_count_keyframes(jxl_allocator_state *alloc, jxl_bs *bs,
                                       const jxl_parsed_image_header *image,
                                       const uint8_t *codestream, size_t cs_len,
                                       uint32_t *count_out) {
    uint32_t count;
    jxl_frame frame;
    if (alloc == NULL || bs == NULL || image == NULL || count_out == NULL) {
        return JXL_FRAME_BITSTREAM_ERROR;
    }

    count = 0;
    jxl_frame_init(&frame);

    for (;;) {
        jxl_frame_status_t st;
        size_t meta_end;
        if (bs->num_read_bits >= (uint64_t)cs_len * 8) {
            break;
        }
        st = jxl_frame_parse(alloc, bs, image, &frame);
        if (st != JXL_FRAME_OK) {
            jxl_frame_free(alloc, &frame);
            return st;
        }

        meta_end = bs->num_read_bits / 8;
        if (meta_end > cs_len || frame.toc.total_size > cs_len - meta_end) {
            jxl_frame_free(alloc, &frame);
            return JXL_FRAME_BITSTREAM_ERROR;
        }

        if (jxl_frame_header_is_keyframe(&frame.header)) {
            count++;
        }

        if (jxl_bs_skip_bits(bs, frame.toc.total_size * 8) != JXL_BS_OK) {
            jxl_frame_free(alloc, &frame);
            return JXL_FRAME_BITSTREAM_ERROR;
        }
        jxl_frame_free(alloc, &frame);
        jxl_frame_init(&frame);
    }

    jxl_frame_free(alloc, &frame);
    *count_out = count;
    return JXL_FRAME_OK;
}

const uint8_t *jxl_frame_feed_bytes(jxl_frame *f, const uint8_t *buf, size_t buf_len,
                                    size_t *consumed_out) {
    size_t pos;
    if (consumed_out != NULL) {
        *consumed_out = 0;
    }
    if (f == NULL || buf == NULL) {
        return buf;
    }

    pos = 0;
    while (f->reading_data_index < f->data_len) {
        size_t left;
        jxl_frame_group_data *g = &f->data[f->reading_data_index];
        size_t need = (size_t)g->toc_group.size;
        size_t have = g->bytes_len;
        if (have >= need) {
            f->reading_data_index++;
            continue;
        }
        left = need - have;
        if (buf_len - pos < left) {
            if (ensure_group_cap(f->alloc, g, have + (buf_len - pos)) != JXL_FRAME_OK) {
                return buf + pos;
            }
            memcpy(g->bytes + have, buf + pos, buf_len - pos);
            g->bytes_len = have + (buf_len - pos);
            pos = buf_len;
            break;
        }
        if (ensure_group_cap(f->alloc, g, need) != JXL_FRAME_OK) {
            return buf + pos;
        }
        memcpy(g->bytes + have, buf + pos, left);
        g->bytes_len = need;
        pos += left;
        f->reading_data_index++;
    }
    if (consumed_out != NULL) {
        *consumed_out = pos;
    }
    return buf + pos;
}

int jxl_frame_is_loading_done(const jxl_frame *f) {
    return f != NULL && f->reading_data_index >= f->data_len;
}

size_t jxl_frame_total_group_bytes(const jxl_frame *f) {
    return f != NULL ? f->toc.total_size : 0;
}

const jxl_frame_group_data *jxl_frame_group_by_kind(const jxl_frame *frame,
                                                    jxl_toc_group_kind kind, uint32_t index) {
    size_t idx;
    if (frame == NULL) {
        return NULL;
    }
    idx = jxl_toc_group_index_bitstream_order(&frame->toc, kind, index);
    if (idx >= frame->data_len) {
        return NULL;
    }
    return &frame->data[idx];
}

int jxl_frame_group_allow_partial(const jxl_frame_group_data *group) {
    if (group == NULL || group->toc_group.size == 0) {
        return 0;
    }
    return group->bytes_len < (size_t)group->toc_group.size;
}
