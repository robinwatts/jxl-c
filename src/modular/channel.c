// SPDX-License-Identifier: MIT OR Apache-2.0
#include "channel.h"

#include <string.h>

jxl_modular_channel_info jxl_modular_channel_info_new(uint32_t original_width,
                                                      uint32_t original_height,
                                                      jxl_channel_shift shift) {
    jxl_modular_channel_info info = {0};
    info.original_width = original_width;
    info.original_height = original_height;
    info.original_shift = shift;
    jxl_channel_shift_shift_size(&shift, original_width, original_height, &info.width, &info.height);
    info.hshift = jxl_channel_shift_hshift(&shift);
    info.vshift = jxl_channel_shift_vshift(&shift);
    return info;
}

jxl_modular_channel_info jxl_modular_channel_info_new_unshiftable(uint32_t width, uint32_t height) {
    jxl_modular_channel_info info = {0};
    info.width = width;
    info.height = height;
    info.original_width = width;
    info.original_height = height;
    info.hshift = -1;
    info.vshift = -1;
    info.original_shift = jxl_channel_shift_from_shift(0);
    return info;
}

void jxl_modular_channels_init(jxl_modular_channels *ch) {
    if (ch != NULL) {
        memset(ch, 0, sizeof(*ch));
    }
}

void jxl_modular_channels_free(jxl_allocator_state *alloc, jxl_modular_channels *ch) {
    if (ch == NULL || alloc == NULL) {
        return;
    }
    jxl_free(alloc, ch->info);
    ch->info = NULL;
    ch->info_len = 0;
    ch->info_cap = 0;
}

jxl_modular_status_t jxl_modular_channels_reserve(jxl_allocator_state *alloc,
                                                  jxl_modular_channels *ch, size_t need) {
    size_t cap;
    jxl_modular_channel_info *grown;
    if (alloc == NULL || ch == NULL) {
        return JXL_MODULAR_BITSTREAM_ERROR;
    }
    if (need <= ch->info_cap) {
        return JXL_MODULAR_OK;
    }
    cap = ch->info_cap == 0 ? 4 : ch->info_cap;
    while (cap < need) {
        if (cap > SIZE_MAX / 2) {
            return JXL_MODULAR_OUT_OF_MEMORY;
        }
        cap *= 2;
    }
    grown = jxl_realloc(alloc, ch->info, cap * sizeof(*ch->info));
    if (grown == NULL) {
        return JXL_MODULAR_OUT_OF_MEMORY;
    }
    ch->info = grown;
    ch->info_cap = cap;
    return JXL_MODULAR_OK;
}

jxl_modular_status_t jxl_modular_channels_from_params(jxl_allocator_state *alloc,
                                                      const jxl_modular_params *params,
                                                      jxl_modular_channels *out) {
                                                          size_t i;
    if (alloc == NULL || params == NULL || out == NULL) {
        return JXL_MODULAR_BITSTREAM_ERROR;
    }
    jxl_modular_channels_free(alloc, out);
    jxl_modular_channels_init(out);
    if (params->num_channels == 0) {
        return JXL_MODULAR_OK;
    }
    if (jxl_modular_channels_reserve(alloc, out, params->num_channels) != JXL_MODULAR_OK) {
        return JXL_MODULAR_OUT_OF_MEMORY;
    }
    for (i = 0; i < params->num_channels; ++i) {
        const jxl_modular_channel_params *cp = &params->channels[i];
        out->info[i] =
            jxl_modular_channel_info_new(cp->width, cp->height, cp->shift);
    }
    out->info_len = params->num_channels;
    return JXL_MODULAR_OK;
}

jxl_modular_status_t jxl_modular_channels_push(jxl_allocator_state *alloc,
                                               jxl_modular_channels *ch,
                                               jxl_modular_channel_info info) {
    if (alloc == NULL || ch == NULL) {
        return JXL_MODULAR_BITSTREAM_ERROR;
    }
    if (jxl_modular_channels_reserve(alloc, ch, ch->info_len + 1) != JXL_MODULAR_OK) {
        return JXL_MODULAR_OUT_OF_MEMORY;
    }
    ch->info[ch->info_len++] = info;
    return JXL_MODULAR_OK;
}

jxl_modular_status_t jxl_modular_channels_insert(jxl_allocator_state *alloc,
                                                 jxl_modular_channels *ch, size_t at,
                                                 jxl_modular_channel_info info) {
    if (alloc == NULL || ch == NULL || at > ch->info_len) {
        return JXL_MODULAR_BITSTREAM_ERROR;
    }
    if (jxl_modular_channels_reserve(alloc, ch, ch->info_len + 1) != JXL_MODULAR_OK) {
        return JXL_MODULAR_OUT_OF_MEMORY;
    }
    memmove(&ch->info[at + 1], &ch->info[at], (ch->info_len - at) * sizeof(*ch->info));
    ch->info[at] = info;
    ch->info_len += 1;
    return JXL_MODULAR_OK;
}

void jxl_modular_channels_remove_range(jxl_modular_channels *ch, size_t begin, size_t end) {
    size_t tail;
    if (ch == NULL || begin >= end || begin >= ch->info_len) {
        return;
    }
    if (end > ch->info_len) {
        end = ch->info_len;
    }
    tail = ch->info_len - end;
    memmove(&ch->info[begin], &ch->info[end], tail * sizeof(*ch->info));
    ch->info_len -= end - begin;
}

void jxl_modular_channels_append_slice(jxl_allocator_state *alloc, jxl_modular_channels *ch,
                                       const jxl_modular_channel_info *items, size_t count) {
    if (alloc == NULL || ch == NULL || items == NULL || count == 0) {
        return;
    }
    if (jxl_modular_channels_reserve(alloc, ch, ch->info_len + count) != JXL_MODULAR_OK) {
        return;
    }
    memcpy(&ch->info[ch->info_len], items, count * sizeof(*items));
    ch->info_len += count;
}
