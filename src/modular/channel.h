// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_MODULAR_CHANNEL_H_
#define JXL_MODULAR_CHANNEL_H_

#include "allocator.h"
#include "modular/error.h"
#include "modular/param.h"

#include <stddef.h>
#include "jxl_oxide/jxl_types.h"

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t original_width;
    uint32_t original_height;
    int32_t hshift;
    int32_t vshift;
    jxl_channel_shift original_shift;
} jxl_modular_channel_info;

typedef struct {
    jxl_modular_channel_info *info;
    size_t info_len;
    size_t info_cap;
    uint32_t nb_meta_channels;
} jxl_modular_channels;

jxl_modular_channel_info jxl_modular_channel_info_new(uint32_t original_width,
                                                      uint32_t original_height,
                                                      jxl_channel_shift shift);
jxl_modular_channel_info jxl_modular_channel_info_new_unshiftable(uint32_t width, uint32_t height);

void jxl_modular_channels_init(jxl_modular_channels *ch);
void jxl_modular_channels_free(jxl_allocator_state *alloc, jxl_modular_channels *ch);
jxl_modular_status_t jxl_modular_channels_reserve(jxl_allocator_state *alloc,
                                                  jxl_modular_channels *ch, size_t need);

jxl_modular_status_t jxl_modular_channels_from_params(jxl_allocator_state *alloc,
                                                      const jxl_modular_params *params,
                                                      jxl_modular_channels *out);

jxl_modular_status_t jxl_modular_channels_push(jxl_allocator_state *alloc,
                                               jxl_modular_channels *ch,
                                               jxl_modular_channel_info info);
jxl_modular_status_t jxl_modular_channels_insert(jxl_allocator_state *alloc,
                                                 jxl_modular_channels *ch, size_t at,
                                                 jxl_modular_channel_info info);
void jxl_modular_channels_remove_range(jxl_modular_channels *ch, size_t begin, size_t end);
void jxl_modular_channels_append_slice(jxl_allocator_state *alloc, jxl_modular_channels *ch,
                                       const jxl_modular_channel_info *items, size_t count);

#endif /* JXL_MODULAR_CHANNEL_H_ */
